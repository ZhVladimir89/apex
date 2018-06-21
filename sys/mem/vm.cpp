#include "mmap.h"
#include "vm.h"

#include <arch.h>
#include <debug.h>
#include <fcntl.h>
#include <fs.h>
#include <kernel.h>
#include <kmem.h>
#include <list.h>
#include <mutex>
#include <sys/mman.h>
#include <sys/uio.h>
#include <task.h>
#include <types.h>
#include <sch.h>

/*
 * TODO: shared mappings
 */

struct seg {
	list link;	/* entry in segment list */
	int prot;	/* segment protection PROT_* */
	void *base;	/* virtual base address of this segment */
	size_t len;	/* size of segment */
	MEM_TYPE type;	/* preferred memory type for pages */
	off_t off;	/* (optional) offset into vnode */
	vnode *vn;	/* (optional) vnode backing region */
};

struct as {
	list segs;	/* list of segments */
	void *base;	/* base address of address space */
	size_t len;	/* size of address space */
	void *brk;	/* current program break */
	unsigned ref;	/* reference count */
#if defined(CONFIG_MMU)
	struct pgd *pgd;/* page directory */
#endif
};

/*
 * do_vm_io - walk local and remote iovs calling f for each overlapping area
 */
template<typename fn>
static ssize_t
do_vm_io(as *a, const iovec *liov, size_t lc, const iovec *riov, size_t rc, fn f)
{
	if (!lc || !rc)
		return 0;

	ssize_t ret = 0;
	iovec r = *riov;
	for (; lc; --lc, ++liov) {
		iovec l = *liov;
		while (l.iov_len) {
			const auto s = std::min(l.iov_len, r.iov_len);
			const auto err = f(a, l.iov_base, r.iov_base, s);
			if (err < 0)
				return err;
			l.iov_len -= s;
			l.iov_base = (char*)l.iov_base + s;
			ret += s;

			if (r.iov_len == s) {
				if (!--rc)
					return ret;
				r = *++riov;
			} else {
				r.iov_len -= s;
				r.iov_base = (char*)r.iov_base + s;
			}
		}
	}

	return ret;
}

/*
 * seg_extend - try to extend segment
 */
static bool
seg_extend(seg *s, void *addr, size_t len, int prot, vnode *vn, off_t off,
    MEM_TYPE type)
{
	if (s->prot != prot || (char*)s->base + len != addr ||
	    s->type != type || s->off + s->len != off || s->vn != vn)
		return false;
	s->len += len;
	return true;
}

/*
 * oflags - calculate required oflags from map flags & protection
 */
static int
oflags(int prot, int flags)
{
	constexpr auto rdwr = PROT_READ | PROT_WRITE;

	/* private mappings don't require write access to underlying file */
	if (flags & MAP_PRIVATE)
		prot &= ~PROT_WRITE;
	if ((prot & rdwr) == rdwr)
		return O_RDWR;
	if (prot & PROT_READ)
		return O_RDONLY;
	if (prot & PROT_WRITE)
		return O_WRONLY;
	return DERR(-EINVAL);
}

/*
 * seg_insert - insert new segment
 */
static int
seg_insert(seg *prev, std::unique_ptr<phys> pages, size_t len, int prot,
    std::unique_ptr<vnode> vn, off_t off, MEM_TYPE type)
{
	seg *ns;
	if (!(ns = (seg*)kmem_alloc(sizeof(seg), MEM_NORMAL)))
		return DERR(-ENOMEM);
	ns->prot = prot;
	ns->base = pages.release();
	ns->len = len;
	ns->type = type;
	ns->off = off;
	ns->vn = vn.release();
	list_insert(&prev->link, &ns->link);
	return 0;
}

/*
 * mmapfor - map memory into task address space
 */
extern "C" void *
mmapfor(as *a, void *addr, size_t len, int prot, int flags, int fd, off_t off,
    MEM_TYPE type)
{
	int err;
	std::unique_ptr<vnode> vn;
	const bool fixed = flags & MAP_FIXED;
	const bool anon = flags & MAP_ANONYMOUS;
	const bool priv = flags & MAP_PRIVATE;
	const bool shared = flags & MAP_SHARED;

	if ((uintptr_t)addr & PAGE_MASK || len & PAGE_MASK || off & PAGE_MASK ||
	    priv == shared)
		return (void*)DERR(-EINVAL);
	if (!anon) {
		int oflg;
		if ((oflg = oflags(prot, flags)) < 0)
			return (void*)oflg;
		/* REVISIT: do we need to check if file is executable? */
		vn.reset(vn_open(fd, oflg));
		if (!vn.get())
			return (void*)DERR(-EBADF);
	}

	std::lock_guard l(global_sch_lock);

	if (fixed && (err = munmapfor(a, addr, len)) < 0)
		return (void*)err;

	return as_map(a, addr, len, prot, flags, std::move(vn), off, type);
}

/*
 * munmapfor - unmap memory in address space
 */
extern "C" int
munmapfor(as *a, void *const vaddr, const size_t ulen)
{
	int err = 0;

	if ((uintptr_t)vaddr & PAGE_MASK || ulen & PAGE_MASK)
		return DERR(-EINVAL);
	if (!ulen)
		return 0;

	std::lock_guard l(global_sch_lock);

	const auto uaddr = (char*)vaddr;
	const auto uend = uaddr + ulen;

	seg *s, *tmp;
	list_for_each_entry_safe(s, tmp, &a->segs, link) {
		const auto send = (char*)s->base + s->len;
		if (send <= uaddr)
			continue;
		if (s->base >= uend)
			break;
		if (s->base >= uaddr && send <= uend) {
			/* entire segment */
			err = as_unmap(a, s->base, s->len, s->vn, s->off);
			list_remove(&s->link);
			if (s->vn)
				vn_close(s->vn);
			kmem_free(s);
		} else if (s->base < uaddr && send > uend) {
			/* hole in segment */
			seg *ns;
			if (!(ns = (seg*)kmem_alloc(sizeof(seg), MEM_NORMAL)))
				return DERR(-ENOMEM);
			s->len = uaddr - (char*)s->base;
			err = as_unmap(a, uaddr, ulen, s->vn, s->off + s->len);

			*ns = *s;
			ns->base = uend;
			ns->len = send - uend;
			if (ns->vn) {
				vn_reference(ns->vn);
				ns->off += uend - (char*)s->base;
			}
			list_insert(&s->link, &ns->link);

			break;
		} else if (s->base < uaddr) {
			/* end of segment */
			const auto l = uaddr - (char*)s->base;
			err = as_unmap(a, uaddr, s->len - l, s->vn, s->off + l);
			s->len = l;
		} else if (s->base < uend) {
			/* start of segment */
			const auto l = uend - (char*)s->base;
			err = as_unmap(a, s->base, l, s->vn, s->off);
			if (s->vn)
				s->off += l;
			s->base = (char*)s->base + l;
			s->len -= l;
		} else
			panic("BUG");
		if (err < 0)
			break;
	}

	return err;
}

/*
 * mprotectfor
 */
extern "C" int
mprotectfor(as *a, void *const vaddr, const size_t ulen, const int prot)
{
	int err = 0;

	if ((uintptr_t)vaddr & PAGE_MASK || ulen & PAGE_MASK)
		return DERR(-EINVAL);
	if ((prot & (PROT_READ | PROT_WRITE | PROT_EXEC)) != prot)
		return DERR(-EINVAL);
	if (!ulen)
		return 0;

	std::lock_guard l(global_sch_lock);

	const auto uaddr = (char*)vaddr;
	const auto uend = uaddr + ulen;

	seg *s, *tmp;
	list_for_each_entry_safe(s, tmp, &a->segs, link) {
		const auto send = (char*)s->base + s->len;
		if (send <= uaddr)
			continue;
		if (s->base >= uend)
			break;
		if (s->base >= uaddr && send <= uend) {
			/* entire segment */
			err = as_mprotect(a, s->base, s->len, prot);
			s->prot = prot;
		} else if (s->base < uaddr && send > uend) {
			/* hole in segment */
			seg *ns1, *ns2;
			if (!(ns1 = (seg*)kmem_alloc(sizeof(seg), MEM_NORMAL)))
				return DERR(-ENOMEM);
			if (!(ns2 = (seg*)kmem_alloc(sizeof(seg), MEM_NORMAL))) {
				kmem_free(ns1);
				return DERR(-ENOMEM);
			}

			err = as_mprotect(a, uaddr, ulen, prot);

			s->len = uaddr - (char*)s->base;

			*ns1 = *s;
			ns1->prot = prot;
			ns1->base = uaddr;
			ns1->len = ulen;
			if (ns1->vn) {
				vn_reference(ns1->vn);
				ns1->off += s->len;
			}
			list_insert(&s->link, &ns1->link);

			*ns2 = *s;
			ns2->base = uend;
			ns2->len = send - uend;
			if (ns2->vn) {
				vn_reference(ns2->vn);
				ns2->off += s->len + ns1->len;
			}
			list_insert(&ns1->link, &ns2->link);

			break;
		} else if (s->base < uaddr) {
			/* end of segment */
			seg *ns;
			if (!(ns = (seg*)kmem_alloc(sizeof(seg), MEM_NORMAL)))
				return DERR(-ENOMEM);

			const auto l = uaddr - (char*)s->base;
			err = as_mprotect(a, uaddr, s->len - l, prot);

			*ns = *s;
			ns->prot = prot;
			ns->base = uaddr;
			ns->len = s->len - l;
			if (ns->vn) {
				vn_reference(ns->vn);
				ns->off += s->len;
			}
			list_insert(&s->link, &ns->link);

			s->len = l;
		} else if (s->base < uend) {
			/* start of segment */
			seg *ns;
			if (!(ns = (seg*)kmem_alloc(sizeof(seg), MEM_NORMAL)))
				return DERR(-ENOMEM);
			const auto l = uend - (char*)s->base;
			err = as_mprotect(a, s->base, l, prot);

			*ns = *s;
			ns->prot = prot;
			ns->len = l;
			if (ns->vn)
				vn_reference(ns->vn);
			list_insert(list_prev(&s->link), &ns->link);

			if (s->vn)
				s->off += l;
			s->base = (char*)s->base + l;
			s->len -= l;
		} else
			panic("BUG");
		if (err < 0)
			break;
	}

	return err;
}

/*
 * vm_set_brk - initialise program break
 */
extern "C" void
vm_init_brk(as *a, void *brk)
{
	assert(!a->brk);
	assert(!((uintptr_t)brk & PAGE_MASK));
	a->brk = brk;
}

/*
 * vm_readv
 */
extern "C" ssize_t
vm_readv(as *a, const iovec *liov, size_t lc, const iovec *riov, size_t rc)
{
	return do_vm_io(a, liov, lc, riov, rc, as_read);
}

/*
 * vm_writev
 */
extern "C" ssize_t
vm_writev(as *a, const iovec *liov, size_t lc, const iovec *riov, size_t rc)
{
	return do_vm_io(a, liov, lc, riov, rc, as_write);
}

/*
 * vm_read
 */
extern "C" ssize_t
vm_read(as *a, void *lbuf, const void *rbuf, size_t len)
{
	const iovec liov = { lbuf, len };
	const iovec riov = { (void*)rbuf, len };
	return vm_readv(a, &liov, 1, &riov, 1);
}

/*
 * vm_write
 */
extern "C" ssize_t
vm_write(as *a, const void *lbuf, void *rbuf, size_t len)
{
	const iovec liov = { (void*)lbuf, len };
	const iovec riov = { rbuf, len };
	return vm_writev(a, &liov, 1, &riov, 1);
}

/*
 * sc_mmap2
 */
extern "C" void*
sc_mmap2(void *addr, size_t len, int prot, int flags, int fd, int pgoff)
{
	return mmapfor(task_cur()->as, addr, len, prot, flags, fd,
	    (off_t)pgoff * CONFIG_PAGE_SIZE, MEM_NORMAL);
}

/*
 * sc_munmap
 */
extern "C" int
sc_munmap(void *addr, size_t len)
{
	return munmapfor(task_cur()->as, addr, len);
}

/*
 * sc_mprotect
 */
extern "C" int
sc_mprotect(void *addr, size_t len, int prot)
{
	return mprotectfor(task_cur()->as, addr, len, prot);
}

/*
 * sc_brk
 */
extern "C" void *
sc_brk(void *addr)
{
	auto a = task_cur()->as;

	std::lock_guard l(global_sch_lock);

	if (!addr)
		return a->brk;

	/* shrink */
	if (addr < a->brk)
		if (auto r = munmapfor(a, addr,
		    (uintptr_t)a->brk - (uintptr_t)addr); r < 0)
			return (void*)r;

	/* grow */
	if (addr > a->brk)
		if (auto r = mmapfor(a, a->brk,
		    (uintptr_t)a->brk - (uintptr_t)addr,
		    PROT_READ | PROT_WRITE,
		    MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE,
		    0, 0, MEM_NORMAL); r > (void*)-4096UL)
			return r;

	return a->brk = addr;
}

/*
 * vm_init
 */
extern "C" void
vm_init()
{

}

/*
 * vm_dump
 */
extern "C" void
vm_dump()
{
	dbg("*** VM Dump ***\n");
	struct task *task;
	dbg("Address space for %s\n", kern_task.name);
	as_dump(kern_task.as);
	list_for_each_entry(task, &kern_task.link, link) {
		dbg("Address space for %s\n", task->name);
		as_dump(task->as);
	}
}

/*
 * as_init
 */
extern "C" as *
as_create(pid_t pid)
{
	std::unique_ptr<as> a((as*)kmem_alloc(sizeof(as), MEM_NORMAL));
	if (!a)
		return nullptr;

	list_init(&a->segs);

#if defined(CONFIG_MMU)
	a->base = CONFIG_PAGE_SIZE;
	a->len = CONFIG_USER_LIMIT - CONFIG_PAGE_SIZE;
	if (auto r = mmu_newmap(&a->pgd, pid); r < 0)
		return r;
#else
	a->base = 0;
	a->len = SIZE_MAX;
#endif
	a->brk = 0;
	a->ref = 1;

	return a.release();
}

/*
 * as_copy
 */
extern "C" as *
as_copy(as *a, pid_t pid)
{
	/* REVISIT: implement */
	return (as*)DERR(-ENOSYS);
}

/*
 * as_destroy
 */
void
as_destroy(as *a)
{
	if (--a->ref > 0)
		return;

	seg *s, *tmp;
	list_for_each_entry_safe(s, tmp, &a->segs, link) {
		as_unmap(a, s->base, s->len, s->vn, s->off);
		if (s->vn)
			vn_close(s->vn);
		kmem_free(s);
	}
	kmem_free(a);
}

/*
 * as_reference
 */
void
as_reference(as *a)
{
	++a->ref;
}

/*
 * as_dump
 */
void
as_dump(as *a)
{
	seg *s;
	list_for_each_entry(s, &a->segs, link) {
		info("  %p-%p %c%c%c%c %8lld %s\n",
		    s->base, (char*)s->base + s->len,
		    s->prot & PROT_READ ? 'r' : '-',
		    s->prot & PROT_WRITE ? 'w' : '-',
		    s->prot & PROT_EXEC ? 'x' : '-',
		    'p',	/* REVISIT: shared regions */
		    s->off,
		    s->vn ? vn_name(s->vn) : "");
	}
}

/*
 * as_insert - insert memory into address space (nommu)
 */
int
as_insert(as *a, std::unique_ptr<phys> pages, size_t len, int prot,
    int flags, std::unique_ptr<vnode> vn, off_t off, MEM_TYPE type)
{
	seg *s;
	list_for_each_entry(s, &a->segs, link) {
		if (s->base > pages.get())
			break;
	}
	s = list_entry(list_prev(&s->link), seg, link);

	if (!list_end(&a->segs, &s->link) &&
	    seg_extend(s, pages.get(), len, prot, vn.get(), off, type)) {
		pages.release();
		return 0;
	}

	return seg_insert(s, std::move(pages), len, prot,
	    std::move(vn), off, type);
}