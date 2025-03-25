#include <kernel/fs/vfs/vfs.h>
#include <kernel/types.h>
#include <util/list.h>

/* Head of the filesystem types list */
static struct list_head file_systems_list;
static spinlock_t file_systems_lock;

static int __lookup_dev_id(const char* dev_name, dev_t* dev_id);

/**
 * fsType_createMount - Mount a filesystem
 * @type: Filesystem type
 * @flags: Mount flags
 * @mount_path: Device name (can be NULL for virtual filesystems)
 * @data: Filesystem-specific mount options
 *
 * Mounts a filesystem of the specified type.
 *
 * Returns the superblock on success, ERR_PTR on failure
 */
struct superblock* fsType_createMount(struct fsType* type, int flags, const char* mount_path, void* data) {
	struct superblock* sb;
	int error;
	dev_t dev_id = 0; /* Default to 0 for virtual filesystems */

	if (unlikely(!type || !type->fs_mount_sb))
		return ERR_PTR(-ENODEV);

	/* Get device ID if we have a device name */
	if (mount_path && *mount_path) {
		error = lookup_dev_id(mount_path, &dev_id);
		if (error)
			return ERR_PTR(error);
	}

	/* Get or allocate superblock */
	sb = fsType_acquireSuperblock(type, dev_id, data);
	if (!sb)
		return ERR_PTR(-ENOMEM);

	/* Set flags */
	sb->s_flags = flags;

	/* If this is a new superblock (no root yet), initialize it */
	if (sb->s_global_root_dentry == NULL) {
		/* Call fs_fill_sb if available */
		if (type->fs_fill_sb) {
			error = fsType_fill_sb(type, sb, data, flags);
			if (error) {
				drop_super(sb);
				return ERR_PTR(error);
			}
		}
		/* Or call mount if fs_fill_sb isn't available */
		else if (type->fs_mount_sb) {
			/* This is a fallback - ideally all filesystems would
			 * implement fs_fill_sb instead */
			struct superblock* new_sb = fsType_mount_sb(type, flags, mount_path, data);
			if (IS_ERR(new_sb)) {
				drop_super(sb);
				return new_sb;
			}
			/* This would need to handle merging the superblocks
			 * but it's a non-standard path */
			drop_super(sb);
			sb = new_sb;
		}
	}

	/* Increment active count */
	grab_super(sb);

	return sb;
}


/**
 * fsType_acquireSuperblock - Get or create a superblock
 * @type: Filesystem type
 * @dev_id: Device identifier (0 for virtual filesystems)
 * @fs_data: Filesystem-specific mount data
 *
 * Returns an existing superblock for the device or creates a new one.
 * Increments the reference count on the returned superblock.
 *
 * Returns: pointer to superblock or NULL on failure
 */
struct superblock* fsType_acquireSuperblock(struct fsType* type, dev_t dev_id, void* fs_data) {
	struct superblock* sb = NULL;

	if (!type)
		return NULL;

	/* Lock to protect the filesystem type's superblock list */
	spin_lock(&type->fs_list_s_lock);

	/* Check if a superblock already exists for this device */
	if (dev_id != 0) {
		list_for_each_entry(sb, &type->fs_list_sb, s_node_fsType) {
			if (sb->s_device_id == dev_id) {
				/* Found matching superblock - increment reference */
				sb->s_refcount++;
				spin_unlock(&type->fs_list_s_lock);
				return sb;
			}
		}
	}

	/* No existing superblock found, allocate a new one */
	spin_unlock(&type->fs_list_s_lock);
	sb = __alloc_super(type);
	if (!sb)
		return NULL;

	/* Set device ID */
	sb->s_device_id = dev_id;

	/* Store filesystem-specific data if provided */
	if (fs_data) {
		/* Note: Filesystem is responsible for managing this data */
		sb->s_fs_specific = fs_data;
	}

	/* Add to the filesystem's list of superblocks */
	spin_lock(&type->fs_list_s_lock);
	list_add(&sb->s_node_fsType, &type->fs_list_sb);
	spin_unlock(&type->fs_list_s_lock);

	return sb;
}

/**
 * Register built-in filesystem types
 * called by vfs_init
 */
int fsType_register_all(void) {
	INIT_LIST_HEAD(&file_systems_list);
	spinlock_init(&file_systems_lock);
	int err;
	/* Register ramfs - our initial root filesystem */
	extern struct fsType ramfs_fsType;
	err = fsType_register(&ramfs_fsType);
	if (err < 0)
		return err;

	/* Register other built-in filesystems */
	extern struct fsType hostfs_fsType;
	err = fsType_register(&hostfs_fsType);
	if (err < 0)
		return err;

	return 0;
}

/**
 * fsType_register - Register a new filesystem type
 * @fs: The filesystem type structure to register
 *
 * Adds a filesystem to the kernel's list of filesystems that can be mounted.
 * Returns 0 on success, error code on failure.
 * fs是下层文件系统静态定义的，所以不需要分配内存
 */
int fsType_register(struct fsType* fs) {
	struct fsType* p;

	if (!fs || !fs->fs_name)
		return -EINVAL;

	/* Initialize filesystem type */
	INIT_LIST_HEAD(&fs->fs_globalFsListNode);
	INIT_LIST_HEAD(&fs->fs_list_sb);

	/* Acquire lock for list manipulation */
	spinlock_init(&file_systems_lock);

	/* Check if filesystem already registered */
	list_for_each_entry(p, &file_systems_list, fs_globalFsListNode) {
		if (strcmp(p->fs_name, fs->fs_name) == 0) {
			/* Already registered */
			release_spinlock(&file_systems_lock);
			sprint("VFS: Filesystem %s already registered\n", fs->fs_name);
			return -EBUSY;
		}
	}

	/* Add filesystem to the list (at the beginning for simplicity) */
	list_add(&fs->fs_globalFsListNode, &file_systems_list);

	spinlock_unlock(&file_systems_lock);
	sprint("VFS: Registered filesystem %s\n", fs->fs_name);
	return 0;
}


/**
 * fsType_unregister - Remove a filesystem type from the kernel's list
 * @fs: The filesystem type structure to unregister
 *
 * Removes a filesystem from the kernel's list of available filesystems.
 * Returns 0 on success, error code on failure.
 */
int fsType_unregister(struct fsType* fs) {
	struct fsType* p;

	if (!fs || !fs->fs_name)
		return -EINVAL;

	/* Acquire lock for list manipulation */
	acquire_spinlock(&file_systems_lock);

	/* Find filesystem in the list */
	list_for_each_entry(p, &file_systems_list, fs_globalFsListNode) {
		if (p == fs) {
			/* Found it - remove from the list */
			list_del(&p->fs_globalFsListNode);
			release_spinlock(&file_systems_lock);
			sprint("VFS: Unregistered filesystem %s\n", p->fs_name);
			return 0;
		}
	}

	release_spinlock(&file_systems_lock);
	sprint("VFS: Filesystem %s not registered\n", fs->fs_name);
	return -ENOENT;
}

/**
 * fsType_lookup - Find a filesystem type by name
 * @name: The filesystem name to find
 *
 * Searches the list of registered filesystems for one with the given name.
 * Returns a pointer to the filesystem type structure or NULL if not found.
 */
struct fsType* fsType_lookup(const char* name) {
	struct fsType* fs;

	if (!name)
		return NULL;

	acquire_spinlock(&file_systems_lock);

	list_for_each_entry(fs, &file_systems_list, fs_globalFsListNode) {
		if (strcmp(fs->fs_name, name) == 0) {
			release_spinlock(&file_systems_lock);
			return fs;
		}
	}

	release_spinlock(&file_systems_lock);
	return NULL;
}


/**
 * lookup_dev_id - Get device ID from device name
 * @dev_name: Name of the device
 * @dev_id: Output parameter for device ID
 *
 * Returns 0 on success, negative error code on failure
 */
static int __lookup_dev_id(const char* dev_name, dev_t* dev_id) {
	/* Implementation would look up the device in the device registry */
	/* For now, we'll just use a simple hash function */
	if (!dev_name || !dev_id)
		return -EINVAL;

	*dev_id = 0;
	while (*dev_name) {
		*dev_id = (*dev_id * 31) + *dev_name++;
	}

	/* Ensure non-zero value for real devices */
	if (*dev_id == 0)
		*dev_id = 1;

	return 0;
}

const char *fsType_error_string(int error_code) {
    if (error_code >= 0 || -error_code >= ARRAY_SIZE(fs_err_msgs))
        return "Unknown error";
    return fs_err_msgs[-error_code];
}


/* Add to fstype.c */
const char* fs_err_msgs[] = {
    [ENODEV] = "Filesystem not found", [EBUSY] = "Filesystem already registered", [EINVAL] = "Invalid parameters",
    // Add more error messages
};