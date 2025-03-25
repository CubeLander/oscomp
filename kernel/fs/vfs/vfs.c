#include <kernel/fs/vfs/vfs.h>
#include <kernel/mm/kmalloc.h>
#include <kernel/sched/sched.h>
#include <kernel/types.h>
#include <util/string.h>

/* Global mount list */
static struct list_head mount_list;
static spinlock_t mount_lock;

/* Mount point hash table */
static struct hashtable mount_hashtable;

/**
 * vfs_kern_mount - Mount a filesystem without adding to namespace
 * @type: Filesystem type to mount
 * @flags: Mount flags
 * @name: Device name to mount (or other identifier)
 * @data: Filesystem-specific data
 *
 * Creates a mount point for a filesystem of the specified type.
 * This performs the mount operation but doesn't add the mount
 * point to any namespace - it's a kernel-internal mount used
 * primarily for:
 *   - Initial root filesystem mounting during boot
 *   - Mounting filesystems that will later be moved to user namespaces
 *   - Temporary internal mounts for operations like fs snapshots
 *
 * Returns a vfsmount structure on success, NULL on failure.
 */
struct vfsmount* vfs_kern_mount(struct fsType* type, int flags, const char* name, void* data) {
	struct vfsmount* mnt;
	struct superblock* sb;
	static int mount_id = 0;

	if (!type)
		return NULL;

	/* Get or create a superblock */
	sb = fsType_createMount(type, flags, name, data);
	if (IS_ERR(sb))
		return NULL;

	/* Create new mount structure */
	mnt = kmalloc(sizeof(struct vfsmount));
	if (!mnt) {
		deactivate_super_safe(sb);
		drop_super(sb);
		return NULL;
	}

	/* Initialize the mount structure */
	memset(mnt, 0, sizeof(struct vfsmount));
	atomic_set(&mnt->mnt_refcount, 1);
	mnt->mnt_superblock = sb;
	mnt->mnt_root = dget(sb->s_global_root_dentry);
	mnt->mnt_flags = flags;
	mnt->mnt_id = mount_id++;

	/* Store device name if provided */
	if (name && *name) {
		mnt->mnt_devname = kstrdup(name,0);
		/* Non-fatal if allocation fails, it's just for informational purposes */
	}

	/* Initialize list heads */
	INIT_LIST_HEAD(&mnt->mnt_list_children);

	/* Initialize list nodes */
	INIT_LIST_HEAD(&mnt->mnt_node_superblock);
	INIT_LIST_HEAD(&mnt->mnt_node_parent);
	INIT_LIST_HEAD(&mnt->mnt_node_global);
	INIT_LIST_HEAD(&mnt->mnt_node_namespace);

	/* Add to superblock's mount list */
	spin_lock(&sb->s_list_mounts_lock);
	list_add(&mnt->mnt_node_superblock, &sb->s_list_mounts);
	spin_unlock(&sb->s_list_mounts_lock);

	return mnt;
}

/**
 * vfs_mkdir - Create a directory
 * @dir: Parent directory's inode
 * @dentry: Dentry for the new directory
 * @mode: Permission mode
 *
 * Returns 0 on success or negative error code
 */
int vfs_mkdir(struct inode* dir, struct dentry* dentry, fmode_t mode) {
	int error;

	if (!dir || !dentry)
		return -EINVAL;

	if (!dir->i_op || !dir->i_op->mkdir)
		return -EPERM;

	/* Check if entry already exists */
	if (dentry->d_inode)
		return -EEXIST;

	/* Check directory permissions */
	error = inode_checkPermission(dir, MAY_WRITE | MAY_EXEC);
	if (error)
		return error;

	return dir->i_op->mkdir(dir, dentry, mode & ~current_task()->umask);
}

/**
 * vfs_rmdir - Remove a directory
 * @dir: Parent directory's inode
 * @dentry: Directory to remove
 *
 * Returns 0 on success or negative error code
 */
int vfs_rmdir(struct inode* dir, struct dentry* dentry) {
	int error;

	if (!dir || !dentry || !dentry->d_inode)
		return -EINVAL;

	if (!dir->i_op || !dir->i_op->rmdir)
		return -EPERM;

	/* Check directory permissions */
	error = inode_checkPermission(dir, MAY_WRITE | MAY_EXEC);
	if (error)
		return error;

	/* Cannot remove non-empty directory */
	if (!is_empty_dir(dentry->d_inode))
		return -ENOTEMPTY;

	return dir->i_op->rmdir(dir, dentry);
}

/**
 * vfs_link - Create a hard link
 * @old_dentry: Existing file's dentry
 * @dir: Directory to create link in
 * @new_dentry: Dentry for new link
 * @new_inode: Output parameter for resulting inode
 *
 * Returns 0 on success or negative error code
 */
int vfs_link(struct dentry* old_dentry, struct inode* dir, struct dentry* new_dentry, struct inode** new_inode) {
	int error;

	if (!old_dentry || !dir || !new_dentry)
		return -EINVAL;

	if (!dir->i_op || !dir->i_op->link)
		return -EPERM;

	/* Check if target exists */
	if (new_dentry->d_inode)
		return -EEXIST;

	/* Check permissions */
	error = inode_checkPermission(dir, MAY_WRITE);
	if (error)
		return error;

	error = dir->i_op->link(old_dentry, dir, new_dentry);
	if (error)
		return error;

	if (new_inode)
		*new_inode = new_dentry->d_inode;

	return 0;
}


/**
 * vfs_init - Initialize the VFS subsystem
 *
 * Initializes all the core VFS components in proper order.
 * Must be called early during kernel initialization before
 * any filesystem operations can be performed.
 */
int vfs_init(void)
{
    int err;
    init_mount_hash();

    /* Initialize the dcache subsystem */
    sprint("VFS: Initializing dentry cache...\n");
    err = init_dentry_hashtable();
    if (err < 0) {
        sprint("VFS: Failed to initialize dentry cache\n");
        return err;
    }
    
    /* Initialize the inode subsystem */
    sprint("VFS: Initializing inode cache...\n");
    err = inode_cache_init();
		if (err < 0) {
				sprint("VFS: Failed to initialize inode cache\n");
				return err;
		}



    /* Register built-in filesystems */
    sprint("VFS: Registering built-in filesystems...\n");
    err = fsType_register_all();
    if (err < 0) {
        sprint("VFS: Failed to register filesystems\n");
        return err;
    }

    
    sprint("VFS: Initialization complete\n");
    return 0;
}


/**
 * hash_mountpoint - Hash a mountpoint
 */
static unsigned int hash_mountpoint(const void *key, unsigned int size) {
  const struct path *path = (const struct path *)key;
  unsigned long hash = (unsigned long)path->dentry;

  hash = hash * 31 + (unsigned long)path->mnt;
  return hash % size;
}

/**
 * mountpoint_equal - Compare two mountpoints
 */
static int mountpoint_equal(const void *k1, const void *k2) {
  const struct path *path1 = (const struct path *)k1;
  const struct path *path2 = (const struct path *)k2;

  return (path1->dentry == path2->dentry && path1->mnt == path2->mnt);
}

/**
 * init_mount_hash - Initialize the mount hash table
 */
void init_mount_hash(void) {
  hashtable_setup(&mount_hashtable, 256, 70, hash_mountpoint, mountpoint_equal);
	INIT_LIST_HEAD(&mount_list);
  spinlock_init(&mount_lock);
}

/**
 * lookup_mnt - Find a mount for a given path
 */
struct vfsmount *lookup_mnt(struct path *path) {
  struct vfsmount *mnt = NULL;

  /* Look up in the mount hash table */
  mnt = hashtable_lookup(&mount_hashtable, path);
  return mnt;
}


/**
 * get_mount - Increment mount reference count
 */
struct vfsmount *get_mount(struct vfsmount *mnt) {
  if (mnt) {
    atomic_inc(&mnt->mnt_refcount);
    return mnt;
  }
  return NULL;
}

/**
 * put_mount - Decrement mount reference count
 */
void put_mount(struct vfsmount *mnt) {
  if (!mnt)
    return;

  if (atomic_dec_and_test(&mnt->mnt_refcount)) {
    /* Last reference - free the mount */
    spin_lock(&mount_lock);
    list_del(&mnt->mnt_node_global);
    spin_unlock(&mount_lock);

    spin_lock(&mnt->mnt_superblock->s_list_mounts_lock);
    list_del(&mnt->mnt_node_superblock);
    spin_unlock(&mnt->mnt_superblock->s_list_mounts_lock);

    dentry_put(mnt->mnt_root);
    if (mnt->mnt_devname)
      kfree(mnt->mnt_devname);
    kfree(mnt);
  }
}