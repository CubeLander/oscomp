#include <kernel/fs/vfs/vfs.h>
#include <kernel/sched/sched.h>
#include <kernel/sched/fs_struct.h>
#include <kernel/types.h>
#include <util/hashtable.h>

/* Global mount data structures */
static struct hashtable mount_hashtable;
static struct list_head global_mounts_list;
static spinlock_t mount_lock;
static int mount_id_counter = 0;
#define MOUNT_HASH_SIZE 256

/* Mount hash functions */
static unsigned int mount_hash_func(const void* key, unsigned int size) {
    /* Hash based on dentry pointer */
    const struct dentry* dentry = (const struct dentry*)key;
    return hash_ptr(dentry, size);
}

static int mount_key_equals(const void* key1, const void* key2) {
    /* Compare dentry pointers */
    return key1 == key2;
}

/**
 * init_mount_hash - Initialize mount namespace subsystem
 */
int init_mount_hash(void) {
    int ret;
    
    /* Initialize the global mount list */
    INIT_LIST_HEAD(&global_mounts_list);
    
    /* Initialize the mount hash table */
    ret = hashtable_setup(&mount_hashtable, MOUNT_HASH_SIZE, 70, 
                        mount_hash_func, mount_key_equals);
    if (ret < 0)
        return ret;
    
    /* Initialize the lock */
    spinlock_init(&mount_lock);
    
    return 0;
}

/**
 * lookup_vfsmount - Find mount point for a dentry
 * @dentry: Dentry to look up
 *
 * Searches for a mount point that is mounted on the specified dentry.
 *
 * Returns the mount if found, NULL otherwise
 */
struct vfsmount* lookup_vfsmount(struct dentry* dentry) {
    struct vfsmount* mnt;
    
    if (!dentry)
        return NULL;
    
    spin_lock(&mount_lock);
    mnt = hashtable_lookup(&mount_hashtable, dentry);
    if (mnt)
        get_mount(mnt);
    spin_unlock(&mount_lock);
    
    return mnt;
}







inline struct mnt_namespace* grab_namespace(struct mnt_namespace* ns) {
	if (ns) {
		atomic_inc(&ns->count);
	}
	return ns;
}

/**
 * put_namespace - Decrement reference count on a mount namespace
 * @ns: Mount namespace
 *
 * Drops a reference on the specified mount namespace.
 * When the last reference is dropped, the namespace is destroyed.
 */
void put_namespace(struct mnt_namespace* ns) {
	if (!ns)
		return;

	if (atomic_dec_and_test(&ns->count)) {
		/* Free all mounts in this namespace */
		struct vfsmount *mnt, *next;
		list_for_each_entry_safe(mnt, next, &ns->mount_list, mnt_node_global) {
			list_del(&mnt->mnt_node_global);
			put_mount(mnt);
		}

		/* Free the namespace structure itself */
		kfree(ns);
	}
}

/**
 * create_namespace - Create a new mount namespace
 * @parent: Parent namespace to clone from (or NULL for empty)
 *
 * Creates a new mount namespace, optionally cloning the contents
 * from an existing namespace.
 *
 * Returns the new namespace or NULL on failure.
 */
struct mnt_namespace* create_namespace(struct mnt_namespace* parent) {
	struct mnt_namespace* ns;

	ns = kmalloc(sizeof(*ns));
	if (!ns)
		return NULL;

	/* Initialize the namespace */
	INIT_LIST_HEAD(&ns->mount_list);
	ns->mount_count = 0;
	atomic_set(&ns->count, 1);
	ns->owner = current_task()->euid;
	spinlock_init(&ns->lock);

	/* Clone parent namespace if provided */
	if (parent) {
		/* Implementation for cloning would go here */
		/* This requires a deep copy of the mount tree */
		/* For simplicity, just starting with an empty namespace */
	}

	return ns;
}

/**
 * get_mount - Increase reference count on a mount
 * @mnt: Mount point to reference
 *
 * Returns the mount with incremented reference, or NULL if invalid
 */
struct vfsmount* get_mount(struct vfsmount* mnt) {
	if (mnt) {
		atomic_inc(&mnt->mnt_refcount);
	}
	return mnt;
}

/**
 * put_mount - Decrease reference count on a mount
 * @mnt: Mount point to dereference
 */
void put_mount(struct vfsmount* mnt) {
	if (!mnt)
		return;

	if (atomic_dec_and_test(&mnt->mnt_refcount)) {
		/* Reference count reached zero, clean up the mount */
		struct superblock* sb = mnt->mnt_superblock;

		/* Remove from any lists */
		spin_lock(&mount_lock);

		/* Remove from superblock's mount list */
		list_del(&mnt->mnt_node_superblock);

		/* Remove from parent's children list */
		if (mnt->mnt_parent) {
			list_del(&mnt->mnt_node_parent);
		}

		/* Remove from global mount list */
		list_del(&mnt->mnt_node_global);

		spin_unlock(&mount_lock);

		/* Free resources */
		if (mnt->mnt_devname)
			kfree((void*)mnt->mnt_devname);

		/* Release references */
		dput(mnt->mnt_root);
		dput(mnt->mnt_mountpoint);

		/* Decrease superblock reference count */
		if (sb)
			drop_super(sb);

		/* Free the mount structure */
		kfree(mnt);
	}
}

/**
 * lookup_vfsmount - Find mount point for a dentry
 * @dentry: Dentry to look up
 *
 * Searches for a mount point that is mounted on the specified dentry.
 *
 * Returns the mount if found, NULL otherwise
 */
struct vfsmount* lookup_vfsmount(struct dentry* dentry) {
    struct vfsmount* mnt;
    
    if (!dentry)
        return NULL;
    
    spin_lock(&mount_lock);
    mnt = hashtable_lookup(&mount_hashtable, dentry);
    if (mnt)
        get_mount(mnt);
    spin_unlock(&mount_lock);
    
    return mnt;
}


/**
 * lookup_mnt - Find mount point for a path
 * @path: Path to look up
 *
 * Returns the mount if found, NULL otherwise
 */
struct vfsmount* lookup_mnt(struct path* path) {
	if (!path || !path->dentry)
		return NULL;

	return lookup_vfsmount(path->dentry);
}

/**
 * is_mounted - Check if a dentry is a mount point
 * @dentry: Dentry to check
 *
 * Returns true if the dentry is a mount point, false otherwise
 */
bool is_mounted(struct dentry* dentry) {
	struct vfsmount* mnt;

	if (!dentry)
		return false;

	mnt = lookup_vfsmount(dentry);
	if (mnt) {
		put_mount(mnt);
		return true;
	}

	return false;
}

/**
 * do_mount - Mount a filesystem
 * @dev_name: Device name
 * @path: Mount point path
 * @fsType: Filesystem type
 * @flags: Mount flags
 * @data: Filesystem-specific data
 *
 * Mounts a filesystem of the specified type at the given path.
 *
 * Returns 0 on success, negative error code otherwise
 */
int do_mount(const char* dev_name, const char* path, const char* fsType, unsigned long flags,
             void* data) {
	struct vfsmount* mnt;
	struct path target_path;
	struct file_system_type* type;
	int error;

	/* Look up filesystem type */
	type = fsType_lookup(fsType);
	if (!type)
		return -ENODEV;

	/* Look up mount point path */
	error = path_create(path, LOOKUP_FOLLOW, &target_path);
	if (error)
		return error;

	/* Must be a directory */
	if (!S_ISDIR(target_path.dentry->d_inode->i_mode)) {
		path_put(&target_path);
		return -ENOTDIR;
	}

	/* Create kernel mount */
	mnt = vfs_kern_mount(type, flags, dev_name, data);
	if (!mnt) {
		path_put(&target_path);
		return -ENOMEM;
	}

	/* Set up mount point */
	mnt->mnt_mountpoint = dget(target_path.dentry);
	mnt->mnt_parent = get_mount(target_path.mnt);

    /* Add to parent's children list */
    spin_lock(&mount_lock);
    list_add(&mnt->mnt_node_parent, &mnt->mnt_parent->mnt_list_children);
    
    /* Add to global mount list */
    list_add(&mnt->mnt_node_global, &global_mounts_list);
    
    /* Add to hash table using mountpoint dentry as key */
    hashtable_insert(&mount_hashtable, mnt->mnt_mountpoint, mnt);

	/* Add to current namespace */
	if (current_task()->fs) {
		struct mnt_namespace* ns = current_task()->fs->mnt_ns;
		if (ns) {
			spin_lock(&ns->lock);
			list_add(&mnt->mnt_node_namespace, &ns->mount_list);
			ns->mount_count++;
			spin_unlock(&ns->lock);
		}
	}

	spin_unlock(&mount_lock);

	path_put(&target_path);
	return 0;
}

/**
 * do_umount - Unmount a filesystem
 * @mnt: Mount to unmount
 * @flags: Unmount flags
 *
 * Removes a mount point from the filesystem.
 *
 * Returns 0 on success, negative error code otherwise
 */
int do_umount(struct vfsmount* mnt, int flags) {
	if (!mnt)
		return -EINVAL;

	/* Check if it has child mounts */
	spin_lock(&mount_lock);
	if (!list_empty(&mnt->mnt_list_children)) {
		spin_unlock(&mount_lock);
		return -EBUSY;
	}

	/* Remove from namespace */
	if (current_task()->fs) {
		struct mnt_namespace* ns = current_task()->fs->mnt_ns;
		if (ns) {
			spin_lock(&ns->lock);
			list_del(&mnt->mnt_node_namespace);
			ns->mount_count--;
			spin_unlock(&ns->lock);
		}
	}

    /* Remove from hash table */
    hashtable_remove(&mount_hashtable, mnt->mnt_mountpoint);


	/* Remove from global hash and parent */
	list_del(&mnt->mnt_node_global);
	if (mnt->mnt_parent) {
		list_del(&mnt->mnt_node_parent);
	}

	spin_unlock(&mount_lock);

	/* Decrease reference count (may free the mount) */
	put_mount(mnt);

	return 0;
}


/**
 * iterate_mounts - Traverse mount points
 * @f: Callback function for each mount
 * @arg: Argument to pass to callback
 * @root: Root of traversal (NULL for all mounts)
 *
 * Traverses mount points, calling the callback for each one.
 * Traversal continues until callback returns non-zero.
 *
 * Returns the last non-zero value from callback, or 0 if all calls succeeded
 */
int iterate_mounts(int (*f)(struct vfsmount*, void*), void* arg, struct vfsmount* root) {
    struct vfsmount* mnt;
    int res = 0;
    
    if (root) {
        /* Just traverse the subtree */
        res = iterate_mount_subtree(f, arg, root);
    } else {
        /* Traverse all mounts */
        spin_lock(&mount_lock);
        list_for_each_entry(mnt, &global_mounts_list, mnt_node_global) {
            res = f(mnt, arg);
            if (res)
                break;
        }
        spin_unlock(&mount_lock);
    }
    
    return res;
}

/* Helper for iterate_mounts */
static int iterate_mount_subtree(int (*f)(struct vfsmount*, void*), void* arg, struct vfsmount* root) {
    struct vfsmount* mnt;
    int res;
    
    /* Call for this mount first */
    res = f(root, arg);
    if (res)
        return res;
    
    /* Then traverse children */
    list_for_each_entry(mnt, &root->mnt_list_children, mnt_node_parent) {
        res = iterate_mount_subtree(f, arg, mnt);
        if (res)
            return res;
    }
    
    return 0;
}