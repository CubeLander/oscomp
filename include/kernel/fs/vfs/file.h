#ifndef _FILE_H
#define _FILE_H

#include "forward_declarations.h"
#include <kernel/fs/vfs/path.h>
#include <kernel/util/atomic.h>
#include <kernel/util/spinlock.h>
#include <kernel/vfs.h>

struct io_vector;
struct io_vector_iterator;
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
	fmode_t f_mode; /* File access mode */
	loff_t f_pos;   /* Current file position */
	uint32 f_flags; /* Kernel internal flags */
};

/**
 * Directory context for readdir operations
 */
struct dir_context {
	int32 (*actor)(struct dir_context*, const char*, int32, loff_t, uint64_t, unsigned);
	loff_t pos; /* Current position in directory */
};

/**
 * File operation structure - provides methods for file manipulation
 */
struct file_operations {
	/* Position manipulation */
	loff_t (*llseek)(struct file*, loff_t, int32);

	/* Basic I/O */
	ssize_t (*read)(struct file*, char*, size_t, loff_t*);
	ssize_t (*write)(struct file*, const char*, size_t, loff_t*);

	/* Vectored I/O */
	ssize_t (*read_iter)(struct kiocb*, struct io_vector_iterator*);
	ssize_t (*write_iter)(struct kiocb*, struct io_vector_iterator*);

	/* Directory s_operations */
	int32 (*iterate)(struct file*, struct dir_context*);
	int32 (*iterate_shared)(struct file*, struct dir_context*);

	/* Polling/selection */
	//__poll_t (*poll)(struct file*, struct poll_table_struct*);

	/* Management s_operations */
	int32 (*open)(struct inode*, struct file*);
	int32 (*flush)(struct file*);
	int32 (*release)(struct inode*, struct file*);
	int32 (*fsync)(struct file*, loff_t, loff_t, int32 datasync);

	/* Memory mapping */
	int32 (*mmap)(struct file*, struct vm_area_struct*);

	/* Special s_operations */
	int64 (*unlocked_ioctl)(struct file*, uint32, uint64);
	int32 (*fasync)(int32, struct file*, int32);

	/* Splice s_operations */
	// ssize_t (*splice_read)(struct file*, loff_t*, struct pipe_inode_info*, size_t, uint32);
	// ssize_t (*splice_write)(struct pipe_inode_info*, struct file*, loff_t*, size_t, uint32);

	/* Space allocation */
	int64 (*fallocate)(struct file*, int32, loff_t, loff_t);
};

#define f_mapping f_inode->i_mapping
#define f_dentry f_path.dentry

/*
 * File API functions
 */
/*打开或创建文件*/
struct file* file_open(const char* filename, int32 flags, fmode_t mode);
struct file* file_openPath(const struct path* path, int32 flags, fmode_t mode);
int32 file_close(struct file* file);

struct file* file_ref(struct file* file);
int32 file_unref(struct file* file);

/*位置与访问管理*/
int32 file_denyWrite(struct file* file);
int32 file_allowWrite(struct file* file);
// inline bool file_isReadable(struct file* file);
// inline bool file_isWriteable(struct file* file);

/*状态管理与通知*/
int32 file_setAccessed(struct file* file);
int32 file_setModified(struct file* file);

/*标准vfs接口*/
ssize_t file_read(struct file*, char*, size_t, loff_t*);
ssize_t file_write(struct file*, const char*, size_t, loff_t*);
loff_t file_llseek(struct file*, loff_t, int32);
// pos的变化与查询统一接口,setpos和getpos都支持
int32 file_sync(struct file*, int32);
/* Vectored I/O functions */
ssize_t file_readv(struct file* file, const struct io_vector* vec, uint64 vlen, loff_t* pos);

ssize_t file_writev(struct file* file, const struct io_vector* vec, uint64 vlen, loff_t* pos);

int32 iterate_dir(struct file*, struct dir_context*);

static inline bool file_isReadable(struct file* file) {
	if (!file || !file->f_inode || atomic_read(&file->f_refcount) <= 0) return false;
	return (file->f_mode & FMODE_READ) != 0;
}

static inline bool file_isWriteable(struct file* file) {
	if (!file || !file->f_inode || atomic_read(&file->f_refcount) <= 0) return false;
	return (file->f_mode & FMODE_WRITE) != 0;
}

/* 文件预读(readahead)相关常量 */
#define READ_AHEAD_DEFAULT 16    /* 默认预读窗口大小(页) */
#define READ_AHEAD_MAX 128       /* 最大预读页数 */
#define READ_AHEAD_MIN 4         /* 最小预读窗口大小 */
#define READ_AHEAD_ASYNC_RATIO 2 /* 异步预读与同步预读的比例 */

/* 特殊文件类型预读参数 */
#define READ_AHEAD_PIPE 16  /* 管道预读大小 */
#define READ_AHEAD_SOCKET 8 /* 套接字预读大小 */
#define READ_AHEAD_TTY 4    /* 终端预读大小 */

#endif /* _FILE_H */