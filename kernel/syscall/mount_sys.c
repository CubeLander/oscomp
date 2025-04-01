// kernel/syscall/mount_sys.c
#include <kernel/mm/kmalloc.h>
#include <kernel/mm/uaccess.h>
#include <kernel/sched/process.h>
#include <kernel/sprint.h>
#include <kernel/syscall/syscall.h>
#include <kernel/types.h>
#include <kernel/util/string.h>
#include <kernel/vfs.h>

/* Add these entries to the syscall_table in kernel/syscall/syscall.c */


/**
 * sys_mount - Mount a filesystem
 * @source: Device name or directory path to mount
 * @target: Directory where to mount
 * @fstype: Filesystem type
 * @flags: Mount flags
 * @data: Data specific to the filesystem type
 *
 * Returns 0 on success, negative error code on failure.
 */
int64 sys_mount(const char* source, const char* target, const char* fstype, uint64 flags, const void* data) {
    char* ksource = NULL;
    char* ktarget = NULL;
    char* kfstype = NULL;
    void* kdata = NULL;
    int64 ret;

    /* Validate required arguments */
    if (!target || !fstype) return -EINVAL;

    /* Allocate and copy target path (required) */
    ktarget = kmalloc(PATH_MAX);
    if (!ktarget) return -ENOMEM;
    if (copy_from_user(ktarget, target, PATH_MAX)) {
        ret = -EFAULT;
        goto out_free;
    }

    /* Allocate and copy fstype (required) */
    kfstype = kmalloc(PATH_MAX);
    if (!kfstype) {
        ret = -ENOMEM;
        goto out_free;
    }
    if (copy_from_user(kfstype, fstype, PATH_MAX)) {
        ret = -EFAULT;
        goto out_free;
    }

    /* If source is provided, allocate and copy it */
    if (source) {
        ksource = kmalloc(PATH_MAX);
        if (!ksource) {
            ret = -ENOMEM;
            goto out_free;
        }
        if (copy_from_user(ksource, source, PATH_MAX)) {
            ret = -EFAULT;
            goto out_free;
        }
    }

    /* Copy mount data if provided */
    if (data) {
        /* Assuming data is a null-terminated string */
        kdata = kmalloc(PATH_MAX);
        if (!kdata) {
            ret = -ENOMEM;
            goto out_free;
        }
        if (copy_from_user(kdata, data, PATH_MAX)) {
            ret = -EFAULT;
            goto out_free;
        }
    }

    /* Call the internal implementation */
    ret = do_mount(ksource, ktarget, kfstype, flags, kdata);

out_free:
    /* Free allocated memory */
    if (ksource) kfree(ksource);
    if (ktarget) kfree(ktarget);
    if (kfstype) kfree(kfstype);
    if (kdata) kfree(kdata);
    return ret;
}

/**
 * sys_umount - Unmount a filesystem
 * @target: Directory to unmount
 * @flags: Unmount flags
 *
 * Returns 0 on success, negative error code on failure.
 */
int64 sys_umount(const char* target, int32 flags) {
    char* ktarget;
    int64 ret;

    if (!target) return -EINVAL;

    /* Allocate and copy target path */
    ktarget = kmalloc(PATH_MAX);
    if (!ktarget) return -ENOMEM;
    if (copy_from_user(ktarget, target, PATH_MAX)) {
        kfree(ktarget);
        return -EFAULT;
    }

    /* Call internal implementation */
    ret = do_umount2(ktarget, flags);

    kfree(ktarget);
    return ret;
}

/**
 * sys_umount2 - Unmount a filesystem with flags
 * @target: Directory to unmount
 * @flags: Unmount flags
 *
 * Alternative version of umount with explicit flags parameter.
 * Returns 0 on success, negative error code on failure.
 */
int64 sys_umount2(const char* target, int32 flags) {
    /* Just forward to sys_umount which already handles flags */
    return sys_umount(target, flags);
}

/**
 * sys_pivot_root - Change the root filesystem
 * @new_root: Path to new root filesystem
 * @put_old: Path where old root should be mounted
 *
 * Changes the root filesystem to the specified directory and moves the
 * old root to the location specified by put_old.
 *
 * Returns 0 on success, negative error code on failure.
 */
int64 sys_pivot_root(const char* new_root, const char* put_old) {
    char* knew_root;
    char* kput_old;
    int64 ret;

    if (!new_root || !put_old) return -EINVAL;

    /* Allocate and copy paths */
    knew_root = kmalloc(PATH_MAX);
    if (!knew_root) return -ENOMEM;
    kput_old = kmalloc(PATH_MAX);
    if (!kput_old) {
        kfree(knew_root);
        return -ENOMEM;
    }

    if (copy_from_user(knew_root, new_root, PATH_MAX) ||
        copy_from_user(kput_old, put_old, PATH_MAX)) {
        kfree(knew_root);
        kfree(kput_old);
        return -EFAULT;
    }

    /* Call internal implementation */
    ret = do_pivot_root(knew_root, kput_old);

    kfree(knew_root);
    kfree(kput_old);
    return ret;
}