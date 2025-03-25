#ifndef _NAMESPACE_H
#define _NAMESPACE_H

#include <kernel/types.h>
#include <util/atomic.h>
#include <util/list.h>

/* Forward declarations */
struct superblock;
struct dentry;
struct path;

/* Mount flags */
#define MNT_RDONLY 1              /* Mount read-only */
#define MNT_NOSUID 2              /* Ignore suid and sgid bits */
#define MNT_NODEV 4               /* Disallow access to device special files */
#define MNT_NOEXEC 8              /* Disallow program execution */
#define MNT_RELATIME (1 << 21)    /* Update atime relative to mtime/ctime */
#define MNT_STRICTATIME (1 << 29) /* Always perform atime updates */

/**
 * Mount namespace structure
 *
 * A mount namespace contains an isolated view of the filesystem hierarchy.
 * Different processes can have different mount namespaces, allowing
 * containerization and isolation.
 */
struct mnt_namespace {
	/* Root of the mount tree for this namespace */
	struct vfsmount* root;

	/* All mounts in this namespace */
	struct list_head mount_list;

	/* Count of mounts in this namespace */
	int mount_count;

	/* Reference counting */
	atomic_t count;

	/* Owner info */
	uid_t owner;

	/* Protection */
	spinlock_t lock;
};

/**
 * Mount point structure
 */
struct vfsmount {
	struct dentry* mnt_root; /* Root of this mount */

	int mnt_flags; /* Mount flags */

	int mnt_id; /* Unique identifier for this mount */
	/* Mount point location */
	struct dentry* mnt_mountpoint; /* Dentry where this fs is mounted */

	/* List management */
	struct superblock* mnt_superblock;   /* Superblock of this mount */
	struct list_node mnt_node_superblock; /* Link in sb->s_list_mounts list */

	struct vfsmount* mnt_parent;        /* Parent mount point */
	struct list_node mnt_node_parent;   /* Link in parent's mnt_list_children */
	struct list_head mnt_list_children; /* List of children mounts */

	struct list_node mnt_node_global; /* Link in global mount list */

	/* Namespace mount list linkage */
	struct list_node mnt_node_namespace;

	/* Reference counting */
	atomic_t mnt_refcount; /* Reference count */

	/* Device info */
	const char* mnt_devname; /* Device name */
};


/*
 * Mount registry and management
 */

/* Initialize mount system */
void init_mount_hash(void);

/* Mount management */
int do_mount(const char* dev_name, const char* path, const char* fsType, unsigned long flags,
             void* data);

/* Unmount operations */
int do_umount(struct vfsmount* mnt, int flags);

/* Reference counting */
struct vfsmount* get_mount(struct vfsmount* mnt);
void put_mount(struct vfsmount* mnt);

/* Mount point lookup */
struct vfsmount* lookup_vfsmount(struct dentry* dentry);
struct vfsmount* lookup_mnt(struct path* path);

/* Mount registry management */
struct mnt_namespace* create_namespace(struct mnt_namespace* parent);
inline struct mnt_namespace* grab_namespace(struct mnt_namespace* ns);
void put_namespace(struct mnt_namespace* ns);

/* Mount traversal */
bool is_mounted(struct dentry* dentry);
int iterate_mounts(int (*f)(struct vfsmount*, void*), void* arg, struct vfsmount* root);

#endif /* _NAMESPACE_H */

// [rootfs mount]
// ├── parent: NULL
// ├── children: [/home mount], [/mnt/cdrom mount]
// │
// ├── [/home mount]
// │   ├── parent: [rootfs mount]
// │   └── children: [/home/user/data mount]
// │       │
// │       └── [/home/user/data mount]
// │           ├── parent: [/home mount]
// │           └── children: []
// │
// └── [/mnt/cdrom mount]
//     ├── parent: [rootfs mount]
//     └── children: []