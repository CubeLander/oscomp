// #include <kernel/vfs/namespace.h>
#include <kernel/mm/kmalloc.h>
#include <kernel/util/string.h>
#include <kernel/vfs.h>

/**
 * fs_struct_create - Initialize a new fs_struct
 * @fs: The fs_struct to initialize
 *
 * Sets up a new fs_struct with default values.
 * Returns instance on success, -ENOMEM on memory allocation failure.
 */
struct fs_struct* fs_struct_create(void) {

	struct fs_struct* fs = kmalloc(sizeof(struct fs_struct));
	if (!fs) return ERR_PTR(-ENOMEM);

	/* Initialize locks and reference count */
	spinlock_init(&fs->lock);
	atomic_set(&fs->count, 1);

	/* Initialize pwd and root paths */
	memset(&fs->pwd, 0, sizeof(fs->pwd));
	memset(&fs->root, 0, sizeof(fs->root));

	return fs;
}

/**
 * copy_fs_struct - Create a copy of an fs_struct
 * @old_fs: The fs_struct to copy
 *
 * Creates a new fs_struct with the same values as the old one.
 * Returns a new fs_struct or NULL on failure.
 */
struct fs_struct* copy_fs_struct(struct fs_struct* old_fs) {

	if (!old_fs) return NULL;
	struct fs_struct* new_fs = fs_struct_create();

	/* Copy root and pwd with proper reference counting */
	spinlock_lock(&old_fs->lock);

	if (old_fs->root.dentry) {
		new_fs->root.dentry = dentry_ref(old_fs->root.dentry);
		new_fs->root.mnt = mount_ref(old_fs->root.mnt);
	}

	if (old_fs->pwd.dentry) {
		new_fs->pwd.dentry = dentry_ref(old_fs->pwd.dentry);
		new_fs->pwd.mnt = mount_ref(old_fs->pwd.mnt);
	}

	spinlock_unlock(&old_fs->lock);

	return new_fs;
}

/**
 * fs_struct_unref - Release a reference to an fs_struct
 * @fs: The fs_struct to release
 *
 * Decrements the reference count and frees the structure when it reaches zero.
 */
void fs_struct_unref(struct fs_struct* fs) {
	if (!fs) return;

	/* Decrease reference count */
	if (atomic_dec_and_test(&fs->count)) {
		/* Release references to root and pwd */
		path_destroy(&fs->root);
		path_destroy(&fs->pwd);

		/* Free the structure */
		kfree(fs);
	}
}

/**
 * set_fs_root - Set the root directory for an fs_struct
 * @fs: The fs_struct to modify
 * @path: The new root path
 *
 * Updates the root directory for an fs_struct.
 */
void set_fs_root(struct fs_struct* fs, const struct path* path) {
	struct path old_root;

	if (!fs || !path) return;

	spinlock_lock(&fs->lock);

	/* Save old path for cleaning up */
	old_root = fs->root;

	/* Set new path with proper reference counts */
	fs->root.dentry = dentry_ref(path->dentry);
	fs->root.mnt = mount_ref(path->mnt);

	spinlock_unlock(&fs->lock);

	/* Release references to old path */
	path_destroy(&old_root);
}

/**
 * set_fs_pwd - Set the working directory for an fs_struct
 * @fs: The fs_struct to modify
 * @path: The new working directory path
 *
 * Updates the working directory for an fs_struct.
 */
void set_fs_pwd(struct fs_struct* fs, const struct path* path) {
	struct path old_pwd;

	if (!fs || !path) return;

	spinlock_lock(&fs->lock);

	/* Save old path for cleaning up */
	old_pwd = fs->pwd;

	/* Set new path with proper reference counts */
	fs->pwd.dentry = dentry_ref(path->dentry);
	fs->pwd.mnt = mount_ref(path->mnt);

	spinlock_unlock(&fs->lock);

	/* Release references to old path */
	path_destroy(&old_pwd);
}
