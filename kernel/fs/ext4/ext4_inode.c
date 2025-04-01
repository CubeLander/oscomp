#include <kernel/fs/ext4_adaptor.h>
#include <kernel/mm/kmalloc.h>
#include <kernel/sched/signal.h>
#include <kernel/time.h>
#include <kernel/types.h>
#include <kernel/util/string.h>
#include <kernel/vfs.h>

int32 __ext4_flush_dirty_inode(struct ext4_inode_ref* inode_ref);
int32 ext4_sync_inode(struct ext4_inode_ref* inode_ref);

static int32 __inode_getExt4InodeRef(struct inode* inode, struct ext4_inode_ref* ref);

static struct inode* ext4_vfs_lookup(struct inode* dir, struct dentry* dentry, uint32 flags);
static int32 ext4_vfs_create(struct inode* dir, struct dentry* dentry, mode_t mode);
static int32 ext4_vfs_link(struct dentry* old_dentry, struct inode* dir, struct dentry* new_dentry);
static int32 ext4_vfs_unlink(struct inode* dir, struct dentry* dentry);
static int32 ext4_vfs_symlink(struct inode* dir, struct dentry* dentry, const char* symname);
static int32 ext4_vfs_mkdir(struct inode* dir, struct dentry* dentry, mode_t mode);
static int32 ext4_vfs_rmdir(struct inode* dir, struct dentry* dentry);
static int32 ext4_vfs_rename(struct inode* old_dir, struct dentry* old_dentry, struct inode* new_dir, struct dentry* new_dentry);

static int32 ext4_vfs_readlink(struct dentry* dentry, char* buffer, int32 buflen);
static int32 ext4_vfs_permission(struct inode* inode, int32 mask);
static int32 ext4_vfs_setattr(struct dentry* dentry, struct iattr* attr);
static int32 ext4_vfs_getattr(struct vfsmount* mnt, struct dentry* dentry, struct kstat* stat);
static int32 ext4_vfs_setxattr(struct dentry* dentry, const char* name, const void* value, size_t size, int32 flags);
static ssize_t ext4_vfs_getxattr(struct dentry* dentry, const char* name, void* buffer, size_t size);
static ssize_t ext4_vfs_listxattr(struct dentry* dentry, char* buffer, size_t size);
static int32 ext4_vfs_removexattr(struct dentry* dentry, const char* name);

/* New VFS interfaces */
// static int32 ext4_vfs_get_acl(struct inode *inode, int32 type);
// static int32 ext4_vfs_set_acl(struct inode *inode, struct posix_acl *acl, int32 type);
static int32 ext4_vfs_get_link(struct dentry* dentry, struct inode* inode, struct path* path);
static int32 ext4_vfs_mknod(struct inode* inode, struct dentry* dentry, fmode_t mode, dev_t dev);
static int32 ext4_vfs_fiemap(struct inode* inode, struct fiemap_extent_info* fiemap_info, uint64_t start, uint64_t len);
static int32 ext4_vfs_get_block(struct inode* inode, sector_t block, struct buffer_head* buffer_head, int32 create);
static sector_t ext4_vfs_bmap(struct inode* inode, sector_t block);
static void ext4_vfs_truncate_blocks(struct inode* inode, loff_t size);
static int32 ext4_vfs_direct_IO(struct kiocb* kiocb, struct io_vector_iterator* iov_iter);
static vm_fault_t ext4_vfs_page_fault(struct vm_area_struct* vma, struct vm_fault* vmf);
// static uint64 ext4_vfs_get_unmapped_area(struct file *file, uint64 addr,
//                                              uint64 len, uint64 pgoff,
//                                              uint64 flags);
static int32 ext4_vfs_atomic_open(struct inode* inode, struct dentry* dentry, struct file* file, unsigned open_flag, umode_t create_mode);
static int32 ext4_vfs_tmpfile(struct inode* inode, struct dentry* dentry, umode_t mode);

/**
 * ext4_file_inode_operations - Operations for regular files
 */
const struct inode_operations ext4_file_inode_operations = {
    .permission = ext4_vfs_permission,
    //.get_acl        = ext4_vfs_get_acl,
    //.set_acl        = ext4_vfs_set_acl,
    .setattr = ext4_vfs_setattr,
    //.getattr        = ext4_vfs_getattr,
    .setxattr = ext4_vfs_setxattr,
    .getxattr = ext4_vfs_getxattr,
    .listxattr = ext4_vfs_listxattr,
    .removexattr = ext4_vfs_removexattr,
    //.fiemap         = ext4_vfs_fiemap,
    .get_block = ext4_vfs_get_block,
    .bmap = ext4_vfs_bmap,
    .truncate_blocks = ext4_vfs_truncate_blocks,
    .direct_IO = ext4_vfs_direct_IO,
    .page_fault = ext4_vfs_page_fault,
    //.get_unmapped_area = ext4_vfs_get_unmapped_area,
};

/**
 * ext4_dir_inode_operations - Operations for directories
 */
const struct inode_operations ext4_dir_inode_operations = {
    //.create         = ext4_vfs_create,
    //.lookup         = ext4_vfs_lookup,
    .link = ext4_vfs_link,
    .unlink = ext4_vfs_unlink,
    .symlink = ext4_vfs_symlink,
    .mkdir = ext4_vfs_mkdir,
    .rmdir = ext4_vfs_rmdir,
    //.rename         = ext4_vfs_rename,
    .permission = ext4_vfs_permission,
    .setattr = ext4_vfs_setattr,
    //.getattr        = ext4_vfs_getattr,
    .setxattr = ext4_vfs_setxattr,
    .getxattr = ext4_vfs_getxattr,
    .listxattr = ext4_vfs_listxattr,
    .mknod = ext4_vfs_mknod,
    .removexattr = ext4_vfs_removexattr,
    //.get_acl        = ext4_vfs_get_acl,
    //.set_acl        = ext4_vfs_set_acl,
    .atomic_open = ext4_vfs_atomic_open,
    .tmpfile = ext4_vfs_tmpfile,
};

/**
 * ext4_symlink_inode_operations - Operations for symbolic links
 */
const struct inode_operations ext4_symlink_inode_operations = {
    // .get_link       = ext4_vfs_get_link,
    // .readlink       = ext4_vfs_readlink,
    // .permission     = ext4_vfs_permission,
    // .setattr        = ext4_vfs_setattr,
    // //.getattr        = ext4_vfs_getattr,
    // .setxattr       = ext4_vfs_setxattr,
    // .getxattr       = ext4_vfs_getxattr,
    // .listxattr      = ext4_vfs_listxattr,
    // .removexattr    = ext4_vfs_removexattr,
    // .get_acl        = ext4_vfs_get_acl,
    //.set_acl        = ext4_vfs_set_acl,
};

/**
 * ext4_inode_init - Initialize a VFS inode from an ext4 inode
 * @sb: The superblock
 * @inode: The VFS inode to initialize
 * @ino: Inode number
 *
 * Returns: 0 on success, negative error code on failure
 */
int32 ext4_inode_init(struct superblock* sb, struct inode* inode, uint32_t ino) {
	struct ext4_inode_ref e_inode_ref;
	struct ext4_fs* e_fs = sb->s_fs_info; // 每一个vfs sb都有一个ext4 fs对应，然后再对应一个ext4 sb
	struct ext4_sblock* e_sb = &e_fs->sb;
	int32 ret;

	/* Get the ext4 inode reference */
	ret = ext4_fs_get_inode_ref(e_fs, ino, &e_inode_ref);
	if (ret != 0) return ret;
	struct ext4_inode* e_inode = e_inode_ref.inode;

	/* Initialize the VFS inode from ext4 inode */
	inode->i_ino = ino;
	inode->i_superblock = sb;
	inode->i_mode = ext4_inode_get_mode(e_sb, e_inode);
	inode->i_uid = ext4_inode_get_uid(e_inode);
	inode->i_gid = ext4_inode_get_gid(e_inode);
	inode->i_size = ext4_inode_get_size(e_sb, e_inode);

	/* Convert uint32 timestamps to timespec format */
	ext4_timestamp_to_timespec64(ext4_inode_get_access_time(e_inode), &inode->i_atime);
	ext4_timestamp_to_timespec64(ext4_inode_get_modif_time(e_inode), &inode->i_mtime);
	ext4_timestamp_to_timespec64(ext4_inode_get_change_inode_time(e_inode), &inode->i_ctime);

	inode->i_blocks = ext4_inode_get_blocks_count(e_sb, e_inode);
	inode->i_nlink = ext4_inode_get_links_cnt(e_inode);

	/* Set appropriate operations based on file type */
	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &ext4_file_inode_operations;
		inode->i_fop = &ext4_file_operations;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &ext4_dir_inode_operations;
		inode->i_fop = &ext4_dir_operations;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &ext4_symlink_inode_operations;
		inode->i_fop = NULL;
	} else if (S_ISBLK(inode->i_mode) || S_ISCHR(inode->i_mode)) {
		/* Handle device files */
		inode->i_rdev = ext4_inode_get_dev(e_inode);
		inode->i_op = &ext4_file_inode_operations;
		inode->i_fop = NULL; /* Special device operations would go here */
	} else {
		/* FIFO, socket, etc. */
		inode->i_op = &ext4_file_inode_operations;
		inode->i_fop = NULL;
	}

	/* Release the ext4 inode reference */
	ext4_fs_put_inode_ref(&e_inode_ref);

	return 0;
}

/**
 * ext4_read_inode - Read the inode data from disk
 * @inode: The VFS inode to fill
 *
 * Returns: 0 on success, negative error code on failure
 */
int32 ext4_read_inode(struct inode* inode) {
	if (!inode || !inode->i_superblock) return -EINVAL;

	return ext4_inode_init(inode->i_superblock, inode, inode->i_ino);
}

static int32 __inode_getExt4InodeRef(struct inode* inode, struct ext4_inode_ref* ref) {
	struct ext4_fs* e_fs = inode->i_superblock->s_fs_info;
	return ext4_fs_get_inode_ref(e_fs, inode->i_ino, ref);
}

/* Implementation of inode operations */

/**
 * ext4_vfs_lookup - Look up a directory entry by name
 * @dir: Directory inode to search in
 * @dentry: Target directory entry to look up
 * @flags: Lookup flags
 *
 * Returns: The inode corresponding to @dentry on success or NULL on failure
 */
static struct inode* ext4_vfs_lookup(struct inode* dir, struct dentry* dentry, uint32 flags) {
	struct ext4_inode_ref e_dir_ref;
	struct ext4_dir_search_result result;
	struct ext4_fs* e_fs = dir->i_superblock->s_fs_info;
	struct inode* inode = NULL;
	int32 ret;

	if (!dir || !dentry) return ERR_PTR(-EINVAL);

	/* Get ext4 inode reference for the directory */
	ret = ext4_fs_get_inode_ref(e_fs, dir->i_ino, &e_dir_ref);
	if (ret != 0) return ERR_PTR(ret);

	/* Call ext4 directory find entry function */
	ret = ext4_dir_find_entry(&result, &e_dir_ref, dentry->d_name->name, dentry->d_name->len);

	/* Release directory reference */
	ext4_fs_put_inode_ref(&e_dir_ref);

	if (ret != 0) {
		/* Entry not found or error occurred */
		if (ret == -ENOENT) return NULL; /* Not an error, just not found */
		return ERR_PTR(ret);
	}

	/* Found the entry, now get the inode */
	inode = inode_acquire(dir->i_superblock, result.dentry->inode);

	/* Clean up the search result */
	ext4_dir_destroy_result(&e_dir_ref, &result);

	return inode;
}

/**
 * ext4_vfs_create - Create a new regular file
 * @dir: Parent directory inode
 * @dentry: Directory entry for the new file
 * @mode: Mode bits for the new file
 *
 * Returns: 0 on success, negative error code on failure
 */
static int32 ext4_vfs_create(struct inode* dir, struct dentry* dentry, mode_t mode) {
	struct ext4_fs* e_fs = dir->i_superblock->s_fs_info;
	struct ext4_sblock* e_sb = &e_fs->sb;
	struct inode* inode;
	int32 ret;

	/* Get directory inode reference */
	struct ext4_inode_ref e_dir_ref;
	if ((ret = ext4_fs_get_inode_ref(e_fs, dir->i_ino, &e_dir_ref)) != 0) { return ret; }

	/* Allocate a new inode */
	struct ext4_inode_ref e_inode_ref;
	if ((ret = ext4_fs_alloc_inode(e_fs, &e_inode_ref, EXT4_DE_REG_FILE)) != 0) {
		ext4_fs_put_inode_ref(&e_dir_ref);
		return ret;
	}

	/* Set mode (permissions) for the new inode */
	ext4_inode_set_mode(e_sb, e_inode_ref.inode, mode);

	/* Add entry to directory */
	ret = ext4_dir_add_entry(&e_dir_ref, dentry->d_name->name, dentry->d_name->len, &e_inode_ref);
	if (ret != 0) {
		/* Free the inode if we couldn't add the directory entry */
		ext4_fs_free_inode(&e_inode_ref);
		ext4_fs_put_inode_ref(&e_dir_ref);
		return ret;
	}

	/* Create VFS inode and add to dentry */
	inode = inode_acquire(dir->i_superblock, e_inode_ref.index);
	if (!inode) {
		ext4_fs_free_inode(&e_inode_ref);
		ext4_fs_put_inode_ref(&e_dir_ref);
		return -ENOMEM;
	}

	/* Initialize the VFS inode */
	inode->i_mode = mode;
	inode->i_op = &ext4_file_inode_operations;
	inode->i_fop = &ext4_file_operations;

	/* Link the dentry to the inode */
	dentry_instantiate(dentry, inode);

	/* Release references */
	ext4_fs_put_inode_ref(&e_inode_ref);
	ext4_fs_put_inode_ref(&e_dir_ref);

	return 0;
}

/**
 * ext4_vfs_link - Create a hard link
 * @old_dentry: The existing dentry
 * @dir: The target directory
 * @new_dentry: The new dentry to create
 *
 * Returns: 0 on success, negative error code on failure
 */
static int32 ext4_vfs_link(struct dentry* old_dentry, struct inode* dir, struct dentry* new_dentry) {
	struct ext4_inode_ref e_dir_ref, e_inode_ref;
	int32 ret;

	/* Get directory and source inode references */
	ret = __inode_getExt4InodeRef(dir, &e_dir_ref);
	if (ret != 0) return ret;

	ret = __inode_getExt4InodeRef(old_dentry->d_inode, &e_inode_ref);
	if (ret != 0) {
		ext4_fs_put_inode_ref(&e_dir_ref);
		return ret;
	}

	/* Add entry to target directory */
	ret = ext4_dir_add_entry(&e_dir_ref, new_dentry->d_name->name, new_dentry->d_name->len, &e_inode_ref);
	if (ret != 0) {
		ext4_fs_put_inode_ref(&e_inode_ref);
		ext4_fs_put_inode_ref(&e_dir_ref);
		return ret;
	}

	/* Increment link count */
	ext4_fs_inode_links_count_inc(&e_inode_ref);

	/* Link the dentry to the inode */
	dentry_instantiate(new_dentry, inode_ref(old_dentry->d_inode));

	/* Release references */
	ext4_fs_put_inode_ref(&e_inode_ref);
	ext4_fs_put_inode_ref(&e_dir_ref);

	return 0;
}

/**
 * ext4_vfs_unlink - Remove a file entry from a directory
 * @dir: Parent directory inode
 * @dentry: The entry to remove
 *
 * Returns: 0 on success, negative error code on failure
 */
static int32 ext4_vfs_unlink(struct inode* dir, struct dentry* dentry) {
	struct ext4_inode_ref e_dir_ref, e_inode_ref;
	int32 ret;

	/* Get directory and target inode references */
	ret = __inode_getExt4InodeRef(dir, &e_dir_ref);
	if (ret != 0) return ret;

	ret = __inode_getExt4InodeRef(dentry->d_inode, &e_inode_ref);
	if (ret != 0) {
		ext4_fs_put_inode_ref(&e_dir_ref);
		return ret;
	}

	/* Remove directory entry */
	ret = ext4_dir_remove_entry(&e_dir_ref, dentry->d_name->name, dentry->d_name->len);
	if (ret != 0) {
		ext4_fs_put_inode_ref(&e_inode_ref);
		ext4_fs_put_inode_ref(&e_dir_ref);
		return ret;
	}

	/* Decrement link count */
	ext4_fs_inode_links_count_dec(&e_inode_ref);

	/* If this was the last link, mark the inode for deletion */
	if (ext4_inode_get_links_cnt(e_inode_ref.inode) == 0) {
		/* Set deletion time */
		ext4_inode_set_del_time(e_inode_ref.inode, do_time(NULL));
		/* Remove the inode from the filesystem */
		ext4_fs_free_inode(&e_inode_ref);
	}

	/* Release references */
	ext4_fs_put_inode_ref(&e_inode_ref);
	ext4_fs_put_inode_ref(&e_dir_ref);

	return 0;
}

/**
 * ext4_vfs_symlink - Create a symbolic link
 * @dir: Parent directory inode
 * @dentry: The symlink entry
 * @symname: The target path
 *
 * Returns: 0 on success, negative error code on failure
 */
static int32 ext4_vfs_symlink(struct inode* dir, struct dentry* dentry, const char* symname) {
	struct ext4_inode_ref e_dir_ref, e_inode_ref;
	struct inode* inode;
	int32 ret;
	size_t symname_len = strlen(symname);

	/* Get directory inode reference */
	ret = __inode_getExt4InodeRef(dir, &e_dir_ref);
	if (ret != 0) return ret;

	/* Allocate a new inode for the symlink */
	ret = ext4_fs_alloc_inode(e_dir_ref.fs, &e_inode_ref, EXT4_DE_SYMLINK);
	if (ret != 0) {
		ext4_fs_put_inode_ref(&e_dir_ref);
		return ret;
	}

	/* Set mode for the symlink */
	ext4_inode_set_mode(&e_dir_ref.fs->sb, e_inode_ref.inode, S_IFLNK | 0777);

	/* Add entry to directory */
	ret = ext4_dir_add_entry(&e_dir_ref, dentry->d_name->name, dentry->d_name->len, &e_inode_ref);
	if (ret != 0) {
		ext4_fs_free_inode(&e_inode_ref);
		ext4_fs_put_inode_ref(&e_dir_ref);
		return ret;
	}

	/* Store the symlink target */
	if (symname_len <= 60) {
		/* Store directly in the inode */
		memcpy(e_inode_ref.inode->blocks, symname, symname_len);
		ext4_inode_set_size(e_inode_ref.inode, symname_len);
	} else {
		/* Store in a separate block */
		ext4_fsblk_t fblock;
		ext4_lblk_t iblock = 0;

		/* Append a data block to the inode */
		ret = ext4_fs_append_inode_dblk(&e_inode_ref, &fblock, &iblock);
		if (ret != 0) {
			ext4_fs_free_inode(&e_inode_ref);
			ext4_fs_put_inode_ref(&e_dir_ref);
			return ret;
		}

		/* Write the symlink target to the data block */
		struct ext4_block block;
		ret = ext4_block_get(e_dir_ref.fs->bdev, &block, fblock);
		if (ret != 0) {
			ext4_fs_free_inode(&e_inode_ref);
			ext4_fs_put_inode_ref(&e_dir_ref);
			return ret;
		}

		memcpy(block.data, symname, symname_len);
		ext4_block_set(e_dir_ref.fs->bdev, &block);
		ext4_inode_set_size(e_inode_ref.inode, symname_len);
	}

	/* Create VFS inode and add to dentry */
	inode = inode_acquire(dir->i_superblock, e_inode_ref.index);
	if (!inode) {
		ext4_fs_free_inode(&e_inode_ref);
		ext4_fs_put_inode_ref(&e_dir_ref);
		return -ENOMEM;
	}

	/* Initialize the VFS inode */
	inode->i_mode = S_IFLNK | 0777;
	inode->i_op = &ext4_symlink_inode_operations;

	/* Link the dentry to the inode */
	dentry_instantiate(dentry, inode);

	/* Release references */
	ext4_fs_put_inode_ref(&e_inode_ref);
	ext4_fs_put_inode_ref(&e_dir_ref);

	return 0;
}

/**
 * ext4_vfs_mkdir - Create a new directory
 * @dir: Parent directory inode
 * @dentry: Directory entry for the new directory
 * @mode: Mode bits for the new directory
 *
 * Returns: 0 on success, negative error code on failure
 */
static int32 ext4_vfs_mkdir(struct inode* dir, struct dentry* dentry, mode_t mode) {
	struct ext4_inode_ref e_dir_ref, e_inode_ref;
	struct inode* inode;
	int32 ret;

	/* Ensure mode includes directory flag */
	mode |= S_IFDIR;

	/* Get directory inode reference */
	ret = __inode_getExt4InodeRef(dir, &e_dir_ref);
	if (ret != 0) return ret;

	/* Allocate a new inode for the directory */
	ret = ext4_fs_alloc_inode(e_dir_ref.fs, &e_inode_ref, EXT4_DE_DIR);
	if (ret != 0) {
		ext4_fs_put_inode_ref(&e_dir_ref);
		return ret;
	}

	/* Set mode for the new directory */
	ext4_inode_set_mode(&e_dir_ref.fs->sb, e_inode_ref.inode, mode);

	/* Initialize directory structure (create "." and "..") */
#if CONFIG_DIR_INDEX_ENABLE
	ret = ext4_dir_dx_init(&e_inode_ref, &e_dir_ref);
#else
	/* Create dot entries manually if directory indexing is disabled */
	struct ext4_block block;
	ext4_fsblk_t fblock;
	ext4_lblk_t iblock = 0;

	/* Append a data block to the inode */
	ret = ext4_fs_append_inode_dblk(&e_inode_ref, &fblock, &iblock);
	if (ret != 0) {
		ext4_fs_free_inode(&e_inode_ref);
		ext4_fs_put_inode_ref(&e_dir_ref);
		return ret;
	}

	/* Get the block to write directory entries */
	ret = ext4_block_get(e_dir_ref.fs->bdev, &block, fblock);
	if (ret != 0) {
		ext4_fs_free_inode(&e_inode_ref);
		ext4_fs_put_inode_ref(&e_dir_ref);
		return ret;
	}

	/* Create "." entry */
	struct ext4_dir_en* entry = (struct ext4_dir_en*)block.data;
	ext4_dir_write_entry(e_dir_ref.fs->sb, entry, 12, &e_inode_ref, ".", 1);

	/* Create ".." entry */
	entry = (struct ext4_dir_en*)((char*)block.data + 12);
	ext4_dir_write_entry(e_dir_ref.fs->sb, entry, block.size - 12, &e_dir_ref, "..", 2);

	/* Write the block back */
	ext4_block_set(e_dir_ref.fs->bdev, &block);
#endif

	if (ret != 0) {
		ext4_fs_free_inode(&e_inode_ref);
		ext4_fs_put_inode_ref(&e_dir_ref);
		return ret;
	}

	/* Add entry to parent directory */
	ret = ext4_dir_add_entry(&e_dir_ref, dentry->d_name->name, dentry->d_name->len, &e_inode_ref);
	if (ret != 0) {
		ext4_fs_free_inode(&e_inode_ref);
		ext4_fs_put_inode_ref(&e_dir_ref);
		return ret;
	}

	/* Create VFS inode and add to dentry */
	inode = inode_acquire(dir->i_superblock, e_inode_ref.index);
	if (!inode) {
		ext4_fs_free_inode(&e_inode_ref);
		ext4_fs_put_inode_ref(&e_dir_ref);
		return -ENOMEM;
	}

	/* Initialize the VFS inode */
	inode->i_mode = mode;
	inode->i_op = &ext4_dir_inode_operations;
	inode->i_fop = &ext4_dir_operations;

	/* Link the dentry to the inode */
	dentry_instantiate(dentry, inode);

	/* Release references */
	ext4_fs_put_inode_ref(&e_inode_ref);
	ext4_fs_put_inode_ref(&e_dir_ref);

	return 0;
}

/**
 * ext4_vfs_rmdir - Remove a directory
 * @dir: Parent directory inode
 * @dentry: The directory to remove
 *
 * Returns: 0 on success, negative error code on failure
 */
static int32 ext4_vfs_rmdir(struct inode* dir, struct dentry* dentry) {
	struct ext4_inode_ref e_dir_ref, e_inode_ref;
	struct ext4_dir_iter it;
	bool is_empty = true;
	int32 ret;

	/* Get directory and target directory references */
	ret = __inode_getExt4InodeRef(dir, &e_dir_ref);
	if (ret != 0) return ret;

	ret = __inode_getExt4InodeRef(dentry->d_inode, &e_inode_ref);
	if (ret != 0) {
		ext4_fs_put_inode_ref(&e_dir_ref);
		return ret;
	}

	/* Check if directory is empty */
	ret = ext4_dir_iterator_init(&it, &e_inode_ref, 0);
	if (ret != 0) {
		ext4_fs_put_inode_ref(&e_inode_ref);
		ext4_fs_put_inode_ref(&e_dir_ref);
		return ret;
	}

	/* Skip "." and ".." entries */
	if (!it.curr) {
		is_empty = true;
	} else {
		/* Skip "." */
		ret = ext4_dir_iterator_next(&it);
		if (ret != 0) {
			ext4_dir_iterator_fini(&it);
			ext4_fs_put_inode_ref(&e_inode_ref);
			ext4_fs_put_inode_ref(&e_dir_ref);
			return ret;
		}

		/* Skip ".." */
		ret = ext4_dir_iterator_next(&it);
		if (ret != 0) {
			ext4_dir_iterator_fini(&it);
			ext4_fs_put_inode_ref(&e_inode_ref);
			ext4_fs_put_inode_ref(&e_dir_ref);
			return ret;
		}

		/* If there are more entries, the directory is not empty */
		is_empty = (it.curr == NULL);
	}

	ext4_dir_iterator_fini(&it);

	if (!is_empty) {
		ext4_fs_put_inode_ref(&e_inode_ref);
		ext4_fs_put_inode_ref(&e_dir_ref);
		return -ENOTEMPTY;
	}

	/* Remove directory entry from parent */
	ret = ext4_dir_remove_entry(&e_dir_ref, dentry->d_name->name, dentry->d_name->len);
	if (ret != 0) {
		ext4_fs_put_inode_ref(&e_inode_ref);
		ext4_fs_put_inode_ref(&e_dir_ref);
		return ret;
	}

	/* Free the inode */
	ext4_fs_free_inode(&e_inode_ref);

	/* Release references */
	ext4_fs_put_inode_ref(&e_inode_ref);
	ext4_fs_put_inode_ref(&e_dir_ref);

	return 0;
}

/**
 * ext4_vfs_rename - Rename a file or directory
 * @old_dir: Parent directory of source
 * @old_dentry: Source dentry
 * @new_dir: Parent directory of destination
 * @new_dentry: Destination dentry
 *
 * Returns: 0 on success, negative error code on failure
 */
static int32 ext4_vfs_rename(struct inode* old_dir, struct dentry* old_dentry, struct inode* new_dir, struct dentry* new_dentry) {
	struct ext4_inode_ref e_old_dir_ref, e_new_dir_ref, e_inode_ref;
	struct ext4_dir_search_result result;
	int32 ret;

	/* Get inode references */
	ret = __inode_getExt4InodeRef(old_dir, &e_old_dir_ref);
	if (ret != 0) return ret;

	ret = __inode_getExt4InodeRef(new_dir, &e_new_dir_ref);
	if (ret != 0) {
		ext4_fs_put_inode_ref(&e_old_dir_ref);
		return ret;
	}

	ret = __inode_getExt4InodeRef(old_dentry->d_inode, &e_inode_ref);
	if (ret != 0) {
		ext4_fs_put_inode_ref(&e_new_dir_ref);
		ext4_fs_put_inode_ref(&e_old_dir_ref);
		return ret;
	}

	/* Check if target already exists */
	ret = ext4_dir_find_entry(&result, &e_new_dir_ref, new_dentry->d_name->name, new_dentry->d_name->len);
	if (ret == 0) {
		/* Target exists, handle differently based on type */
		struct ext4_inode_ref target_ref;
		ret = __inode_getExt4InodeRef(new_dentry->d_inode, &target_ref);
		if (ret != 0) {
			ext4_dir_destroy_result(&e_new_dir_ref, &result);
			ext4_fs_put_inode_ref(&e_inode_ref);
			ext4_fs_put_inode_ref(&e_new_dir_ref);
			ext4_fs_put_inode_ref(&e_old_dir_ref);
			return ret;
		}

		/* Check if source is a directory */
		bool source_is_dir = S_ISDIR(old_dentry->d_inode->i_mode);
		bool target_is_dir = S_ISDIR(new_dentry->d_inode->i_mode);

		if (source_is_dir && !target_is_dir) {
			/* Can't overwrite non-directory with directory */
			ext4_fs_put_inode_ref(&target_ref);
			ext4_dir_destroy_result(&e_new_dir_ref, &result);
			ext4_fs_put_inode_ref(&e_inode_ref);
			ext4_fs_put_inode_ref(&e_new_dir_ref);
			ext4_fs_put_inode_ref(&e_old_dir_ref);
			return -EISDIR;
		} else if (!source_is_dir && target_is_dir) {
			/* Can't overwrite directory with non-directory */
			ext4_fs_put_inode_ref(&target_ref);
			ext4_dir_destroy_result(&e_new_dir_ref, &result);
			ext4_fs_put_inode_ref(&e_inode_ref);
			ext4_fs_put_inode_ref(&e_new_dir_ref);
			ext4_fs_put_inode_ref(&e_old_dir_ref);
			return -ENOTDIR;
		} else if (target_is_dir) {
			/* Both are directories, check if target is empty */
			struct ext4_dir_iter it;
			bool is_empty = true;

			ret = ext4_dir_iterator_init(&it, &target_ref, 0);
			if (ret != 0) {
				ext4_fs_put_inode_ref(&target_ref);
				ext4_dir_destroy_result(&e_new_dir_ref, &result);
				ext4_fs_put_inode_ref(&e_inode_ref);
				ext4_fs_put_inode_ref(&e_new_dir_ref);
				ext4_fs_put_inode_ref(&e_old_dir_ref);
				return ret;
			}

			/* Skip "." and ".." entries */
			if (!it.curr) {
				is_empty = true;
			} else {
				/* Skip "." */
				ret = ext4_dir_iterator_next(&it);
				if (ret != 0) {
					ext4_dir_iterator_fini(&it);
					ext4_fs_put_inode_ref(&target_ref);
					ext4_dir_destroy_result(&e_new_dir_ref, &result);
					ext4_fs_put_inode_ref(&e_inode_ref);
					ext4_fs_put_inode_ref(&e_new_dir_ref);
					ext4_fs_put_inode_ref(&e_old_dir_ref);
					return ret;
				}

				/* Skip ".." */
				ret = ext4_dir_iterator_next(&it);
				if (ret != 0) {
					ext4_dir_iterator_fini(&it);
					ext4_fs_put_inode_ref(&target_ref);
					ext4_dir_destroy_result(&e_new_dir_ref, &result);
					ext4_fs_put_inode_ref(&e_inode_ref);
					ext4_fs_put_inode_ref(&e_new_dir_ref);
					ext4_fs_put_inode_ref(&e_old_dir_ref);
					return ret;
				}

				/* If there are more entries, the directory is not empty */
				is_empty = (it.curr == NULL);
			}

			ext4_dir_iterator_fini(&it);

			if (!is_empty) {
				ext4_fs_put_inode_ref(&target_ref);
				ext4_dir_destroy_result(&e_new_dir_ref, &result);
				ext4_fs_put_inode_ref(&e_inode_ref);
				ext4_fs_put_inode_ref(&e_new_dir_ref);
				ext4_fs_put_inode_ref(&e_old_dir_ref);
				return -ENOTEMPTY;
			}
		}

		/* Remove the target */
		ret = ext4_dir_remove_entry(&e_new_dir_ref, new_dentry->d_name->name, new_dentry->d_name->len);
		if (ret != 0) {
			ext4_fs_put_inode_ref(&target_ref);
			ext4_dir_destroy_result(&e_new_dir_ref, &result);
			ext4_fs_put_inode_ref(&e_inode_ref);
			ext4_fs_put_inode_ref(&e_new_dir_ref);
			ext4_fs_put_inode_ref(&e_old_dir_ref);
			return ret;
		}

		/* Decrement link count of target */
		ext4_fs_inode_links_count_dec(&target_ref);

		/* Free the inode if it was the last link */
		if (ext4_inode_get_links_cnt(target_ref.inode) == 0) {
			/* Set deletion time */
			ext4_inode_set_del_time(target_ref.inode, do_time(NULL));
			/* Remove the inode from the filesystem */
			ext4_fs_free_inode(&target_ref);
		}

		ext4_fs_put_inode_ref(&target_ref);
		ext4_dir_destroy_result(&e_new_dir_ref, &result);
	} else if (ret != -ENOENT) {
		/* Error other than "not found" */
		ext4_fs_put_inode_ref(&e_inode_ref);
		ext4_fs_put_inode_ref(&e_new_dir_ref);
		ext4_fs_put_inode_ref(&e_old_dir_ref);
		return ret;
	}

	/* Add entry to new directory */
	ret = ext4_dir_add_entry(&e_new_dir_ref, new_dentry->d_name->name, new_dentry->d_name->len, &e_inode_ref);
	if (ret != 0) {
		ext4_fs_put_inode_ref(&e_inode_ref);
		ext4_fs_put_inode_ref(&e_new_dir_ref);
		ext4_fs_put_inode_ref(&e_old_dir_ref);
		return ret;
	}

	/* Update parent reference in directory if needed */
	if (S_ISDIR(old_dentry->d_inode->i_mode) && old_dir != new_dir) {
		/* Update ".." entry in the moved directory */
#if CONFIG_DIR_INDEX_ENABLE
		ret = ext4_dir_dx_reset_parent_inode(&e_inode_ref, new_dir->i_ino);
#else
		/* Manually update the parent inode reference */
		struct ext4_block block;
		ext4_fsblk_t fblock;

		/* Get the first block of the directory which contains "." and ".." */
		ret = ext4_fs_get_inode_dblk_idx(&e_inode_ref, 0, &fblock, false);
		if (ret == 0 && fblock != 0) {
			ret = ext4_block_get(e_inode_ref.fs->bdev, &block, fblock);
			if (ret == 0) {
				struct ext4_dir_en* dotdot;

				/* Get pointer to ".." entry (typically the second entry) */
				dotdot = (struct ext4_dir_en*)((char*)block.data + ext4_dir_en_get_entry_len((struct ext4_dir_en*)block.data));

				/* Update the inode number */
				ext4_dir_en_set_inode(dotdot, new_dir->i_ino);

				/* Write the block back */
				ext4_block_set(e_inode_ref.fs->bdev, &block);
			}
		}
#endif
		if (ret != 0) {
			/* Failed to update parent reference, but entry was already added */
			/* Should probably try to roll back, but for now just report error */
			ext4_fs_put_inode_ref(&e_inode_ref);
			ext4_fs_put_inode_ref(&e_new_dir_ref);
			ext4_fs_put_inode_ref(&e_old_dir_ref);
			return ret;
		}
	}

	/* Remove entry from old directory */
	ret = ext4_dir_remove_entry(&e_old_dir_ref, old_dentry->d_name->name, old_dentry->d_name->len);
	if (ret != 0) {
		/* Failed to remove from old directory, but already added to new directory */
		ext4_fs_put_inode_ref(&e_inode_ref);
		ext4_fs_put_inode_ref(&e_new_dir_ref);
		ext4_fs_put_inode_ref(&e_old_dir_ref);
		return ret;
	}

	/* Update the dentry in VFS */
	dentry_instantiate(new_dentry, inode_ref(old_dentry->d_inode));

	/* Release references */
	ext4_fs_put_inode_ref(&e_inode_ref);
	ext4_fs_put_inode_ref(&e_new_dir_ref);
	ext4_fs_put_inode_ref(&e_old_dir_ref);

	return 0;
}

/**
 * ext4_vfs_readlink - Read the target of a symbolic link
 * @param dentry The dentry of the symlink
 * @param buffer Output buffer for the link target
 * @param buflen Maximum buffer length
 * @return The number of bytes read on success, negative error code on failure
 */
static int32 ext4_vfs_readlink(struct dentry* dentry, char* buffer, int32 buflen) {
	struct ext4_inode_ref e_inode_ref;
	struct ext4_fs* fs = dentry->d_superblock->s_fs_info;
	int32 ret;

	/* Get inode reference for the symlink */
	ret = ext4_fs_get_inode_ref(fs, dentry->d_inode->i_ino, &e_inode_ref);
	if (ret != 0) return ret;

	/* Check if it's really a symlink */
	if (!S_ISLNK(dentry->d_inode->i_mode)) {
		ext4_fs_put_inode_ref(&e_inode_ref);
		return -EINVAL;
	}

	if (dentry->d_inode->i_size <= 60) {
		/* Symlink target is stored directly in the inode */
		size_t size = dentry->d_inode->i_size;
		if (size > buflen) size = buflen;

		memcpy(buffer, e_inode_ref.inode->blocks, size);
		ret = size;
	} else {
		/* Symlink target is stored in data blocks */
		ext4_fsblk_t fblock;
		struct ext4_block block;

		/* Get first data block of symlink */
		ret = ext4_fs_get_inode_dblk_idx(&e_inode_ref, 0, &fblock, false);
		if (ret != 0) {
			ext4_fs_put_inode_ref(&e_inode_ref);
			return ret;
		}

		if (fblock == 0) {
			ext4_fs_put_inode_ref(&e_inode_ref);
			return -EIO;
		}

		/* Read the block containing symlink target */
		ret = ext4_block_get(fs->bdev, &block, fblock);
		if (ret != 0) {
			ext4_fs_put_inode_ref(&e_inode_ref);
			return ret;
		}

		/* Copy data to user buffer */
		size_t size = dentry->d_inode->i_size;
		if (size > buflen) size = buflen;

		memcpy(buffer, block.data, size);
		ext4_block_set(fs->bdev, &block);
		ret = size;
	}

	ext4_fs_put_inode_ref(&e_inode_ref);
	return ret;
}

/**
 * ext4_vfs_permission - Check permission for an inode
 * @param inode Inode to check permission for
 * @param mask Permission mask to check
 * @return 0 if permitted, negative error code otherwise
 */
static int32 ext4_vfs_permission(struct inode* inode, int32 mask) {
	/* Basic permission check - simplified for initial implementation
	 * A full implementation would involve:
	 * 1. Checking user/group IDs against current user
	 * 2. Handling special cases like root user privileges
	 * 3. Checking ACLs if supported
	 */

	/* No permission checks needed if mask is 0 */
	if (!mask) return 0;

	/* Always permit if the inode is in-memory only */
	if (inode->i_state & I_NEW) return 0;

	/* Special case: checking for existence only (MAY_ACCESS) */
	if (mask == MAY_ACCESS) return 0;

	/* Check execute permission */
	if ((mask & MAY_EXEC) && !(inode->i_mode & S_IXUSR)) return -EACCES;

	/* Check write permission */
	if ((mask & MAY_WRITE) && !(inode->i_mode & S_IWUSR)) return -EACCES;

	/* Check read permission */
	if ((mask & MAY_READ) && !(inode->i_mode & S_IRUSR)) return -EACCES;

	return 0;
}

/**
 * ext4_vfs_setattr - Change attributes of an inode
 * @param dentry Dentry of the target file
 * @param attr Attributes to set
 * @return 0 on success, negative error code on failure
 */
static int32 ext4_vfs_setattr(struct dentry* dentry, struct iattr* attr) {
	struct inode* inode = dentry->d_inode;
	struct ext4_inode_ref e_inode_ref;
	int32 ret;

	/* Get a reference to the inode */
	ret = ext4_fs_get_inode_ref(inode->i_superblock->s_fs_info, inode->i_ino, &e_inode_ref);
	CHECK_RET(ret, ret);
	struct ext4_sblock* e_sb = &e_inode_ref.fs->sb;
	struct ext4_inode* e_inode = e_inode_ref.inode;

	/* Check permissions first */
	ret = setattr_prepare(dentry, attr);
	if (ret != 0) {
		ext4_fs_put_inode_ref(&e_inode_ref);
		return ret;
	}

	/* Update mode if requested */
	if (attr->ia_valid & ATTR_MODE) {
		ext4_inode_set_mode(e_sb, e_inode, attr->ia_mode);
		inode->i_mode = attr->ia_mode;
		e_inode_ref.dirty = true;
	}

	/* Update ownership if requested */
	if (attr->ia_valid & ATTR_UID) {
		ext4_inode_set_uid(e_inode, attr->ia_uid);
		inode->i_uid = attr->ia_uid;
		e_inode_ref.dirty = true;
	}

	if (attr->ia_valid & ATTR_GID) {
		ext4_inode_set_gid(e_inode, attr->ia_gid);
		inode->i_gid = attr->ia_gid;
		e_inode_ref.dirty = true;
	}

	/* Update size if requested */
	if (attr->ia_valid & ATTR_SIZE) {
		if (attr->ia_size < inode->i_size) {
			/* Truncate to smaller size */
			ret = ext4_fs_truncate_inode(&e_inode_ref, attr->ia_size);
			if (ret != 0) {
				ext4_fs_put_inode_ref(&e_inode_ref);
				return ret;
			}
		} else if (attr->ia_size > inode->i_size) {
			/* Expanding the file is implemented by zero-filling the new space
			 * when it is first accessed by read/write operations.
			 */
		}

		inode->i_size = attr->ia_size;
		ext4_inode_set_size(e_inode, attr->ia_size);
		e_inode_ref.dirty = true;
	}

	/* Update timestamps if requested */
	if (attr->ia_valid & ATTR_ATIME) {
		inode->i_atime = attr->ia_atime;
		ext4_inode_set_access_time(e_inode, attr->ia_atime.tv_sec);
		e_inode_ref.dirty = true;
	}

	if (attr->ia_valid & ATTR_MTIME) {
		inode->i_mtime = attr->ia_mtime;
		ext4_inode_set_modif_time(e_inode, attr->ia_mtime.tv_sec);
		e_inode_ref.dirty = true;
	}

	if (attr->ia_valid & ATTR_CTIME) {
		inode->i_ctime = attr->ia_ctime;
		ext4_inode_set_change_inode_time(e_inode, attr->ia_ctime.tv_sec);
		e_inode_ref.dirty = true;
	}

	/* Release the inode reference */
	return ext4_fs_put_inode_ref(&e_inode_ref);
}

/**
 * ext4_vfs_getattr - Get attributes of a file
 * @param mnt Mount point
 * @param dentry Directory entry
 * @param stat Structure to store attributes
 * @return 0 on success, negative error code on failure
 */
static int32 ext4_vfs_getattr(struct vfsmount* mnt, struct dentry* dentry, struct kstat* stat) {
	struct inode* inode = dentry->d_inode;

	/* Copy basic attributes from the inode */
	stat->dev = inode->i_rdev;
	stat->ino = inode->i_ino;
	stat->mode = inode->i_mode;
	stat->nlink = inode->i_nlink;
	stat->uid = inode->i_uid;
	stat->gid = inode->i_gid;

	stat->size = inode->i_size;
	stat->atime = inode->i_atime;
	stat->mtime = inode->i_mtime;
	stat->ctime = inode->i_ctime;
	stat->blksize = inode->i_superblock->s_blocksize;
	stat->blocks = inode->i_blocks;

	return 0;
}

/**
 * ext4_vfs_setxattr - Set an extended attribute
 * @param dentry Dentry of the file
 * @param name Name of the attribute
 * @param value Value of the attribute
 * @param size Size of the value
 * @param flags Flags for the operation
 * @return 0 on success, negative error code on failure
 */
static int32 ext4_vfs_setxattr(struct dentry* dentry, const char* name, const void* value, size_t size, int32 flags) {
#if CONFIG_XATTR_ENABLE
	struct inode* inode = dentry->d_inode;
	struct ext4_inode_ref e_inode_ref;
	uint8_t name_index;
	size_t name_len;
	const char* real_name;
	bool found;
	int32 ret;

	/* Get inode reference */
	ret = ext4_fs_get_inode_ref(inode->i_superblock->s_fs_info, inode->i_ino, &e_inode_ref);
	if (ret != 0) return ret;

	/* Extract the attribute namespace and name */
	real_name = ext4_extract_xattr_name(name, strlen(name), &name_index, &name_len, &found);
	if (!found) {
		ext4_fs_put_inode_ref(&e_inode_ref);
		return -EINVAL;
	}

	/* Remove the extended attribute */
	ret = ext4_xattr_remove(&e_inode_ref, name_index, real_name, name_len);

	/* Clean up and return result */
	ext4_fs_put_inode_ref(&e_inode_ref);
	return ret;
#else
	return -ENOTSUP;
#endif
}

/*
 * Implementations of extended VFS interfaces
 */

/**
 * @brief Create a special file (device file, FIFO, etc.)
 *
 * @param inode Parent directory inode
 * @param dentry Target dentry
 * @param mode File mode
 * @param dev Device number (for device files)
 * @return int32 Error code
 */
static int32 ext4_vfs_mknod(struct inode* inode, struct dentry* dentry, fmode_t mode, dev_t dev) {
	struct ext4_inode_ref parent_ref;
	struct ext4_inode_ref child_ref;
	int32 ret;
	int32 filetype;

	/* Map VFS inode to ext4 inode reference */
	ret = __inode_getExt4InodeRef(inode, &parent_ref);
	if (ret != 0) return ret;

	/* Determine file type based on mode */
	if (S_ISCHR(mode))
		filetype = EXT4_DE_CHRDEV;
	else if (S_ISBLK(mode))
		filetype = EXT4_DE_BLKDEV;
	else if (S_ISFIFO(mode))
		filetype = EXT4_DE_FIFO;
	else if (S_ISSOCK(mode))
		filetype = EXT4_DE_SOCK;
	else
		return -EINVAL;

	/* Allocate a new inode */
	ret = ext4_fs_alloc_inode(parent_ref.fs, &child_ref, filetype);
	if (ret != 0) return ret;

	/* Set device number for device files */
	if (S_ISCHR(mode) || S_ISBLK(mode)) ext4_inode_set_dev(child_ref.inode, dev);

	/* Set permissions */
	ext4_inode_set_mode(&parent_ref.fs->sb, child_ref.inode, mode);

	/* Add entry to parent directory */
	ret = ext4_dir_add_entry(&parent_ref, dentry->d_name->name, dentry->d_name->len, &child_ref);
	if (ret != 0) {
		ext4_fs_free_inode(&child_ref);
		return ret;
	}

	/* Setup VFS inode */
	struct inode* child_inode = inode_acquire(inode->i_superblock, child_ref.index);
	if (!child_inode) {
		ext4_fs_free_inode(&child_ref);
		return -ENOMEM;
	}

	/* Fill VFS inode with ext4 inode data */
	child_inode->i_ino = child_ref.index;
	child_inode->i_mode = mode;
	child_inode->i_rdev = dev;

	/* Link the dentry to the inode */
	dentry_instantiate(dentry, child_inode);

	ext4_fs_put_inode_ref(&child_ref);
	ext4_fs_put_inode_ref(&parent_ref);

	return 0;
}

/**
 * @brief Get target of a symbolic link
 *
 * @param dentry Link dentry
 * @param inode Link inode
 * @param path Target path
 * @return int32 Error code
 */
static int32 ext4_vfs_get_link(struct dentry* dentry, struct inode* inode, struct path* path) {
	struct ext4_inode_ref e_inode_ref;
	char* link_target;
	int32 ret, len;

	/* Get ext4 inode reference */
	ret = __inode_getExt4InodeRef(inode, &e_inode_ref);
	if (ret != 0) return ret;

	/* Check if it's a symlink */
	if (!ext4_inode_is_type(&e_inode_ref.fs->sb, e_inode_ref.inode, EXT4_INODE_MODE_SOFTLINK)) {
		ext4_fs_put_inode_ref(&e_inode_ref);
		return -EINVAL;
	}

	/* Allocate buffer for link target */
	link_target = kmalloc(inode->i_size + 1);
	if (!link_target) {
		ext4_fs_put_inode_ref(&e_inode_ref);
		return -ENOMEM;
	}

	/* Read link target */
	if (inode->i_size < 60) {
		/* Fast symlink (target stored in inode blocks) */
		memcpy(link_target, e_inode_ref.inode->blocks, inode->i_size);
		link_target[inode->i_size] = '\0';
	} else {
		/* Read from file blocks */
		size_t rcnt;
		char* pathname = dentry_allocRawPath(dentry);
		ret = ext4_readlink(pathname, link_target, inode->i_size, &rcnt);
		kfree(pathname);

		if (ret != 0) {
			kfree(link_target);
			ext4_fs_put_inode_ref(&e_inode_ref);
			return ret;
		}

		link_target[rcnt] = '\0';
	}

	/* Parse link target into path */
	ret = path_create(link_target, 0, path);
	kfree(link_target);
	ext4_fs_put_inode_ref(&e_inode_ref);

	return ret;
}

// /**
//  * @brief Get POSIX access control list for inode
//  *
//  * @param inode Inode to get ACL for
//  * @param type ACL type (access or default)
//  * @return struct posix_acl* ACL structure or error code
//  */
// static int32 ext4_vfs_get_acl(struct inode *inode, int32 type)
// {
//     struct ext4_inode_ref e_inode_ref;
//     struct posix_acl *acl = NULL;
//     int32 ret;

//     /* Currently posix ACLs are not supported in lwext4 */
//     /* This is a placeholder implementation */

//     /* Get ext4 inode reference */
//     ret = __inode_getExt4InodeRef(inode, &e_inode_ref);
//     if (ret != 0)
//         return ret;

//     /* Fallback to mode bits for access ACLs */
//     if (type == ACL_TYPE_ACCESS) {
//         acl = posix_acl_from_mode(inode->i_mode, GFP_KERNEL);
//     }

//     ext4_fs_put_inode_ref(&e_inode_ref);
//     return (int32)acl;
// }

/**
 * @brief Set POSIX access control list for inode
 *
 * @param inode Inode to set ACL for
 * @param acl ACL to set
 * @param type ACL type (access or default)
 * @return int32 Error code
 */
// static int32 ext4_vfs_set_acl(struct inode *inode, struct posix_acl *acl, int32 type)
// {
//     struct ext4_inode_ref e_inode_ref;
//     int32 ret;

//     /* Currently posix ACLs are not supported in lwext4 */
//     /* This is a placeholder implementation */

//     /* Get ext4 inode reference */
//     ret = __inode_getExt4InodeRef(inode, &e_inode_ref);
//     if (ret != 0)
//         return ret;

//     /* Update mode bits if setting access ACL */
//     if (type == ACL_TYPE_ACCESS && acl) {
//         mode_t mode = inode->i_mode;
//         ret = posix_acl_update_mode(inode, &mode, &acl);
//         if (ret == 0 && mode != inode->i_mode) {
//             inode->i_mode = mode;
//             ext4_inode_set_mode(inode->i_superblock, e_inode_ref.inode, mode);
//             e_inode_ref.dirty = true;
//         }
//     }

//     ext4_fs_put_inode_ref(&e_inode_ref);
//     return ret;
// }

/**
 * @brief File extent block mapping information
 *
 * @param inode Target inode
 * @param fiemap_info Mapping info structure
 * @param start Start offset
 * @param len Length to map
 * @return int32 Error code
 */
// static int32 ext4_vfs_fiemap(struct inode *inode, struct fiemap_extent_info *fiemap_info,
//                      uint64_t start, uint64_t len)
// {
//     struct ext4_inode_ref e_inode_ref;
//     int32 ret;

//     /* Get ext4 inode reference */
//     ret = __inode_getExt4InodeRef(inode, &e_inode_ref);
//     if (ret != 0)
//         return ret;

//     /* Simple implementation - just report basic extent info */
//     fiemap_info->fi_flags |= FIEMAP_FLAG_SYNC;

//     /* Process block mappings */
//     uint32_t block_size = e_inode_ref.fs->sb.log_block_size;
//     uint64_t start_block = start / block_size;
//     uint64_t end_block = (start + len + block_size - 1) / block_size;

//     /* Limit to file size */
//     uint64_t file_blocks = (inode->i_size + block_size - 1) / block_size;
//     if (end_block > file_blocks)
//         end_block = file_blocks;

//     /* Add extents to the mapping info */
//     uint64_t blk = start_block;
//     while (blk < end_block) {
//         ext4_fsblk_t phys_block;
//         uint64_t extent_len = 0;
//         uint64_t logical_start = blk;

//         /* Get physical block mapping */
//         ret = ext4_fs_get_inode_dblk_idx(&e_inode_ref, blk, &phys_block, false);
//         if (ret != 0 || phys_block == 0) {
//             /* Sparse region - skip ahead */
//             blk++;
//             continue;
//         }

//         /* Find extent length (contiguous blocks) */
//         ext4_fsblk_t prev_phys = phys_block;
//         while (blk < end_block) {
//             ret = ext4_fs_get_inode_dblk_idx(&e_inode_ref, blk, &phys_block, false);
//             if (ret != 0 || phys_block == 0 || phys_block != prev_phys + extent_len)
//                 break;

//             extent_len++;
//             blk++;
//         }

//         /* Add this extent to the mapping */
//         struct fiemap_extent extent;
//         extent.fe_logical = logical_start * block_size;
//         extent.fe_physical = prev_phys * block_size;
//         extent.fe_length = extent_len * block_size;

//         /* Set flags */
//         extent.fe_flags = 0;
//         if (logical_start == 0)
//             extent.fe_flags |= FIEMAP_EXTENT_DATA_INLINE;
//         if (logical_start + extent_len >= file_blocks)
//             extent.fe_flags |= FIEMAP_EXTENT_LAST;

//         /* Add the extent to the result */
//         ret = fiemap_fill_next_extent(fiemap_info,
//                                      extent.fe_logical,
//                                      extent.fe_physical,
//                                      extent.fe_length,
//                                      extent.fe_flags);

//         if (ret != 0) {
//             if (ret == 1) /* No more space */
//                 break;

//             ext4_fs_put_inode_ref(&e_inode_ref);
//             return ret;
//         }
//     }

//     ext4_fs_put_inode_ref(&e_inode_ref);
//     return 0;
// }

/**
 * @brief Get physical block address for logical block
 *
 * @param inode Target inode
 * @param block Logical block number
 * @param buffer_head Buffer head to fill
 * @param create Create block if it doesn't exist
 * @return int32 Error code
 */
static int32 ext4_vfs_get_block(struct inode* inode, sector_t block, struct buffer_head* buffer_head, int32 create) {
	struct ext4_inode_ref e_inode_ref;
	ext4_fsblk_t phys_block;
	int32 ret;

	/* Get ext4 inode reference */
	ret = __inode_getExt4InodeRef(inode, &e_inode_ref);
	if (ret != 0) return ret;

	/* Get physical block */
	ret = ext4_fs_get_inode_dblk_idx(&e_inode_ref, block, &phys_block, !create);

	if (ret == 0 && create && phys_block == 0) {
		/* Need to allocate block */
		ret = ext4_fs_init_inode_dblk_idx(&e_inode_ref, block, &phys_block);
		if (ret != 0) {
			ext4_fs_put_inode_ref(&e_inode_ref);
			return ret;
		}
	}

	if (ret == 0 && phys_block > 0) {
		/* Set buffer head */
		buffer_head->b_bdev = inode->i_superblock->s_bdev; // 需要做一个devid到device的转换
		buffer_head->b_blocknr = phys_block;
		buffer_head->b_size = e_inode_ref.fs->sb.log_block_size;
		if (create)
			buffer_head->b_state |= (1 << BH_New);
		else
			buffer_head->b_state |= (1 << BH_Mapped);
	}

	ext4_fs_put_inode_ref(&e_inode_ref);
	return ret;
}

/**
 * @brief Convert logical block number to physical block number
 *
 * @param inode Target inode
 * @param block Logical block number
 * @return sector_t Physical block number or error code
 */
static sector_t ext4_vfs_bmap(struct inode* inode, sector_t block) {
	struct ext4_inode_ref e_inode_ref;
	ext4_fsblk_t phys_block;
	int32 ret;

	/* Get ext4 inode reference */
	ret = __inode_getExt4InodeRef(inode, &e_inode_ref);
	if (ret != 0) return 0;

	/* Get physical block */
	ret = ext4_fs_get_inode_dblk_idx(&e_inode_ref, block, &phys_block, true);

	ext4_fs_put_inode_ref(&e_inode_ref);

	if (ret != 0) return 0;

	return phys_block;
}

/**
 * @brief Truncate file blocks
 *
 * @param inode Target inode
 * @param size New file size
 */
static void ext4_vfs_truncate_blocks(struct inode* inode, loff_t size) {
	struct ext4_inode_ref e_inode_ref;
	int32 ret;

	/* Get ext4 inode reference */
	ret = __inode_getExt4InodeRef(inode, &e_inode_ref);
	if (ret != 0) return;

	/* Truncate using lwext4 function */
	ret = ext4_fs_truncate_inode(&e_inode_ref, size);

	/* Update inode size */
	if (ret == 0) {
		inode->i_size = size;
		ext4_inode_set_size(e_inode_ref.inode, size);
		e_inode_ref.dirty = true;
	}

	ext4_fs_put_inode_ref(&e_inode_ref);
}

/**
 * @brief Direct I/O implementation
 *
 * @param kiocb Kernel I/O control block
 * @param iov_iter I/O vector iterator
 * @return int32 Number of bytes transferred or error code
 */
static int32 ext4_vfs_direct_IO(struct kiocb* kiocb, struct io_vector_iterator* iov_iter) {
	/* Direct I/O is not fully implemented for this adapter */
	/* Fallback to buffered I/O */
	return -EOPNOTSUPP;
}

/**
 * @brief Handle memory page fault in mmap
 *
 * @param vma Virtual memory area
 * @param vmf VM fault information
 * @return vm_fault_t Fault handling result
 */
static vm_fault_t ext4_vfs_page_fault(struct vm_area_struct* vma, struct vm_fault* vmf) {
	struct inode* inode = vma->vm_file->f_inode;
	struct page* page;

	/* Get page from page cache or read it */
	page = addrSpace_getPage(inode->i_mapping, vmf->pgoff);
	if (!page) {
		page = addrSpace_readPage(inode->i_mapping, vmf->pgoff);
		CHECK_PTR_VALID(page, VM_FAULT_ERROR);
	}

	/* Map page to userspace */
	int32 ret = vm_insert_page(vma, vmf->address, page);
	if (ret != 0) {
		addrSpace_putPage(inode->i_mapping, page);
		return VM_FAULT_ERROR;
	}

	return VM_FAULT_NOPAGE;
}

// /**
//  * @brief Get unmapped memory area for mmap
//  *
//  * @param file File being mapped
//  * @param addr Suggested address (optional)
//  * @param len Length to map
//  * @param pgoff Page offset in file
//  * @param flags Mapping flags
//  * @return uint64 Address for mapping or error
//  */
// static uint64 ext4_vfs_get_unmapped_area(struct file *file, uint64 addr,
//                                          uint64 len, uint64 pgoff,
//                                          uint64 flags)
// {
//     /* Use generic implementation */
//     return generic_get_unmapped_area(file, addr, len, pgoff, flags);
// }

/**
 * @brief Atomic open operation
 *
 * @param inode Directory inode
 * @param dentry File dentry
 * @param file File structure to fill
 * @param open_flag Open flags
 * @param create_mode Creation mode
 * @return int32 Error code
 */
static int32 ext4_vfs_atomic_open(struct inode* inode, struct dentry* dentry, struct file* file, unsigned open_flag, umode_t create_mode) {
	int32 ret;
	struct inode* found_inode = ext4_vfs_lookup(inode, dentry, 0);

	if (PTR_IS_ERROR(found_inode)) return PTR_ERR(found_inode);

	if (!found_inode && (open_flag & O_CREAT)) {
		/* File doesn't exist, create it */
		ret = ext4_vfs_create(inode, dentry, create_mode);
		if (ret != 0) return ret;
	} else if (!found_inode) {
		/* File doesn't exist and not creating */
		return -ENOENT;
	} else {
		/* File exists */
		dentry_instantiate(dentry, found_inode);
	}

	/* Open the file */
	if (file) {
		ret = file->f_operations->open(dentry->d_inode, file);
		if (ret != 0) return ret;
	}

	return 0;
}

/**
 * @brief Create a temporary file
 *
 * @param inode Directory inode
 * @param dentry Dentry to use
 * @param mode File mode
 * @return int32 Error code
 */
static int32 ext4_vfs_tmpfile(struct inode* inode, struct dentry* dentry, umode_t mode) {
	struct ext4_inode_ref parent_ref;
	struct ext4_inode_ref child_ref;
	int32 ret;

	/* Get ext4 inode reference for parent */
	ret = __inode_getExt4InodeRef(inode, &parent_ref);
	if (ret != 0) return ret;

	/* Allocate a new inode without directory entry */
	ret = ext4_fs_alloc_inode(parent_ref.fs, &child_ref, EXT4_DE_REG_FILE);
	if (ret != 0) {
		ext4_fs_put_inode_ref(&parent_ref);
		return ret;
	}

	/* Set permissions */
	ext4_inode_set_mode(&parent_ref.fs->sb, child_ref.inode, mode);

	/* Set it to be unlinked (0 link count but still accessible) */
	ext4_inode_set_links_cnt(child_ref.inode, 0);

	/* Create VFS inode */
	struct inode* child_inode = inode_acquire(inode->i_superblock, child_ref.index);
	if (!child_inode) {
		ext4_fs_free_inode(&child_ref);
		ext4_fs_put_inode_ref(&parent_ref);
		return -ENOMEM;
	}

	/* Fill VFS inode with ext4 inode data */
	child_inode->i_ino = child_ref.index;
	child_inode->i_mode = mode;
	child_inode->i_nlink = 0; /* Unlinked but open */

	/* Instantiate dentry */
	dentry_instantiate(dentry, child_inode);

	ext4_fs_put_inode_ref(&child_ref);
	ext4_fs_put_inode_ref(&parent_ref);
	return 0;
}

/**
 * ext4_vfs_getxattr - Get an extended attribute
 * @param dentry Dentry of the file
 * @param name Name of the attribute
 * @param buffer Buffer to store the value
 * @param size Size of the buffer
 * @return Size of the attribute value on success, negative error code on failure
 */
static ssize_t ext4_vfs_getxattr(struct dentry* dentry, const char* name, void* buffer, size_t size) {
#if CONFIG_XATTR_ENABLE
	struct inode* inode = dentry->d_inode;
	struct ext4_inode_ref e_inode_ref;
	uint8_t name_index;
	size_t name_len;
	const char* real_name;
	bool found;
	size_t data_len;
	int32 ret;

	/* Get inode reference */
	ret = ext4_fs_get_inode_ref(inode->i_superblock->s_fs_info, inode->i_ino, &e_inode_ref);
	if (ret != 0) return ret;

	/* Extract the attribute namespace and name */
	real_name = ext4_extract_xattr_name(name, strlen(name), &name_index, &name_len, &found);
	if (!found) {
		ext4_fs_put_inode_ref(&e_inode_ref);
		return -EINVAL;
	}

	/* Get the extended attribute */
	ret = ext4_xattr_get(&e_inode_ref, name_index, real_name, name_len, buffer, size, &data_len);

	/* Clean up and return result */
	ext4_fs_put_inode_ref(&e_inode_ref);
	if (ret == 0) return data_len;
	return ret;
#else
	return -ENOTSUP;
#endif
}

/**
 * ext4_vfs_listxattr - List extended attributes
 * @param dentry Dentry of the file
 * @param buffer Buffer to store the list
 * @param size Size of the buffer
 * @return Size of the attribute list on success, negative error code on failure
 */
static ssize_t ext4_vfs_listxattr(struct dentry* dentry, char* buffer, size_t size) {
#if CONFIG_XATTR_ENABLE
	struct inode* inode = dentry->d_inode;
	struct ext4_inode_ref e_inode_ref;
	struct ext4_xattr_list_entry list;
	size_t list_len;
	int32 ret;

	/* Get inode reference */
	ret = ext4_fs_get_inode_ref(inode->i_superblock->s_fs_info, inode->i_ino, &e_inode_ref);
	if (ret != 0) return ret;

	/* Initialize list head */
	list.next = NULL;

	/* Get the list of extended attributes */
	ret = ext4_xattr_list(&e_inode_ref, &list, &list_len);
	if (ret != 0) {
		ext4_fs_put_inode_ref(&e_inode_ref);
		return ret;
	}

	/* Copy the list to the output buffer if it's large enough */
	if (buffer && size >= list_len) {
		size_t offset = 0;
		struct ext4_xattr_list_entry* entry = &list;

		while ((entry = entry->next) != NULL) {
			const char* prefix;
			size_t prefix_len;

			/* Get the namespace prefix */
			prefix = ext4_get_xattr_name_prefix(entry->name_index, &prefix_len);

			/* Copy the full name (prefix:name) */
			if (prefix && prefix_len > 0) {
				memcpy(buffer + offset, prefix, prefix_len);
				offset += prefix_len;
			}

			memcpy(buffer + offset, entry->name, entry->name_len);
			offset += entry->name_len;

			/* Null-terminate the entry */
			buffer[offset++] = '\0';
		}
	}

	/* Free the list */
	struct ext4_xattr_list_entry* entry = list.next;
	while (entry) {
		struct ext4_xattr_list_entry* next = entry->next;
		if (entry->name) ext4_free(entry->name);
		ext4_free(entry);
		entry = next;
	}

	/* Clean up and return result */
	ext4_fs_put_inode_ref(&e_inode_ref);
	return list_len;
#else
	return -ENOTSUP;
#endif
}

/**
 * ext4_vfs_removexattr - Remove an extended attribute
 * @param dentry Dentry of the file
 * @param name Name of the attribute
 * @return 0 on success, negative error code on failure
 */
static int32 ext4_vfs_removexattr(struct dentry* dentry, const char* name) {
#if CONFIG_XATTR_ENABLE
	struct inode* inode = dentry->d_inode;
	struct ext4_inode_ref e_inode_ref;
	uint8_t name_index;
	size_t name_len;
	const char* real_name;
	bool found;
	int32 ret;

	/* Get inode reference */
	ret = ext4_fs_get_inode_ref(inode->i_superblock->s_fs_info, inode->i_ino, &e_inode_ref);
	if (ret != 0) return ret;

	/* Extract the attribute namespace and name */
	real_name = ext4_extract_xattr_name(name, strlen(name), &name_index, &name_len, &found);
	if (!found) {
		/* Invalid attribute name or namespace */
		ext4_fs_put_inode_ref(&e_inode_ref);
		return -ENODATA;
	}

	/* Call ext4 library to remove the extended attribute */
	ret = ext4_xattr_remove(&e_inode_ref, name_index, real_name, name_len);

	/* Release the inode reference */
	ext4_fs_put_inode_ref(&e_inode_ref);

	return ret;

#else
	/* Extended attributes not enabled in this build */
	return -ENOTSUP;
#endif
}

/**
 * ext4_sync_inode - Synchronize an inode to disk
 * @inode_ref: The inode reference containing filesystem, inode, and tracking info
 *
 * This function marks the inode's block as dirty in the block cache and ensures
 * it gets flushed to disk. It uses the ext4_inode_ref which already contains
 * all necessary context information about the inode location.
 *
 * Returns: 0 on success, negative error code on failure
 */
int32 ext4_sync_inode(struct ext4_inode_ref* inode_ref) {
	int32 ret;

	if (!inode_ref || !inode_ref->fs || !inode_ref->inode) return -EINVAL;

	/* Mark the inode as dirty in the block cache */
	if (inode_ref->block.buf) {
		/* Set dirty flag directly on the buffer */
		ext4_bcache_set_dirty(inode_ref->block.buf);

		/* Add to dirty list if not already there */
		if (!inode_ref->block.buf->on_dirty_list) { ext4_bcache_insert_dirty_node(inode_ref->fs->bdev->bc, inode_ref->block.buf); }
	}

	/* Set the dirty flag in the inode ref */
	inode_ref->dirty = true;

	/*
	 * In lwext4, putting an inode ref will handle writing it back if dirty.
	 * The ext4_fs_put_inode_ref function handles flushing dirty inodes to disk.
	 */
	ret = ext4_fs_put_inode_ref(inode_ref);

	return ret;
}

/**
 * __ext4_flush_dirty_inode - Flush a dirty inode to disk
 * @inode_ref: The inode reference to flush
 *
 * This is an alternative implementation that explicitly flushes the buffer
 * without releasing the inode reference.
 *
 * Returns: 0 on success, negative error code on failure
 */
int32 __ext4_flush_dirty_inode(struct ext4_inode_ref* inode_ref) {
	int32 ret = 0;

	if (!inode_ref || !inode_ref->fs || !inode_ref->inode) return -EINVAL;

	/* If the inode is dirty, flush its block to disk */
	if (inode_ref->dirty && inode_ref->block.buf) {
		/* Flush the buffer containing the inode */
		ret = ext4_block_flush_buf(inode_ref->fs->bdev, inode_ref->block.buf);
		if (ret == 0) {
			/* Clear dirty flag if flush succeeded */
			inode_ref->dirty = false;
		}
	}

	return ret;
}