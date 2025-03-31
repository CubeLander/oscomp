
#include <kernel/vfs.h>
#include <kernel/mm/kmalloc.h>
#include <kernel/sched.h>
#include <kernel/sprint.h>
#include <kernel/types.h>
#include <kernel/util/string.h>
#include <sys/poll.h>

#define FDTABLE_INIT_SIZE 16

static struct fdtable* __fdtable_alloc(void);
static void __fdtable_free(struct fdtable* fdt);
static int32 __find_next_fd(struct fdtable* fdt, uint32 start);
static int32 _fdtable_do_poll(struct fdtable* fdt, struct pollfd* fds, uint32 nfds, int32 timeout);

/**
 * 获取fdtable引用
 */
struct fdtable* fdtable_acquire(struct fdtable* fdt) {
	if (!fdt) return __fdtable_alloc();
	if (atomic_read(&fdt->fdt_refcount) <= 0) return NULL;
	// 增加引用计数
	atomic_inc(&fdt->fdt_refcount);
	return fdt;
}

/**
 * 释放fdtable引用
 */
int32 fdtable_unref(struct fdtable* fdt) {
	if (!fdt) return -EINVAL;
	if (atomic_read(&fdt->fdt_refcount) <= 0) { panic("fdtable_unref: fdt_refcount is already 0\n"); }

	// 减少引用计数，如果到0则释放
	if (atomic_dec_and_test(&fdt->fdt_refcount)) __fdtable_free(fdt);
	return 0;
}

/**
 * 复制fdtable（用于fork）
 */
struct fdtable* fdtable_copy(struct fdtable* old) {
	struct fdtable* new;
	int32 size, i;

	if (!old) return NULL;
	if (atomic_read(&old->fdt_refcount) <= 0) return NULL;

	spinlock_lock(&old->fdt_lock);
	size = old->max_fds;
	spinlock_unlock(&old->fdt_lock);

	new = __fdtable_alloc();
	if (!new) return NULL;

	// 扩展新表以匹配原表大小
	if (size > FDTABLE_INIT_SIZE) {
		if (fdtable_expand(new, size) < 0) {
			fdtable_unref(new);
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
			// file_ref(new->fd_array[i]);
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
	if (!fdt) return NULL;

	fdt->fd_array = kmalloc(sizeof(struct file*) * FDTABLE_INIT_SIZE);
	if (!fdt->fd_array) {
		kfree(fdt);
		return NULL;
	}

	fdt->fd_flags = kmalloc(sizeof(uint32) * FDTABLE_INIT_SIZE);
	if (!fdt->fd_flags) {
		kfree(fdt->fd_array);
		kfree(fdt);
		return NULL;
	}

	// 初始化数组
	memset(fdt->fd_array, 0, sizeof(struct file*) * FDTABLE_INIT_SIZE);
	memset(fdt->fd_flags, 0, sizeof(uint32) * FDTABLE_INIT_SIZE);

	fdt->max_fds = FDTABLE_INIT_SIZE;
	fdt->fdt_nextfd = 0;
	spinlock_init(&fdt->fdt_lock);
	atomic_set(&fdt->fdt_refcount, 1);

	return fdt;
}

/**
 * 释放文件描述符表
 */
static void __fdtable_free(struct fdtable* fdt) {
	int32 i;

	// 关闭所有打开的文件
	for (i = 0; i < fdt->max_fds; i++) {
		if (fdt->fd_array[i]) {
			// file_unref(fdt->fd_array[i]);
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
int32 fdtable_expand(struct fdtable* fdt, uint32 new_size) {
	struct file** new_array;
	uint32* new_flags;

	if (!fdt || new_size <= fdt->max_fds) return -EINVAL;

	// 分配新数组
	new_array = kmalloc(sizeof(struct file*) * new_size);
	if (!new_array) return -ENOMEM;

	new_flags = kmalloc(sizeof(uint32) * new_size);
	if (!new_flags) {
		kfree(new_array);
		return -ENOMEM;
	}

	// 初始化新空间
	memset(new_array, 0, sizeof(struct file*) * new_size);
	memset(new_flags, 0, sizeof(uint32) * new_size);

	// 复制旧数据
	spinlock_lock(&fdt->fdt_lock);
	memcpy(new_array, fdt->fd_array, sizeof(struct file*) * fdt->max_fds);
	memcpy(new_flags, fdt->fd_flags, sizeof(uint32) * fdt->max_fds);

	// 替换数组
	kfree(fdt->fd_array);
	kfree(fdt->fd_flags);
	fdt->fd_array = new_array;
	fdt->fd_flags = new_flags;
	fdt->max_fds = new_size;

	spinlock_unlock(&fdt->fdt_lock);

	return 0;
}

/**
 * 获取当前fdtable大小
 */
uint64 fdtable_getSize(struct fdtable* fdt) {
	if (!fdt) return 0;
	return fdt->max_fds;
}

/**
 * 查找下一个可用的文件描述符
 */
static int32 __find_next_fd(struct fdtable* fdt, uint32 start) {
	uint32 i;

	for (i = start; i < fdt->max_fds; i++) {
		if (!fdt->fd_array[i] && !(fdt->fd_flags[i] & FD_ALLOCATED)) return i;
	}

	return -1; // 没有可用描述符
}

/**
 * 分配一个新的文件描述符
 */
int32 fdtable_allocFd(struct fdtable* fdt, uint32 flags) {
	int32 fd;

	if (!fdt) return -EINVAL;

	spinlock_lock(&fdt->fdt_lock);

	// 从fdt_nextfd开始查找
	fd = __find_next_fd(fdt, fdt->fdt_nextfd);

	// 如果没找到，尝试从头开始查找
	if (fd < 0) fd = __find_next_fd(fdt, 0);

	// 如果仍然没有，尝试扩展表
	if (fd < 0) {
		spinlock_unlock(&fdt->fdt_lock);

		// 尝试扩展表
		if (fdtable_expand(fdt, fdt->max_fds * 2) < 0) return -EMFILE; // 文件描述符表已满

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

	if (!fdt || fd >= fdt->max_fds) return;

	spinlock_lock(&fdt->fdt_lock);

	file = fdt->fd_array[fd];
	if (file) {
		fdt->fd_array[fd] = NULL;
		fdt->fd_flags[fd] = 0;

		// 更新fdt_nextfd以优化后续分配
		if (fd < fdt->fdt_nextfd) fdt->fdt_nextfd = fd;
	}
	fdt->fd_flags[fd] &= ~FD_ALLOCATED; // 清除占位标志
	spinlock_unlock(&fdt->fdt_lock);

	// 减少文件引用
	if (file) { file_unref(file); }
}

/**
 * 安装文件到描述符
 */
int32 fdtable_installFd(struct fdtable* fdt, uint64 fd, struct file* file) {
	if (!fdt || !file || fd >= fdt->max_fds) return -EINVAL;
	if (!(fdt->fd_flags[fd] & FD_ALLOCATED)) {
		// 错误：尝试安装到未分配的描述符
		return -EBADF;
	}

	spinlock_lock(&fdt->fdt_lock);

	// 如果描述符已被占用，先关闭
	if (fdt->fd_array[fd]) {
		struct file* old_file = fdt->fd_array[fd];
		fdt->fd_array[fd] = NULL;
		// file_unref(old_file); // 在spinlock外部执行
		spinlock_unlock(&fdt->fdt_lock);
		// file_unref(old_file);
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

	if (!fdt || fd >= fdt->max_fds) return NULL;

	spinlock_lock(&fdt->fdt_lock);
	file = fdt->fd_array[fd];
	spinlock_unlock(&fdt->fdt_lock);
	file_ref(file); // 增加引用计数
	return file;
}

/**
 * 设置文件描述符标志
 */
int32 fdtable_setFdFlags(struct fdtable* fdt, uint64 fd, uint32 flags) {
	if (!fdt || fd >= fdt->max_fds) return -EINVAL;

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
uint32 fdtable_getFdFlags(struct fdtable* fdt, uint64 fd) {
	uint32 flags = 0;

	if (!fdt || fd >= fdt->max_fds) return 0;

	spinlock_lock(&fdt->fdt_lock);

	if (fdt->fd_array[fd]) flags = fdt->fd_flags[fd];

	spinlock_unlock(&fdt->fdt_lock);
	return flags;
}

/**
 * 实现dup2系统调用
 */
int32 do_dup2(struct fdtable* fdt, uint64 oldfd, uint64 newfd) {
	struct file* file;
	int32 ret = 0;

	if (!fdt || oldfd >= fdt->max_fds) return -EBADF;

	// 获取源文件
	file = fdtable_getFile(fdt, oldfd);
	if (!file) return -EBADF;

	// 如果oldfd和newfd相同，直接返回
	if (oldfd == newfd) return newfd;

	// 如果newfd超出当前范围，需要扩展表
	if (newfd >= fdt->max_fds) {
		ret = fdtable_expand(fdt, newfd + 1);
		if (ret < 0) return ret;
	}

	// 增加文件引用
	// file_ref(file);

	// 关闭newfd如果已打开
	fdtable_closeFd(fdt, newfd);

	// 安装到新描述符
	ret = fdtable_installFd(fdt, newfd, file);
	if (ret < 0) {
		// file_unref(file);
		return ret;
	}

	// 复制fd标志位，但清除close-on-exec标志
	uint32 flags = fdtable_getFdFlags(fdt, oldfd) & ~FD_CLOEXEC;
	fdtable_setFdFlags(fdt, newfd, flags);

	return newfd;
}

/**
 * 实现fcntl系统调用
 */
int32 do_fcntl(struct fdtable* fdt, uint64 fd, uint32 cmd, uint64 arg) {
	struct file* filp;
	int32 ret = 0;

	if (!fdt || fd >= fdt->max_fds) return -EBADF;

	filp = fdtable_getFile(fdt, fd);
	if (!filp) return -EBADF;

	switch (cmd) {
	case F_DUPFD:
		// 分配大于等于arg的最小未使用描述符
		ret = fdtable_allocFd(fdt, 0);
		if (ret < 0) return ret;

		if (ret < arg) {
			fdtable_closeFd(fdt, ret);
			spinlock_lock(&fdt->fdt_lock);
			for (ret = arg; ret < fdt->max_fds; ret++) {
				if (!fdt->fd_array[ret]) break;
			}
			spinlock_unlock(&fdt->fdt_lock);

			if (ret >= fdt->max_fds) {
				ret = fdtable_expand(fdt, ret + 1);
				if (ret < 0) return ret;
				ret = arg;
			}
		}

		// file_ref(filp);
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
static struct pollfd* convert_fdsets_to_pollfds(int32 nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds) {
	struct pollfd* pfds;
	int32 i, count = 0;

	// 分配足够大的数组
	pfds = kmalloc(sizeof(struct pollfd) * nfds);
	if (!pfds) return NULL;

	// 转换所有set中的fd到pollfd
	for (i = 0; i < nfds; i++) {
		short events = 0;

		if (readfds && FD_ISSET(i, readfds)) events |= POLLIN;

		if (writefds && FD_ISSET(i, writefds)) events |= POLLOUT;

		if (exceptfds && FD_ISSET(i, exceptfds)) events |= POLLPRI;

		if (events) {
			pfds[count].fd = i;
			pfds[count].events = events;
			pfds[count].revents = 0;
			count++;
		}
	}

	// 设置剩余为-1表示无效
	for (i = count; i < nfds; i++) { pfds[i].fd = -1; }

	return pfds;
}

/**
 * 将pollfd结果更新回fd_set
 */
static void update_fdsets_from_pollfds(struct pollfd* pfds, int32 count, fd_set* readfds, fd_set* writefds, fd_set* exceptfds) {
	int32 i;

	// 先清空所有集合
	if (readfds) FD_ZERO(readfds);
	if (writefds) FD_ZERO(writefds);
	if (exceptfds) FD_ZERO(exceptfds);

	// 根据revents更新fd_set
	for (i = 0; i < count; i++) {
		int32 fd = pfds[i].fd;
		short revents = pfds[i].revents;

		if (fd < 0) continue;

		if (readfds && (revents & POLLIN)) FD_SET(fd, readfds);

		if (writefds && (revents & POLLOUT)) FD_SET(fd, writefds);

		if (exceptfds && (revents & (POLLPRI | POLLERR | POLLHUP))) FD_SET(fd, exceptfds);
	}
}

/**
 * 统一的底层轮询实现
 */
static int32 _fdtable_do_poll(struct fdtable* fdt, struct pollfd* fds, uint32 nfds, int32 timeout) {
	int32 i, ready = 0;
	struct poll_table_struct pt;

	// 以下是伪代码，实际实现需要等待队列支持

	/* 初始化poll_table
	poll_initwait(&pt);
	*/

	// 首次非阻塞检查
	for (i = 0; i < nfds; i++) {
		struct file* file;
		int32 fd = fds[i].fd;

		if (fd < 0) continue;

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
		if (fds[i].revents & fds[i].events) ready++;
	}

	// 如果没有就绪事件且需要等待
	if (!ready && timeout != 0) {
		/* 设置等待和定时器
		schedule_timeout(timeout);
		*/

		// 被唤醒后重新检查
		for (i = 0; i < nfds; i++) {
			struct file* file;
			int32 fd = fds[i].fd;

			if (fd < 0) continue;

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
			if (fds[i].revents & fds[i].events) ready++;
		}
	}

	/* 清理poll等待
	poll_freewait(&pt);
	*/

	return ready;
}

// do_select实现示例
int32 do_select(struct fdtable* fdt, int32 nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, struct timeval* timeout) {
	// 1. 将fd_set转换为pollfd数组
	struct pollfd* pfds = convert_fdsets_to_pollfds(nfds, readfds, writefds, exceptfds);
	if (!pfds) return -ENOMEM;

	// 2. 计算超时值（毫秒）
	int32 timeout_ms = -1;
	if (timeout) timeout_ms = timeout->tv_sec * 1000 + timeout->tv_usec / 1000;

	// 3. 调用统一轮询函数
	int32 count = nfds; // 实际可能更少，但这是最大值
	int32 ready = _fdtable_do_poll(fdt, pfds, count, timeout_ms);

	// 4. 将结果转回fd_set格式
	if (ready > 0) update_fdsets_from_pollfds(pfds, count, readfds, writefds, exceptfds);

	// 5. 清理并返回结果
	kfree(pfds);
	return ready;
}

/**
 * 实现poll系统调用
 */
int32 do_poll(struct fdtable* fdt, struct pollfd* fds, uint32 nfds, int32 timeout) {
	// 直接调用统一轮询实现
	return _fdtable_do_poll(fdt, fds, nfds, timeout);
}

/**
 * 创建epoll实例 - 基本实现
 */
int32 do_epoll_create(struct fdtable* fdt, int32 flags) {
	// 这里应创建一个epoll文件对象并分配描述符
	/*
	struct file* epfile = create_epoll_file();
	if (PTR_IS_ERROR(epfile))
	    return PTR_ERR(epfile);

	int32 fd = fdtable_allocFd(fdt, 0);
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
int32 do_epoll_ctl(struct fdtable* fdt, int32 epfd, int32 op, int32 fd, struct epoll_event* event) {
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
int32 do_epoll_wait(struct fdtable* fdt, int32 epfd, struct epoll_event* events, int32 maxevents, int32 timeout) {
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

/**
 * Kernel-internal implementation of open syscall
 */
int32 do_open(const char* pathname, int32 flags, ...) {
	// Get mode if O_CREAT is set
	mode_t mode = 0;
	if (flags & O_CREAT) {
		va_list args;
		va_start(args, flags);
		mode = va_arg(args, mode_t);
		va_end(args);
	}

	// Call into VFS to get a file struct
	struct file* filp = file_open(pathname, flags, mode);
	if (PTR_IS_ERROR(filp)) return PTR_ERR(filp);

	// Allocate a file descriptor
	int32 fd = fdtable_allocFd(current_task()->fdtable, 0);
	if (fd < 0) {
		file_unref(filp);
		return fd;
	}

	// Install the file in the fd table
	fdtable_installFd(current_task()->fdtable, fd, filp);
	return fd;
}

/**
 * Kernel-internal implementation of close syscall
 */
int32 do_close(int32 fd) {
	struct file* filp = fdtable_getFile(current_task()->fdtable, fd);
	if (!filp) return -EBADF;

	fdtable_closeFd(current_task()->fdtable, fd);
	return file_unref(filp);
}

/**
 * Kernel-internal implementation of lseek syscall
 */
off_t do_lseek(int32 fd, off_t offset, int32 whence) {
	struct file* filp = fdtable_getFile(current_task()->fdtable,fd);
	if (!filp) return -EBADF;

	loff_t pos = file_llseek(filp, offset, whence);
	file_unref(filp);
	return pos;
}

/**
 * Kernel-internal implementation of read syscall
 */
ssize_t do_read(int32 fd, void* buf, size_t count) {
	struct file* filp = fdtable_getFile(current_task()->fdtable,fd);
	if (!filp) return -EBADF;

	ssize_t ret = file_read(filp, buf, count, &filp->f_pos);
	file_unref(filp);
	return ret;
}

/**
 * Kernel-internal implementation of write syscall
 */
ssize_t do_write(int32 fd, const void* buf, size_t count) {
	struct file* filp = fdtable_getFile(current_task()->fdtable,fd);
	if (!filp) return -EBADF;

	ssize_t ret = file_write(filp, buf, count, &filp->f_pos);
	file_unref(filp);
	return ret;
}


/**
 * fd_monkey - translate a possible fd in fcontext to file object
 * @fctx: fcontext to be translated
 * @return: 0 on success or no fd in ctx, negative error code on unexpected failures
 */
int32 fd_monkey(struct fcontext* fctx){

	fctx->fc_file = fdtable_getFile(fctx->fc_task->fdtable, fctx->fc_fd);
	CHECK_PTR_VALID(fctx->fc_file, -EBADF);

	fctx->fc_inode = inode_ref(fctx->fc_file->f_inode);
	CHECK_PTR_VALID(fctx->fc_inode, -EBADF);

	return 0;	
}