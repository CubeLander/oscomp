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

	void* f_private;
};

/* 在 file.h 中为 f_flags 添加新的标志位 */
#define F_SPECIAL_SEEK 0x10000000  /* 需要特殊 seek 处理的标志 */

/**
 * Directory context for readdir operations
 */
struct dir_context {
	int32 (*actor)(struct dir_context*, const char*, int32, loff_t, uint64_t, unsigned);
	loff_t pos; /* Current position in directory */
};

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