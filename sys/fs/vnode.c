/*
 * Copyright (c) 2005-2008, Kohsuke Ohtani
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * vnode.c - vnode service
 */

#include "vnode.h"

#include "debug.h"
#include "mount.h"
#include "pipe.h"
#include "vfs.h"
#include <assert.h>
#include <debug.h>
#include <dirent.h>
#include <errno.h>
#include <jhash3.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <thread.h>

/*
 * Memo:
 *
 * Function   Ref count Parent ref count Lock
 * ---------- ---------	---------------- ----------
 * vn_lookup  +1	*		 Lock
 * vn_lock     *	*		 Lock
 * vn_unlock   *	*		 Unlock
 * vget       +1	+1		 Lock
 * vput       -1	-1		 Unlock
 * vref       +1	*		 *
 */

#define VNODE_BUCKETS 128		/* size of vnode hash table */

/*
 * vnode table.
 * All active (opened) vnodes are stored on this hash table.
 */
static struct list vnode_table[VNODE_BUCKETS];

/*
 * Global lock to access vnode table.
 *
 * This lock also protects the v_refcnt member of the vnode structure.
 *
 * DO NOT modify the contents of struct vnode without holding v_lock.
 */
static struct mutex vnode_mutex;

/*
 * vn_hash - Get the hash value for a vnode from its parent and name.
 */
static unsigned
vn_hash(struct vnode *parent, const char *name, size_t len)
{
	return jhash_2words(jhash(name, len, 0), (uint32_t)parent) &
	    (VNODE_BUCKETS - 1);
}

/*
 * vn_lock_interruptible - lock vnode
 */
int
vn_lock_interruptible(struct vnode *vp)
{
	assert(vp->v_refcnt > 0);

	return mutex_lock_interruptible(&vp->v_lock);
}

/*
 * vn_lock - lock vnode
 */
void
vn_lock(struct vnode *vp)
{
	assert(vp->v_refcnt > 0);

	mutex_lock(&vp->v_lock);
}

/*
 * vn_unlock - unlock vnode
 */
void
vn_unlock(struct vnode *vp)
{
	assert(vp->v_refcnt > 0);

	mutex_unlock(&vp->v_lock);
}

/*
 * Returns locked vnode for specified parent and name.
 */
struct vnode *
vn_lookup(struct vnode *parent, const char *name, size_t len)
{
	struct list *head, *n;
	struct vnode *vp;

	vdbgvn("vn_lookup: parent=%p name=%s len=%zu\n", parent, name, len);

	head = &vnode_table[vn_hash(parent, name, len)];

	mutex_lock(&vnode_mutex);
	for (n = list_first(head); n != head; n = list_next(n)) {
		vp = list_entry(n, struct vnode, v_link);
		if (vp->v_parent == parent &&
		    !strncmp(vp->v_name, name, len) &&
		    !vp->v_name[len] &&
		    vp->v_refcnt > 0 &&
		    !(vp->v_flags & VHIDDEN)) {
			vp->v_refcnt++;
			mutex_unlock(&vnode_mutex);
			vn_lock(vp);
			return vp;
		}
	}
	mutex_unlock(&vnode_mutex);
	return NULL;		/* not found */
}

/*
 * Hide a vnode
 */
void
vn_hide(struct vnode *vp)
{
	assert(mutex_owner(&vp->v_lock) == thread_cur());

	vp->v_flags |= VHIDDEN;
}

/*
 * Unhide a vnode
 */
void
vn_unhide(struct vnode *vp)
{
	assert(mutex_owner(&vp->v_lock) == thread_cur());

	vp->v_flags &= ~VHIDDEN;
}

/*
 * vn_stat - get stat from vnode
 */
int
vn_stat(struct vnode *vp, struct stat *st)
{
	assert(mutex_owner(&vp->v_lock) == thread_cur());

	memset(st, 0, sizeof(struct stat));

	st->st_ino = (intptr_t)vp;
	st->st_size = vp->v_size;
	st->st_mode = vp->v_mode;
	st->st_blksize = DEV_BSIZE;
	st->st_blocks = vp->v_size / DEV_BSIZE;
	st->st_uid = 0;
	st->st_gid = 0;
	st->st_rdev = 0;

	return 0;
}

/*
 * Allocate new vnode for specified parent & name
 *
 * Returns locked vnode with reference count 1.
 */
struct vnode *
vget(struct mount *mount, struct vnode *parent, const char *name, size_t len)
{
	char *v_name;
	int err;
	struct list *head;
	struct vnode *vp;

	assert(len < PATH_MAX);

	vdbgvn("vget: parent=%p name=%s len=%zu\n", parent, name, len);

	if (!(vp = malloc(sizeof(struct vnode))))
		return NULL;
	if (!(v_name = malloc(len + 1))) {
		free(vp);
		return NULL;
	}

	strlcpy(v_name, name, len + 1);

	*vp = (struct vnode) {
		.v_parent = parent,
		.v_mount = mount,
		.v_refcnt = 1,
		.v_name = v_name,
	};

	mutex_init(&vp->v_lock);

	/* allocate fs specific data for vnode  */
	if ((err = VFS_VGET(vp)) != 0) {
		free(vp->v_name);
		free(vp);
		return NULL;
	}
	vfs_busy(vp->v_mount);
	vn_lock(vp);

	head = &vnode_table[vn_hash(parent, name, len)];

	mutex_lock(&vnode_mutex);
	list_insert(head, &vp->v_link);
	mutex_unlock(&vnode_mutex);

	/* reference parent */
	if (parent) {
		assert(mutex_owner(&parent->v_lock) == thread_cur());
		vref(parent);
	}

	return vp;
}

/*
 * Allocate a new vnode for pipe
 */
struct vnode *
vget_pipe(void)
{
	struct vnode *vp;

	if (!(vp = malloc(sizeof(struct vnode))))
		return NULL;

	*vp = (struct vnode) {
		.v_mode = S_IFIFO,
		.v_refcnt = 1,
	};

	mutex_init(&vp->v_lock);

	vn_lock(vp);

	return vp;
}

/*
 * Unlock vnode and decrement its reference count.
 *
 * Releases vnode if reference count reaches 0.
 */
void
vput(struct vnode *vp)
{
	vdbgvn("vput: vp=%p v_refcnt=%u v_name=%s\n",
	    vp, vp->v_refcnt, vp->v_name);

	assert(vp);
	assert(vp->v_refcnt > 0);
	assert(mutex_owner(&vp->v_lock) == thread_cur());

	struct vnode *pvp = vp->v_parent;

	mutex_lock(&vnode_mutex);
	--vp->v_refcnt;
	if (vp->v_refcnt > 0) {
		mutex_unlock(&vnode_mutex);
		vn_unlock(vp);
		return;
	}
	/* unnamed vnodes (pipes) are not in hash table */
	if (vp->v_name)
		list_remove(&vp->v_link);
	mutex_unlock(&vnode_mutex);

	/* deallocate fs specific vnode data */
	if (vp->v_mount) {
		VOP_INACTIVE(vp);
		vfs_unbusy(vp->v_mount);
	}
	mutex_unlock(&vp->v_lock);
	assert(mutex_owner(&vp->v_lock) == NULL);
	free(vp->v_name);
	free(vp);

	/* release parent */
	if (pvp) {
		vn_lock(pvp);
		vput(pvp);
	}
}

/*
 * Increment the reference count on an active vnode.
 */
void
vref(struct vnode *vp)
{
	assert(vp->v_refcnt > 0);	/* Need vget */

	vdbgvn("vref: vp=%p v_refcnt=%u v_name=%s\n",
	    vp, vp->v_refcnt, vp->v_name);

	mutex_lock(&vnode_mutex);
	++vp->v_refcnt;
	mutex_unlock(&vnode_mutex);
}

/*
 * vgone() is called when unreferenced vnode is no longer valid.
 */
void
vgone(struct vnode *vp)
{
	assert(vp->v_refcnt == 1);
	assert(mutex_owner(&vp->v_lock) == thread_cur());

	vdbgvn("vgone: vp=%p v_refcnt=%u v_name=%s\n",
	    vp, vp->v_refcnt, vp->v_name);

	/* release parent */
	if (vp->v_parent) {
		assert(mutex_owner(&vp->v_parent->v_lock) == thread_cur());
		vn_lock(vp->v_parent);
		vput(vp->v_parent);
	}

	mutex_lock(&vnode_mutex);
	list_remove(&vp->v_link);
	mutex_unlock(&vnode_mutex);

	vfs_unbusy(vp->v_mount);
	mutex_unlock(&vp->v_lock);
	assert(mutex_owner(&vp->v_lock) == NULL);
	free(vp->v_name);
	free(vp);
}

/*
 * Dump all all vnode.
 */
const char *
vnode_type(mode_t mode)
{
	switch (IFTODT(mode)) {
	case DT_UNKNOWN: return "UNK";
	case DT_FIFO: return "FIFO";
	case DT_CHR: return "CHR";
	case DT_DIR: return "DIR";
	case DT_BLK: return "BLK";
	case DT_REG: return "REG";
	case DT_LNK: return "LNK";
	case DT_SOCK: return "SOCK";
	case DT_WHT: return "WHT";
	default: return "????";
	}
}

void
vnode_dump(void)
{
	int i;
	struct list *head, *n;
	struct vnode *vp;

	mutex_lock(&vnode_mutex);
	info("vnode dump\n");
	info("==========\n");
	info(" vnode      parent     mount      type refcnt blkno    data       name\n");
	info(" ---------- ---------- ---------- ---- ------ -------- ---------- ----------\n");

	for (i = 0; i < VNODE_BUCKETS; i++) {
		head = &vnode_table[i];
		for (n = list_first(head); n != head; n = list_next(n)) {
			vp = list_entry(n, struct vnode, v_link);

			info(" %10p %10p %10p %4s %6d %8d %10p %s\n",
			    vp, vp->v_parent, vp->v_mount, vnode_type(vp->v_mode),
			    vp->v_refcnt, vp->v_blkno, vp->v_data, vp->v_name);
		}
	}
	mutex_unlock(&vnode_mutex);
}

int
vop_nullop(void)
{
	return 0;
}

int
vop_einval(void)
{
	return -EINVAL;
}

void
vnode_init(void)
{
	mutex_init(&vnode_mutex);
	for (size_t i = 0; i < VNODE_BUCKETS; i++)
		list_init(&vnode_table[i]);
}

