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