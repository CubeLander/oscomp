#include <kernel/mmu.h>
#include <kernel/sprint.h>
#include <kernel/types.h>
#include <kernel/util.h>
#include <kernel/vfs.h>

/* Head of the filesystem types list */
static struct list_head file_systems_list;
static spinlock_t file_systems_lock;

static struct superblock* __fstype_allocSuperblock(struct fstype* type);

/**
 * fstype_mount - Get or create a superblock
 * @type: Filesystem type
 * @dev_id: Device identifier (0 for virtual filesystems)
 * @fs_data: Filesystem-specific mount data
 *
 * Returns an existing superblock for the device or creates a new one.
 * Increments the reference count on the returned superblock.
 *
 * Returns: pointer to superblock or NULL on failure
 */
struct superblock* fstype_mount(struct fstype* type, int32 flags, dev_t dev_id, void* fs_data) {
	struct superblock* sb = NULL;

	if (!type) return NULL;

	/* Lock to protect the filesystem type's superblock list */
	spinlock_lock(&type->fs_list_superblock_lock);
	/* Check if a superblock already exists for this device */
	if (dev_id != 0) {
		list_for_each_entry(sb, &type->fs_list_superblock, s_node_fstype) {
			if (sb->s_device_id == dev_id) {
				/* Found matching superblock - increment reference */
				// sb->s_refcount++;
				spinlock_unlock(&type->fs_list_superblock_lock);
				return sb;
			}
		}
	}
	/* No existing superblock found, allocate a new one */
	spinlock_unlock(&type->fs_list_superblock_lock);

	/* NEW CODE: Call filesystem-specific mount if available */
	if (type->fs_mount) {
		sb = type->fs_mount(type, flags, dev_id, fs_data);
		CHECK_PTR_VALID(sb, sb);
	} else {
		/* Fall back to generic allocation and initialization */
		sb = __fstype_allocSuperblock(type);
		CHECK_PTR_VALID(sb, ERR_PTR(-ENOMEM));
	}

	/* Set device ID */
	sb->s_device_id = dev_id;

	/* Add to the filesystem's list of superblocks */
	spinlock_lock(&type->fs_list_superblock_lock);
	list_add(&sb->s_node_fstype, &type->fs_list_superblock);
	spinlock_unlock(&type->fs_list_superblock_lock);

	return sb;
}

/**
 * Register built-in filesystem types
 * called by vfs_init
 */
int32 fstype_register_all(void) {
	INIT_LIST_HEAD(&file_systems_list);
	spinlock_init(&file_systems_lock);
	int32 err;
	/* Register ramfs - our initial root filesystem */
	extern struct fstype ramfs_fstype;
	err = fstype_register(&ramfs_fstype);
	if (err < 0) return err;

	/* Register other built-in filesystems */
	extern struct fstype hostfs_fstype;
	err = fstype_register(&hostfs_fstype);
	if (err < 0) return err;

	return 0;
}

/**
 * fstype_register - Register a new filesystem type
 * @fs: The filesystem type structure to register
 *
 * Adds a filesystem to the kernel's list of filesystems that can be mounted.
 * Returns 0 on success, error code on failure.
 * fs是下层文件系统静态定义的，所以不需要分配内存
 */
int32 fstype_register(struct fstype* fs) {
	struct fstype* p;

	if (!fs || !fs->fs_name) return -EINVAL;

	/* Initialize filesystem type */
	INIT_LIST_HEAD(&fs->fs_globalFsListNode);
	INIT_LIST_HEAD(&fs->fs_list_superblock);

	/* Acquire lock for list manipulation */
	spinlock_init(&file_systems_lock);

	/* Check if filesystem already registered */
	list_for_each_entry(p, &file_systems_list, fs_globalFsListNode) {
		if (strcmp(p->fs_name, fs->fs_name) == 0) {
			/* Already registered */
			spinlock_unlock(&file_systems_lock);
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
 * fstype_unregister - Remove a filesystem type from the kernel's list
 * @fs: The filesystem type structure to unregister
 *
 * Removes a filesystem from the kernel's list of available filesystems.
 * Returns 0 on success, error code on failure.
 */
int32 fstype_unregister(struct fstype* fs) {
	struct fstype* p;

	if (!fs || !fs->fs_name) return -EINVAL;

	/* Acquire lock for list manipulation */
	spinlock_lock(&file_systems_lock);

	/* Find filesystem in the list */
	list_for_each_entry(p, &file_systems_list, fs_globalFsListNode) {
		if (p == fs) {
			/* Found it - remove from the list */
			list_del(&p->fs_globalFsListNode);
			spinlock_unlock(&file_systems_lock);
			sprint("VFS: Unregistered filesystem %s\n", p->fs_name);
			return 0;
		}
	}

	spinlock_unlock(&file_systems_lock);
	sprint("VFS: Filesystem %s not registered\n", fs->fs_name);
	return -ENOENT;
}

/**
 * fstype_lookup - Find a filesystem type by name
 * @name: The filesystem name to find
 *
 * Searches the list of registered filesystems for one with the given name.
 * Returns a pointer to the filesystem type structure or NULL if not found.
 */
struct fstype* fstype_lookup(const char* name) {
	struct fstype* fs;

	if (!name) return NULL;

	spinlock_lock(&file_systems_lock);

	list_for_each_entry(fs, &file_systems_list, fs_globalFsListNode) {
		if (strcmp(fs->fs_name, name) == 0) {
			spinlock_unlock(&file_systems_lock);
			return fs;
		}
	}

	spinlock_unlock(&file_systems_lock);
	return NULL;
}


/**
 * __fstype_allocSuperblock - Allocate a new superblock from a fstype
 * @type: Filesystem type
 *
 * Allocates and initializes a new superblock structure
 *
 * Returns a new superblock or NULL on failure
 */
static struct superblock* __fstype_allocSuperblock(struct fstype* type) {
	struct superblock* sb;

	sb = kmalloc(sizeof(struct superblock));
	if (!sb) return NULL;

	/* Initialize to zeros */
	memset(sb, 0, sizeof(struct superblock));

	/* Initialize lists */
	INIT_LIST_HEAD(&sb->s_list_all_inodes);
	spinlock_init(&sb->s_list_all_inodes_lock);

	INIT_LIST_HEAD(&sb->s_list_clean_inodes);
	INIT_LIST_HEAD(&sb->s_list_dirty_inodes);
	INIT_LIST_HEAD(&sb->s_list_io_inodes);
	spinlock_init(&sb->s_list_inode_states_lock);

	INIT_LIST_HEAD(&sb->s_list_mounts);

	INIT_LIST_HEAD(&sb->s_node_fstype);

	/* Initialize locks */
	spinlock_init(&sb->s_lock);
	spinlock_init(&sb->s_list_mounts_lock);
	// 初始化原子计数器
	atomic_set(&sb->s_refcount, 1);
	atomic_set(&sb->s_ninodes, 0);
	atomic64_set(&sb->s_next_ino, 1);
	// ino=0是保留的
	/* Set filesystem type */
	sb->s_fstype = type;

	return sb;
}
