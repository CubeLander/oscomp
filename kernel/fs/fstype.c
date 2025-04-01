#include <kernel/mmu.h>
#include <kernel/sprint.h>
#include <kernel/types.h>
#include <kernel/util.h>
#include <kernel/vfs.h>

/* Head of the filesystem types list */
static struct list_head file_systems_list;
static spinlock_t file_systems_lock;

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