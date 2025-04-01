/**
 * @file buffer_head.h
 * @brief Buffer head handling for block I/O operations
 *
 * This file contains definitions for buffer_head structure and related
 * functions, which are used for managing buffers that represent disk blocks
 * in memory. Inspired by Linux kernel's buffer_head implementation.
 */

 #ifndef _FS_BUFFER_HEAD_H
 #define _FS_BUFFER_HEAD_H
 
 #include <sys/types.h>
 #include <stddef.h>
 #include <stdbool.h>
 #include <stdint.h>
 #include <kernel/util/atomic.h>
 #include <kernel/util/spinlock.h>
 #include <kernel/util/list.h>
 
 /* Forward declarations */
 struct page;
 struct block_device;
 struct inode;
 struct buffer_head;
 /**
  * Buffer state bit flags
  */
 enum bh_state_bits {
	 BH_Uptodate,    /* Buffer contains valid data */
	 BH_Dirty,       /* Buffer is dirty */
	 BH_Lock,        /* Buffer is locked */
	 BH_Req,         /* Buffer has been submitted for I/O */
	 BH_Mapped,      /* Buffer is mapped to disk */
	 BH_New,         /* Buffer is new and not yet written out */
	 BH_Async_Read,  /* Buffer is under async read */
	 BH_Async_Write, /* Buffer is under async write */
	 BH_Delay,       /* Buffer is not yet allocated on disk */
	 BH_Boundary,    /* Block is followed by a discontiguity */
	 BH_Write_EIO,   /* I/O error on write */
	 BH_Ordered,     /* Ordered write */
	 BH_Eopnotsupp,  /* Operation not supported */
	 BH_Unwritten,   /* Buffer is allocated on disk but not written */
	 BH_Quiet,       /* Buffer error should be silent */
	 BH_State_Bits   /* Must be last */
 };
 
 /* Convenience macros for buffer state operations */
 #define BH_OFFSET(nr) (1UL << (nr))
 #define BH_STATE(bit, bh) (((bh)->b_state & BH_OFFSET(bit)) != 0)
 
 /* Buffer state test/set/clear functions */
 #define buffer_uptodate(bh) BH_STATE(BH_Uptodate, bh)
 #define buffer_dirty(bh)    BH_STATE(BH_Dirty, bh)
 #define buffer_locked(bh)   BH_STATE(BH_Lock, bh)
 #define buffer_mapped(bh)   BH_STATE(BH_Mapped, bh)
 #define buffer_new(bh)      BH_STATE(BH_New, bh)
 #define buffer_delay(bh)    BH_STATE(BH_Delay, bh)
 
  /* Common buffer head end I/O function signature */
  typedef void (*bh_end_io_t)(struct buffer_head *bh, int32 uptodate);
 
 /**
  * struct buffer_head - Buffer for block I/O operations
  *
  * The buffer_head structure is used to represent a disk block in memory.
  * It maps between logical blocks in a file and physical blocks on the disk.
  */
 struct buffer_head {
	 /* First cache line */
	 uint64 b_state;        /* Buffer state flags */
	 struct buffer_head *b_this_page; /* Circular list of page's buffers */
	 struct page *b_page;          /* The page this buffer is mapped to */
	 sector_t b_blocknr;           /* Block number (relative to b_bdev) */
	 size_t b_size;                /* Buffer size in bytes */
	 char *b_data;                 /* Pointer to data within the page */
	 struct block_device *b_bdev;  /* Device this buffer is mapped to */
	 
	 /* Second cache line */
	 atomic_t b_count;             /* Reference count */
	 spinlock_t b_uptodate_lock;   /* Lock for b_uptodate field */
	 bh_end_io_t b_end_io;         /* I/O completion function */
	 void *b_private;              /* Private data for b_end_io */
	 struct list_head b_assoc_buffers; /* Associated mappings */
	 struct list_head b_lru;       /* List for LRU management */
 };


 
 /**
  * Initialize a buffer_head structure
  *
  * @param bh The buffer_head to initialize
  */
 void init_buffer_head(struct buffer_head *bh);
 
 /**
  * Allocate a new buffer_head
  *
  * @return Pointer to newly allocated buffer_head or NULL on failure
  */
 struct buffer_head *alloc_buffer_head(void);
 
 /**
  * Free a buffer_head structure
  *
  * @param bh The buffer_head to free
  */
 void free_buffer_head(struct buffer_head *bh);
 
 /**
  * Get a reference to a buffer_head
  *
  * @param bh The buffer_head to get a reference to
  * @return The same buffer_head with its reference count incremented
  */
 struct buffer_head *get_buffer_head(struct buffer_head *bh);
 
 /**
  * Release a reference to a buffer_head
  *
  * @param bh The buffer_head to release
  */
 void put_buffer_head(struct buffer_head *bh);
 
 /**
  * Mark a buffer_head as dirty
  *
  * @param bh The buffer_head to mark dirty
  */
 void mark_buffer_dirty(struct buffer_head *bh);
 
 /**
  * Mark a buffer_head as clean
  *
  * @param bh The buffer_head to mark clean
  */
 void mark_buffer_clean(struct buffer_head *bh);
 
 /**
  * Lock a buffer_head for I/O
  *
  * @param bh The buffer_head to lock
  */
 void lock_buffer(struct buffer_head *bh);
 
 /**
  * Unlock a buffer_head
  *
  * @param bh The buffer_head to unlock
  */
 void unlock_buffer(struct buffer_head *bh);
 
 /**
  * Wait for a buffer_head to complete I/O
  *
  * @param bh The buffer_head to wait for
  */
 void wait_on_buffer(struct buffer_head *bh);
 
 /**
  * Read a buffer synchronously
  *
  * @param bh The buffer_head to read
  * @return 0 on success, negative error code on failure
  */
 int32 sync_read_buffer(struct buffer_head *bh);
 
 /**
  * Write a buffer synchronously
  *
  * @param bh The buffer_head to write
  * @return 0 on success, negative error code on failure
  */
 int32 sync_write_buffer(struct buffer_head *bh);
 
 /**
  * Submit a buffer for asynchronous I/O
  *
  * @param op I/O operation (read or write)
  * @param bh The buffer_head to submit
  * @param end_io Completion function
  * @return 0 on success, negative error code on failure
  */
 int32 submit_bh(int32 op, struct buffer_head *bh, bh_end_io_t end_io);
 
 /**
  * Create and map a buffer for a given device block
  *
  * @param dev Block device
  * @param block Block number
  * @param size Block size
  * @return Mapped buffer_head or NULL on failure
  */
 struct buffer_head *map_bh(struct block_device *dev, sector_t block, size_t size);
 
 /**
  * Read block from a file
  *
  * @param inode Target inode
  * @param block Logical block number
  * @param bh Buffer head to fill
  * @param create Create block if it doesn't exist
  * @return 0 on success, negative error code on failure
  */
 int32 get_block(struct inode *inode, sector_t block, struct buffer_head *bh, int32 create);
 
 #endif /* _FS_BUFFER_HEAD_H */