#include <errno.h>
#include <kernel/sched/fdtable.h>
#include <kernel/fs/vfs/file.h>
#include <kernel/mm/kmalloc.h>
#include <sys/poll.h>
#include <util/string.h>
#include <spike_interface/spike_utils.h>

#define FDTABLE_INIT_SIZE 16

static struct fdtable* __fdtable_alloc(void);
static void __fdtable_free(struct fdtable* fdt);
static int __find_next_fd(struct fdtable* fdt, unsigned int start);
static int _fdtable_do_poll(struct fdtable* fdt, struct pollfd* fds, unsigned int nfds, int timeout);

/**
 * 获取fdtable引用
 */
struct fdtable* fdtable_get(struct fdtable* fdt) {
	if (!fdt)
		return __fdtable_alloc();
	if(atomic_read(&fdt->fdt_refcount) <= 0)
		return NULL;
	// 增加引用计数
	atomic_inc(&fdt->fdt_refcount);
	return fdt;
}

/**
 * 释放fdtable引用
 */
int fdtable_put(struct fdtable* fdt) {
	if (!fdt)
		return -EINVAL;
	if(atomic_read(&fdt->fdt_refcount) <= 0){
		panic("fdtable_put: fdt_refcount is already 0\n");
	}

	// 减少引用计数，如果到0则释放
	if (atomic_dec_and_test(&fdt->fdt_refcount))
		__fdtable_free(fdt);
}

/**
 * 复制fdtable（用于fork）
 */
struct fdtable* fdtable_copy(struct fdtable* old) {
	struct fdtable* new;
	int size, i;

	if (!old)
		return NULL;
	if(atomic_read(&old->fdt_refcount) <= 0)
		return NULL;

	spinlock_lock(&old->fdt_lock);
	size = old->fdt_size;
	spinlock_unlock(&old->fdt_lock);

	new = __fdtable_alloc();
	if (!new)
		return NULL;

	// 扩展新表以匹配原表大小
	if (size > FDTABLE_INIT_SIZE) {
		if (fdtable_expand(new, size) < 0) {
			fdtable_put(new);
			return NULL;
		}
	}

	// 复制文件描述符和标志位
	spinlock_lock(&old->fdt_lock);
	spinlock_lock(&new->fdt_lock);

	for (i = 0; i < size; i++) {
		if (old->fd_array[i]) {
			new->fd_array[i] = old->fd_array[i];
			// 增加file引用计数
			// file_get(new->fd_array[i]);
			new->fd_flags[i] = old->fd_flags[i];
		}
	}

	new->fdt_nextfd = old->fdt_nextfd;

	spinlock_unlock(&new->fdt_lock);
	spinlock_unlock(&old->fdt_lock);

	return new;
}

/**
 * 分配文件描述符表
 */
static struct fdtable* __fdtable_alloc(void) {
	struct fdtable* fdt = kmalloc(sizeof(struct fdtable));
	if (!fdt)
		return NULL;

	fdt->fd_array = kmalloc(sizeof(struct file*) * FDTABLE_INIT_SIZE);
	if (!fdt->fd_array) {
		kfree(fdt);
		return NULL;
	}

	fdt->fd_flags = kmalloc(sizeof(unsigned int) * FDTABLE_INIT_SIZE);
	if (!fdt->fd_flags) {
		kfree(fdt->fd_array);
		kfree(fdt);
		return NULL;
	}

	// 初始化数组
	memset(fdt->fd_array, 0, sizeof(struct file*) * FDTABLE_INIT_SIZE);
	memset(fdt->fd_flags, 0, sizeof(unsigned int) * FDTABLE_INIT_SIZE);

	fdt->fdt_size = FDTABLE_INIT_SIZE;
	fdt->fdt_nextfd = 0;
	spinlock_init(&fdt->fdt_lock);
	atomic_set(&fdt->fdt_refcount, 1);

	return fdt;
}

/**
 * 释放文件描述符表
 */
static void __fdtable_free(struct fdtable* fdt) {
	int i;

	// 关闭所有打开的文件
	for (i = 0; i < fdt->fdt_size; i++) {
		if (fdt->fd_array[i]) {
			// file_put(fdt->fd_array[i]);
			fdt->fd_array[i] = NULL;
		}
	}

	kfree(fdt->fd_array);
	kfree(fdt->fd_flags);
	kfree(fdt);
}

/**
 * 扩展文件描述符表容量
 */
int fdtable_expand(struct fdtable* fdt, unsigned int new_size) {
	struct file** new_array;
	unsigned int* new_flags;

	if (!fdt || new_size <= fdt->fdt_size)
		return -EINVAL;

	// 分配新数组
	new_array = kmalloc(sizeof(struct file*) * new_size);
	if (!new_array)
		return -ENOMEM;

	new_flags = kmalloc(sizeof(unsigned int) * new_size);
	if (!new_flags) {
		kfree(new_array);
		return -ENOMEM;
	}

	// 初始化新空间
	memset(new_array, 0, sizeof(struct file*) * new_size);
	memset(new_flags, 0, sizeof(unsigned int) * new_size);

	// 复制旧数据
	spinlock_lock(&fdt->fdt_lock);
	memcpy(new_array, fdt->fd_array, sizeof(struct file*) * fdt->fdt_size);
	memcpy(new_flags, fdt->fd_flags, sizeof(unsigned int) * fdt->fdt_size);

	// 替换数组
	kfree(fdt->fd_array);
	kfree(fdt->fd_flags);
	fdt->fd_array = new_array;
	fdt->fd_flags = new_flags;
	fdt->fdt_size = new_size;

	spinlock_unlock(&fdt->fdt_lock);

	return 0;
}

/**
 * 获取当前fdtable大小
 */
uint64 fdtable_getSize(struct fdtable* fdt) {
	if (!fdt)
		return 0;
	return fdt->fdt_size;
}

/**
 * 查找下一个可用的文件描述符
 */
static int __find_next_fd(struct fdtable* fdt, unsigned int start) {
	unsigned int i;

	for (i = start; i < fdt->fdt_size; i++) {
		if (!fdt->fd_array[i] && !(fdt->fd_flags[i] & FD_ALLOCATED))
			return i;
	}

	return -1; // 没有可用描述符
}

/**
 * 分配一个新的文件描述符
 */
int fdtable_allocFd(struct fdtable* fdt, unsigned int flags) {
	int fd;

	if (!fdt)
		return -EINVAL;

	spinlock_lock(&fdt->fdt_lock);

	// 从fdt_nextfd开始查找
	fd = __find_next_fd(fdt, fdt->fdt_nextfd);

	// 如果没找到，尝试从头开始查找
	if (fd < 0)
		fd = __find_next_fd(fdt, 0);

	// 如果仍然没有，尝试扩展表
	if (fd < 0) {
		spinlock_unlock(&fdt->fdt_lock);

		// 尝试扩展表
		if (fdtable_expand(fdt, fdt->fdt_size * 2) < 0)
			return -EMFILE; // 文件描述符表已满

		spinlock_lock(&fdt->fdt_lock);
		fd = __find_next_fd(fdt, fdt->fdt_nextfd);
	}

	if (fd >= 0) {
		fdt->fd_array[fd] = NULL; // 占位，表示已分配但未安装
		fdt->fd_flags[fd] = flags | FD_ALLOCATED;
		fdt->fdt_nextfd = fd + 1; // 更新下一个可能的fd
	}

	spinlock_unlock(&fdt->fdt_lock);
	return fd;
}

/**
 * 关闭一个文件描述符
 */
void fdtable_closeFd(struct fdtable* fdt, uint64 fd) {
	struct file* file;

	if (!fdt || fd >= fdt->fdt_size)
		return;

	spinlock_lock(&fdt->fdt_lock);

	file = fdt->fd_array[fd];
	if (file) {
		fdt->fd_array[fd] = NULL;
		fdt->fd_flags[fd] = 0;

		// 更新fdt_nextfd以优化后续分配
		if (fd < fdt->fdt_nextfd)
			fdt->fdt_nextfd = fd;
	}
    fdt->fd_flags[fd] &= ~FD_ALLOCATED;  // 清除占位标志
	spinlock_unlock(&fdt->fdt_lock);

	// 减少文件引用
	if (file) {
		file_put(file);
	}
}

/**
 * 安装文件到描述符
 */
int fdtable_installFd(struct fdtable* fdt, uint64 fd, struct file* file) {
	if (!fdt || !file || fd >= fdt->fdt_size)
		return -EINVAL;
	if (!(fdt->fd_flags[fd] & FD_ALLOCATED)) {
		// 错误：尝试安装到未分配的描述符
		return -EBADF;
	}

	spinlock_lock(&fdt->fdt_lock);

	// 如果描述符已被占用，先关闭
	if (fdt->fd_array[fd]) {
		struct file* old_file = fdt->fd_array[fd];
		fdt->fd_array[fd] = NULL;
		// file_put(old_file); // 在spinlock外部执行
		spinlock_unlock(&fdt->fdt_lock);
		// file_put(old_file);
		spinlock_lock(&fdt->fdt_lock);
	}

	fdt->fd_array[fd] = file;

	spinlock_unlock(&fdt->fdt_lock);
	return fd;
}

/**
 * 获取文件描述符关联的文件
 */
struct file* fdtable_getFile(struct fdtable* fdt, uint64 fd) {
	struct file* file = NULL;

	if (!fdt || fd >= fdt->fdt_size)
		return NULL;

	spinlock_lock(&fdt->fdt_lock);
	file = fdt->fd_array[fd];
	// 如果需要，这里可以增加文件引用计数
	spinlock_unlock(&fdt->fdt_lock);

	return file;
}

/**
 * 设置文件描述符标志
 */
int fdtable_setFdFlags(struct fdtable* fdt, uint64 fd, unsigned int flags) {
	if (!fdt || fd >= fdt->fdt_size)
		return -EINVAL;

	spinlock_lock(&fdt->fdt_lock);

	if (!fdt->fd_array[fd]) {
		spinlock_unlock(&fdt->fdt_lock);
		return -EBADF; // 文件描述符无效
	}

	fdt->fd_flags[fd] = flags;

	spinlock_unlock(&fdt->fdt_lock);
	return 0;
}

/**
 * 获取文件描述符标志
 */
unsigned int fdtable_getFdFlags(struct fdtable* fdt, uint64 fd) {
	unsigned int flags = 0;

	if (!fdt || fd >= fdt->fdt_size)
		return 0;

	spinlock_lock(&fdt->fdt_lock);

	if (fdt->fd_array[fd])
		flags = fdt->fd_flags[fd];

	spinlock_unlock(&fdt->fdt_lock);
	return flags;
}

/**
 * 实现dup2系统调用
 */
int do_dup2(struct fdtable* fdt, uint64 oldfd, uint64 newfd) {
	struct file* file;
	int ret = 0;

	if (!fdt || oldfd >= fdt->fdt_size)
		return -EBADF;

	// 获取源文件
	file = fdtable_getFile(fdt, oldfd);
	if (!file)
		return -EBADF;

	// 如果oldfd和newfd相同，直接返回
	if (oldfd == newfd)
		return newfd;

	// 如果newfd超出当前范围，需要扩展表
	if (newfd >= fdt->fdt_size) {
		ret = fdtable_expand(fdt, newfd + 1);
		if (ret < 0)
			return ret;
	}

	// 增加文件引用
	// file_get(file);

	// 关闭newfd如果已打开
	fdtable_closeFd(fdt, newfd);

	// 安装到新描述符
	ret = fdtable_installFd(fdt, newfd, file);
	if (ret < 0) {
		// file_put(file);
		return ret;
	}

	// 复制fd标志位，但清除close-on-exec标志
	unsigned int flags = fdtable_getFdFlags(fdt, oldfd) & ~FD_CLOEXEC;
	fdtable_setFdFlags(fdt, newfd, flags);

	return newfd;
}

/**
 * 实现fcntl系统调用
 */
int do_fcntl(struct fdtable* fdt, uint64 fd, unsigned int cmd, unsigned long arg) {
	struct file* filp;
	int ret = 0;

	if (!fdt || fd >= fdt->fdt_size)
		return -EBADF;

	filp = fdtable_getFile(fdt, fd);
	if (!filp)
		return -EBADF;

	switch (cmd) {
	case F_DUPFD:
		// 分配大于等于arg的最小未使用描述符
		ret = fdtable_allocFd(fdt, 0);
		if (ret < 0)
			return ret;

		if (ret < arg) {
			fdtable_closeFd(fdt, ret);
			spinlock_lock(&fdt->fdt_lock);
			for (ret = arg; ret < fdt->fdt_size; ret++) {
				if (!fdt->fd_array[ret])
					break;
			}
			spinlock_unlock(&fdt->fdt_lock);

			if (ret >= fdt->fdt_size) {
				ret = fdtable_expand(fdt, ret + 1);
				if (ret < 0)
					return ret;
				ret = arg;
			}
		}

		// file_get(filp);
		fdtable_installFd(fdt, ret, filp);
		return ret;

	case F_GETFD:
		return fdtable_getFdFlags(fdt, fd);

	case F_SETFD:
		return fdtable_setFdFlags(fdt, fd, arg);

	case F_GETFL:
		// 获取文件状态标志
		// 需要从文件对象获取
		// return filp->f_flags;
		return 0; // 占位符，实际应获取文件标志

	case F_SETFL:
		// 设置文件状态标志
		// 需要更新文件对象
		// filp->f_flags = (filp->f_flags & ~O_NONBLOCK) | (arg & O_NONBLOCK);
		return 0; // 占位符，实际应设置文件标志

	default:
		return -EINVAL;
	}
}

/**
 * 将fd_set转换为pollfd数组
 */
static struct pollfd* convert_fdsets_to_pollfds(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds) {
	struct pollfd* pfds;
	int i, count = 0;

	// 分配足够大的数组
	pfds = kmalloc(sizeof(struct pollfd) * nfds);
	if (!pfds)
		return NULL;

	// 转换所有set中的fd到pollfd
	for (i = 0; i < nfds; i++) {
		short events = 0;

		if (readfds && FD_ISSET(i, readfds))
			events |= POLLIN;

		if (writefds && FD_ISSET(i, writefds))
			events |= POLLOUT;

		if (exceptfds && FD_ISSET(i, exceptfds))
			events |= POLLPRI;

		if (events) {
			pfds[count].fd = i;
			pfds[count].events = events;
			pfds[count].revents = 0;
			count++;
		}
	}

	// 设置剩余为-1表示无效
	for (i = count; i < nfds; i++) {
		pfds[i].fd = -1;
	}

	return pfds;
}

/**
 * 将pollfd结果更新回fd_set
 */
static void update_fdsets_from_pollfds(struct pollfd* pfds, int count, fd_set* readfds, fd_set* writefds, fd_set* exceptfds) {
	int i;

	// 先清空所有集合
	if (readfds)
		FD_ZERO(readfds);
	if (writefds)
		FD_ZERO(writefds);
	if (exceptfds)
		FD_ZERO(exceptfds);

	// 根据revents更新fd_set
	for (i = 0; i < count; i++) {
		int fd = pfds[i].fd;
		short revents = pfds[i].revents;

		if (fd < 0)
			continue;

		if (readfds && (revents & POLLIN))
			FD_SET(fd, readfds);

		if (writefds && (revents & POLLOUT))
			FD_SET(fd, writefds);

		if (exceptfds && (revents & (POLLPRI | POLLERR | POLLHUP)))
			FD_SET(fd, exceptfds);
	}
}

/**
 * 统一的底层轮询实现
 */
static int _fdtable_do_poll(struct fdtable* fdt, struct pollfd* fds, unsigned int nfds, int timeout) {
	int i, ready = 0;
	struct poll_table_struct pt;

	// 以下是伪代码，实际实现需要等待队列支持

	/* 初始化poll_table
	poll_initwait(&pt);
	*/

	// 首次非阻塞检查
	for (i = 0; i < nfds; i++) {
		struct file* file;
		int fd = fds[i].fd;

		if (fd < 0)
			continue;

		file = fdtable_getFile(fdt, fd);
		if (!file) {
			fds[i].revents = POLLNVAL;
			ready++;
			continue;
		}

		/* 调用文件的poll方法
		fds[i].revents = file->f_operations->poll(file, &pt);
		*/

		// 占位符：模拟poll查询
		fds[i].revents = 0;
		// 如果请求的事件已就绪，增加ready计数
		if (fds[i].revents & fds[i].events)
			ready++;
	}

	// 如果没有就绪事件且需要等待
	if (!ready && timeout != 0) {
		/* 设置等待和定时器
		schedule_timeout(timeout);
		*/

		// 被唤醒后重新检查
		for (i = 0; i < nfds; i++) {
			struct file* file;
			int fd = fds[i].fd;

			if (fd < 0)
				continue;

			file = fdtable_getFile(fdt, fd);
			if (!file) {
				fds[i].revents = POLLNVAL;
				ready++;
				continue;
			}

			/* 不再注册等待，只检查状态
			fds[i].revents = file->f_operations->poll(file, NULL);
			*/

			// 占位符：模拟唤醒后的poll结果
			fds[i].revents = 0;
			if (fds[i].revents & fds[i].events)
				ready++;
		}
	}

	/* 清理poll等待
	poll_freewait(&pt);
	*/

	return ready;
}

// do_select实现示例
int do_select(struct fdtable* fdt, int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, struct timeval* timeout) {
	// 1. 将fd_set转换为pollfd数组
	struct pollfd* pfds = convert_fdsets_to_pollfds(nfds, readfds, writefds, exceptfds);
	if (!pfds)
		return -ENOMEM;

	// 2. 计算超时值（毫秒）
	int timeout_ms = -1;
	if (timeout)
		timeout_ms = timeout->tv_sec * 1000 + timeout->tv_usec / 1000;

	// 3. 调用统一轮询函数
	int count = nfds; // 实际可能更少，但这是最大值
	int ready = _fdtable_do_poll(fdt, pfds, count, timeout_ms);

	// 4. 将结果转回fd_set格式
	if (ready > 0)
		update_fdsets_from_pollfds(pfds, count, readfds, writefds, exceptfds);

	// 5. 清理并返回结果
	kfree(pfds);
	return ready;
}

/**
 * 实现poll系统调用
 */
int do_poll(struct fdtable* fdt, struct pollfd* fds, unsigned int nfds, int timeout) {
	// 直接调用统一轮询实现
	return _fdtable_do_poll(fdt, fds, nfds, timeout);
}

/**
 * 创建epoll实例 - 基本实现
 */
int do_epoll_create(struct fdtable* fdt, int flags) {
	// 这里应创建一个epoll文件对象并分配描述符
	/*
	struct file* epfile = create_epoll_file();
	if (IS_ERR(epfile))
	    return PTR_ERR(epfile);

	int fd = fdtable_allocFd(fdt, 0);
	if (fd < 0) {
	    // epoll文件清理
	    return fd;
	}

	fdtable_installFd(fdt, fd, epfile);
	return fd;
	*/

	// 返回伪值表示未实现
	return -ENOSYS;
}

/**
 * 控制epoll实例 - 基本实现
 */
int do_epoll_ctl(struct fdtable* fdt, int epfd, int op, int fd, struct epoll_event* event) {
	/*
	struct file* epfile = fdtable_getFile(fdt, epfd);
	if (!epfile || !is_epoll_file(epfile))
	    return -EBADF;

	struct file* target = fdtable_getFile(fdt, fd);
	if (!target)
	    return -EBADF;

	// 调用epoll文件的ctl操作
	return epfile->f_operations->epoll_ctl(epfile, op, target, event);
	*/

	// 返回伪值表示未实现
	return -ENOSYS;
}

/**
 * 等待epoll事件 - 基本实现
 */
int do_epoll_wait(struct fdtable* fdt, int epfd, struct epoll_event* events, int maxevents, int timeout) {
	/*
	struct file* epfile = fdtable_getFile(fdt, epfd);
	if (!epfile || !is_epoll_file(epfile))
	    return -EBADF;

	// 调用epoll文件的等待操作
	return epfile->f_operations->epoll_wait(epfile, events, maxevents, timeout);
	*/

	// 返回伪值表示未实现
	return -ENOSYS;
}