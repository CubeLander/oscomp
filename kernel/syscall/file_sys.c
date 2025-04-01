#include <kernel/mm/kmalloc.h>
#include <kernel/mm/uaccess.h>
#include <kernel/sched/process.h>
#include <kernel/sprint.h>
#include <kernel/syscall/syscall.h>
#include <kernel/types.h>
#include <kernel/util/string.h>
#include <kernel/vfs.h>






/* lseek syscall implementation */
int64 sys_lseek(int32 fd, off_t offset, int32 whence) {
	/* Simple arguments, no memory allocation needed */
	return do_lseek(fd, offset, whence);
}

/* Implementations of actual syscall handlers follow */

int64 sys_open(const char* pathname, int32 flags, mode_t mode) {
	if (!pathname) return -EFAULT;

	/* Copy pathname from user space */
	char* kpathname = kmalloc(PATH_MAX);
	if (!kpathname) return -ENOMEM;

	if (copy_from_user(kpathname, pathname, PATH_MAX)) {
		kfree(kpathname);
		return -EFAULT;
	}
	return do_open(kpathname, flags, mode);
}

int64 sys_close(int32 fd) {
	if (fd < 0) return -EBADF; // 无效的文件描述符
	return do_close(fd);
}

/* Add the rest of your syscall implementations here */

int64 sys_read(int32 fd, void* buf, size_t count) {
	if (fd < 0) return -EBADF; // Invalid file descriptor

	if (!buf || count == 0) return 0; // Nothing to read

	/* 验证用户空间缓冲区是否有效 */
	if (!access_ok(buf, count)) return -EFAULT;

	/* 在内核空间分配临时缓冲区 */
	void* kbuf = kmalloc(count);
	if (!kbuf) return -ENOMEM;
	return do_read(fd, kbuf, count);
}

int64 sys_write(int32 fd, const void* buf, size_t count) {
	if (fd < 0) return -EBADF; // Invalid file descriptor

	if (!buf || count == 0) return 0; // Nothing to write

	/* 验证用户空间缓冲区是否有效 */
	if (!access_ok(buf, count)) return -EFAULT;

	/* 在内核空间分配临时缓冲区 */
	void* kbuf = kmalloc(count);
	if (!kbuf) return -ENOMEM;

	/* Set up file context for write operation */
	struct fcontext fctx = {
	    .fc_fd = fd,                   // File descriptor to write to
	    .fc_path_remaining = NULL,     // No path needed for fd operation
	    .user_flags = 0,                 // No special flags needed
	    .fc_action = VFS_ACTION_WRITE, // Write operation
	    .user_buf = (void*)kbuf,      // User buffer to write from
	    .user_buf_size = count,       // Number of bytes to write
	    .fc_task = current_task(),     // Current task
	};

	/* Send to fd_monkey to convert fd to file */
	int32 ret = MONKEY_WITH_ACTION(fd_monkey, &fctx, FD_ACTION_OPEN, 0);
	if (ret < 0) {
		fcontext_cleanup(&fctx);
		return ret;
	}

	/* Send to inode_monkey to perform the actual write */
	ret = MONKEY_WITH_ACTION(inode_monkey, &fctx, INODE_ACTION_WRITE, 0);

	/* Clean up and return bytes written or error code */
	fcontext_cleanup(&fctx);
	return ret;
}