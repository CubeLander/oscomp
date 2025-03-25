#include <kernel/fs/vfs/vfs.h>
#include <kernel/mm/kmalloc.h>
#include <kernel/sched/sched.h>
#include <kernel/types.h>
#include <util/list.h>
#include <util/spinlock.h>
#include <util/string.h>

#include <spike_interface/spike_utils.h>



static struct superblock* __alloc_super(struct fsType* type);

/**
 * __alloc_super - Allocate a new superblock from a fsType
 * @type: Filesystem type
 *
 * Allocates and initializes a new superblock structure
 *
 * Returns a new superblock or NULL on failure
 */
static struct superblock* __alloc_super(struct fsType* type) {
	struct superblock* sb;

	sb = kmalloc(sizeof(struct superblock));
	if (!sb)
		return NULL;

	/* Initialize to zeros */
	memset(sb, 0, sizeof(struct superblock));

	/* Initialize lists */
	INIT_LIST_HEAD(&sb->s_list_all_inodes);
	spinlock_init(&sb->s_list_all_inodes_lock);

	INIT_LIST_HEAD(&sb->s_list_clean_inodes);
	INIT_LIST_HEAD(&sb->s_list_dirty_inodes);
	INIT_LIST_HEAD(&sb->s_list_io_inodes);
	spinlock_init(&sb->s_list_inode_states_lock);

	INIT_LIST_HEAD(&sb->s_list_mounts);

	INIT_LIST_HEAD(&sb->s_node_fsType);

	/* Initialize locks */
	spinlock_init(&sb->s_lock);
	spinlock_init(&sb->s_list_mounts_lock);

	/* Set up reference counts */
	sb->s_refcount = 1; /* Initial reference */
	//sb->s_active = 0;  /* No active references yet */

	/* Set filesystem type */
	sb->s_fsType = type;

	return sb;
}


/**
 * drop_super - Decrease reference count of superblock
 * @sb: Superblock to drop reference to
 *
 * Decrements the reference count and frees the superblock if
 * it reaches zero.
 */
void drop_super(struct superblock* sb) {
	if (!sb)
		return;
	if (atomic_dec_and_test(&sb->s_refcount)) {
		deactivate_super(sb);
	}
}

/**
 * deactivate_super - Clean up and free a superblock
 * @sb: Superblock to destroy
 *
 * Releases all resources associated with a superblock.
 * Should only be called when reference count reaches zero.
 */
static void deactivate_super(struct superblock* sb) {
	if (!sb)
		return;

	/* Call filesystem's put_super if defined */
	if (sb->s_operations && sb->s_operations->put_super)
		sb->s_operations->put_super(sb);

	spinlock_lock(&sb->s_fsType->fs_list_s_lock);
	/* Remove from filesystem's list */
	list_del(&sb->s_node_fsType);
	spinlock_unlock(&sb->s_fsType->fs_list_s_lock);

	/* Free any filesystem-specific info */
	if (sb->s_fs_specific)
		kfree(sb->s_fs_specific);

	/* Free the superblock itself */
	kfree(sb);
}

/**
 * grab_super - Increase active reference count
 * @sb: Superblock to reference
 *
 * Increases the active reference count of a superblock,
 * indicating that it's actively being used.
 */
void grab_super(struct superblock* sb) {
	if (!sb)
		return;

	spin_lock(&sb->s_lock);
	atomic_inc(&sb->s_refcount);
	//sb->s_active++;
	spin_unlock(&sb->s_lock);
}

/**
 * deactivate_super_safe - Decrease active reference count
 * @sb: Superblock to dereference
 *
 * Decreases the active reference count of a superblock.
 */
void deactivate_super_safe(struct superblock* sb) {
	if (!sb)
		return;

	spin_lock(&sb->s_lock);
	atomic_dec(&sb->s_refcount);

	//sb->s_active--;
	spin_unlock(&sb->s_lock);
}

/**
 * sync_filesystem - Synchronize a filesystem to disk
 * @sb: Superblock of filesystem to sync
 * @wait: Whether to wait for I/O completion
 *
 * Synchronizes all dirty inodes and other filesystem data to disk.
 *
 * Returns 0 on success, negative error code on failure
 */
int sync_filesystem(struct superblock* sb, int wait) {
	int ret = 0;
	struct inode *inode, *next;

	if (!sb)
		return -EINVAL;

	/* Call filesystem's sync_fs if defined */
	if (sb->s_operations && sb->s_operations->sync_fs) {
		ret = sb->s_operations->sync_fs(sb, wait);
		if (ret)
			return ret;
	}

	/* Sync all dirty inodes */
	spin_lock(&sb->s_list_inode_states_lock);
	list_for_each_entry_safe(inode, next, &sb->s_list_dirty_inodes, i_state_list_node) {
		if (sb->s_operations && sb->s_operations->write_inode) {
			spin_unlock(&sb->s_list_inode_states_lock);
			ret = sb->s_operations->write_inode(inode, wait);
			spin_lock(&sb->s_list_inode_states_lock);

			if (ret == 0 && wait) {
				/* Move from dirty list to LRU list */
				list_del(&inode->i_state_list_node);
				inode->i_state &= ~I_DIRTY;
				list_add(&inode->i_state_list_node, &sb->s_list_clean_inodes);
			}

			if (ret)
				break;
		}
	}
	spin_unlock(&sb->s_list_inode_states_lock);

	return ret;
}

/**
 * generic_shutdown_super - Generic superblock shutdown
 * @sb: Superblock to shut down
 *
 * Generic implementation for unmounting a filesystem.
 * Releases all inodes and drops the superblock.
 */
void generic_shutdown_super(struct superblock* sb) {
	struct inode *inode, *next;

	if (!sb)
		return;

	/* Write any dirty data */
	sync_filesystem(sb, 1);

	/* Free all inodes */
	spin_lock(&sb->s_list_all_inodes_lock);
	list_for_each_entry_safe(inode, next, &sb->s_list_all_inodes, i_s_list_node) {
		spin_unlock(&sb->s_list_all_inodes_lock);

		/* Forcibly evict the inode */
		if (inode->i_state & I_DIRTY) {
			if (sb->s_operations && sb->s_operations->write_inode)
				sb->s_operations->write_inode(inode, 1);
		}

		if (sb->s_operations && sb->s_operations->evict_inode)
			sb->s_operations->evict_inode(inode);
		else
			clear_inode(inode);

		spin_lock(&sb->s_list_all_inodes_lock);
	}
	spin_unlock(&sb->s_list_all_inodes_lock);

	/* Free root dentry */
	if (sb->s_global_root_dentry) {
		dput(sb->s_global_root_dentry);
		sb->s_global_root_dentry = NULL;
	}

	/* Decrease active count */
	deactivate_super_safe(sb);

	/* Drop reference */
	drop_super(sb);
}







