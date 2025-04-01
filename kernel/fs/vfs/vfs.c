#include <kernel/mm/kmalloc.h>
#include <kernel/sched.h>
#include <kernel/sprint.h>
#include <kernel/types.h>
#include <kernel/util/string.h>
#include <kernel/vfs.h>
#include <kernel/device/device.h>

/* Add global variable */
struct dentry* global_root_dentry = NULL;

/**
 * vfs_mkdir_path - Create a directory using a path string
 * @path: Path string for the directory to create
 * @mode: Directory mode/permissions
 *
 * Creates a directory at the specified path.
 * Path can be absolute (from root) or relative (from cwd).
 *
 * Returns 0 on success, negative error code on failure
 */
struct dentry* vfs_mkdir_path(const char* path, fmode_t mode) {
	struct path parent;
	int32 name_pos;
	struct dentry* result;

	if (!path || !*path) return ERR_PTR(-EINVAL);

	/* Special case for creating root - impossible */
	if (strcmp(path, "/") == 0) return ERR_PTR(-EEXIST);

	/* Resolve the parent directory */
	name_pos = resolve_path_parent(path, &parent);
	if (name_pos < 0) return ERR_PTR(name_pos); /* Error code */

	/* Create the directory */
	result = dentry_mkdir(parent.dentry, &path[name_pos], mode);

	/* Clean up */
	path_destroy(&parent);
	return result;
}

/**
 * vfs_kern_mount
 * @fstype: Filesystem type
 * @flags: Mount flags
 * @device_path: 设备的虚拟文件路径 (虚拟文件系统为NULL)，用来解析devid
 * @data: 最终传递给fs_fill_super解析
 *
 * Mounts a filesystem of the specified type.
 * 这个函数只负责生成挂载点，在后续的mountpoint attachment中会将挂载点关联到目标路径
 *
 * Returns the superblock on success, ERR_PTR on failure
 * 正在优化的vfs_kern_mount函数
 */
struct vfsmount* vfs_kern_mount(struct fstype* fstype, int32 flags, const char* device_path, void* data) {
	CHECK_PTR_VALID(fstype, ERR_PTR(-EINVAL));
	dev_t dev_id = 0;
	/***** 对于挂载实体设备的处理 *****/
	if (device_path && *device_path) {
		int32 ret = lookup_dev_id(device_path, &dev_id);
		if (ret < 0) {
			sprint("VFS: Failed to get device ID for %s\n", device_path);
			return ERR_PTR(ret);
		}
	}
	struct superblock* sb = fstype_mount(fstype,flags, dev_id, data);
	CHECK_PTR_VALID(sb, ERR_PTR(-ENOMEM));

	struct vfsmount* mount = superblock_acquireMount(sb, flags, device_path);
	CHECK_PTR_VALID(mount, ERR_PTR(-ENOMEM));

	return mount;
}


/**
 * vfs_link - Create a hard link
 * @old_dentry: Existing file's dentry
 * @dir: Directory to create link in
 * @new_dentry: Dentry for new link
 * @new_inode: Output parameter for resulting inode
 *
 * Returns 0 on success or negative error code
 */
int32 vfs_link(struct dentry* old_dentry, struct inode* dir, struct dentry* new_dentry, struct inode** new_inode) {
	int32 error;

	if (!old_dentry || !dir || !new_dentry) return -EINVAL;

	if (!dir->i_op || !dir->i_op->link) return -EPERM;

	/* Check if target exists */
	if (new_dentry->d_inode) return -EEXIST;

	/* Check permissions */
	error = inode_checkPermission(dir, MAY_WRITE);
	if (error) return error;

	error = dir->i_op->link(old_dentry, dir, new_dentry);
	if (error) return error;

	if (new_inode) *new_inode = new_dentry->d_inode;

	return 0;
}

/**
 * vfs_init - Initialize the VFS subsystem
 *
 * Initializes all the core VFS components in proper order.
 * Must be called early during kernel initialization before
 * any filesystem operations can be performed.
 */
int32 vfs_init(void) {
	int32 err;
	init_mount_hash();

	/* Initialize the dcache subsystem */
	sprint("VFS: Initializing dentry cache...\n");
	err = init_dentry_hashtable();
	if (err < 0) {
		sprint("VFS: Failed to initialize dentry cache\n");
		return err;
	}

	/* Initialize the inode subsystem */
	sprint("VFS: Initializing inode cache...\n");
	err = inode_cache_init();
	if (err < 0) {
		sprint("VFS: Failed to initialize inode cache\n");
		return err;
	}

	/* Register built-in filesystems */
	sprint("VFS: Registering built-in filesystems...\n");
	err = fstype_register_all();
	if (err < 0) {
		sprint("VFS: Failed to register filesystems\n");
		return err;
	}

	sprint("VFS: Initialization complete\n");
	return 0;
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
int32 vfs_path_lookup(struct dentry* base_dentry, struct vfsmount* base_mnt, const char* path_str, uint32 flags, struct path* result) {
	struct dentry* dentry;
	struct vfsmount* mnt;
	char* component;
	char* path_copy;
	char* next_slash;
	int32 len;

	/* Validate parameters */
	if (!base_dentry || !path_str || !result) return -EINVAL;

	/* Initialize with starting point */
	dentry = dentry_ref(base_dentry);
	mnt = base_mnt ? mount_ref(base_mnt) : NULL;

	/* Handle absolute paths - start from root */
	if (path_str[0] == '/') {
		/* Use filesystem root */
		if (mnt) {
			dentry_unref(dentry);
			dentry = dentry_ref(mnt->mnt_root);
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
		dentry_unref(dentry);
		if (mnt) mount_unref(mnt);
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
				struct vfsmount* parent_mnt = mnt->mnt_path.mnt;
				struct dentry* mountpoint = mnt->mnt_path.dentry;

				if (parent_mnt && parent_mnt != mnt) {
					/* Cross mount boundary upward */
					dentry_unref(dentry);
					mount_unref(mnt);

					mnt = mount_ref(parent_mnt);
					dentry = dentry_ref(mountpoint);

					/* Now go to parent of the mountpoint */
					struct dentry* parent = dentry->d_parent;
					if (parent) {
						dentry_unref(dentry);
						dentry = dentry_ref(parent);
					}
				} else {
					/* We're at the root of the root filesystem */
					/* Stay at the root */
				}
			} else {
				/* Regular parent dentry */
				struct dentry* parent = dentry->d_parent;
				if (parent) {
					dentry_unref(dentry);
					dentry = dentry_ref(parent);
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

		/* Create a dentry (from cache or allocate new) */
		struct dentry* next = dentry_acquireRaw(dentry, component, -1, true, true);
		if (PTR_IS_ERROR(next) || !next) {
			dentry_unref(dentry);
			if (mnt) mount_unref(mnt);
			kfree(path_copy);
			return next ? PTR_ERR(next) : -ENOMEM;
		}

		/* If negative dentry, ask filesystem to look it up */
		if (!next->d_inode && dentry->d_inode && dentry->d_inode->i_op && dentry->d_inode->i_op->lookup) {

			/* Call filesystem lookup method */
			struct dentry* found = dentry->d_inode->i_op->lookup(dentry->d_inode, next, 0);
			if (PTR_IS_ERROR(found)) {
				dentry_unref(next);
				dentry_unref(dentry);
				if (mnt) mount_unref(mnt);
				kfree(path_copy);
				return PTR_ERR(found);
			}

			if (found && found->d_inode) {
				/* Instantiate the dentry with the found inode */
				dentry_instantiate(next, inode_ref(found->d_inode));
				dentry_unref(found);
			}
		}

		/* Release the parent dentry */
		dentry_unref(dentry);
		dentry = next;

		/* Check if this is a mount point */
		if (flags & LOOKUP_AUTOMOUNT && dentry_isMountpoint(dentry)) {
			/* Find the mount for this mountpoint */
			struct vfsmount* mounted = dentry_lookupMount(dentry);
			if (mounted) {
				/* Cross mount point downward */
				if (mnt) mount_unref(mnt);
				mnt = mounted; /* already has incremented ref count */

				/* Switch to the root of the mounted filesystem */
				struct dentry* mnt_root = dentry_ref(mounted->mnt_root);
				dentry_unref(dentry);
				dentry = mnt_root;
			}
		}

		// /* Handle symbolic links if needed */
		// if (flags & LOOKUP_FOLLOW && dentry->d_inode && S_ISLNK(dentry->d_inode->i_mode)) {
		// 	/* Implement symlink resolution */
		// 	struct dentry* link_target = dentry_follow_link(dentry);
		// 	if (PTR_IS_ERROR(link_target)) {
		// 		dentry_unref(dentry);
		// 		if (mnt)
		// 			mount_unref(mnt);
		// 		kfree(path_copy);
		// 		return PTR_ERR(link_target);
		// 	}

		// 	dentry_unref(dentry);
		// 	dentry = link_target;
		// }

		/* Move to the next component */
		component = next_slash;
	}

	/* Set the result */
	result->dentry = dentry;
	result->mnt = mnt;

	kfree(path_copy);
	return 0;
}

/**
 * vfs_mkdir - Create a directory
 * @parent: Parent directory dentry, or NULL to use absolute/relative paths
 *          - If NULL and name starts with '/', uses global root (absolute)
 *          - If NULL and name doesn't start with '/', uses current dir (relative)
 * @name: Name of the new directory
 * @mode: Directory permissions
 *
 * Returns: New directory dentry on success, ERR_PTR on failure
 */
struct dentry* vfs_mkdir(struct dentry* parent, const char* name, fmode_t mode) {
	if (!name || !*name) return ERR_PTR(-EINVAL);
	int32 pos = 0;
	if (!parent) {
		struct path parent_path;
		pos = resolve_path_parent(name, &parent_path);
		if (pos < 0) return ERR_PTR(pos); /* Error code */
		parent = parent_path.dentry;
		path_destroy(&parent_path);
	}

	/* Validate parent after potential NULL handling */
	if (!parent) return ERR_PTR(-EINVAL);

	/* Create the directory */
	return dentry_mkdir(parent, &name[pos], mode);
}

/**
 * vfs_mknod - Create a special file
 * @parent: Parent directory dentry, or NULL to use absolute/relative paths
 * @name: Name of the new node
 * @mode: File mode including type (S_IFBLK, S_IFCHR, etc.)
 * @dev: Device number for device nodes
 *
 * Creates a special file (device node, FIFO, socket). If parent is NULL,
 * resolves the path to find the parent directory.
 *
 * Returns: New dentry on success, ERR_PTR on failure
 */
struct dentry* vfs_mknod(struct dentry* parent, const char* name, mode_t mode, dev_t dev) {
    const char* filename = name;
    int32 name_pos = 0;
    
    if (!name || !*name)
        return ERR_PTR(-EINVAL);
    
    /* Handle NULL parent case by resolving the path */
    if (!parent) {
        struct path parent_path;
        name_pos = resolve_path_parent(name, &parent_path);
        if (name_pos < 0)
            return ERR_PTR(name_pos); /* Error code */
            
        parent = parent_path.dentry;
        filename = &name[name_pos];
        
        /* Create the special file */
        struct dentry* result = dentry_mknod(parent, filename, mode, dev);
        
        /* Clean up */
        path_destroy(&parent_path);
        return result;
    }
    
    /* If parent was provided directly, just create the node */
    return dentry_mknod(parent, filename, mode, dev);
}

/**
 * vfs_mknod_block - Create a block device node
 * @path: Path where to create the node
 * @mode: Access mode bits (permissions)
 * @dev: Device ID
 *
 * Simplified helper to create block device nodes.
 *
 * Returns: 0 on success, negative error on failure
 */
int32 vfs_mknod_block(const char* path, mode_t mode, dev_t dev) {
	struct dentry* dentry;
	int32 error = 0;

	dentry = vfs_mknod(NULL, path, S_IFBLK | (mode & 0777), dev);

	if (PTR_IS_ERROR(dentry)) {
		error = PTR_ERR(dentry);
		/* Special case: if the node exists, don't treat as error */
		if (error == -EEXIST) return 0;
		return error;
	}

	/* Release dentry reference */
	dentry_unref(dentry);
	return 0;
}


/**
 * vfs_monkey - Main entry function to handle filesystem syscalls
 * @fctx: fcontext to be operated on
 * @return: 0 or positive on success, negative error code on failure
 */
int32 vfs_monkey(struct fcontext* fctx) {
	if(fctx->fc_fd >= 0) {
		/* Handle file descriptor operations */
		int32 ret = fd_monkey(fctx);
		if (ret < 0) {
			sprint("vfs_monkey: fd_monkey failed: %d\n", ret);
			return ret;
		}
	}

	/* Handle the path and perform the remaining operation */
	int32 ret = path_monkey(fctx);
	if (ret < 0) {
		sprint("vfs_monkey: path_monkey failed: %d\n", ret);
		return ret;
	}

	return 0;
}