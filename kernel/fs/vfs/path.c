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
static void path_acquireRoot(struct path* path) {

	if (path->dentry) { dentry_unref(path->dentry); }
	if (path->mnt) { mount_unref(path->mnt); }

	path->dentry = dentry_ref(current_task()->fs->root.dentry);
	path->mnt = mount_ref(current_task()->fs->root.mnt);

	return;
}

/**
 * path_next_component - Extract the next component from a path string
 * @fctx: File context
 *
 * Extracts the next path component from fc_path_remaining and sets it in fc_string
 * Does not interpret "." or ".." - just extracts the raw component string
 *
 * Returns:
 *   1: Successfully extracted a component
 *   0: Path parsing is complete (no more components)
 *   Negative: Error code
 */
static int32 path_next_component(struct fcontext* fctx) {
	char* next_slash;
	int32 len;

	if (!fctx || !fctx->fc_path_remaining) return -EINVAL;

	/* Check if path is already complete */
	if (*fctx->fc_path_remaining == '\0') return 0; /* Parsing complete */

	/* Skip leading slashes */
	while (*fctx->fc_path_remaining == '/') { fctx->fc_path_remaining++; }

	/* Check if we've reached the end after skipping slashes */
	if (*fctx->fc_path_remaining == '\0') return 0;

	/* Find the next component boundary */
	next_slash = strchr(fctx->fc_path_remaining, '/');

	if (next_slash) {
		len = next_slash - fctx->fc_path_remaining;
	} else {
		len = strlen(fctx->fc_path_remaining);
	}

	/* Set fc_string fields */
	fctx->fc_charbuf = fctx->fc_path_remaining;
	fctx->fc_strlen = len;
	fctx->fc_hash = full_name_hash(fctx->fc_charbuf, len);

	/* Move to next component */
	fctx->fc_path_remaining += len;
	if (*fctx->fc_path_remaining == '/') {
		/* Point to the character after the slash */
		while (*fctx->fc_path_remaining == '/') { fctx->fc_path_remaining++; }
	}

	return 1; /* Successfully extracted a component */
}

/**
 * isAbsolutePath - Check if a path is absolute
 * @path: Path string to check
 *
 * Returns: true if path is absolute (starts with '/'), false otherwise
 */
static bool isAbsolutePath(const char* path) { return path && path[0] == '/'; }

/**
 * path_acquireRoot - Set the path to the root directory
 * @path: Path structure to initialize with root
 */
static void path_acquireRoot(struct path* path) {
	if (path->dentry) { dentry_unref(path->dentry); }
	if (path->mnt) { mount_unref(path->mnt); }

	path->dentry = dentry_ref(current_task()->fs->root.dentry);
	path->mnt = mount_ref(current_task()->fs->root.mnt);
}

/**
 * path_monkey - Handle path traversal with unified dentry approach
 * @fctx: File context
 *
 * Resolves paths for all operation types, resulting in either:
 * - A positive dentry for existing objects (lookup/read/write)
 * - A negative dentry for creation operations
 * 
 * Returns: 0 for success, negative value for error
 */
int32 path_monkey(struct fcontext* fctx) {
    int32 ret;
    bool is_create_intent;

    /* Validate context */
    if (!fctx || !fctx->fc_path_remaining) return -EINVAL;

    /* Determine operation intent */
    is_create_intent = (fctx->fc_action == VFS_ACTION_CREATE || 
                        fctx->fc_action == VFS_ACTION_MKDIR || 
                        fctx->fc_action == VFS_ACTION_MKNOD ||
                        ((fctx->fc_action == VFS_ACTION_OPEN) && (fctx->fc_flags & O_CREAT)));

    /* Handle absolute path */
    if (isAbsolutePath(fctx->fc_path_remaining)) {
        fctx->fc_path_remaining++; /* Skip leading slash */
        path_acquireRoot(&fctx->fc_path);
    }

    /* Initialize dentry and mount if needed */
    if (!fctx->fc_dentry) {
        if (fctx->fc_file) {
            fctx->fc_dentry = dentry_ref(fctx->fc_file->f_path.dentry);
            fctx->fc_mount = mount_ref(fctx->fc_file->f_path.mnt);
        } else {
            fctx->fc_dentry = dentry_ref(fctx->fc_task->fs->pwd.dentry);
            fctx->fc_mount = mount_ref(fctx->fc_task->fs->pwd.mnt);
        }
    }

    /* Main path resolution loop */
    while (true) {
        /* Extract next component */
        ret = path_next_component(fctx);
        if (ret <= 0) return (ret < 0) ? ret : 0; /* Return error or success */

        /* Check if this is the last component */
        bool is_last_component = (*fctx->fc_path_remaining == '\0');

        /* For last component of create operations, use LOOKUP_CREATE flag */
        uint32 lookup_flags = 0;
        if (is_create_intent && is_last_component) {
            lookup_flags = open_to_lookup_flags(fctx->fc_flags) | LOOKUP_CREATE;
        } else {
            lookup_flags = open_to_lookup_flags(fctx->fc_flags);
        }

        /* Process component with appropriate flags */
        ret = MONKEY_WITH_ACTION(dentry_monkey, fctx, DENTRY_ACTION_LOOKUP, lookup_flags);
		/* dentry_monkey, PATHWALK 会将fc_dentry和fc_string解析为下一级的fc_dentry */
        if (ret < 0) return ret;

        /* For create operations, negative dentry on last component is expected and ok */
        if (is_last_component) {
            if (dentry_isNegative(fctx->fc_dentry)) {
                if (!is_create_intent) {
                    return -ENOENT; /* Error for non-create operations */
                }
                /* Success for create operations - leave negative dentry */
            }
            return 0; /* Path resolution complete */
        }

        /* Non-last component must exist for all operations */
        if (dentry_isNegative(fctx->fc_dentry)) {
            return -ENOENT; /* Intermediate component doesn't exist */
        }

        /* Check for mount point and cross if needed */
        if (fctx->fc_dentry->d_flags & DCACHE_MOUNTED) {
            /* Cross mount point */
            struct vfsmount* mounted = fctx->fc_dentry->d_mount;
            if (mounted) {
                if (fctx->fc_mount) mount_unref(fctx->fc_mount);
                fctx->fc_mount = mount_ref(mounted);

                /* Switch to mounted filesystem's root */
                struct dentry* mnt_root = dentry_ref(mounted->mnt_root);
                dentry_unref(fctx->fc_dentry);
                fctx->fc_dentry = mnt_root;
            }
        }

        /* Clear component data before processing next component */
        fctx->fc_charbuf = NULL;
        fctx->fc_strlen = 0;
        fctx->fc_hash = 0;
    }
}