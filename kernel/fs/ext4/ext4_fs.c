#include <kernel/fs/ext4_adaptor.h>
#include <kernel/vfs/superblock.h>
#include <kernel/vfs.h>
#include <kernel/mm/kmalloc.h>

#include <kernel/types.h>

/**
 * ext4_fs_sync - Synchronize filesystem data to disk
 * @fs: The ext4 filesystem to sync
 *
 * Flushes all dirty buffers in the block device cache to disk.
 *
 * Returns: 0 on success, negative error code on failure
 */
int32 ext4_fs_sync(struct ext4_fs *fs)
{
    if (!fs || !fs->bdev)
        return -EINVAL;
    
    /* Flush all dirty buffers to disk */
    return ext4_block_cache_flush(fs->bdev);
}


// /**
//  * ext4_fs_flush_journal - Flush journal to disk
//  * @fs: The ext4 filesystem
//  *
//  * Commits any pending transactions in the journal and
//  * ensures they are written to disk.
//  *
//  * Returns: 0 on success, negative error code on failure
//  */
// int32 ext4_fs_flush_journal(struct ext4_fs *fs)
// {
//     if (!fs || !fs->bdev || !fs->bdev->journal)
//         return -EINVAL;
    
//     /* Commit any pending journal transactions */
//     int32 ret = ext4_journal_flush(fs->bdev->journal);
//     if (ret != 0)
//         return ret;
    
//     /* Flush all buffers to ensure journal is written to disk */
//     return ext4_block_cache_flush(fs->bdev);
// }