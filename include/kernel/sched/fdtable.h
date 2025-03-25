#ifndef _FDTABLE_H
#define _FDTABLE_H

// #include <kernel/fs/vfs/file.h>
#include <kernel/types.h>
#include <util/atomic.h>
#include <util/spinlock.h>
#include <sys/select.h>

struct file;

/* 等待队列相关前向声明 */
struct wait_queue_head;
struct wait_queue_entry;
struct epoll_event;

/* 轮询队列处理函数类型 */
typedef void (*poll_queue_proc)(struct file *file, struct wait_queue_head *wq, struct poll_table_struct *p);

/**
 * poll_table_struct - 表示轮询操作的数据结构
 * 用于将进程注册到各个文件的等待队列
 */
struct poll_table_struct {
    poll_queue_proc qproc;              /* 队列回调函数，用于注册到等待队列 */
    unsigned long key;                  /* 事件掩码，标识感兴趣的事件类型 */
    struct wait_queue_entry *entry;     /* 等待队列条目 */
    struct task_struct *polling_task;   /* 执行轮询的任务 */
};

/* 轮询表初始化与清理 */
void poll_initwait(struct poll_table_struct *pt);
void poll_freewait(struct poll_table_struct *pt);

/* 文件描述符标志 */
#define FD_ALLOCATED   0x01   /* 描述符已分配但可能尚未关联文件 */
#define FD_CLOEXEC     0x02   /* 执行时关闭标志 */

/**
 * fdtable - File descriptor table structure
 */
struct fdtable {
	struct file** fd_array; /* Array of file pointers */
	unsigned int* fd_flags; /* Array of fd flags */

	unsigned int fdt_size;   /* Size of the array */
	unsigned int fdt_nextfd; /* Next free fd number */
	spinlock_t fdt_lock;     /* Lock for the struct */
	atomic_t fdt_refcount;   /* Reference count */
};

/* Process-level file table management */
struct fdtable* fdtable_get(struct fdtable*);  // thread
struct fdtable* fdtable_copy(struct fdtable*); // fork
int fdtable_put(struct fdtable* fdt);

int fdtable_allocFd(struct fdtable* fdt, unsigned int flags);
void fdtable_closeFd(struct fdtable* fdt, uint64 fd);
int fdtable_installFd(struct fdtable* fdt, uint64 fd, struct file* file);

struct file* fdtable_getFile(struct fdtable* fdt, uint64 fd);
// put_file是file的方法
/*fdtable容量管理*/
int fdtable_expand(struct fdtable* fdt, unsigned int new_size);
uint64 fdtable_getSize(struct fdtable* fdt);

/*fd标志位管理*/
int fdtable_setFdFlags(struct fdtable* fdt, uint64 fd, unsigned int flags);
unsigned int fdtable_getFdFlags(struct fdtable* fdt, uint64 fd);

/*文件系统调用*/
/*重定向文件描述符*/
int do_dup2(struct fdtable* fdt, uint64 oldfd, uint64 newfd);
/*文件描述符控制系统调用，用于修改文件描述符的属性*/
int do_fcntl(struct fdtable* fdt, uint64 fd, unsigned int cmd, unsigned long arg);

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
int do_select(struct fdtable* fdt, int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, struct timeval* timeout);

/**
 * 实现poll系统调用逻辑
 *
 * @param fdt       文件描述符表
 * @param fds       pollfd结构体数组
 * @param nfds      数组中的描述符数量
 * @param timeout   超时时间（毫秒），-1表示永久等待
 * @return          就绪的文件描述符数量，错误时返回负值
 */
int do_poll(struct fdtable *fdt, struct pollfd *fds,
	unsigned int nfds, int timeout);



/**
 * 创建epoll实例
 *
 * @param fdt       文件描述符表
 * @param flags     创建标志
 * @return          新的epoll文件描述符，错误时返回负值
 */
int do_epoll_create(struct fdtable *fdt, int flags);

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
int do_epoll_ctl(struct fdtable *fdt, int epfd, int op, int fd,
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
int do_epoll_wait(struct fdtable *fdt, int epfd,
                 struct epoll_event *events, int maxevents, int timeout);

#endif /* _FDTABLE_H */