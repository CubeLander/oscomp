#ifndef _VFSMOUNT_H
#define _VFSMOUNT_H

#include "forward_declarations.h"
#include <kernel/util.h>

/**
 * Mount point structure
 */
struct vfsmount {
	struct dentry* mnt_root; /* Root of this mount */
	struct path mnt_path;    /* Mount point path (location) */
	int32 mnt_flags;         /* Mount flags */
	int32 mnt_id;            /* Unique identifier for this mount */

	/* List management */
	struct superblock* mnt_superblock;    /* Superblock of this mount */
	struct list_head mnt_node_superblock; /* Link in sb->s_list_mounts list */
	struct list_head mnt_node_global;     /* Link in global mount list */
	struct list_head mnt_hash_node;       /* For hashtable */

	/* Namespace mount list linkage */
	struct mnt_namespace* mnt_ns;        /* Namespace containing this mount */
	struct list_head mnt_node_namespace; /* Link in namespace */
	struct list_head mnt_child_list;     /* List of child mounts */
	struct list_head mnt_child_node;     /* Node in parent's child list */

	/* Reference counting */
	atomic_t mnt_refcount; /* Reference count */

	/* Device info */
	char* mnt_devname; /* Device name */
};

/*
 * Mount registry and management
 */

/* Initialize mount system */
void init_mount_hash(void);

/* Mount management */
int32 do_mount(const char* dev_name, const char* path, const char* fstype, uint64 flags, void* data);
/* Unmount operations */
int32 do_umount(struct vfsmount* mnt, int32 flags);

/* Reference counting */
struct vfsmount* mount_ref(struct vfsmount* mnt);
void mount_unref(struct vfsmount* mnt);

/* Mount point lookup */
struct vfsmount* lookup_vfsmount(struct dentry* dentry);

/* Mount traversal */
int32 iterate_mounts(int32 (*f)(struct vfsmount*, void*), void* arg, struct vfsmount* root);


extern struct list_head mount_list;
extern spinlock_t mount_lock;

#endif