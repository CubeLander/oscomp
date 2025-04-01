#ifndef _FDTABLE_H
#define _FDTABLE_H

// #include <kernel/vfs/file.h>
#include <kernel/types.h>
#include <kernel/util/atomic.h>
#include <kernel/util/spinlock.h>
#include <sys/select.h>

struct file;

/* 等待队列相关前向声明 */
struct wait_queue_head;
struct wait_queue_entry;
struct epoll_event;
struct poll_table_struct;

/* 轮询队列处理函数类型 */
typedef void (*poll_queue_proc)(struct file *file, struct wait_queue_head *wq, struct poll_table_struct *p);

/**
 * poll_table_struct - 表示轮询操作的数据结构
 * 用于将进程注册到各个文件的等待队列
 */
struct poll_table_struct {
    poll_queue_proc qproc;              /* 队列回调函数，用于注册到等待队列 */
    uint64 key;                  /* 事件掩码，标识感兴趣的事件类型 */
    struct wait_queue_entry *entry;     /* 等待队列条目 */
    struct task_struct *polling_task;   /* 执行轮询的任务 */
};

/* 轮询表初始化与清理 */
void poll_initwait(struct poll_table_struct *pt);
void poll_freewait(struct poll_table_struct *pt);

/**
 * fdtable - File descriptor table structure
 */
struct fdtable {
	struct file** fd_array; /* Array of file pointers */
	uint32* fd_flags; /* Array of fd flags */

	uint32 max_fds;   /* Size of the array */
	uint32 fdt_nextfd; /* Next free fd number */
	spinlock_t fdt_lock;     /* Lock for the struct */
	atomic_t fdt_refcount;   /* Reference count */
};

int32 fd_monkey(struct fcontext* fctx);

/* Process-level file table management */
struct fdtable* fdtable_acquire(struct fdtable*);  // thread
struct fdtable* fdtable_copy(struct fdtable*); // fork
int32 fdtable_unref(struct fdtable* fdt);

int32 fdtable_allocFd(struct fdtable* fdt, uint32 flags);
void fdtable_closeFd(struct fdtable* fdt, uint64 fd);
int32 fdtable_installFd(struct fdtable* fdt, uint64 fd, struct file* file);

struct file* fdtable_getFile(struct fdtable* fdt, uint64 fd);
// put_file是file的方法
/*fdtable容量管理*/
int32 fdtable_expand(struct fdtable* fdt, uint32 new_size);
uint64 fdtable_getSize(struct fdtable* fdt);

/*fd标志位管理*/
int32 fdtable_setFdFlags(struct fdtable* fdt, uint64 fd, uint32 flags);
uint32 fdtable_getFdFlags(struct fdtable* fdt, uint64 fd);


/* File descriptor flags - using high bits to avoid conflicts with fcntl.h flags */

/* Internal allocation state flags - use high bits (bits 24-31) */
#define FD_ALLOCATED    (1U << 24)  /* File descriptor number is allocated (even if file ptr is NULL) */
#define FD_RESERVED     (1U << 25)  /* Reserved for future allocation */

/* Internal state and tracking flags - also using high bits */
#define FD_INTERNAL_ASYNC     (1U << 26)  /* Internal async notification tracking */
#define FD_INTERNAL_CACHE     (1U << 27)  /* Internal cache state tracking */
#define FD_INTERNAL_CLONING   (1U << 28)  /* Being cloned during fork operation */




#endif /* _FDTABLE_H */