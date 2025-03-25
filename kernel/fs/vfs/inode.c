#include <kernel/types.h>
#include <kernel/fs/vfs/vfs.h>
#include <kernel/mm/kmalloc.h>
#include <kernel/sched/sched.h>
#include <util/string.h>

static void __clear_inode(struct inode* inode);
static void* __inode_get_key(struct list_node* node);
static unsigned int __inode_hash_func(const void *key, unsigned int size);
static int __inode_key_equals(const void *k1, const void *k2);
static void hash_inode(struct inode *inode);
static void unhash_inode(struct inode *inode);
static void wake_up_inode(struct inode *inode);
static struct inode *__inode_acquireLocked(struct superblock *sb, unsigned long ino);
static void evict_inode(struct inode* inode);
static int generic_permission(struct inode* inode, int mask);
static void __unlock_new_inode(struct inode *inode);

/* Global inode hash table */
struct hashtable inode_hashtable;

struct inode_key{
	struct superblock* sb;
	unsigned long ino;
};



/**
 * Initialize the inode cache and hash table
 */
int inode_cache_init(void) {
    int err;

    sprint("Initializing inode cache\n");

    /* Initialize hash table with our callbacks */
    err = hashtable_setup(&inode_hashtable, 1024, 75, 
                         __inode_hash_func, 
                         __inode_get_key,
                         __inode_key_equals);
    if (err != 0) {
        sprint("Failed to initialize inode hashtable: %d\n", err);
        return err;
    }

    sprint("Inode cache initialized\n");
    return 0;
}

/**
 * generic_permission - Check for access rights on a Unix-style file system
 * @inode: inode to check permissions on
 * @mask: access mode to check for (MAY_READ, MAY_WRITE, MAY_EXEC)
 *
 * Standard Unix permission checking implementation that most filesystems
 * can use directly or as a basis for their permission checking.
 *
 * Returns 0 if access is allowed, -EACCES otherwise.
 */
static int generic_permission(struct inode* inode, int mask) {
  int mode = inode->i_mode;
  int res = 0;

  /* Root can do anything */
  if (current_task()->euid == 0)
    return 0;

  /* Nobody gets write access to a read-only filesystem */
  if ((mask & MAY_WRITE) && IS_RDONLY(inode))
    return -EROFS;

  /* Check if file is accessible by the user */
  if (current_task()->euid == inode->i_uid) {
    mode >>= 6; /* Use the user permissions */
  } else if (current_is_in_group(inode->i_gid)) {
    mode >>= 3; /* Use the group permissions */
  }

  /* Check if the mask is allowed in the mode */
  if ((mask & MAY_READ) && !(mode & S_IRUSR))
    res = -EACCES;
  if ((mask & MAY_WRITE) && !(mode & S_IWUSR))
    res = -EACCES;
  if ((mask & MAY_EXEC) && !(mode & S_IXUSR))
    res = -EACCES;

  return res;
}

/**
 * inode_checkPermission - Check for access rights to a given inode
 * @inode: inode to check permission on
 * @mask: access mode to check for
 *
 * This is the main entry point for permission checking in the VFS.
 * It performs additional checks before delegating to filesystem-specific
 * permission methods.
 *
 * Returns 0 if access is allowed, negative error code otherwise.
 */
int inode_checkPermission(struct inode* inode, int mask) {
  int retval;

  if (!inode)
    return -EINVAL;

  /* Always grant access to special inodes like device files */
  if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
    return 0;

  /* Use the fs-specific permission function if available */
  if (inode->i_op && inode->i_op->permission) {
    retval = inode->i_op->permission(inode, mask);
    if (retval != -EAGAIN)
      return retval;
  }

  /* Fall back to generic permission checking */
  return generic_permission(inode, mask);
}

/**
 * inode_create - Allocate a new inode for a specific filesystem
 * @sb: superblock the inode belongs to
 *
 * Allocates and initializes a new inode. If the superblock has
 * an inode_create operation, use that; otherwise allocate a generic inode.
 *
 * Returns the new inode or NULL if allocation failed.
 */
struct inode* inode_create(struct superblock* sb) {
  struct inode* inode;

  if (sb->s_operations && sb->s_operations->alloc_inode) {
    inode = sb->s_operations->alloc_inode(sb);
  } else {
    inode = kmalloc(sizeof(struct inode));
  }
  if (unlikely(!inode))
    return NULL;

  /* Initialize the inode */
  memset(inode, 0, sizeof(struct inode));
  atomic_set(&inode->i_count, 1);
  INIT_LIST_HEAD(&inode->i_dentryList);
  INIT_LIST_HEAD(&inode->i_s_list_node);
  INIT_LIST_HEAD(&inode->i_state_list_node);
  spinlock_init(&inode->i_lock);
  inode->i_superblock = sb;
  inode->i_state = I_NEW; /* Mark as new */
  list_add(&inode->i_s_list_node, &sb->s_list_all_inodes);
  list_add(&inode->i_state_list_node, &sb->s_list_clean_inodes);

  return inode;
}

/**
 * inode_acquire - Get a fully initialized inode
 * @sb: superblock to get inode from
 * @ino: inode number to look up
 *
 * Tries to find the specified inode in the inode cache. If not found,
 * allocates a new inode and calls the filesystem to read it.
 *
 * Returns the inode or NULL if an error occurs.
 */
struct inode* inode_acquire(struct superblock* sb, unsigned long ino) {
  struct inode* inode;

  /* Look in the inode hash table first */
  inode = __inode_acquireLocked(sb, ino);
  if (!inode)
    return NULL;

  /* If the inode was already in the cache, just return it */
  if (!(inode->i_state & I_NEW))
    return inode;

  /* Initialize new inode from disk */
  if (sb->s_operations && sb->s_operations->read_inode) {
    sb->s_operations->read_inode(inode);
  }

  /* Mark inode as initialized and wake up waiters */
  __unlock_new_inode(inode);

  return inode;
}

/**
 * inode_get - Increment inode reference count
 * @inode: inode to grab
 *
 * Increments the reference count on an inode if it's valid.
 *
 * Returns the inode with incremented count, or NULL if the inode is invalid.
 */
struct inode* inode_get(struct inode* inode) {
  if (!inode || inode_isBad(inode))
    return NULL;

  atomic_inc(&inode->i_count);
  return inode;
}

/**
 * inode_put - Release a reference to an inode
 * @inode: inode to put
 *
 * Decrements the reference count on an inode. If the count reaches zero,
 * the inode is added to the LRU list waiting for recycle.
 */
void inode_put(struct inode* inode) {
  if (unlikely(!inode))
    return;

  struct superblock* sb = inode->i_superblock;
  spinlock_lock(&inode->i_lock);
  /* Decrease reference count */
  if (atomic_dec_and_test(&inode->i_count)) {
    /* Last reference gone - add to superblock's LRU */
    if (!(inode->i_state & I_DIRTY)) {
      /* If it's on another state list, remove it first */
      spin_lock(&sb->s_list_inode_states_lock);
      if (!list_empty(&inode->i_state_list_node)) {
        list_del_init(&inode->i_state_list_node);
      }

      list_add_tail(&inode->i_state_list_node, &sb->s_list_clean_inodes);
      spin_unlock(&sb->s_list_inode_states_lock);
    }
  }
  spinlock_unlock(&inode->i_lock);
}

/**
 * inode_isBad - Check if an inode is invalid
 * @inode: inode to check
 *
 * Returns true if the specified inode has been marked as bad.
 */
int inode_isBad(struct inode* inode) {
  return (!inode || inode->i_op == NULL);
}

/**
 * setattr_prepare - Check if attribute change is allowed
 * @dentry: dentry of the inode to change
 * @attr: attributes to change
 *
 * Validates that the requested attribute changes are allowed
 * based on permissions and constraints.
 *
 * Returns 0 if the change is allowed, negative error code otherwise.
 */
int setattr_prepare(struct dentry* dentry, struct iattr* attr) {
  struct inode* inode = dentry->d_inode;
  int error = 0;

  if (!inode)
    return -EINVAL;

  /* Check for permission to change attributes */
  if (attr->ia_valid & ATTR_MODE) {
    error = inode_checkPermission(inode, MAY_WRITE);
    if (error)
      return error;
  }

  /* Check if user can change ownership */
  if (attr->ia_valid & (ATTR_UID | ATTR_GID)) {
    /* Only root can change ownership */
    if (current_task()->euid != 0)
      return -EPERM;
  }

  /* Check if size can be changed */
  if (attr->ia_valid & ATTR_SIZE) {
    error = inode_checkPermission(inode, MAY_WRITE);
    if (error)
      return error;

    /* Cannot change size of directories */
    if (S_ISDIR(inode->i_mode))
      return -EISDIR;
  }

  return 0;
}

/**
 * notify_change - Notify filesystem of attribute changes
 * @dentry: dentry of the changed inode
 * @attr: attributes that changed
 *
 * After validating attribute changes with setattr_prepare,
 * this function applies the changes and notifies the filesystem.
 *
 * Returns 0 on success, negative error code on failure.
 */
int notify_change(struct dentry* dentry, struct iattr* attr) {
  struct inode* inode = dentry->d_inode;
  int error;

  if (!inode)
    return -EINVAL;

  /* Validate changes */
  error = setattr_prepare(dentry, attr);
  if (error)
    return error;

  /* Call the filesystem's setattr method if available */
  if (inode->i_op && inode->i_op->setattr)
    return inode->i_op->setattr(dentry, attr);

  /* Apply attribute changes to the inode */
  if (attr->ia_valid & ATTR_MODE)
    inode->i_mode = attr->ia_mode;
  if (attr->ia_valid & ATTR_UID)
    inode->i_uid = attr->ia_uid;
  if (attr->ia_valid & ATTR_GID)
    inode->i_gid = attr->ia_gid;
  if (attr->ia_valid & ATTR_SIZE)
    inode->i_size = attr->ia_size;
  if (attr->ia_valid & ATTR_ATIME)
    inode->i_atime = attr->ia_atime;
  if (attr->ia_valid & ATTR_MTIME)
    inode->i_mtime = attr->ia_mtime;
  if (attr->ia_valid & ATTR_CTIME)
    inode->i_ctime = attr->ia_ctime;

  /* Mark the inode as dirty */
  inode_setDirty(inode);

  return 0;
}

/**
 * inode_setDirty - Mark an inode as needing writeback
 * @inode: inode to mark dirty
 *
 * Adds the inode to the superblock's list of dirty inodes
 * that need to be written back to disk.
 */
void inode_setDirty(struct inode* inode) {
  if (!inode)
    return;

  struct superblock* sb = inode->i_superblock;

  /* Add to superblock's dirty list if not already there */
  if (!(inode->i_state & I_DIRTY) && sb) {
    spinlock_lock(&sb->s_list_inode_states_lock);
    spinlock_lock(&inode->i_lock);

    if (likely(!list_empty(&inode->i_state_list_node))) {
      list_del_init(&inode->i_state_list_node);
    }

    inode->i_state |= I_DIRTY;
    list_add(&inode->i_state_list_node, &sb->s_list_dirty_inodes);

    spinlock_unlock(&inode->i_lock);
    spinlock_unlock(&sb->s_list_inode_states_lock);
  }
}

/**
 * evict_inode - Remove an inode from the filesystem
 * @inode: inode to evict
 *
 * This function is called when an inode should be removed from
 * the filesystem (when its link count reaches zero). It handles
 * cleaning up filesystem-specific resources and then clearing
 * the inode.
 */
static void evict_inode(struct inode* inode) {
  if (!inode)
    return;

  /* Mark inode as being freed */
  inode->i_state |= I_FREEING;

  /* Call filesystem-specific cleanup through superblock operations */
  if (inode->i_superblock && inode->i_superblock->s_operations &&
      inode->i_superblock->s_operations->evict_inode) {
    inode->i_superblock->s_operations->evict_inode(inode);
  }

  /* Delete any remaining pages in the page cache */
  if (inode->i_mapping) {
    /* truncate_inode_pages(inode->i_mapping, 0); */
    /* Implementation would call truncate_inode_pages here */
  }

  /* Clean out the inode */
  __clear_inode(inode);
}

/**
 * __clear_inode - Clean up an inode and prepare it for freeing
 * @inode: inode to clear
 *
 * Final cleanup of an inode before it's memory is freed.
 */
void __clear_inode(struct inode* inode) {
  if (!inode)
    return;

  /* Remove any state flags */
  inode->i_state = 0;

  /* Remove from superblock lists */
  if (inode->i_superblock) {
    spin_lock(&inode->i_superblock->s_list_all_inodes_lock);
    list_del_init(&inode->i_s_list_node);
    list_del_init(&inode->i_state_list_node);
    spin_unlock(&inode->i_superblock->s_list_all_inodes_lock);
  }

  /* Clear file system specific data if needed */
  if (inode->i_fs_info) {
    kfree(inode->i_fs_info);
    inode->i_fs_info = NULL;
  }

  /* Clear block mapping data if present */
  if (inode->i_data) {
    kfree(inode->i_data);
    inode->i_data = NULL;
  }

  /* Free the inode memory */
  kfree(inode);
}


/**
 * __inode_acquireLocked - Get inode from cache or allocate a new one
 * @sb: superblock to get inode from
 * @ino: inode number to look up
 *
 * Looks up an inode in the hash table. If found, increments its
 * reference count. If not found, allocates a new inode and adds
 * it to the hash table with the I_NEW flag set.
 *
 * Returns locked inode on success, NULL on allocation failure.
 */
static struct inode *__inode_acquireLocked(struct superblock *sb, unsigned long ino) {
	struct inode *inode;
	struct inode_key key = { .sb = sb, .ino = ino };
	
	/* Look up in the hash table */
	inode = hashtable_lookup(&inode_hashtable, &key);
	
	if (inode) {
			/* Found in cache - grab a reference */
			ihold(inode);
			return inode;
	}
	
	/* Not found - allocate a new one */
	inode = new_inode(sb);
	if (!inode)
			return NULL;
			
	/* Set the inode number */
	inode->i_ino = ino;
	
	/* Add to the hash table */
	hashtable_insert(&inode_hashtable, &key, inode);
	
	return inode;
}


/**
 * Hash function for inode keys
 * Uses both superblock pointer and inode number to generate a hash
 */
static unsigned int __inode_hash_func(const void* key, unsigned int size) {
    const struct inode_key *ikey = key;
    
    /* Combine superblock pointer and inode number */
    unsigned long val = (unsigned long)ikey->sb ^ ikey->ino;
    
    /* Mix the bits for better distribution */
    val = (val * 11400714819323198485ULL) >> 32;  /* Fast hash multiply */
    return (unsigned int)val;
}

/**
 * Compare two inode keys for equality
 */
static int __inode_key_equals(const void* k1, const void* k2) {
    const struct inode_key *key1 = k1, *key2 = k2;

    return (key1->sb == key2->sb && key1->ino == key2->ino);
}



/**
 * hash_inode - Add an inode to the hash table
 * @inode: The inode to add
 *
 * Adds an inode to the inode hash table for fast lookups.
 */
static void hash_inode(struct inode *inode) {
  struct inode_key *key;

  if (!inode || !inode->i_superblock)
    return;

  /* Create hash key */
  key = kmalloc(sizeof(struct inode_key));
  if (!key)
    return;

  key->sb = inode->i_superblock;
  key->ino = inode->i_ino;

  /* Insert into hash table */
  hashtable_insert(&inode_hashtable, key, inode);
}

/**
 * unhash_inode - Remove an inode from the hash table
 * @inode: The inode to remove
 */
static void unhash_inode(struct inode *inode) {
  struct inode_key key;

  if (!inode || !inode->i_superblock)
    return;

  key.sb = inode->i_superblock;
  key.ino = inode->i_ino;

  /* Remove from hash table */
  hashtable_remove(&inode_hashtable, &key);
}


/**
 * __unlock_new_inode - Unlock a newly allocated inode
 * @inode: inode to unlock
 *
 * Clears the I_NEW state bit and wakes up any processes
 * waiting on this inode to be initialized.
 */
static void __unlock_new_inode(struct inode *inode) {
	if (!inode)
			return;
			
	spin_lock(&inode->i_lock);
	inode->i_state &= ~I_NEW;
	spin_unlock(&inode->i_lock);
	
	/* Wake up anyone waiting for this inode to be initialized */
	wake_up_inode(inode);
}


/**
 * inode_sync - Write an inode to disk
 * @inode: inode to write
 * @wait: whether to wait for I/O to complete
 *
 * Calls the filesystem's writeback function to synchronize
 * the inode to disk.
 *
 * Returns 0 on success or negative error code.
 */
int inode_sync(struct inode *inode, int wait) {
	int ret = 0;
	
	if (!inode)
			return -EINVAL;
			
	/* If clean, nothing to do */
	if (!(inode->i_state & I_DIRTY))
			return 0;
			
	/* Call the filesystem's write_inode method if available */
	if (inode->i_superblock && inode->i_superblock->s_operations && 
			inode->i_superblock->s_operations->write_inode) {
			ret = inode->i_superblock->s_operations->write_inode(inode, wait);
	}
	
	/* If successful and waiting requested, clear dirty state */
	if (ret == 0 && wait) {
			spin_lock(&inode->i_superblock->s_list_inode_states_lock);
			/* Remove from dirty list */
			if (inode->i_state & I_DIRTY) {
					list_del_init(&inode->i_state_list_node);
					inode->i_state &= ~I_DIRTY;
					list_add(&inode->i_state_list_node, &inode->i_superblock->s_list_clean_inodes);
			}
			spin_unlock(&inode->i_superblock->s_list_inode_states_lock);
	}
	
	return ret;
}

/**
* inode_sync_metadata - Sync only inode metadata to disk
* @inode: inode to sync
* @wait: whether to wait for I/O to complete
*
* Like inode_sync(), but only writes inode metadata, not data blocks.
* Used when only attributes have changed.
*
* Returns 0 on success or negative error code.
*/
int inode_sync_metadata(struct inode *inode, int wait) {
	/* For now, just delegate to inode_sync */
	/* In a more complete implementation, this would only sync metadata */
	return inode_sync(inode, wait);
}

/**
 * wake_up_inode - Wake up processes waiting on an inode
 * @inode: the inode to wake up waiters for
 *
 * Helper function to wake up processes waiting for an inode
 * to become available (e.g., after initialization).
 */
static void wake_up_inode(struct inode *inode) {
	/* In a full implementation, this would use wait queues */
	/* For now, we'll assume no waiters or that it's handled elsewhere */
}


// Enhanced permission checking function
int inode_checkPermission(struct inode *inode, int mask) {
    // Enhanced implementation...
    // Consider credentials, ACLs, capability-based security
}

/**
 * Get key from an inode hash node
 * This function extracts the key (superblock + inode number) from a list node
 */
static void* __inode_get_key(struct list_node* node) {
    struct inode* inode = container_of(node, struct inode, i_hash_node);
    static struct {
        struct superblock* sb;
        unsigned long ino;
    } key;
    
    key.sb = inode->i_superblock;
    key.ino = inode->i_ino;
    
    return &key;
}
