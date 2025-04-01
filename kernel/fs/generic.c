#include <kernel/vfs.h>
#include <kernel/mmu.h>

/**
 * Implementation of superblock operation handlers using the intent system
 */

int32 generic_alloc_inode(struct fcontext* fctx) {
	struct superblock* sb = fctx->fc_superblock;

	/* Allocate a new inode */
	struct inode* inode = kzalloc(sizeof(struct inode));
	if (!inode) {
		return -ENOMEM;
	}

	/* Initialize inode */
	atomic_set(&inode->i_refcount, 1);
	inode->i_superblock = sb;
	inode->i_ino = atomic64_inc_return(&sb->s_next_ino);
	atomic_inc(&sb->s_ninodes);

	/* Initialize locks and lists */
	spin_lock_init(&inode->i_lock);
	INIT_LIST_HEAD(&inode->i_dentryList);
	spin_lock_init(&inode->i_dentryList_lock);

	/* Add to superblock's inode list */
	spin_lock(&sb->s_list_all_inodes_lock);
	list_add(&inode->i_s_list_node, &sb->s_list_all_inodes);
	spin_unlock(&sb->s_list_all_inodes_lock);

	/* Store the result in the context */
	fctx->fc_iostruct = inode;

	return 0;
}


int32 generic_destroy_inode(struct fcontext* fctx) {
	struct inode* inode = fctx->fc_dentry->d_inode;

	/* Remove from superblock's inode list */
	spin_lock(&inode->i_superblock->s_list_all_inodes_lock);
	list_del(&inode->i_s_list_node);
	spin_unlock(&inode->i_superblock->s_list_all_inodes_lock);

	/* Free inode memory */
	kfree(inode);

	return 0;
}