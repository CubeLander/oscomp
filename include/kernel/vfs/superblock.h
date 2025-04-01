#ifndef _SUPERBLOCK_H
#define _SUPERBLOCK_H
//#include <kernel/vfs/fstype.h>
#include "forward_declarations.h"
#include <kernel/vfs/stat.h>
#include <kernel/util/list.h>
#include <kernel/util/spinlock.h>

struct fstype;
struct superblock_operations;
struct dentry;
struct block_device;
struct seq_file;
struct writeback_control;

/* Superblock structure representing a mounted filesystem */
struct superblock {


	/*************file system type and mounts***************/

	/* Mount info */
	struct list_head s_list_mounts;
	spinlock_t s_list_mounts_lock;
	struct dentry* s_root;
	dev_t s_device_id;      // Device identifier, 目前简单通过对挂载路径做哈希得到
	struct block_device* s_bdev; // Block device

	/*********************** fs specified ******************************/
	struct fstype* s_fstype;
	struct list_node s_node_fstype;

	void* s_fs_info;

	uint32 s_magic;             // Magic number identifying filesystem
	uint64 s_blocksize;      // Block size in bytes
	uint64 s_blocksize_bits; // Block size bits (log2 of blocksize)
	uint32 s_max_links;
	uint64 s_file_maxbytes; // Max file size
	int32 s_nblocks;               // Number of blocks
	uint64 s_time_granularity;  /* Time granularity in nanoseconds */
	time_t s_time_min; // Earliest time the fs can represent
	time_t s_time_max; // Latest time the fs can represent
	                   // 取决于文件系统自身的属性，例如ext4的时间戳范围是1970-2106
	uint64 s_flags; // 只由fs决定
	const struct superblock_operations* s_operations; // Superblock operations

	/*********************** vfs variables ******************************/
	spinlock_t s_lock; // Lock protecting the superblock
	atomic_t s_refcount; // Reference count: mount point count + open file count
	atomic_t s_ninodes;          // Number of inodes
	atomic64_t s_next_ino; // Next inode number to allocate


	/*********************** inode fields ******************************/
	/* Master list - all inodes belong to this superblock */
	struct list_head s_list_all_inodes; // List of inodes belonging to this sb
	spinlock_t s_list_all_inodes_lock;  // Lock for s_list_all_inodes

	/* State lists - an inode is on exactly ONE of these at any time */
	struct list_head s_list_clean_inodes; // Clean, unused inodes (for reclaiming)
	struct list_head s_list_dirty_inodes; // Dirty inodes (need write-back)
	struct list_head s_list_io_inodes;    // Inodes currently under I/O
	spinlock_t s_list_inode_states_lock; // Lock for all state lists

};

struct superblock_operations {
	struct inode* (*alloc_inode)(struct superblock* sb, uint64 ino);
	/*上面是已维护的*/


	int64 (*get_next_ino)(struct superblock* sb);
    // 分配文件系统特定的超级块信息
    void* (*alloc_fs_info)(void);
    // 释放文件系统特定的超级块信息
    void (*free_fs_info)(void *fs_info);
	/* Inode lifecycle management */
	void (*destroy_inode)(struct inode* inode);
	void (*dirty_inode)(struct inode* inode);

	/* Inode I/O operations */
	int32 (*write_inode)(struct inode* inode, int32 wait);
	int32 (*read_inode)(struct inode* inode);
	void (*evict_inode)(struct inode* inode);
	void (*drop_inode)(struct inode* inode);
	void (*delete_inode)(struct inode* inode);

	/* Superblock management */
	int32 (*sync_fs)(struct superblock* sb, int32 wait);
	int32 (*freeze_fs)(struct superblock* sb);
	int32 (*unfreeze_fs)(struct superblock* sb);
	int32 (*statfs)(struct superblock* sb, struct statfs* statfs);
	int32 (*remount_fs)(struct superblock* sb, int32* flags, char* data);
	void (*umount_begin)(struct superblock* sb);

	/* Superblock lifecycle */
	void (*put_super)(struct superblock* sb);
	int32 (*sync_super)(struct superblock* sb, int32 wait);

	/* Filesystem-specific clear operations */
	void (*__inode__free)(struct inode* inode);
	int32 (*show_options)(struct seq_file* seq, struct dentry* root);

	int32 (*get_block)(struct inode* inode, sector_t iblock, struct buffer_head* bh_result, int32 create);

    // 阶段1: 初始化前检查 - 验证设备和传入的参数
    int32 (*pre_mount_check)(struct superblock *sb, struct block_device *bdev, 
		void *mount_options, int32 flags);
    // // 阶段2: 读取文件系统元数据 - 验证文件系统格式并填充superblock
    // int32 (*fill_super)(struct superblock *sb, struct block_device *bdev, 
	// 	void *mount_options, int32 silent);
	// 这两个过程是fstype中实现的虚函数
	// // 阶段3: 文件系统特定初始化 - 分配缓存、建立根目录、初始化特殊结构
	// int32 (*fs_init)(struct superblock *sb);
    // 阶段4: 挂载点创建 - 创建vfsmount结构并完成挂载
    struct vfsmount* (*create_mount)(struct superblock *sb, int32 flags, 
		const char* device_path, void *mount_options);
    /* 卸载生命周期操作 */
    
    // 阶段1: 准备卸载 - 检查是否可以安全卸载
    int32 (*pre_unmount)(struct superblock *sb);
    
    // 阶段2: 同步文件系统数据 - 确保所有修改都已写入磁盘
    //int32 (*sync_fs)(struct superblock *sb, int32 wait);
    
    // 阶段3: 文件系统特定清理 - 释放文件系统特有资源
    int32 (*cleanup)(struct superblock *sb);
    
    // 阶段4: 强制卸载处理 - 处理强制卸载场景，返回0表示成功强制卸载
    int32 (*force_unmount)(struct superblock *sb);
};

/* Function prototypes */
//struct superblock* fstype_mount(struct fstype* type, dev_t dev_id, void* fs_data);
void superblock_put(struct superblock* sb);
struct vfsmount* superblock_acquireMount(struct superblock* sb, int32 flags, const char* device_path);
struct inode* superblock_createInode(struct superblock* sb);




// ... existing code ...



#define ST_RDONLY       0x0001  // 文件系统是只读的
#define ST_NOSUID       0x0002  // 忽略 SUID 和 SGID 权限位
#define ST_NODEV        0x0004  // 禁止访问设备文件
#define ST_NOEXEC       0x0008  // 禁止执行程序
#define ST_SYNCHRONOUS  0x0010  // 所有写入操作都同步进行
#define ST_MANDLOCK     0x0040  // 支持强制锁（Mandatory locking）
#define ST_WRITE        0x0080  // 文件系统当前是可写的（不是标准 POSIX，但有些内核提供）
#define ST_APPEND       0x0100  // 支持 append-only 文件
#define ST_IMMUTABLE    0x0200  // 支持 immutable 文件
#define ST_NOATIME      0x0400  // 不更新访问时间
#define ST_NODIRATIME   0x0800  // 不更新目录访问时间
#define ST_RELATIME     0x1000  // 更新访问时间仅在 mtime 变动时

/*
 * Writeback flags
 */
#define WB_SYNC_NONE    0       /* Don't wait on completion */
#define WB_SYNC_ALL     1       /* Wait on all write completion */
/**
 * Reason for writeback
 */
enum wb_reason {
    WB_REASON_BACKGROUND,        /* Regular background writeback */
    WB_REASON_SYNC,              /* Explicit sync operation */
    WB_REASON_PERIODIC,          /* Periodic flush */
    WB_REASON_VMSCAN,            /* Memory pressure */
    WB_REASON_SHUTDOWN           /* System shutdown */
};



/**
 * Controls writeback operations for dirty pages/buffers
 */
struct writeback_control {
    int64 nr_to_write;           /* Number of pages to write */
    int64 pages_skipped;         /* Pages skipped because they weren't dirty */
    
    /* Writeback range */
    loff_t range_start;         /* Start offset for writeback */
    loff_t range_end;           /* End offset for writeback */
    
    /* Flags */
    uint32 for_kupdate:1; /* Operation for kupdate functionality */
    uint32 for_background:1; /* Background operation */
    uint32 for_reclaim:1;   /* Page reclaim writeback */
    uint32 range_cyclic:1;  /* Range is cyclic */
    uint32 sync_mode:1;     /* Sync mode (WB_SYNC_ALL or WB_SYNC_NONE) */
    
    /* In case we need other flags in the future */
    uint32 more_io:1;      /* More IO to follow */
    uint32 no_cgroup_owner:1; /* Don't cgroup this writeback */
    uint32 punt_to_cgroup:1;  /* Cgroup should do this writeback */
    
    /* For tracking which process initiated writeback */
    uid_t uid;                   /* Owner UID of writeback task */
    
    /* Reason for writeback */
    enum wb_reason reason;        /* Why writeback was triggered */
};

/* Core writeback functions */
void writeback_inodes_sb(struct superblock *, enum wb_reason);
int64 writeback_inodes_s_if_idle(struct superblock *, enum wb_reason);
void init_writeback_control(struct writeback_control *wbc, uint32 sync_mode);








#endif
