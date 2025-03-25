#ifndef _ADDRESS_SPACE_H
#define _ADDRESS_SPACE_H


#include <util/radix_tree.h>
#include <util/spinlock.h>

struct inode;

/* Memory management */
struct addrSpace {
	// struct inode *host;               /* Owning inode */
	struct radixTreeRoot page_tree;             /* Page cache radix tree */
	spinlock_t tree_lock;                         /* Lock for tree manipulation */
	unsigned long nrpages;                        /* Number of total pages */
	const struct addrSpace_ops* a_ops; /* s_operations */
};

struct addrSpace* addrSpace_create(struct inode* inode);

struct page* addrSpace_getPage(struct addrSpace* mapping, unsigned long index);
struct page* addrSpace_acquirePage(struct addrSpace* mapping, unsigned long index, unsigned int gfp_mask);
int addrSpace_addPage(struct addrSpace* mapping, struct page* page, unsigned long index);
int addrSpace_putPage(struct addrSpace *mapping, struct page *page);
int addrSpace_setPageDirty(struct addrSpace *mapping, struct page *page);
unsigned int addrSpace_getDirtyPages(struct addrSpace* mapping, struct page** pages, unsigned int nr_pages, unsigned long start);
int addrSpace_removeDirtyTag(struct addrSpace *mapping, struct page *page);
int addrSpace_writeBack(struct addrSpace *mapping);
int addrSpace_writeback_range(struct addrSpace *mapping, loff_t start, loff_t end, int sync_mode);
int addrSpace_invalidate(struct addrSpace *mapping, struct page *page);

struct page* addrSpace_readPage(struct addrSpace* mapping, unsigned long index);

/*
 * Address space s_operations (page cache)
 */
struct addrSpace_ops {
	int (*readpage)(struct file*, struct page*);
	int (*writepage)(struct page*, struct writeback_control*);
	int (*readpages)(struct file*, struct addrSpace*, struct list_head*, unsigned);
	int (*writepages)(struct addrSpace*, struct writeback_control*);
	void (*invalidatepage)(struct page*, unsigned int);
	int (*releasepage)(struct page*, int);
	int (*direct_IO)(int, struct kiocb*, const struct io_vector*, loff_t, unsigned long);
};

#endif