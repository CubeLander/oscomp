#include <kernel/fs/vfs/vfs.h>
#include <kernel/types.h>
#include <util/list.h>

/* Head of the filesystem types list */
static struct list_head file_systems_list;
static spinlock_t file_systems_lock;

static int __lookup_dev_id(const char* dev_name, dev_t* dev_id);
static void __fsType_init(void);



static void __fsType_init(void) {
	spinlock_init(&file_systems_lock);
	INIT_LIST_HEAD(&file_systems_list);
}

int fsType_register_all(void) {
	__fsType_init();
	int ret;
	struct fsType* fs;
	extern struct fsType hostfs_fsType;
	ret = fsType_register(&hostfs_fsType);
	if (ret != 0) {
		sprint("VFS: Failed to register filesystem %s: %d\n", fs->fs_name, ret);
		return ret;
	}

	return ret;
}

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
			error = type->fs_fill_sb(sb, data, flags);
			if (error) {
				drop_super(sb);
				return ERR_PTR(error);
			}
		}
		/* Or call mount if fs_fill_sb isn't available */
		else if (type->fs_mount_sb) {
			/* This is a fallback - ideally all filesystems would
			 * implement fs_fill_sb instead */
			struct superblock* new_sb = type->fs_mount_sb(type, flags, mount_path, data);
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

int fsType_register(struct fsType* fs) {
	int ret = 0;
	struct fsType* p;

	if (!fs || !fs->fs_name)
		return -EINVAL;

	/* Initialize filesystem type */
	INIT_LIST_HEAD(&fs->fs_globalFsListNode);
	INIT_LIST_HEAD(&fs->fs_list_sb);
	spinlock_init(&fs->fs_list_s_lock);

	/* Check if filesystem already registered */
	acquire_spinlock(&file_systems_lock);
	list_for_each_entry(p, &file_systems_list, fs_globalFsListNode) {
		if (strcmp(p->fs_name, fs->fs_name) == 0) {
			release_spinlock(&file_systems_lock);
			sprint("VFS: Filesystem %s already registered\n", fs->fs_name);
			return -EBUSY;
		}
	}
	release_spinlock(&file_systems_lock);

	/* Call filesystem-specific registration if provided */
	if (fs->fs_register) {
		ret = fs->fs_register(fs);
		if (ret != 0) {
			sprint("VFS: %s registration failed: %d\n", fs->fs_name, ret);
			return ret;
		}
	}

	/* Add filesystem to the list */
	acquire_spinlock(&file_systems_lock);
	list_add(&fs->fs_globalFsListNode, &file_systems_list);
	release_spinlock(&file_systems_lock);

	/* Call filesystem initialization */
	if (fs->fs_init) {
		ret = fs->fs_init();
		if (ret != 0) {
			/* Initialization failed, remove from list */
			acquire_spinlock(&file_systems_lock);
			list_del(&fs->fs_globalFsListNode);
			release_spinlock(&file_systems_lock);

			/* Call unregister to clean up */
			if (fs->fs_unregister)
				fs->fs_unregister(fs);

			sprint("VFS: %s initialization failed: %d\n", fs->fs_name, ret);
			return ret;
		}
	}

	sprint("VFS: Registered filesystem %s\n", fs->fs_name);
	return 0;
}

int fsType_unregister(struct fsType* fs) {
	int ret = 0;
	struct fsType* p;
	bool found = false;

	if (!fs || !fs->fs_name)
		return -EINVAL;

	/* Check if filesystem has mounted instances */
	if (!list_empty(&fs->fs_list_sb)) {
		sprint("VFS: Cannot unregister %s - has mounted instances\n", fs->fs_name);
		return -EBUSY;
	}

	/* Call filesystem exit function if available */
	if (fs->fs_exit)
		fs->fs_exit();

	/* Find and remove from global list */
	acquire_spinlock(&file_systems_lock);
	list_for_each_entry(p, &file_systems_list, fs_globalFsListNode) {
		if (p == fs) {
			list_del(&p->fs_globalFsListNode);
			found = true;
			break;
		}
	}
	release_spinlock(&file_systems_lock);

	if (!found) {
		sprint("VFS: Filesystem %s not registered\n", fs->fs_name);
		return -ENOENT;
	}

	/* Call filesystem-specific unregistration if provided */
	if (fs->fs_unregister) {
		ret = fs->fs_unregister(fs);
		if (ret != 0) {
			sprint("VFS: %s unregistration failed: %d\n", fs->fs_name, ret);
			/* Continue anyway - filesystem is already removed from list */
		}
	}

	sprint("VFS: Unregistered filesystem %s\n", fs->fs_name);
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

const char* fsType_error_string(int error_code) {
	if (error_code >= 0 || -error_code >= ARRAY_SIZE(fs_err_msgs))
		return "Unknown error";
	return fs_err_msgs[-error_code];
}

/* Add to fstype.c */
const char* fs_err_msgs[] = {
    [ENODEV] = "Filesystem not found", [EBUSY] = "Filesystem already registered", [EINVAL] = "Invalid parameters",
    // Add more error messages
};