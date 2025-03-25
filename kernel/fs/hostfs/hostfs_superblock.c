#include <kernel/fs/hostfs/hostfs.h>
#include <kernel/mm/kmalloc.h>
#include <spike_interface/spike_file.h>
#include <spike_interface/spike_utils.h>
#include <util/string.h>
#include <kernel/types.h>
#include <kernel/fs/vfs/vfs.h>

const struct super_operations hostfs_super_operations = {
    .alloc_inode = hostfs_alloc_inode,
    .destroy_inode = hostfs_destroy_inode,
    .dirty_inode = hostfs_dirty_inode,
    .write_inode = hostfs_write_inode,
    .read_inode = hostfs_read_inode,
    .evict_inode = hostfs_evict_inode,
    .drop_inode = hostfs_drop_inode,
    .delete_inode = hostfs_delete_inode,
    .sync_fs = hostfs_sync_fs,
    .freeze_fs = hostfs_freeze_fs,
    .unfreeze_fs = hostfs_unfreeze_fs,
    .statfs = hostfs_statfs,
    .remount_fs = hostfs_remount_fs,
    .umount_begin = hostfs_umount_begin,
    .put_super = hostfs_put_super,
    .sync_super = hostfs_sync_super,
    .__clear_inode = hostfs___clear_inode,
    .show_options = hostfs_show_options,
};

struct inode* hostfs_alloc_inode(struct superblock* sb) {
    struct inode* inode = kmalloc(sizeof(struct inode));
    if (unlikely(!inode))
        return ERR_PTR(-ENOMEM);
    
    memset(inode, 0, sizeof(struct inode));
    //inode->i_superblock = sb;
    
    // Initialize hostfs-specific data
    inode->i_private = NULL; // Will store spike_file_t*

    // NOTE: Don't set i_op here - it will be set later based on file type
    // NOTE: Don't initialize generic fields - inode_create will do that
    
    return inode;
}

void hostfs_destroy_inode(struct inode* inode) {
    // Clean up any host file handle
    if (inode->i_private) {
        spike_file_t* f = (spike_file_t*)inode->i_private;
        if ((int64)f > 0) { // Valid file descriptor, not directory marker
            // Log that file should have been closed already
            sprint("Warning: destroying inode with open file handle\n");
            // Close it to prevent leaks
            //spike_file_close(f);
        }
        inode->i_private = NULL;
    }
    kfree(inode);
}

void hostfs_dirty_inode(struct inode* inode) {
    // For hostfs, inodes don't need to be marked dirty
    // Host OS handles actual file persistence
    // This is a no-op
}



int hostfs_write_inode(struct inode* inode, int wait) {
    // Host filesystem handles actual file persistence
    // No need to write inode metadata separately
    return 0;
}

int hostfs_read_inode(struct inode* inode) {
    // This would update inode metadata from the host
    // But typically this is done in lookup/create
    if (!inode->i_private) {
        // Nothing to read from
        return -EINVAL;
    }
    
    // Update inode metadata based on host file
    return hostfs_update_inode(inode);
}


void hostfs_evict_inode(struct inode* inode) {
    // Clear any associated data
    if (inode->i_private) {
        // Close the file only if it wasn't closed by hook_close
        spike_file_t* f = (spike_file_t*)inode->i_private;
        if ((int64)f > 0) // Valid file descriptor
            spike_file_close(f);
        inode->i_private = NULL;
    }
    
    // Clear inode
    inode->i_size = 0;
}

void hostfs_drop_inode(struct inode* inode) {
    // Default behavior - can be customized to handle special cases
    // For hostfs, standard behavior (invalidating inode) is fine
    
    // Mark the inode as invalid
    inode->i_state |= I_FREEING;
    
    // Remove from dentry cache if no links
    if (!inode->i_nlink)
        hostfs_delete_inode(inode);
}
/**
 * Called for inodes that are no longer linked in the filesystem (zero link count)
 * Responsible for removing the file's data from disk (in traditional filesystems)
 * Should handle the actual deletion of the file's contents
 * Called earlier in the inode removal process
 * 
 */
void hostfs_delete_inode(struct inode* inode) {
    // Called for inodes with 0 links
    // For hostfs, we only need to close file and clear data
    
    // Close the file if open
    if (inode->i_private) {
        spike_file_t* f = (spike_file_t*)inode->i_private;
        if ((int64)f > 0) {
            spike_file_close(f);
        }
        inode->i_private = NULL;
    }
    
    // Clear inode data
    inode->i_size = 0;
    inode->i_mode = 0;
    inode->i_state |= I_CLEAR;
}



/* Superblock management */

int hostfs_sync_fs(struct superblock* sb, int wait) {
    // Host filesystem handles synchronization
    // Just return success
    return 0;
}

int hostfs_freeze_fs(struct superblock* sb) {
    // Hostfs doesn't need special freeze handling
    // Return success
    return 0;
}

int hostfs_unfreeze_fs(struct superblock* sb) {
    // Hostfs doesn't need special unfreeze handling
    // Return success
    return 0;
}

int hostfs_statfs(struct superblock* sb, struct statfs* statfs) {
    // Fill in filesystem statistics
    // For hostfs, we can approximate based on host filesystem
    
    // Set defaults
    statfs->f_type = HOSTFS_MAGIC;
    statfs->f_bsize = PAGE_SIZE;
    statfs->f_blocks = 1000000; // Arbitrary large number
    statfs->f_bfree = 900000;   // Arbitrary large number
    statfs->f_bavail = 900000;  // Arbitrary large number
    statfs->f_files = 10000;    // Arbitrary number
    statfs->f_ffree = 9000;     // Arbitrary number
    
    // Could get actual host fs stats if needed
    return 0;
}


int hostfs_remount_fs(struct superblock* sb, int* flags, char* data) {
    // Hostfs doesn't need special remount handling
    // Return success
    return 0;
}

void hostfs_umount_begin(struct superblock* sb) {
    // Optional preparation for unmounting
    // Can be no-op for hostfs
}

/* Superblock lifecycle */

void hostfs_put_super(struct superblock* sb) {
    // Clean up filesystem-specific superblock data
    if (sb->s_fs_specific) {
        kfree(sb->s_fs_specific);
        sb->s_fs_specific = NULL;
    }
}


int hostfs_sync_super(struct superblock* sb, int wait) {
    // Sync superblock to disk
    // For hostfs, host handles persistence, so this is a no-op
    return 0;
}

/* Filesystem-specific clear operations */



/**
 * Called during the final inode cleanup, just before memory deallocation
 * Used for releasing any filesystem-specific resources attached to the inode
 * Final chance to clean up before the inode is freed
 * Called later in the inode lifecycle
 */
void hostfs___clear_inode(struct inode* inode) {
    // Final cleanup of inode before freeing
    // For hostfs, ensure file handle is closed
    if (inode->i_private) {
        spike_file_t* f = (spike_file_t*)inode->i_private;
        if ((int64)f > 0) {
            spike_file_close(f);
        }
        inode->i_private = NULL;
    }
}

int hostfs_show_options(struct seq_file* seq, struct dentry* root) {
    // For debugging or /proc/mounts
    // Show hostfs-specific mount options
    if (seq) {
        // Assuming we have a custom function to write to seq_file
        seq_printf(seq, ",root=%s", H_ROOT_DIR);
    }
    return 0;
}