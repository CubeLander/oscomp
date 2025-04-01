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

/*文件系统调用*/
/*重定向文件描述符*/
int32 do_dup2(struct fdtable* fdt, uint64 oldfd, uint64 newfd);
/*文件描述符控制系统调用，用于修改文件描述符的属性*/
int32 do_fcntl(struct fdtable* fdt, uint64 fd, uint32 cmd, uint64 arg);
int32 do_open(const char *pathname, int32 flags, ...);
int32 do_close(int32 fd);
off_t do_lseek(int32 fd, off_t offset, int32 whence);
ssize_t do_read(int32 fd, void *buf, size_t count);
ssize_t do_write(int32 fd, const void *buf, size_t count);
/**
 * 实现select系统调用逻辑
 *
 * @param fdt       文件描述符表
 * @param nfds      最大的文件描述符加1
 * @param readfds   用于检查可读状态的描述符集
 * @param writefds  用于检查可写状态的描述符集
 * @param exceptfds 用于检查异常状态的描述符集
 * @param timeout   超时时间结构体，NULL表示永久等待
 * @return          就绪的文件描述符数量，错误时返回负值
 */
int32 do_select(struct fdtable* fdt, int32 nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, struct timeval* timeout);

// /**
//  * 实现poll系统调用逻辑
//  *
//  * @param fdt       文件描述符表
//  * @param fds       pollfd结构体数组
//  * @param nfds      数组中的描述符数量
//  * @param timeout   超时时间（毫秒），-1表示永久等待
//  * @return          就绪的文件描述符数量，错误时返回负值
//  */
// int32 do_poll(struct fdtable *fdt, struct pollfd *fds,
// 	uint32 nfds, int32 timeout);



/**
 * 创建epoll实例
 *
 * @param fdt       文件描述符表
 * @param flags     创建标志
 * @return          新的epoll文件描述符，错误时返回负值
 */
int32 do_epoll_create(struct fdtable *fdt, int32 flags);

/**
 * 控制epoll实例上的文件描述符
 *
 * @param fdt       文件描述符表
 * @param epfd      epoll文件描述符
 * @param op        操作类型（EPOLL_CTL_ADD/MOD/DEL）
 * @param fd        目标文件描述符
 * @param event     事件描述
 * @return          成功返回0，错误时返回负值
 */
int32 do_epoll_ctl(struct fdtable *fdt, int32 epfd, int32 op, int32 fd,
                struct epoll_event *event);

/**
 * 等待epoll事件
 *
 * @param fdt       文件描述符表
 * @param epfd      epoll文件描述符
 * @param events    用于返回事件的数组
 * @param maxevents 数组大小
 * @param timeout   超时时间（毫秒）
 * @return          就绪的文件描述符数量，错误时返回负值
 */
int32 do_epoll_wait(struct fdtable *fdt, int32 epfd,
                 struct epoll_event *events, int32 maxevents, int32 timeout);

/* File descriptor flags - using high bits to avoid conflicts with fcntl.h flags */

/* Internal allocation state flags - use high bits (bits 24-31) */
#define FD_ALLOCATED    (1U << 24)  /* File descriptor number is allocated (even if file ptr is NULL) */
#define FD_RESERVED     (1U << 25)  /* Reserved for future allocation */

/* Internal state and tracking flags - also using high bits */
#define FD_INTERNAL_ASYNC     (1U << 26)  /* Internal async notification tracking */
#define FD_INTERNAL_CACHE     (1U << 27)  /* Internal cache state tracking */
#define FD_INTERNAL_CLONING   (1U << 28)  /* Being cloned during fork operation */




#endif /* _FDTABLE_H */