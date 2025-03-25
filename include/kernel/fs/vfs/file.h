#ifndef _FILE_H
#define _FILE_H

#include <kernel/fs/vfs/vfs.h>
#include <kernel/types.h>
#include <util/atomic.h>
#include <util/spinlock.h>


/**
 * Represents an open file in the system
 */
struct file {
	spinlock_t f_lock;
	// 一般来说，file只需要一个成员锁，使用inode的锁保护写入操作
	atomic_t f_refcount;

	/* File identity */
	struct path f_path;    /* Path to file */
	struct inode* f_inode; /* Inode of the file */

	/* File state */
	fmode_t f_mode;       /* File access mode */
	loff_t f_pos;         /* Current file position */
	unsigned int f_flags; /* Kernel internal flags */

	/* Memory management */
	//struct addrSpace* f_mapping; /* Page cache mapping */
	// 直接在inode里边

	/* Private data */
	void* f_private; /* Filesystem/driver private data */

	/* Data read-ahead */
	struct file_ra_state {
		unsigned long start;      /* Current window start */
		unsigned long size;       /* Size of read-ahead window */
		unsigned long async_size; /* Async read-ahead size */
		unsigned int ra_pages;    /* Maximum pages to read ahead */
		unsigned int mmap_miss;   /* Cache miss stat for mmap */
		unsigned int prev_pos;
	} f_read_ahead;               /* Read-ahead state */

	const struct file_operations* f_operations; /* File s_operations */
};

#define f_mapping f_inode->i_mapping
#define f_dentry f_path.dentry

/*
 * File API functions
 */
/*打开或创建文件*/
struct file* file_open(const char* filename, int flags, fmode_t mode);
struct file* file_openPath(const struct path* path, int flags, fmode_t mode);

struct file* file_get(struct file* file);
void file_put(struct file* file);

/*位置与访问管理*/
int file_denyWrite(struct file* file);
int file_allowWrite(struct file* file);
inline bool file_readable(struct file* file);
inline bool file_writable(struct file* file);

/*状态管理与通知*/
int file_setAccessed(struct file *file);
int file_setModified(struct file *file);

/*标准vfs接口*/
ssize_t file_read(struct file*, char*, size_t, loff_t*);
ssize_t file_write(struct file*, const char*, size_t, loff_t*);
loff_t file_llseek(struct file*, loff_t, int);	
	// pos的变化与查询统一接口,setpos和getpos都支持
int file_sync(struct file*, int);
/* Vectored I/O functions */
ssize_t file_readv(struct file *file, const struct io_vector *vec, 
	unsigned long vlen, loff_t *pos);
	
ssize_t file_writev(struct file *file, const struct io_vector *vec, 
	 unsigned long vlen, loff_t *pos);


/**
 * File operation structure - provides methods for file manipulation
 */
struct file_operations {
	/* Position manipulation */
	loff_t (*llseek)(struct file*, loff_t, int);

	/* Basic I/O */
	ssize_t (*read)(struct file*, char*, size_t, loff_t*);
	ssize_t (*write)(struct file*, const char*, size_t, loff_t*);

	/* Vectored I/O */
	ssize_t (*read_iter)(struct kiocb*, struct io_vector_iterator*);
	ssize_t (*write_iter)(struct kiocb*, struct io_vector_iterator*);

	/* Directory s_operations */
	int (*iterate)(struct file*, struct dir_context*);
	int (*iterate_shared)(struct file*, struct dir_context*);

	/* Polling/selection */
	__poll_t (*poll)(struct file*, struct poll_table_struct*);

	/* Management s_operations */
	int (*open)(struct inode*, struct file*);
	int (*flush)(struct file*);
	int (*release)(struct inode*, struct file*);
	int (*fsync)(struct file*, loff_t, loff_t, int datasync);

	/* Memory mapping */
	int (*mmap)(struct file*, struct vm_area_struct*);

	/* Special s_operations */
	long (*unlocked_ioctl)(struct file*, unsigned int, unsigned long);
	int (*fasync)(int, struct file*, int);

	/* Splice s_operations */
	ssize_t (*splice_read)(struct file*, loff_t*, struct pipe_inode_info*, size_t, unsigned int);
	ssize_t (*splice_write)(struct pipe_inode_info*, struct file*, loff_t*, size_t, unsigned int);

	/* Space allocation */
	long (*fallocate)(struct file*, int, loff_t, loff_t);
};


/**
 * Open flags - used when opening files
 */
#define O_ACCMODE 00000003         /* Access mode mask */
#define O_RDONLY 00000000          /* Open read-only */
#define O_WRONLY 00000001          /* Open write-only */
#define O_RDWR 00000002            /* Open read-write */
#define O_CREAT 00000100           /* Create if nonexistent */
#define O_EXCL 00000200            /* Error if already exists */
#define O_NOCTTY 00000400          /* Don't assign controlling terminal */
#define O_TRUNC 00001000           /* Truncate to zero length */
#define O_APPEND 00002000          /* Append to file */
#define O_NONBLOCK 00004000        /* Non-blocking I/O */
#define O_DSYNC 00010000           /* Synchronize data */
#define O_SYNC (O_DSYNC | O_RSYNC) /* Synchronize data and metadata */
#define O_RSYNC 00040000           /* Synchronize read s_operations */
#define O_DIRECT 00100000          /* Direct I/O */
#define O_DIRECTORY 00200000       /* Must be a directory */
#define O_NOFOLLOW 00400000        /* Don't follow symbolic links */
#define O_CLOEXEC 02000000         /* Close on exec */
#define O_PATH 010000000           /* Path-only access */


/* 文件预读(readahead)相关常量 */
#define READ_AHEAD_DEFAULT      16      /* 默认预读窗口大小(页) */
#define READ_AHEAD_MAX          128     /* 最大预读页数 */
#define READ_AHEAD_MIN          4       /* 最小预读窗口大小 */
#define READ_AHEAD_ASYNC_RATIO  2       /* 异步预读与同步预读的比例 */

/* 特殊文件类型预读参数 */
#define READ_AHEAD_PIPE         16      /* 管道预读大小 */
#define READ_AHEAD_SOCKET       8       /* 套接字预读大小 */
#define READ_AHEAD_TTY          4       /* 终端预读大小 */



#endif /* _FILE_H */