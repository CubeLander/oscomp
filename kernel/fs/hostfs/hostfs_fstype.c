#include <kernel/fs/hostfs/hostfs.h>
#include <kernel/mm/kmalloc.h>

#include <spike_interface/spike_file.h>
#include <spike_interface/spike_utils.h>
#include <util/string.h>
#include <kernel/types.h>
#include <kernel/fs/vfs/vfs.h>



struct fsType hostfs_fsType = {
	.fs_name = "hostfs",
	.fs_flags = 0,
	.fs_fill_sb = hostfs_fill_super,
	.fs_mount_sb = hostfs_mount,
	.fs_kill_sb = hostfs_kill_super,
	.fs_init = hostfs_init,
	.fs_exit = hostfs_exit,
};

int hostfs_fill_super(struct superblock* sb, void* data, int silent) {
    // Set filesystem-specific information
    sb->s_magic = HOSTFS_MAGIC;
    sb->s_blocksize = PAGE_SIZE;
    sb->s_blocksize_bits = PAGE_SHIFT;
    sb->s_operations = &hostfs_super_operations;
    
    // Create root inode and dentry
    struct inode *root_inode = hostfs_alloc_vinode(sb);
    if (!root_inode)
        return -ENOMEM;
    
    root_inode->i_mode = S_IFDIR | 0755;
    root_inode->i_private = (void*)(-1); // Indicate it's a directory
    
    struct dentry *root_dentry = kmalloc(sizeof(struct dentry));
    if (!root_dentry) {
        inode_put(root_inode);
        return -ENOMEM;
    }
    
    memset(root_dentry, 0, sizeof(struct dentry));
    root_dentry->d_name = qstr_create("/");
    root_dentry->d_inode = root_inode;
    root_dentry->d_superblock = sb;
    
    sb->s_global_root_dentry = root_dentry;
    
    return 0;
}

struct superblock* hostfs_mount(struct fsType* type, int flags, 
                              const char* mount_path, void* data) {
    struct superblock* sb = kmalloc(sizeof(struct superblock));
    if (!sb)
        return NULL;
    
    memset(sb, 0, sizeof(struct superblock));
    
    // Initialize superblock
    spinlock_init(&sb->s_lock);
    INIT_LIST_HEAD(&sb->s_list_all_inodes);
    spinlock_init(&sb->s_list_all_inodes_lock);
    INIT_LIST_HEAD(&sb->s_list_clean_inodes);
    INIT_LIST_HEAD(&sb->s_list_dirty_inodes);
    INIT_LIST_HEAD(&sb->s_list_io_inodes);
    spinlock_init(&sb->s_list_inode_states_lock);
    INIT_LIST_HEAD(&sb->s_list_mounts);
    spinlock_init(&sb->s_list_mounts_lock);
    
    sb->s_fsType = type;
    
    // Add to type's superblock list
    list_add(&sb->s_node_fsType, &type->fs_list_sb);
    
    // Fill superblock with filesystem-specific data
    int error = hostfs_fill_super(sb, data, flags & MNT_SILENT);
    if (error) {
        kfree(sb);
        return NULL;
    }
    
    return sb;
}

void hostfs_kill_super(struct superblock* sb) {
    // Sync the superblock if needed
    if (sb->s_operations && sb->s_operations->sync_fs)
        sb->s_operations->sync_fs(sb, 1);
    
    // Remove from filesystem type's list
    list_del(&sb->s_node_fsType);
    
    // Free superblock
    kfree(sb);
}


int hostfs_init(void) {
    // Initialize any global hostfs state
    return 0;
}

void hostfs_exit(void) {
    // Clean up any global hostfs state
}