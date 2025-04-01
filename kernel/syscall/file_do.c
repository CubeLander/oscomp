#include <kernel/mm/kmalloc.h>
#include <kernel/mm/uaccess.h>
#include <kernel/sched/process.h>
#include <kernel/sprint.h>
#include <kernel/syscall/syscall.h>
#include <kernel/types.h>
#include <kernel/util/string.h>
#include <kernel/vfs.h>

/* 修改 do_lseek 函数实现 */
int64 do_lseek(int32 fd, off_t offset, int32 whence) {
	if (fd < 0) return -EBADF; // Invalid file descriptor

	struct fcontext fctx = {
	    .fc_fd = fd,
	    .fc_path_remaining = NULL,
	    .fc_action = VFS_NONE,
	    .fc_iostruct = (void*)(uintptr_t)offset,
	    .fc_task = current_task(),
	};

	// 获取文件对象
	int32 ret = MONKEY_WITH_ACTION(fd_monkey, &fctx, FD_OPEN, 0);
	if (ret < 0) {
		fcontext_cleanup(&fctx);
		return ret;
	}

	struct file* file = fctx.fc_file;
	loff_t new_pos;

	// 检查是否需要特殊 seek 处理
	if (file->f_flags & F_SPECIAL_SEEK) {
		// 将操作转发到 inode 处理
		ret = MONKEY_WITH_ACTION(inode_monkey, &fctx, INODE_LSEEK, whence);
		if (ret >= 0) {
			new_pos = ret;
		} else {
			fcontext_cleanup(&fctx);
			return ret; // 错误代码
		}
	} else {
		// 使用通用实现
		spinlock_lock(&file->f_lock);

		switch (whence) {
		case SEEK_SET:
			new_pos = offset;
			break;
		case SEEK_CUR:
			new_pos = file->f_pos + offset;
			break;
		case SEEK_END:
			new_pos = file->f_inode->i_size + offset;
			break;
		default:
			spinlock_unlock(&file->f_lock);
			fcontext_cleanup(&fctx);
			return -EINVAL;
		}

		if (new_pos < 0) {
			spinlock_unlock(&file->f_lock);
			fcontext_cleanup(&fctx);
			return -EINVAL;
		}

		file->f_pos = new_pos;
		spinlock_unlock(&file->f_lock);
	}

	fcontext_cleanup(&fctx);
	return new_pos;
}

int64 do_mount(const char* ksource, const char* ktarget, const char* kfstype, uint64 flags, const void* kdata) {
	int32 ret;

	// Look up filesystem type
	struct fstype* type = fstype_lookup(kfstype);
	if (!type) return -ENODEV;

	// Create a single context for mount operation
	struct fcontext mount_ctx = {
	    .path_string = ktarget,       // Target path
	    .fc_path_remaining = ktarget, // For path resolution
	    .fc_action = VFS_NONE, // Will be set later
	    .user_flags = flags,            // Mount flags
	    .user_buf = (void*)ksource,  // Source path
	    .fc_iostruct = (void*)kdata,  // Mount options
	    .fc_task = current_task(),
	    .fc_fstype = type,
	};

	// Resolve the mount point path
	ret = MONKEY_WITH_ACTION(path_monkey, &mount_ctx, PATH_LOOKUP, 0);
	if (ret < 0) {
		fcontext_cleanup(&mount_ctx);
		return ret;
	}

	// Perform the mount operation
	ret = MONKEY_WITH_ACTION(type->fs_monkey, &mount_ctx, FS_MOUNT, 0);

	fcontext_cleanup(&mount_ctx);
	return ret;
}

int64 do_read(int32 fd, void* kbuf, size_t count) {

	/* Set up file context for read operation */
	struct fcontext fctx = {
	    .fc_fd = fd,                  // File descriptor to read from
	    .fc_path_remaining = NULL,    // No path needed for fd operation
	    .user_flags = 0,                // No special flags needed
	    .fc_action = VFS_READ, // Read operation
	    .user_buf = kbuf,            // User buffer to read into
	    .user_buf_size = count,      // Number of bytes to read
	    .fc_task = current_task(),    // Current task
	};

	/* Send to fd_monkey to convert fd to file */
	int32 ret = MONKEY_WITH_ACTION(fd_monkey, &fctx, FD_OPEN, 0);
	if (ret < 0) {
		fcontext_cleanup(&fctx);
		return ret;
	}

	/* Send to inode_monkey to perform the actual read */
	ret = MONKEY_WITH_ACTION(inode_monkey, &fctx, INODE_READ, 0);

	/* Clean up and return bytes read or error code */
	fcontext_cleanup(&fctx);
	return ret;
}

int64 do_close(int32 fd) {
	struct fcontext fctx = {
	    .fc_fd = fd,                   // 要关闭的文件描述符
	    .fc_path_remaining = NULL,     // 不需要路径
	    .user_flags = 0,                 // 不需要特殊标志
	    .fc_action = VFS_CLOSE, // 关闭操作
	    .fc_task = current_task(),     // 当前任务
	};
	int32 ret = MONKEY_WITH_ACTION(fd_monkey, &fctx, FD_CLOSE, 0);
	fcontext_cleanup(&fctx);

	return ret;
}

int64 do_open(const char* pathname, int32 flags, mode_t mode) {
	if (!pathname) return -EFAULT;

	/* Copy pathname from user space */
	char* kpathname = kmalloc(PATH_MAX);
	if (!kpathname) return -ENOMEM;

	if (copy_from_user(kpathname, pathname, PATH_MAX)) {
		kfree(kpathname);
		return -EFAULT;
	}
	return do_open(kpathname, flags, mode);
	struct fcontext fctx = {
	    .path_string = kpathname,
	    .fc_path_remaining = kpathname,
	    .fc_fd = -1,
	    .user_flags = flags, // 操作行为
	    .user_mode = mode,   // 创建权限
	    .fc_action = VFS_OPEN,
	    .fc_task = current_task(),
	};

	int32 ret = vfs_monkey(&fctx);
	fcontext_cleanup(&fctx);

	kfree(kpathname);
	return ret;
}
