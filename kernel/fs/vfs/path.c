#include <kernel/fs/vfs/vfs.h>
#include <kernel/types.h>

#include <kernel/sched/process.h>
#include <kernel/sched/sched.h>
#include <spike_interface/spike_utils.h>
#include <util/list.h>
#include <util/string.h>

static int vfs_path_lookup(struct dentry* base_dentry, struct vfsmount* base_mnt, const char* path_str, unsigned int flags, struct path* result);

/**
 * path_create - Look up a path from the current working directory
 * @name: Path to look up
 * @flags: Lookup flags
 * @result: Result path
 *
 * This is a wrapper around vfs_path_lookup that uses the current
 * working directory as the starting point.
 */
int path_create(const char* name, unsigned int flags, struct path* path) {
	int error;
	struct task_struct* current;
	struct dentry* start_dentry;
	struct vfsmount* start_mnt;

	/* Get current working directory (simplified) */
	start_dentry = CURRENT->fs->pwd.dentry;
	start_mnt = CURRENT->fs->pwd.mnt;

	/* Perform the lookup */
	error = vfs_path_lookup(start_dentry, start_mnt, name, flags, path);
	return error;
}

// Add this function to support qstr path lookups

/**
 * kern_path_qstr - Look up a path from qstr
 * @name: Path as qstr
 * @flags: Lookup flags
 * @result: Result path
 */
int kern_path_qstr(const struct qstr* name, unsigned int flags, struct path* result) {
	/* For the initial implementation, convert to string and use existing path_create */
	int ret;
	char* path_str;

	if (!name || !result)
		return -EINVAL;

	/* Convert qstr to char* */
	path_str = kmalloc(name->len + 1);
	if (!path_str)
		return -ENOMEM;

	memcpy(path_str, name->name, name->len);
	path_str[name->len] = '\0';

	/* Use existing path lookup */
	ret = path_create(path_str, flags, result);

	kfree(path_str);
	return ret;
}

/**
 * path_destroy - Release a reference to a path
 * @path: Path to release
 *
 * Decrements the reference counts for both the dentry and vfsmount
 * components of a path structure.
 */
void path_destroy(struct path* path) {
	if (!path)
		return;

	/* Release dentry reference */
	if (path->dentry)
		dentry_put(path->dentry);

	/* Release mount reference */
	if (path->mnt)
		put_mount(path->mnt);

	/* Clear the path structure */
	path->dentry = NULL;
	path->mnt = NULL;
}

/**
 * filename_lookup - Look up a filename relative to a directory file descriptor
 * @dfd: Directory file descriptor (or AT_FDCWD for current working directory)
 * @name: Filename to look up (simple string)
 * @flags: Lookup flags
 * @path: Output path result
 * @started: Output path indicating starting directory (can be NULL)
 *
 * This function handles looking up a filename relative to a directory
 * file descriptor, supporting the *at() family of system calls.
 *
 * Returns 0 on success, negative error code on failure.
 */
int filename_lookup(int dfd, const char* name, unsigned int flags, struct path* path, struct path* started) {
	struct task_struct* current;
	struct dentry* start_dentry;
	struct vfsmount* start_mnt;
	int error;

	/* Validate parameters */
	if (!name || !path)
		return -EINVAL;

	/* Check for absolute path */
	if (name[0] == '/') {
		/* Absolute path - always starts at root directory */
		current = CURRENT;
		start_dentry = current->fs->root.dentry;
		start_mnt = current->fs->root.mnt;
	} else {
		/* Relative path - get the starting directory */
		if (dfd == AT_FDCWD) {
			/* Use current working directory */
			current = CURRENT;
			start_dentry = current->fs->pwd.dentry;
			start_mnt = current->fs->pwd.mnt;
		} else {
			/* Use the directory referenced by the file descriptor */
			// struct file* file = get_file(dfd, CURRENT);
			struct file* file = fdtable_getFile(CURRENT->fdtable, dfd);

			if (!file)
				return -EBADF;

			/* Check if it's a directory */
			if (!S_ISDIR(file->f_inode->i_mode)) {
				file_put(file);
				return -ENOTDIR;
			}

			start_dentry = file->f_path.dentry;
			start_mnt = file->f_path.mnt;

			/* Take reference to starting path components */
			start_dentry = get_dentry(start_dentry);
			if (start_mnt)
				get_mount(start_mnt);

			file_put(file);
		}
	}

	/* Save the starting path if requested */
	if (started) {
		started->dentry = get_dentry(start_dentry);
		started->mnt = start_mnt ? get_mount(start_mnt) : NULL;
	}

	/* Do the actual lookup */
	error = vfs_path_lookup(start_dentry, start_mnt, name, flags, path);

	/* Release references to starting directory */
	dentry_put(start_dentry);
	if (start_mnt)
		put_mount(start_mnt);

	return error;
}

/**
 * vfs_path_lookup - Look up a path relative to a dentry/mount pair
 * @base_dentry: Starting dentry
 * @base_mnt: Starting vfsmount
 * @path_str: Path string to look up
 * @flags: Lookup flags (LOOKUP_*)
 * @result: Result path (output)
 *
 * This function resolves a path string to a dentry/vfsmount pair.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int vfs_path_lookup(struct dentry* base_dentry, struct vfsmount* base_mnt, const char* path_str, unsigned int flags, struct path* result) {
	struct dentry* dentry;
	struct vfsmount* mnt;
	char* component;
	char* path_copy;
	char* next_slash;
	int len;

	/* Validate parameters */
	if (!base_dentry || !path_str || !result)
		return -EINVAL;

	/* Initialize with starting point */
	dentry = get_dentry(base_dentry);
	mnt = base_mnt ? get_mount(base_mnt) : NULL;

	/* Handle absolute paths - start from root */
	if (path_str[0] == '/') {
		/* Use filesystem root */
		if (mnt) {
			dentry_put(dentry);
			dentry = get_dentry(mnt->mnt_root);
		}
		/* Skip leading slash */
		path_str++;
	}

	/* Empty path means current directory */
	if (!*path_str) {
		result->dentry = dentry;
		result->mnt = mnt;
		return 0;
	}

	/* Make a copy of the path so we can modify it */
	path_copy = kmalloc(strlen(path_str) + 1);
	if (!path_copy) {
		dentry_put(dentry);
		if (mnt)
			put_mount(mnt);
		return -ENOMEM;
	}

	strcpy(path_copy, path_str);
	component = path_copy;

	/* Walk the path component by component */
	while (*component) {
		/* Find the next slash or end of string */
		next_slash = strchr(component, '/');
		if (next_slash) {
			*next_slash = '\0';
			next_slash++;
		} else {
			next_slash = component + strlen(component);
		}

		len = strlen(component);

		/* Handle "." - current directory */
		if (len == 1 && component[0] == '.') {
			component = next_slash;
			continue;
		}

		/* Handle ".." - parent directory */
		if (len == 2 && component[0] == '.' && component[1] == '.') {
			/* Check if we're at a mount point */
			if (mnt && dentry == mnt->mnt_root) {
				/* Go to the parent mount */
				struct vfsmount* parent_mnt = mnt->mnt_parent;
				struct dentry* mountpoint = mnt->mnt_mountpoint;

				if (parent_mnt && parent_mnt != mnt) {
					/* Cross mount boundary upward */
					dentry_put(dentry);
					put_mount(mnt);

					mnt = get_mount(parent_mnt);
					dentry = get_dentry(mountpoint);

					/* Now go to parent of the mountpoint */
					struct dentry* parent = dentry->d_parent;
					if (parent) {
						dentry_put(dentry);
						dentry = get_dentry(parent);
					}
				} else {
					/* We're at the root of the root filesystem */
					/* Stay at the root */
				}
			} else {
				/* Regular parent dentry */
				struct dentry* parent = dentry->d_parent;
				if (parent) {
					dentry_put(dentry);
					dentry = get_dentry(parent);
				}
			}

			component = next_slash;
			continue;
		}

		/* Skip empty components */
		if (len == 0) {
			component = next_slash;
			continue;
		}

		/* Lookup the next component in the current directory */
		struct dentry* next = NULL;
		struct qstr qname;
		qname.name = component;
		qname.len = len;
		qname.hash = full_name_hash(component, len);

		/* First try to find in dentry cache */
		next = dentry_locate(dentry, &qname, -1, true, false);

		if (!next) {
			/* Not in cache, ask the filesystem */
			if (!dentry->d_inode || !dentry->d_inode->i_op || !dentry->d_inode->i_op->lookup) {
				dentry_put(dentry);
				if (mnt)
					put_mount(mnt);
				kfree(path_copy);
				return -ENOTDIR;
			}

			/* Create a negative dentry for the lookup */
			next = dentry_locate(dentry, &qname, -1, false, true);
			if (!next) {
				dentry_put(dentry);
				if (mnt)
					put_mount(mnt);
				kfree(path_copy);
				return -ENOMEM;
			}

			/* Call the filesystem's lookup method */
			int error = dentry_instantiate(next, dentry->d_inode->i_op->lookup(dentry->d_inode, next, 0));
			if (error) {
				dentry_put(next);
				dentry_put(dentry);
				if (mnt)
					put_mount(mnt);
				kfree(path_copy);
				return error;
			}
		}

		/* Release the parent dentry */
		dentry_put(dentry);
		dentry = next;

		/* Check if this is a mount point */
		if (flags & LOOKUP_AUTOMOUNT && dentry_isMountpoint(dentry)) {
			/* Find the mount for this mountpoint */
			struct vfsmount* mounted = lookup_vfsmount(dentry);
			if (mounted) {
				/* Cross mount point downward */
				if (mnt)
					put_mount(mnt);
				mnt = mounted; /* already has incremented ref count */

				/* Switch to the root of the mounted filesystem */
				struct dentry* mnt_root = get_dentry(mounted->mnt_root);
				dentry_put(dentry);
				dentry = mnt_root;
			}
		}

		/* Handle symbolic links if needed */
		if (flags & LOOKUP_FOLLOW && dentry->d_inode && S_ISLNK(dentry->d_inode->i_mode)) {
			/* Implement symlink resolution */
			struct dentry* link_target = dentry_follow_link(dentry);
			if (IS_ERR(link_target)) {
				dentry_put(dentry);
				if (mnt)
					put_mount(mnt);
				kfree(path_copy);
				return PTR_ERR(link_target);
			}

			dentry_put(dentry);
			dentry = link_target;
		}

		/* Move to the next component */
		component = next_slash;
	}

	/* Set the result */
	result->dentry = dentry;
	result->mnt = mnt;

	kfree(path_copy);
	return 0;
}