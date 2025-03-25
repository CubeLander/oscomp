#include <kernel/sched/fs_struct.h>
#include <kernel/fs/vfs/namespace.h>
#include <kernel/fs/vfs/vfs.h>
#include <kernel/mm/kmalloc.h>
#include <util/string.h>

/**
 * setup_fs_struct - Initialize a new fs_struct
 * @fs: The fs_struct to initialize
 *
 * Sets up a new fs_struct with default values.
 * Returns 0 on success, -ENOMEM on memory allocation failure.
 */
struct fs_struct *setup_fs_struct(void) {

	struct fs_struct *fs = kmalloc(sizeof(struct fs_struct));
  if (!fs)
    return NULL;

  /* Initialize locks and reference count */
  spinlock_init(&fs->lock);
  atomic_set(&fs->count, 1);

  /* Initialize pwd and root paths */
  memset(&fs->pwd, 0, sizeof(fs->pwd));
  memset(&fs->root, 0, sizeof(fs->root));

  return 0;
}


/**
 * copy_fs_struct - Create a copy of an fs_struct
 * @old_fs: The fs_struct to copy
 * 
 * Creates a new fs_struct with the same values as the old one.
 * Returns a new fs_struct or NULL on failure.
 */
struct fs_struct *copy_fs_struct(struct fs_struct *old_fs)
{
    struct fs_struct *new_fs;
    
    if (!old_fs)
        return NULL;
    
    new_fs = kmalloc(sizeof(struct fs_struct));
    if (!new_fs)
        return NULL;
    
    /* Initialize the new structure */
    init_fs_struct(new_fs);
    
    /* Copy root and pwd with proper reference counting */
    spin_lock(&old_fs->lock);
    
    if (old_fs->root.dentry) {
        new_fs->root.dentry = get_dentry(old_fs->root.dentry);
        new_fs->root.mnt = get_mount(old_fs->root.mnt);
    }
    
    if (old_fs->pwd.dentry) {
        new_fs->pwd.dentry = get_dentry(old_fs->pwd.dentry);
        new_fs->pwd.mnt = get_mount(old_fs->pwd.mnt);
    }
    
    spin_unlock(&old_fs->lock);
    
    return new_fs;
}

/**
 * put_fs_struct - Release a reference to an fs_struct
 * @fs: The fs_struct to release
 * 
 * Decrements the reference count and frees the structure when it reaches zero.
 */
void put_fs_struct(struct fs_struct *fs)
{
    if (!fs)
        return;
    
    /* Decrease reference count */
    if (atomic_dec_and_test(&fs->count)) {
        /* Release references to root and pwd */
        path_destroy(&fs->root);
        path_destroy(&fs->pwd);
        
        /* Free the structure */
        kfree(fs);
    }
}

/**
 * set_fs_root - Set the root directory for an fs_struct
 * @fs: The fs_struct to modify
 * @path: The new root path
 * 
 * Updates the root directory for an fs_struct.
 */
void set_fs_root(struct fs_struct *fs, const struct path *path)
{
    struct path old_root;
    
    if (!fs || !path)
        return;
    
    spin_lock(&fs->lock);
    
    /* Save old path for cleaning up */
    old_root = fs->root;
    
    /* Set new path with proper reference counts */
    fs->root.dentry = get_dentry(path->dentry);
    fs->root.mnt = get_mount(path->mnt);
    
    spin_unlock(&fs->lock);
    
    /* Release references to old path */
    path_destroy(&old_root);
}

/**
 * set_fs_pwd - Set the working directory for an fs_struct
 * @fs: The fs_struct to modify
 * @path: The new working directory path
 * 
 * Updates the working directory for an fs_struct.
 */
void set_fs_pwd(struct fs_struct *fs, const struct path *path)
{
    struct path old_pwd;
    
    if (!fs || !path)
        return;
    
    spin_lock(&fs->lock);
    
    /* Save old path for cleaning up */
    old_pwd = fs->pwd;
    
    /* Set new path with proper reference counts */
    fs->pwd.dentry = get_dentry(path->dentry);
    fs->pwd.mnt = get_mount(path->mnt);
    
    spin_unlock(&fs->lock);
    
    /* Release references to old path */
    path_destroy(&old_pwd);
}
