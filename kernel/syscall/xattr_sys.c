#include <kernel/mmu.h>
#include <kernel/sched.h>
#include <kernel/sprint.h>
#include <kernel/syscall/syscall.h>
#include <kernel/types.h>
#include <kernel/util/string.h>
#include <kernel/vfs.h>

#define XATTR_NAME_MAX 255 // Maximum length of an extended attribute name

/**
 * Implementation of sys_fremovexattr syscall
 * Removes an extended attribute using a file descriptor
 */
int32 sys_fremovexattr(int fd, const char* name) {
	char* kname = kmalloc(XATTR_NAME_MAX);

	if (!kname) { return -ENOMEM; }

	if (copy_from_user(kname, name, XATTR_NAME_MAX)) {
		kfree(kname);
		return -EFAULT;
	}

	/* Call the internal implementation with the file descriptor */
	int32 ret = do_removexattr(NULL, fd, kname, 0);

	/* Clean up and return */
	kfree(kname);
	return ret;
}

/**
 * Implementation of sys_lremovexattr syscall
 * Removes an extended attribute, not following symbolic links
 */
int32 sys_lremovexattr(const char* path, const char* name) {
	char* kpath = kmalloc(PATH_MAX);
	char* kname = kmalloc(XATTR_NAME_MAX);

	if (!kpath || !kname) {
		if (kpath) kfree(kpath);
		if (kname) kfree(kname);
		return -ENOMEM;
	}

	if (copy_from_user(kpath, path, PATH_MAX) || copy_from_user(kname, name, XATTR_NAME_MAX)) {
		kfree(kpath);
		kfree(kname);
		return -EFAULT;
	}

	/* Call the internal implementation with 0 to avoid following symlinks */
	int32 ret = do_removexattr(kpath, -1, kname, 0);

	/* Clean up and return */
	kfree(kpath);
	kfree(kname);
	return ret;
}

/**
 * Implementation of sys_removexattr syscall
 * Removes an extended attribute, following symbolic links
 */
int32 sys_removexattr(const char* path, const char* name) {
	char* kpath = kmalloc(PATH_MAX);
	char* kname = kmalloc(XATTR_NAME_MAX);

	if (!kpath || !kname) {
		if (kpath) kfree(kpath);
		if (kname) kfree(kname);
		return -ENOMEM;
	}

	if (copy_from_user(kpath, path, PATH_MAX) || copy_from_user(kname, name, XATTR_NAME_MAX)) {
		kfree(kpath);
		kfree(kname);
		return -EFAULT;
	}

	/* Call the internal implementation with LOOKUP_FOLLOW to follow symlinks */
	int32 ret = do_removexattr(kpath, -1, kname, LOOKUP_FOLLOW);

	/* Clean up and return */
	kfree(kpath);
	kfree(kname);
	return ret;
}

/**
 * Implementation of sys_flistxattr syscall
 * Lists extended attributes using a file descriptor
 */
ssize_t sys_flistxattr(int fd, char* list, size_t size) {
	char* klist = NULL;

	/* Allocate list buffer if needed */
	if (list && size > 0) {
		klist = kmalloc(size);
		if (!klist) { return -ENOMEM; }
	}

	/* Call the internal implementation with the file descriptor */
	ssize_t ret = do_listxattr(NULL, fd, klist, size, 0);

	/* Copy data back to user if successful */
	if (ret > 0 && list && klist) {
		if (copy_to_user(list, klist, ret < size ? ret : size)) { ret = -EFAULT; }
	}

	/* Clean up and return */
	if (klist) kfree(klist);
	return ret;
}

/**
 * Implementation of sys_llistxattr syscall
 * Lists extended attributes, not following symbolic links
 */
ssize_t sys_llistxattr(const char* path, char* list, size_t size) {
	char* kpath = kmalloc(PATH_MAX);
	char* klist = NULL;

	if (!kpath) return -ENOMEM;

	if (copy_from_user(kpath, path, PATH_MAX)) {
		kfree(kpath);
		return -EFAULT;
	}

	/* Allocate list buffer if needed */
	if (list && size > 0) {
		klist = kmalloc(size);
		if (!klist) {
			kfree(kpath);
			return -ENOMEM;
		}
	}

	/* Call the internal implementation with 0 to avoid following symlinks */
	ssize_t ret = do_listxattr(kpath, -1, klist, size, 0);

	/* Copy data back to user if successful */
	if (ret > 0 && list && klist) {
		if (copy_to_user(list, klist, ret < size ? ret : size)) { ret = -EFAULT; }
	}

	/* Clean up and return */
	kfree(kpath);
	if (klist) kfree(klist);
	return ret;
}

/**
 * Implementation of sys_listxattr syscall
 * Lists extended attributes, following symbolic links
 */
ssize_t sys_listxattr(const char* path, char* list, size_t size) {
	char* kpath = kmalloc(PATH_MAX);
	char* klist = NULL;

	if (!kpath) return -ENOMEM;

	if (copy_from_user(kpath, path, PATH_MAX)) {
		kfree(kpath);
		return -EFAULT;
	}

	/* Allocate list buffer if needed */
	if (list && size > 0) {
		klist = kmalloc(size);
		if (!klist) {
			kfree(kpath);
			return -ENOMEM;
		}
	}

	/* Call the internal implementation with LOOKUP_FOLLOW to follow symlinks */
	ssize_t ret = do_listxattr(kpath, -1, klist, size, LOOKUP_FOLLOW);

	/* Copy data back to user if successful */
	if (ret > 0 && list && klist) {
		if (copy_to_user(list, klist, ret < size ? ret : size)) { ret = -EFAULT; }
	}

	/* Clean up and return */
	kfree(kpath);
	if (klist) kfree(klist);
	return ret;
}

/**
 * Implementation of sys_setxattr syscall
 * Sets an extended attribute, following symbolic links
 */
int64 sys_setxattr(const char* path, const char* name, const void* value, size_t size, int flags) {
	/* Validate and copy user parameters */
	char* kpath = kmalloc(PATH_MAX);
	char* kname = kmalloc(XATTR_NAME_MAX);
	void* kvalue = kmalloc(size);

	if (!kpath || !kname || !kvalue) {
		if (kpath) kfree(kpath);
		if (kname) kfree(kname);
		if (kvalue) kfree(kvalue);
		return -ENOMEM;
	}

	if (copy_from_user(kpath, path, PATH_MAX) || copy_from_user(kname, name, XATTR_NAME_MAX) || copy_from_user(kvalue, value, size)) {
		kfree(kpath);
		kfree(kname);
		kfree(kvalue);
		return -EFAULT;
	}

	/* Call the internal implementation with LOOKUP_FOLLOW to follow symlinks */
	int64 ret = do_setxattr(kpath, -1, kname, kvalue, size, flags, LOOKUP_FOLLOW);

	/* Clean up and return */
	kfree(kpath);
	kfree(kname);
	kfree(kvalue);
	return ret;
}

/**
 * Implementation of sys_lsetxattr syscall
 * Sets an extended attribute, not following symbolic links
 */
int64 sys_lsetxattr(const char* path, const char* name, const void* value, size_t size, int flags) {
	/* Validate and copy user parameters */
	char* kpath = kmalloc(PATH_MAX);
	char* kname = kmalloc(XATTR_NAME_MAX);
	void* kvalue = kmalloc(size);

	if (!kpath || !kname || !kvalue) {
		if (kpath) kfree(kpath);
		if (kname) kfree(kname);
		if (kvalue) kfree(kvalue);
		return -ENOMEM;
	}

	if (copy_from_user(kpath, path, PATH_MAX) || copy_from_user(kname, name, XATTR_NAME_MAX) || copy_from_user(kvalue, value, size)) {
		kfree(kpath);
		kfree(kname);
		kfree(kvalue);
		return -EFAULT;
	}

	/* Call the internal implementation with 0 to avoid following symlinks */
	int64 ret = do_setxattr(kpath, -1, kname, kvalue, size, flags, 0);

	/* Clean up and return */
	kfree(kpath);
	kfree(kname);
	kfree(kvalue);
	return ret;
}

/**
 * Implementation of sys_fsetxattr syscall
 * Sets an extended attribute using a file descriptor
 */
int64 sys_fsetxattr(int fd, const char* name, const void* value, size_t size, int flags) {
	/* Validate and copy user parameters */
	char* kname = kmalloc(XATTR_NAME_MAX);
	void* kvalue = kmalloc(size);

	if (!kname || !kvalue) {
		if (kname) kfree(kname);
		if (kvalue) kfree(kvalue);
		return -ENOMEM;
	}

	if (copy_from_user(kname, name, XATTR_NAME_MAX) || copy_from_user(kvalue, value, size)) {
		kfree(kname);
		kfree(kvalue);
		return -EFAULT;
	}

	/* Call the internal implementation with the file descriptor */
	int64 ret = do_setxattr(NULL, fd, kname, kvalue, size, flags, 0);

	/* Clean up and return */
	kfree(kname);
	kfree(kvalue);
	return ret;
}
