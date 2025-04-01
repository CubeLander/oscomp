#include <kernel/mm/kmalloc.h>
#include <kernel/mm/uaccess.h>
#include <kernel/sched/process.h>
#include <kernel/sprint.h>
#include <kernel/syscall/syscall.h>
#include <kernel/types.h>
#include <kernel/util/string.h>
#include <kernel/vfs.h>

/* Debug flag to enable syscall tracing */
#define SYSCALL_DEBUG 0

/* Wrapper functions for each syscall type */

/* File wrappers */
static int64 open_wrapper(int64 pathname, int64 flags, int64 mode, int64 a3, int64 a4, int64 a5) { return sys_open((const char*)pathname, (int32)flags, (mode_t)mode); }

static int64 close_wrapper(int64 fd, int64 a1, int64 a2, int64 a3, int64 a4, int64 a5) { return sys_close((int32)fd); }

static int64 read_wrapper(int64 fd, int64 buf, int64 count, int64 a3, int64 a4, int64 a5) { return sys_read((int32)fd, (void*)buf, (size_t)count); }

static int64 write_wrapper(int64 fd, int64 buf, int64 count, int64 a3, int64 a4, int64 a5) { return sys_write((int32)fd, (const void*)buf, (size_t)count); }

static int64 lseek_wrapper(int64 fd, int64 offset, int64 whence, int64 a3, int64 a4, int64 a5) { return sys_lseek((int32)fd, (off_t)offset, (int32)whence); }

static int64 mount_wrapper(int64 source, int64 target, int64 fstype, int64 flags, int64 data, int64 a5) {
	return sys_mount((const char*)source, (const char*)target, (const char*)fstype, (uint64)flags, (const void*)data);
}

/* Process wrappers */
static int64 exit_wrapper(int64 status, int64 a1, int64 a2, int64 a3, int64 a4, int64 a5) { return sys_exit((int32)status); }

static int64 getpid_wrapper(int64 a0, int64 a1, int64 a2, int64 a3, int64 a4, int64 a5) { return sys_getpid(); }

static int64 clone_wrapper(int64 flags, int64 stack, int64 ptid, int64 tls, int64 ctid, int64 a5) { return sys_clone((uint64)flags, (uint64)stack, (uint64)ptid, (uint64)tls, (uint64)ctid); }

/* Memory wrappers */
static int64 mmap_wrapper(int64 addr, int64 length, int64 prot, int64 flags, int64 fd, int64 offset) {
	return sys_mmap((void*)addr, (size_t)length, (int32)prot, (int32)flags, (int32)fd, (off_t)offset);
}

/* Time wrappers */
static int64 time_wrapper(int64 tloc, int64 a1, int64 a2, int64 a3, int64 a4, int64 a5) { return sys_time((time_t*)tloc); }

/* Complete syscall table */
static struct syscall_entry syscall_table[] = {
    /* File operations */
    [SYS_open] = {open_wrapper, "open", 3},
    [SYS_close] = {close_wrapper, "close", 1},
    [SYS_read] = {read_wrapper, "read", 3},
    [SYS_write] = {write_wrapper, "write", 3},
    [SYS_lseek] = {lseek_wrapper, "lseek", 3},
    [SYS_mount] = {mount_wrapper, "mount", 5},

    /* Process operations */
    [SYS_exit] = {exit_wrapper, "exit", 1},
    [SYS_getpid] = {getpid_wrapper, "getpid", 0},
    [SYS_getppid] = {NULL, "getppid", 0}, // Not implemented yet
    [SYS_clone] = {clone_wrapper, "clone", 5},

    /* Memory operations */
    [SYS_mmap] = {mmap_wrapper, "mmap", 6},
    [SYS_brk] = {NULL, "brk", 1}, // Not implemented yet

    /* Time operations */
    [SYS_time] = {time_wrapper, "time", 1},

    /* Add more syscalls as needed */
};

#define SYSCALL_TABLE_SIZE (sizeof(syscall_table) / sizeof(syscall_table[0]))

/**
 * The main syscall dispatcher
 */
int64 do_syscall(int64 syscall_num, int64 a0, int64 a1, int64 a2, int64 a3, int64 a4, int64 a5) {

	/* Validate syscall number */
	if (syscall_num < 0 || syscall_num >= SYSCALL_TABLE_SIZE || !syscall_table[syscall_num].func) {
		sprint("Invalid syscall: %ld\n", syscall_num);
		return -ENOSYS;
	}

	struct syscall_entry* entry = &syscall_table[syscall_num];

#if SYSCALL_DEBUG
	/* Debug output before syscall */
	sprint("SYSCALL: %s(%ld, %ld, ...)\n", entry->name, a0, a1);
#endif

	/* Execute syscall through wrapper */
	int64 ret = entry->func(a0, a1, a2, a3, a4, a5);

#if SYSCALL_DEBUG
	/* Debug output after syscall */
	sprint("SYSCALL: %s returned %ld\n", entry->name, ret);
#endif

	return ret;
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
	struct fcontext fctx = {
	    .fc_filename = kpathname,
	    .fc_path_remaining = kpathname,
	    .fc_fd = -1,
	    .fc_flags = flags, // 操作行为
	    .fc_mode = mode,   // 创建权限
	    .fc_action = VFS_ACTION_OPEN,
	    .fc_task = current_task(),
	};

	int32 ret = vfs_monkey(&fctx);
	fcontext_cleanup(&fctx);

	kfree(kpathname);
	return ret;
}

int64 sys_close(int32 fd) {
	if (fd < 0) return -EBADF; // 无效的文件描述符

	struct fcontext fctx = {
	    .fc_fd = fd,                   // 要关闭的文件描述符
	    .fc_path_remaining = NULL,     // 不需要路径
	    .fc_flags = 0,                 // 不需要特殊标志
	    .fc_action = VFS_ACTION_CLOSE, // 关闭操作
	    .fc_task = current_task(),     // 当前任务
	};
	int32 ret = MONKEY_WITH_ACTION(fd_monkey, &fctx, FD_ACTION_CLOSE, 0);
	fcontext_cleanup(&fctx);

	return ret;
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

	/* Set up file context for read operation */
	struct fcontext fctx = {
	    .fc_fd = fd,                  // File descriptor to read from
	    .fc_path_remaining = NULL,    // No path needed for fd operation
	    .fc_flags = 0,                // No special flags needed
	    .fc_action = VFS_ACTION_READ, // Read operation
	    .fc_buffer = kbuf,            // User buffer to read into
	    .fc_buffer_size = count,      // Number of bytes to read
	    .fc_task = current_task(),    // Current task
	};

	/* Send to fd_monkey to convert fd to file */
	int32 ret = MONKEY_WITH_ACTION(fd_monkey, &fctx, FD_ACTION_OPEN, 0);
	if (ret < 0) {
		fcontext_cleanup(&fctx);
		return ret;
	}

	/* Send to inode_monkey to perform the actual read */
	ret = MONKEY_WITH_ACTION(inode_monkey, &fctx, INODE_ACTION_READ, 0);

	/* Clean up and return bytes read or error code */
	fcontext_cleanup(&fctx);
	return ret;
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
	    .fc_flags = 0,                 // No special flags needed
	    .fc_action = VFS_ACTION_WRITE, // Write operation
	    .fc_buffer = (void*)kbuf,      // User buffer to write from
	    .fc_buffer_size = count,       // Number of bytes to write
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