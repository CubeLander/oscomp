#include <kernel/mmu.h>
#include <kernel/sched.h>
#include <kernel/sprint.h>
#include <kernel/syscall/syscall.h>
#include <kernel/types.h>
#include <kernel/util/string.h>
#include <kernel/vfs.h>

/**
 * do_removexattr - Remove an extended attribute, specified by path or file descriptor
 * @path: Path to the file (used if fd is negative)
 * @fd: File descriptor (used if non-negative, ignoring path)
 * @name: Name of the extended attribute to remove
 * @lookup_flags: Path lookup flags (for path-based access only)
 *
 * Removes an extended attribute from a file.
 *
 * Returns: 0 on success, negative error code on failure
 */
int32 do_removexattr(const char* path, int fd, const char* name, int lookup_flags) {
	struct fcontext fctx = {0};
	int32 ret;

	/* Validate common parameters */
	if (!name) return -EINVAL;

	/* Set up common fields in the context */
	fctx.fc_action = VFS_ACTION_REMOVEXATTR;
	fctx.fc_task = current_task();

	/* Set up name in string context */
	fctx.fc_charbuf = (char*)name;
	fctx.fc_strlen = strlen(name);
	fctx.fc_hash = full_name_hash(name, fctx.fc_strlen);

	/* Choose between fd-based or path-based access */
	if (fd >= 0) {
		/* Using file descriptor */
		fctx.fc_fd = fd;

		/* Get the file from the file descriptor */
		ret = MONKEY_WITH_ACTION(fd_monkey, &fctx, FD_ACTION_OPEN, 0);
		if (ret < 0) {
			fcontext_cleanup(&fctx);
			return ret;
		}
	} else if (path && *path) {
		/* Using path */
		fctx.user_string = path;
		fctx.fc_path_remaining = (char*)path;
		fctx.fc_action_flags = lookup_flags;

		/* Resolve the path */
		ret = MONKEY_WITH_ACTION(path_monkey, &fctx, PATH_ACTION_LOOKUP, lookup_flags);
		if (ret < 0) {
			fcontext_cleanup(&fctx);
			return ret;
		}

		/* Verify the resolved path is valid */
		if (!fctx.fc_dentry || !fctx.fc_dentry->d_inode) {
			fcontext_cleanup(&fctx);
			return -ENOENT;
		}
	} else {
		/* Neither valid fd nor path provided */
		return -EINVAL;
	}

	/* Perform the xattr operation on the inode */
	ret = MONKEY_WITH_ACTION(inode_monkey, &fctx, INODE_ACTION_REMOVEXATTR, 0);

	/* Clean up and return the result */
	fcontext_cleanup(&fctx);
	return ret;
}

/**
 * do_listxattr - List extended attributes, specified by path or file descriptor
 * @path: Path to the file (used if fd is negative)
 * @fd: File descriptor (used if non-negative, ignoring path)
 * @list: Buffer to store the list of attribute names
 * @size: Size of the buffer
 * @lookup_flags: Path lookup flags (for path-based access only)
 *
 * Returns the list of extended attribute names associated with a file.
 * The names are null-terminated strings packed into a single buffer.
 *
 * Returns: Size of the attribute name list on success (or required buffer size if list is NULL),
 *          negative error code on failure
 */
ssize_t do_listxattr(const char* path, int fd, char* list, size_t size, int lookup_flags) {
	struct fcontext fctx = {0};
	int32 ret;

	/* Set up common fields in the context */
	fctx.user_buf = list;
	fctx.user_buf_size = size;
	fctx.fc_action = VFS_ACTION_LISTXATTR;
	fctx.fc_task = current_task();

	/* Choose between fd-based or path-based access */
	if (fd >= 0) {
		/* Using file descriptor */
		fctx.fc_fd = fd;

		/* Get the file from the file descriptor */
		ret = MONKEY_WITH_ACTION(fd_monkey, &fctx, FD_ACTION_OPEN, 0);
		if (ret < 0) {
			fcontext_cleanup(&fctx);
			return ret;
		}
	} else if (path && *path) {
		/* Using path */
		fctx.user_string = path;
		fctx.fc_path_remaining = (char*)path;
		fctx.fc_action_flags = lookup_flags;

		/* Resolve the path */
		ret = MONKEY_WITH_ACTION(path_monkey, &fctx, PATH_ACTION_LOOKUP, lookup_flags);
		if (ret < 0) {
			fcontext_cleanup(&fctx);
			return ret;
		}

		/* Verify the resolved path is valid */
		if (!fctx.fc_dentry || !fctx.fc_dentry->d_inode) {
			fcontext_cleanup(&fctx);
			return -ENOENT;
		}
	} else {
		/* Neither valid fd nor path provided */
		return -EINVAL;
	}

	/* Perform the xattr operation on the inode */
	ret = MONKEY_WITH_ACTION(inode_monkey, &fctx, INODE_ACTION_LISTXATTR, 0);

	/* Clean up and return the result */
	fcontext_cleanup(&fctx);
	return ret;
}

/**
 * do_getxattr - Get an extended attribute from a file, specified by path or file descriptor
 * @path: Path to the file (used if fd is negative)
 * @fd: File descriptor (used if non-negative, ignoring path)
 * @name: Name of the extended attribute
 * @value: Buffer to store the attribute value
 * @size: Size of the buffer
 * @lookup_flags: Path lookup flags (for path-based access only)
 *
 * This unified function handles retrieving extended attributes for both
 * path-based access (getxattr, lgetxattr) and file descriptor-based
 * access (fgetxattr). It chooses the appropriate method based on whether
 * a valid file descriptor is provided.
 *
 * Returns: Size of the attribute value on success (or required buffer size if value is NULL),
 *          negative error code on failure
 */
ssize_t do_getxattr(const char* path, int fd, const char* name, void* value, size_t size, int lookup_flags) {
	struct fcontext fctx = {0};
	int32 ret;

	/* Validate common parameters */
	if (!name) return -EINVAL;

	/* Set up common fields in the context */
	fctx.user_buf = value;
	fctx.user_buf_size = size;
	fctx.fc_action = VFS_ACTION_GETXATTR;
	fctx.fc_task = current_task();

	/* Set up name in string context */
	fctx.fc_charbuf = (char*)name;
	fctx.fc_strlen = strlen(name);
	fctx.fc_hash = full_name_hash(name, fctx.fc_strlen);

	/* Choose between fd-based or path-based access */
	if (fd >= 0) {
		/* Using file descriptor */
		fctx.fc_fd = fd;

		/* Get the file from the file descriptor */
		ret = MONKEY_WITH_ACTION(fd_monkey, &fctx, FD_ACTION_OPEN, 0);
		if (ret < 0) {
			fcontext_cleanup(&fctx);
			return ret;
		}
	} else if (path && *path) {
		/* Using path */
		fctx.user_string = path;
		fctx.fc_path_remaining = (char*)path;
		fctx.fc_action_flags = lookup_flags;

		/* Resolve the path */
		ret = MONKEY_WITH_ACTION(path_monkey, &fctx, PATH_ACTION_LOOKUP, lookup_flags);
		if (ret < 0) {
			fcontext_cleanup(&fctx);
			return ret;
		}

		/* Verify the resolved path is valid */
		if (!fctx.fc_dentry || !fctx.fc_dentry->d_inode) {
			fcontext_cleanup(&fctx);
			return -ENOENT;
		}
	} else {
		/* Neither valid fd nor path provided */
		return -EINVAL;
	}

	/* Perform the xattr operation on the inode */
	ret = MONKEY_WITH_ACTION(inode_monkey, &fctx, INODE_ACTION_GETXATTR, 0);

	/* Clean up and return the result */
	fcontext_cleanup(&fctx);
	return ret;
}

/**
 * do_setxattr - Set an extended attribute on a file, specified by path or file descriptor
 * @path: Path to the file (used if fd is negative)
 * @fd: File descriptor (used if non-negative, ignoring path)
 * @name: Name of the extended attribute
 * @value: Value to set
 * @size: Size of the value
 * @flags: Control flags (XATTR_CREATE, XATTR_REPLACE)
 * @lookup_flags: Path lookup flags (for path-based access only)
 *
 * This unified function handles setting extended attributes for both
 * path-based access (setxattr, lsetxattr) and file descriptor-based
 * access (fsetxattr). It chooses the appropriate method based on whether
 * a valid file descriptor is provided.
 *
 * Returns: 0 on success, negative error code on failure
 */
int64 do_setxattr(const char* path, int fd, const char* name, const void* value, size_t size, int flags, int lookup_flags) {
	struct fcontext fctx = {0};
	int32 ret;

	/* Validate common parameters */
	if (!name || !value) return -EINVAL;

	/* Set up common fields in the context */
	fctx.user_buf = (void*)value;
	fctx.user_buf_size = size;
	fctx.fc_action = VFS_ACTION_SETXATTR;
	fctx.user_flags = flags;
	fctx.fc_task = current_task();

	/* Set up name in string context */
	fctx.fc_charbuf = (char*)name;
	fctx.fc_strlen = strlen(name);
	fctx.fc_hash = full_name_hash(name, fctx.fc_strlen);

	/* Choose between fd-based or path-based access */
	if (fd >= 0) {
		/* Using file descriptor */
		fctx.fc_fd = fd;

		/* Get the file from the file descriptor */
		ret = MONKEY_WITH_ACTION(fd_monkey, &fctx, FD_ACTION_OPEN, 0);
		if (ret < 0) {
			fcontext_cleanup(&fctx);
			return ret;
		}
	} else if (path && *path) {
		/* Using path */
		fctx.user_string = path;
		fctx.fc_path_remaining = (char*)path;
		fctx.fc_action_flags = lookup_flags;

		/* Resolve the path */
		ret = MONKEY_WITH_ACTION(path_monkey, &fctx, PATH_ACTION_LOOKUP, lookup_flags);
		if (ret < 0) {
			fcontext_cleanup(&fctx);
			return ret;
		}

		/* Verify the resolved path is valid */
		if (!fctx.fc_dentry || !fctx.fc_dentry->d_inode) {
			fcontext_cleanup(&fctx);
			return -ENOENT;
		}
	} else {
		fcontext_cleanup(&fctx);
		/* Neither valid fd nor path provided */
		return -EINVAL;
	}

	/* Perform the xattr operation on the inode */
	ret = MONKEY_WITH_ACTION(inode_monkey, &fctx, INODE_ACTION_SETXATTR, 0);

	/* Clean up and return the result */
	fcontext_cleanup(&fctx);
	return ret;
}