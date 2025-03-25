#ifndef _BLOCK_DEVICE_H
#define _BLOCK_DEVICE_H

#include <kernel/types.h>
#include <util/list.h>
#include <util/spinlock.h>
#include <kernel/fs/vfs/inode.h>

/* Block device structure */
struct block_device {
    dev_t                   bd_dev;        /* Device identifier */
    struct inode           *bd_inode;      /* Inode of this device */
    struct superblock     *bd_super;      /* Superblock mounted on this device */
    
    unsigned int            bd_block_size; /* Block size in bytes */
    unsigned long           bd_nr_blocks;  /* Number of blocks in device */
    
    struct block_operations *bd_ops;       /* Block device s_operations */
    
    void                   *bd_private;    /* Private data for driver */
    spinlock_t              bd_lock;       /* Device lock */
};

/* Block device s_operations */
struct block_operations {
    /* Basic block I/O */
    int (*read_block)(struct block_device *bdev, sector_t sector, 
                      void *buffer, size_t count);
    int (*write_block)(struct block_device *bdev, sector_t sector, 
                       const void *buffer, size_t count);
    
    /* Cleanup */
    void (*release)(struct block_device *bdev);
};

/* Buffer head - tracks a single block in the buffer cache */
struct buffer_head {
    struct block_device *b_bdev;         /* Block device this buffer is from */
    sector_t            b_blocknr;       /* Block number */
    size_t              b_size;          /* Block size */
    unsigned long       b_state;         /* State flags */
    
    char               *b_data;          /* Pointer to data block */
    
    struct list_head    b_lru;           /* LRU list entry */
};

/* Buffer head state flags */
#define BH_Uptodate    0  /* Buffer contains valid data */
#define BH_Dirty       1  /* Buffer is dirty */
#define BH_Lock        2  /* Buffer is locked */
#define BH_Mapped      3  /* Buffer is mapped to disk */

/* Block device registration */
int register_blkdev(unsigned int major, const char *name, 
                   struct block_operations *ops);
int unregister_blkdev(unsigned int major, const char *name);

/* Buffer s_operations */
struct buffer_head *getblk(struct block_device *bdev, sector_t block, size_t size);
struct buffer_head *bread(struct block_device *bdev, sector_t block, size_t size);
void brelse(struct buffer_head *bh);
void mark_buffer_dirty(struct buffer_head *bh);
int sync_dirty_buffers(struct block_device *bdev);

/* Block layer initialization */
void block_dev_init(void);

#endif /* _BLOCK_DEVICE_H */