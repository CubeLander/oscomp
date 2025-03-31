#include <kernel/mmu.h>
#include <kernel/sched.h>
#include <kernel/sprint.h>
#include <kernel/types.h>
#include <kernel/util.h>
#include <kernel/vfs.h>

/**
 * path_create - Look up a path from the current working directory
 * @name: Path to look up
 * @flags: Lookup flags
 * @result: Result path
 *
 * This is a wrapper around vfs_path_lookup that uses the current
 * working directory as the starting point.
 */
int32 path_create(const char* name, uint32 flags, struct path* path) {
	int32 error;
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
int32 kern_path_qstr(const struct qstr* name, uint32 flags, struct path* result) {
	/* For the initial implementation, convert to string and use existing path_create */
	int32 ret;
	char* path_str;

	if (!name || !result) return -EINVAL;

	/* Convert qstr to char* */
	path_str = kmalloc(name->len + 1);
	if (!path_str) return -ENOMEM;

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
	if (!path) return;

	/* Release dentry reference */
	if (path->dentry) dentry_unref(path->dentry);

	/* Release mount reference */
	if (path->mnt) mount_unref(path->mnt);

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
int32 filename_lookup(int32 dfd, const char* name, uint32 flags, struct path* path, struct path* started) {
	struct task_struct* current;
	struct dentry* start_dentry;
	struct vfsmount* start_mnt;
	int32 error;

	/* Validate parameters */
	if (!name || !path) return -EINVAL;

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

			if (!file) return -EBADF;

			/* Check if it's a directory */
			if (!S_ISDIR(file->f_inode->i_mode)) {
				file_unref(file);
				return -ENOTDIR;
			}

			start_dentry = file->f_path.dentry;
			start_mnt = file->f_path.mnt;

			/* Take reference to starting path components */
			start_dentry = dentry_ref(start_dentry);
			if (start_mnt) mount_ref(start_mnt);

			file_unref(file);
		}
	}

	/* Save the starting path if requested */
	if (started) {
		started->dentry = dentry_ref(start_dentry);
		started->mnt = start_mnt ? mount_ref(start_mnt) : NULL;
	}

	/* Do the actual lookup */
	error = vfs_path_lookup(start_dentry, start_mnt, name, flags, path);

	/* Release references to starting directory */
	dentry_unref(start_dentry);
	if (start_mnt) mount_unref(start_mnt);

	return error;
}

/**
 * path_lookupMount - Find a mount for a given path
 */
struct vfsmount* path_lookupMount(struct path* path) {
	extern struct hashtable mount_hashtable;
	/* Look up in the mount hash table */
	struct list_node* node = hashtable_lookup(&mount_hashtable, path);
	CHECK_PTR_VALID(node, NULL);
	struct vfsmount* mount = container_of(node, struct vfsmount, mnt_hash_node);
	return mount;
}

/**
 * resolve_path_parent - Resolve a path to find its parent directory
 * @path_str: Path to resolve
 * @out_parent: Output path for the parent directory
 *
 * This resolves a path to its parent directory.
 * If path is absolute, resolves from root. Otherwise, from cwd.
 *
 * Returns: Positive index of the final component in path_str on success,
 *          or a negative error code on failure
 */
int32 resolve_path_parent(const char* path_str, struct path* out_parent) {
	struct path start_path;
	char *path_copy, *name;
	int32 error;
	int32 name_index;

	if (!path_str || !*path_str || !out_parent) return -EINVAL;

	/* Initialize with starting point based on absolute/relative path */
	if (path_str[0] == '/') {
		/* Absolute path - start from root */
		start_path.dentry = dentry_ref(current_task()->fs->root.dentry);
		start_path.mnt = mount_ref(current_task()->fs->root.mnt);
	} else {
		/* Relative path - start from cwd */
		start_path.dentry = dentry_ref(current_task()->fs->pwd.dentry);
		start_path.mnt = mount_ref(current_task()->fs->pwd.mnt);
	}

	/* Find the last component in the original string */
	// name = strrchr(path_str, '/');
	name = strchr(path_str, '/');

	if (name) {
		/* Found a slash - component starts after it */
		name_index = name - path_str + 1;

		/* Make copy of parent path for lookup */
		path_copy = kstrndup(path_str, name_index - 1, GFP_KERNEL);
		if (!path_copy) {
			path_destroy(&start_path);
			return -ENOMEM;
		}

		/* If there's a parent path, look it up */
		if (*path_copy) {
			error = vfs_path_lookup(start_path.dentry, start_path.mnt, path_copy, LOOKUP_FOLLOW, out_parent);
			path_destroy(&start_path);
			kfree(path_copy);

			if (error) return error;
		} else {
			/* Path was just "/filename" - parent is root */
			*out_parent = start_path;
			kfree(path_copy);
		}
	} else {
		/* No slashes - parent is starting directory */
		*out_parent = start_path;
		name_index = 0; /* Name starts at beginning */
	}

	/* Return the position of the final component */
	return name_index;
}

/**
 * isAbsolutePath - Check if a path is absolute
 * @path: Path string to check
 *
 * Returns: true if path is absolute (starts with '/'), false otherwise
 */
static bool isAbsolutePath(const char* path) { return path && path[0] == '/'; }

/**
 * path_acquireRoot - Get a reference to the root path
 *
 * Allocates and initializes a path structure pointing to the root directory
 * with proper reference counting.
 *
 * Returns: Pointer to allocated path structure on success, NULL on failure
 */
static struct path* path_acquireRoot(void) {
	struct path* root_path = kmalloc(sizeof(struct path));
	if (!root_path) return -ENOMEM;

	root_path->dentry = dentry_ref(current_task()->fs->root.dentry);
	root_path->mnt = mount_ref(current_task()->fs->root.mnt);

	return root_path;
}

/**
 * path_monkey - Process path traversal in an fcontext
 * @fctx: File context structure containing path info and state
 *
 * Processes the path in fctx->fc_path_remaining one component at a time,
 * updating the context state after each component.
 *
 * Returns: 0 on success, negative error code on failure
 */
int32 path_monkey(struct fcontext* fctx) {

	int32 error = 0;

	/* Validate the context */
	if (!fctx || !fctx->fc_path_remaining) return -EINVAL;

	/* Skip leading slash for absolute paths */
	if (isAbsolutePath(fctx->fc_path_remaining)) {
		fctx->fc_path_remaining++;
		if (!fctx->fc_path) {
			fctx->fc_path = path_acquireRoot();
			CHECK_PTR_VALID(fctx->fc_path, -ENOMEM);
		} else {
			return -EINVAL;
		}
	}

	/* If we don't have a path yet, initialize from appropriate starting point */
	if (!fctx->fc_path) {
		fctx->fc_path = kmalloc(sizeof(struct path));
		if (!fctx->fc_path) return -ENOMEM;

		if (fctx->fc_file) {
			/* Start from file's path */
			fctx->fc_path->dentry = dentry_ref(fctx->fc_file->f_path.dentry);
			fctx->fc_path->mnt = mount_ref(fctx->fc_file->f_path.mnt);
		} else {
			/* Start from current working directory */
			fctx->fc_path->dentry = dentry_ref(fctx->fc_task->fs->pwd.dentry);
			fctx->fc_path->mnt = mount_ref(fctx->fc_task->fs->pwd.mnt);
		}
	}

	/* Process one component in each iteration */
	while (*fctx->fc_path_remaining != '\0') {
		int32 len;
		char* component;
		char* next_slash;
		/* Find the next component */
		next_slash = strchr(fctx->fc_path_remaining, '/');
		if (next_slash) {
			/* Temporarily terminate the component */
			*next_slash = '\0';
		}

		component = fctx->fc_path_remaining;
		len = strlen(component);

		/* Restore the slash if we modified the string */
		if (next_slash) { *next_slash = '/'; }

		/* Get current dentry and mount from context */
		fctx->fc_mount = fctx->fc_path->mnt;

		/* Handle "." - current directory */
		if (len == 1 && component[0] == '.') {
			/* Skip to next component */
			if (next_slash) {
				fctx->fc_path_remaining = next_slash + 1;
			} else {
				fctx->fc_path_remaining += len; /* Move past the "." */
			}
			continue;
		}

		/* Handle ".." - parent directory */
		if (len == 2 && component[0] == '.' && component[1] == '.') {
			/* Check if we're at a mount point */
			if (fctx->fc_mount && fctx->fc_dentry == fctx->fc_mount->mnt_root) {
				/* Go to the parent mount */
				struct vfsmount* parent_mnt = fctx->fc_mount->mnt_path.mnt;
				struct dentry* mountpoint = fctx->fc_mount->mnt_path.dentry;

				if (parent_mnt && parent_mnt != fctx->fc_mount) {
					/* Cross mount boundary upward */
					mount_unref(fctx->fc_mount);
					fctx->fc_mount = mount_ref(parent_mnt);

					dentry_unref(fctx->fc_dentry);
					fctx->fc_dentry = dentry_ref(mountpoint);

					/* Now go to parent of the mountpoint */
					struct dentry* parent = fctx->fc_dentry->d_parent;
					if (parent) {
						dentry_unref(fctx->fc_dentry);
						fctx->fc_dentry = dentry_ref(parent);
					}

				}
			} else {
				/* Regular parent dentry */
				struct dentry* parent = fctx->fc_dentry->d_parent;
				if (parent && parent != fctx->fc_dentry) {
					dentry_unref(fctx->fc_dentry);
					fctx->fc_dentry = dentry_ref(parent);

				}
			}

			/* Move to next component */
			if (next_slash) {
				fctx->fc_path_remaining = next_slash + 1;
			} else {
				fctx->fc_path_remaining += len; /* Move past the ".." */
			}
			continue;
		}

		/* Skip empty components */
		if (len == 0) {
			if (next_slash) {
				fctx->fc_path_remaining = next_slash + 1;
			} else {
				fctx->fc_path_remaining += len;
			}
			continue;
		}


		/* Temporarily terminate component for lookup */
		if (next_slash) { *next_slash = '\0'; }
		char* name_save = fctx->fc_path_remaining;
		fctx->fc_path_remaining = component;
		MONKEY_WITH_ACTION(fctx, VFS_ACTION_LOOKUP, open_to_lookup_flags(fctx->fc_flags), {
			int error = dentry_monkey(fctx);
			if(error) return error;
		});
		/* Restore component */
		if (next_slash) { *next_slash = '/'; }


		if(dentry_isNegative(fctx->fc_sub_dentry)) {
			MONKEY_WITH_ACTION(fctx, VFS_ACTION_LOOKUP, open_to_lookup_flags(fctx->fc_flags), {
				int error = inode_monkey(fctx);
				if(error) return error;
			});
		}

		//next = dentry_acquireRaw(fctx->fc_dentry, component, -1, true, true);
		struct dentry* next = fctx->fc_sub_dentry;


		/* If negative dentry, ask filesystem to look it up */
		if (!next->d_inode && fctx->fc_dentry->d_inode && fctx->fc_dentry->d_inode->i_op && fctx->fc_dentry->d_inode->i_op->lookup) {

			/* Call filesystem lookup method */
			struct dentry* found = fctx->fc_dentry->d_inode->i_op->lookup(fctx->fc_dentry->d_inode, next, 0);
			if (PTR_IS_ERROR(found)) {
				dentry_unref(next);
				/* Don't clean up fc_path, caller needs to do that */
				return PTR_ERR(found);
			}

			if (found && found->d_inode) {
				/* Instantiate the dentry with the found inode */
				dentry_instantiate(next, inode_ref(found->d_inode));
				dentry_unref(found);
			}
		}

		/* Update path in context */
		dentry_unref(fctx->fc_dentry);
		fctx->fc_path->dentry = next;

		/* Check if this is a mount point */
		if (dentry_isMountpoint(next)) {
			/* Find the mount for this mountpoint */
			struct vfsmount* mounted = dentry_lookupMountpoint(next);
			if (mounted) {
				/* Cross mount point downward */
				if (fctx->fc_mount) mount_unref(fctx->fc_mount);
				fctx->fc_path->mnt = mounted; /* already has incremented ref count */

				/* Switch to the root of the mounted filesystem */
				struct dentry* mnt_root = dentry_ref(mounted->mnt_root);
				dentry_unref(next);
				fctx->fc_path->dentry = mnt_root;
			}
		}

		/* Update inode in context */
		fctx->fc_inode = fctx->fc_path->dentry->d_inode;

		/* Move to next component */
		if (next_slash) {
			fctx->fc_path_remaining = next_slash + 1;
			/* Return after processing one component - let caller decide if we continue */
			return 0;
		} else {
			/* Last component processed - we're done */
			fctx->fc_path_remaining += len;
			break;
		}
	}

	return 0;
}