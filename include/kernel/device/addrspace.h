#ifndef _ADDRESS_SPACE_H
#define _ADDRESS_SPACE_H


#include <kernel/util/radix_tree.h>
#include <kernel/util/spinlock.h>
#include "forward_declarations.h"



// 对于addrspace，不再关联inode，而是直接关联设备文件。
// 它会有非常高的并发强度
// 参考mar30:统一的缓存预读策略


// struct monkey_buffer {
//     持有一段连续的内核虚拟地址空间，直接kmalloc分配
//     但是在一些dma设备里 会要求buffer与page对齐
//     可以在分配时要求kmalloc这么做。
//     uint64_t block_id;
//     char *data;                    // 指向 page 中偏移的 buffer
//     bool uptodate, dirty;
//     uint16_t offset_in_page;
//     atomic_t refcount;
//     struct list_head hashnode;
//     ...
//		spinlock_t lock;   // ✅ 每个 buffer 自带锁
// };




struct inode;
struct writeback_control;
/* Memory management */
struct addrSpace {
	// struct inode *host;               /* Owning inode */
	struct radixTreeRoot page_tree;             /* Page cache radix tree */
	spinlock_t tree_lock;                         /* Lock for tree manipulation */
	uint64 nrpages;                        /* Number of total pages */
	const struct addrSpace_ops* a_ops; /* s_operations */
};


/*
 * Address space s_operations (page cache)
 */
struct addrSpace_ops {
	int32 (*readpage)(struct file*, struct page*);
	int32 (*writepage)(struct page*, struct writeback_control*);
	int32 (*readpages)(struct file*, struct addrSpace*, struct list_head*, unsigned);
	int32 (*writepages)(struct addrSpace*, struct writeback_control*);
	void (*invalidatepage)(struct page*, uint32);
	int32 (*releasepage)(struct page*, int32);
	int32 (*direct_IO)(int32, struct kiocb*, const struct io_vector*, loff_t, uint64);
};

struct addrSpace* addrSpace_create(struct inode* inode);

struct page* addrSpace_getPage(struct addrSpace* mapping, uint64 index);
struct page* addrSpace_acquirePage(struct addrSpace* mapping, uint64 index, uint32 gfp_mask);
int32 addrSpace_addPage(struct addrSpace* mapping, struct page* page, uint64 index);
int32 addrSpace_putPage(struct addrSpace *mapping, struct page *page);
int32 addrSpace_setPageDirty(struct addrSpace *mapping, struct page *page);
uint32 addrSpace_getDirtyPages(struct addrSpace* mapping, struct page** pages, uint32 nr_pages, uint64 start);
int32 addrSpace_removeDirtyTag(struct addrSpace *mapping, struct page *page);
int32 addrSpace_writeBack(struct addrSpace *mapping);
int32 addrSpace_writeback_range(struct addrSpace *mapping, loff_t start, loff_t end, int32 sync_mode);
int32 addrSpace_invalidate(struct addrSpace *mapping, struct page *page);

struct page* addrSpace_readPage(struct addrSpace* mapping, uint64 index);



#endif