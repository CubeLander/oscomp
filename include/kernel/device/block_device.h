#ifndef _BLOCK_DEVICE_H
#define _BLOCK_DEVICE_H

#include <kernel/vfs/inode.h>
#include <kernel/types.h>
#include <kernel/util/list.h>
#include <kernel/util/spinlock.h>

/*linux-style block_device structure - minimum viable for ext4*/
struct block_device {
	dev_t bd_dev;                 // 设备号 (主设备号 + 次设备号)
	int32 bd_openers;             // 打开计数
	struct inode* bd_inode;       // 对应的 inode
	struct super_block* bd_super; // 挂载的 superblock

	struct list_head bd_list; // 所有 bdev 的全局链表
	uint32 bd_block_size;     // 块大小（bytes）
	uint64_t bd_nr_blocks;    // 块数量 - ext4需要知道设备大小

	void* bd_private;   // 驱动私有数据
	atomic_t bd_refcnt; // 引用计数
	fmode_t bd_mode;    // 打开模式（读/写）

	spinlock_t bd_lock; // 设备访问锁

	struct block_operations* bd_ops; // 块设备操作函数

	/* 以下成员可以稍后实现 */
	/*
	struct mutex            bd_mutex;       // 打开/关闭保护锁
	struct list_head        bd_inodes;      // 持有该 bdev 的所有 inode 列表
	void                   *bd_disk;        // 对应的 genhd (磁盘描述结构)
	struct block_device    *bd_contains;    // 如果是分区，指向整个磁盘
	struct hd_geometry     *bd_geometry;    // 几何信息（分区/柱面）
	struct gendisk         *bd_disk_info;   // 磁盘信息结构（gendisk）
	struct request_queue   *bd_queue;       // 请求队列（I/O 调度）
	struct kobject          bd_kobj;        // sysfs 对象
	struct backing_dev_info *bd_bdi;        // 回写支持信息（writeback）
	struct bdev_inode      *bd_inode_internal; // bdev 的 internal inode
	*/
};

/* Block device operations - 最小子集 */
struct block_operations {
	/* 必要的块I/O操作 */
	int32 (*read_blocks)(struct block_device* bdev, void* buffer, sector_t sector, size_t count);
	int32 (*write_blocks)(struct block_device* bdev, const void* buffer, sector_t sector, size_t count);

	/* 设备生命周期管理 */
	int32 (*open)(struct block_device* bdev, fmode_t mode);
	void (*release)(struct block_device* bdev);

	/* 设备控制 - ext4可能需要进行一些特定操作 */
	int32 (*ioctl)(struct block_device* bdev, unsigned cmd, uint64 arg);

	/* 以下操作可以稍后实现 */
	/*
	int32 (*flush)(struct block_device *bdev);
	int32 (*getgeo)(struct block_device *bdev, struct hd_geometry *geo);
	int32 (*direct_access)(struct block_device *bdev, sector_t sector,
	                    void **kaddr, uint64 *pfn);
	int32 (*secure_erase)(struct block_device *bdev, sector_t start,
	                  sector_t nsect);
	*/
};

/* Block device 创建和注册函数 */
struct block_device* alloc_block_device(void);
void free_block_device(struct block_device* bdev);

int32 register_blkdev(uint32 major, const char* name, struct block_operations* ops);
int32 unregister_blkdev(uint32 major, const char* name);

/* 获取块设备 */
struct block_device* blockdevice_lookup(dev_t dev);
void blockdevice_unref(struct block_device* bdev);

/* 块设备操作辅助函数 */
int32 blockdevice_open(struct block_device* bdev, fmode_t mode);
void blockdevice_close(struct block_device* bdev);

/* 缓冲区管理 - 可选但建议实现 */
int32 sync_dirty_buffers(struct block_device* bdev);

/* Block layer initialization */
void block_dev_init(void);

#endif /* _BLOCK_DEVICE_H */