#include <kernel/mmu.h>
#include <kernel/sched.h>
#include <kernel/sprint.h>
#include <kernel/time.h>
#include <kernel/types.h>
#include <kernel/util.h>
#include <kernel/vfs.h>


int32 inode_monkey(struct fcontext* fctx) {
	/* Get the current inode from the path's dentry */
	struct inode* current_inode = fctx->fc_dentry->d_inode;
	if (!current_inode) { return -ENOENT; }

	return current_inode->i_superblock->s_fstype->fs_monkey(fctx);
}
