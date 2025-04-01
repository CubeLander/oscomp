
#include <kernel/mm/kmalloc.h>
#include <kernel/sched.h>
#include <kernel/sprint.h>
#include <kernel/types.h>
#include <kernel/util/string.h>
#include <kernel/vfs.h>
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



int32 fd_monkey_open(struct fcontext* fctx) {
	fctx->fc_file = fdtable_getFile(fctx->fc_task->fdtable, fctx->fc_fd);
	CHECK_PTR_VALID(fctx->fc_file, -EBADF);
	return 0;
}

int32 fd_monkey_close(struct fcontext* fctx) {
	fctx->fc_file = fdtable_getFile(fctx->fc_task->fdtable, fctx->fc_fd);
	CHECK_PTR_VALID(fctx->fc_file, -EBADF);
	fdtable_closeFd(fctx->fc_task->fdtable, fctx->fc_fd);
	// 减少文件引用计数
	int32 ret = file_unref(fctx->fc_file);
	return 0;
}

/**
 * fd_monkey - translate a possible fd in fcontext to file object
 * @fctx: fcontext to be translated
 * @return: 0 on success or no fd in ctx, negative error code on unexpected failures
 */
int32 fd_monkey(struct fcontext* fctx) {
	if (fctx->fc_action >= VFS_ACTION_MAX) return -EINVAL;
	monkey_intent_handler_t handler = fd_intent_table[fctx->fc_action];
	if (!handler) return -ENOTSUP;
	return handler(fctx);

	return 0;
}

monkey_intent_handler_t fd_intent_table[VFS_ACTION_MAX] = {
    [FD_ACTION_OPEN] = fd_monkey_open, // 处理fc_string的路径字符串，并继续执行path_walk
    [FD_ACTION_CLOSE] = fd_monkey_close,

};