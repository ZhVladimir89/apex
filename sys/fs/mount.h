#ifndef mount_h
#define mount_h

#include <list.h>
#include <sys/mount.h>

struct statfs;

/*
 * Mount data
 */
struct mount {
	struct list		 m_link;	/* link to next mount point */
	const struct vfsops	*m_op;		/* pointer to vfs operations */
	unsigned long		 m_flags;	/* mount flags */
	unsigned		 m_count;	/* reference count */
	int			 m_devfd;	/* mounted device handle */
	struct vnode		*m_root;	/* root vnode */
	struct vnode		*m_covered;	/* vnode covered on parent fs */
	void			*m_data;	/* private data for fs */
};

/*
 * Filesystem type switch table
 */
struct vfssw {
	const char		*vs_name;	/* name of file system */
	const struct vfsops	*vs_op;		/* pointer to vfs operation */
};

/*
 * Operations supported on virtual file system
 */
typedef int (*vfsop_init_fn)(void);
typedef int (*vfsop_mount_fn)(struct mount *, int, const void *);
typedef int (*vfsop_umount_fn)(struct mount *);
typedef int (*vfsop_sync_fn)(struct mount *);
typedef int (*vfsop_vget_fn)(struct vnode *);
typedef int (*vfsop_statfs_fn)(struct mount *, struct statfs *);

struct vfsops {
	vfsop_init_fn	     vfs_init;
	vfsop_mount_fn	     vfs_mount;
	vfsop_umount_fn	     vfs_umount;
	vfsop_sync_fn	     vfs_sync;
	vfsop_vget_fn	     vfs_vget;
	vfsop_statfs_fn	     vfs_statfs;
	const struct vnops  *vfs_vnops;
};

/*
 * VFS interface
 */
#define VFS_MOUNT(MP, FL, DAT)	((MP)->m_op->vfs_mount)(MP, FL, DAT)
#define VFS_UMOUNT(MP)		((MP)->m_op->vfs_umount)(MP)
#define VFS_SYNC(MP)		((MP)->m_op->vfs_sync)(MP)
#define VFS_VGET(VP)		((VP)->v_mount->m_op->vfs_vget)(VP)
#define VFS_STATFS(MP, SFP)	((MP)->m_op->vfs_statfs)(MP, SFP)

/*
 * File system registration
 */
#define REGISTER_FILESYSTEM(name) \
	static const struct vfssw __filesystem_##name \
	    __attribute__((section(".filesystems"), used)) = { \
		.vs_name = #name, \
		.vs_op = &name##_vfsops, \
	}

/*
 * Generic null/invalid operations
 */
int vfs_nullop(void);
int vfs_einval(void);

#endif /* !mount_h */
