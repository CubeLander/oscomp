#include <kernel/fs/ext4_adaptor.h>
#include <kernel/vfs/superblock.h>
#include <kernel/vfs.h>
#include <kernel/mm/kmalloc.h>

#include <kernel/types.h>
#include <kernel/util/string.h>

/* Forward declarations */
static int32 ext4_read_inode(struct inode* inode);
static int32 ext4_write_inode(struct inode* inode, int32 wait);
static void ext4_put_super(struct superblock* sb);
static int32 ext4_statfs(struct superblock* sb, struct statfs* stat);
static int32 ext4_sync_fs(struct superblock* sb, int32 wait);

/* Define the superblock operations structure for ext4 */
const struct superblock_operations ext4_superblock_operations = {
    .read_inode = ext4_read_inode,
    .write_inode = ext4_write_inode,
    .put_super = ext4_put_super,
    .statfs = ext4_statfs,
    .sync_fs = ext4_sync_fs,
};

/**
 * ext4_read_inode - Read an inode from the filesystem
 * @inode: The inode to read
 *
 * Returns: 0 on success, negative error code on failure
 */
static int32 ext4_read_inode(struct inode* inode) {
	/* This is implemented in ext4_inode_operations.c */
	extern int32 ext4_inode_init(struct superblock * sb, struct inode * inode, uint32_t ino);
	return ext4_inode_init(inode->i_superblock, inode, inode->i_ino);
}

/**
 * ext4_write_inode - Write an inode to the filesystem
 * @inode: The inode to write
 * @wait: Whether to wait for I/O to complete
 *
 * Returns: 0 on success, negative error code on failure
 */
static int32 ext4_write_inode(struct inode* inode, int32 wait) {
	struct ext4_inode_ref inode_ref;
	struct ext4_fs* e_fs = inode->i_superblock->s_fs_info;
	struct ext4_sblock* e_sb = &e_fs->sb;
	int32 ret;

	if (!inode || !e_fs) return -EINVAL;

	/* Get the ext4 inode reference */
	ret = ext4_fs_get_inode_ref(e_fs, inode->i_ino, &inode_ref);
	if (ret != 0) return ret;

	/* Update the ext4 inode from VFS inode */
	ext4_inode_set_mode(e_sb, inode_ref.inode, inode->i_mode);
	ext4_inode_set_uid(inode_ref.inode, inode->i_uid);
	ext4_inode_set_gid(inode_ref.inode, inode->i_gid);
	ext4_inode_set_size(inode_ref.inode, inode->i_size);

	ext4_inode_set_access_time(inode_ref.inode, timespec64_to_ext4_timestamp(&inode->i_atime));
	ext4_inode_set_modif_time(inode_ref.inode, timespec64_to_ext4_timestamp(&inode->i_mtime));
	ext4_inode_set_change_inode_time(inode_ref.inode, timespec64_to_ext4_timestamp(&inode->i_ctime));
	ext4_inode_set_links_cnt(inode_ref.inode, inode->i_nlink);

	/* Sync the inode if requested */
	if (wait) { ret = ext4_sync_inode(&inode_ref); }

	/* Release the ext4 inode reference */
	ext4_fs_put_inode_ref(&inode_ref);

	return ret;
}

/**
 * ext4_put_super - Release a superblock
 * @sb: The superblock to release
 */
static void ext4_put_super(struct superblock* sb) {
	struct ext4_fs* fs = sb->s_fs_info;

	if (!fs) return;

	/* Sync the filesystem */
	ext4_fs_sync(fs);

	/* Unmount the filesystem */
	ext4_fs_fini(fs);

	/* Free the ext4_fs structure */
	kfree(fs);
	sb->s_fs_info = NULL;
}

/**
 * ext4_statfs - Get filesystem statistics
 * @sb: The superblock
 * @stat: Structure to fill with filesystem statistics
 *
 * Returns: 0 on success, negative error code on failure
 * 
 * 取自lwext4库中 ext4_mount_point_stats
 * 
 */
static int32 ext4_statfs(struct superblock* sb, struct statfs* stats) {
	CHECK_PTR_VALID(sb, -EINVAL);
	CHECK_PTR_VALID(stats, -EINVAL);

	struct ext4_fs* e_fs = sb->s_fs_info;
	CHECK_PTR_VALID(e_fs, -EINVAL);

	struct ext4_sblock* e_sb = &e_fs->sb;
	CHECK_PTR_VALID(e_sb, -EINVAL);

	ext4_lock();
	stats->f_type = ext4_get16(e_sb, magic);
	stats->f_bsize = ext4_sb_get_block_size(e_sb);
	stats->f_blocks = ext4_sb_get_blocks_cnt(e_sb);
	stats->f_bfree = ext4_sb_get_free_blocks_cnt(e_sb);
	stats->f_bavail = ext4_sb_get_free_blocks_cnt(e_sb);
	stats->f_files = ext4_get32(e_sb, inodes_count);
	stats->f_ffree = ext4_get32(e_sb, free_inodes_count);
	make_fsid_from_uuid(e_sb->uuid, &stats->f_fsid);
	stats->f_namelen = EXT4_DIRECTORY_FILENAME_LEN;
	stats->f_frsize = ext4_sb_get_block_size(e_sb);
	stats->f_flags = ST_NOSUID | ST_NODEV | ST_NOEXEC;
	ext4_unlock();

	return 0;
}

/**
 * ext4_sync_fs - Sync the filesystem
 * @sb: The superblock
 * @wait: Whether to wait for I/O to complete
 *
 * Returns: 0 on success, negative error code on failure
 */
static int32 ext4_sync_fs(struct superblock* sb, int32 wait) {
	CHECK_PTR_VALID(sb, -EINVAL);
	struct ext4_fs* fs = sb->s_fs_info;

	if (!fs) return -EINVAL;

	/* Sync the filesystem */
	return ext4_fs_sync(fs);
}

int32 ext4_fill_super(struct superblock* sb, void* data, int32 silent) {
    /* Allocate and initialize the ext4_fs structure */
    struct ext4_fs* e_fs = kmalloc(sizeof(struct ext4_fs));
    if (!e_fs) return -ENOMEM;
    memset(e_fs, 0, sizeof(struct ext4_fs));

    /* Get the kernel block device and create the adapter */
    struct block_device *kernel_bdev = sb->s_bdev;
    if (!kernel_bdev) {
        kfree(e_fs);
        return -EINVAL;
    }

    /* Create the ext4_blockdev adapter */
    struct ext4_blockdev *e_blockdevice = ext4_blockdev_create_adapter(kernel_bdev);
    if (!e_blockdevice) {
        kfree(e_fs);
        return -ENOMEM;
    }
    
    /* Store the adapter in the filesystem */
    e_fs->bdev = e_blockdevice;
    e_blockdevice->fs = e_fs;

    /* Initialize the filesystem */
    int32 ret = ext4_fs_init(e_fs, e_blockdevice, sb->s_flags & MS_RDONLY);
    if (ret != 0) {
        ext4_blockdev_free_adapter(e_blockdevice);
        kfree(e_fs);
        return ret;
    }

    /* Set up the superblock */
    sb->s_blocksize = ext4_sb_get_block_size(&e_fs->sb);
    sb->s_blocksize_bits = __builtin_ffs(sb->s_blocksize) - 1;
    sb->s_magic = EXT4_SUPERBLOCK_MAGIC;
    sb->s_operations = &ext4_superblock_operations;
    sb->s_fs_info = e_fs;
    sb->s_max_links = EXT4_LINK_MAX;

    return 0;
}

/**
 * ext4_mount - Mount an ext4 filesystem
 * @fs_type: The filesystem type
 * @flags: Mount flags
 * @dev_name: Device name
 * @data: Mount options
 *
 * Returns: The mounted superblock or an error pointer
 */
// struct superblock* ext4_mount_adapter(struct fstype* fs_type, int32 flags, const char* dev_name, void* data) {
// 	struct superblock* sb;
// 	int32 ret;

// 	/* Create a new superblock */
// 	sb = fstype_mount(fs_type, NULL, ext4_fill_super, flags, data);
// 	if (PTR_IS_ERROR(sb)) return sb;

// 	/* Set the device name */
// 	if (dev_name && *dev_name) sb->s_dev_name = kstrdup(dev_name, 0);

// 	return sb;
// }

/* Filesystem type registration structure */
static struct fstype ext4_fs_type = {
    .fs_name = "ext4",
    .fs_flags = FS_REQUIRES_DEV,
    //.fs_mount = ext4_mount_adapter,
    //.fs_kill_sb = kill_block_super,
};