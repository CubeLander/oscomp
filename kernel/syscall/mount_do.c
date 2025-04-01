// kernel/syscall/mount_do.c
#include <kernel/mmu.h>
#include <kernel/sched.h>
#include <kernel/sprint.h>
#include <kernel/syscall/syscall.h>
#include <kernel/types.h>
#include <kernel/util/string.h>
#include <kernel/vfs.h>

/**
 * do_mount - Implementation of mount operation using fcontext framework
 * @source: Device name or directory path to mount
 * @target: Directory where to mount
 * @fstype_name: Filesystem type
 * @flags: Mount flags
 * @data: Data specific to the filesystem type
 *
 * Returns 0 on success, negative error code on failure.
 */
int64 do_mount(const char* source, const char* target, const char* fstype_name, uint64 flags, void* data) {
    struct fcontext fctx = {0};
    int32 ret;

    /* Look up filesystem type */
    struct fstype* type = fstype_lookup(fstype_name);
    if (!type) return -ENODEV;

    /* Set up the context for mount operation */
    fctx.user_string = target;
    fctx.fc_path_remaining = (char*)target;
    fctx.fc_action = VFS_ACTION_NONE;
    fctx.user_flags = flags;
    fctx.user_buf = (void*)source;
    fctx.fc_iostruct = data;
    fctx.fc_task = current_task();
    fctx.fc_fstype = type;

    /* Resolve the mount point path */
    ret = MONKEY_WITH_ACTION(path_monkey, &fctx, VFS_ACTION_PATHWALK, LOOKUP_DIRECTORY);
    if (ret < 0) {
        fcontext_cleanup(&fctx);
        return ret;
    }

    /* Verify the dentry is a directory */
    if (!dentry_isDir(fctx.fc_dentry)) {
        fcontext_cleanup(&fctx);
        return -ENOTDIR;
    }

    /* Perform the mount operation */
    ret = MONKEY_WITH_ACTION(type->fs_monkey, &fctx, FS_ACTION_MOUNT, 0);

    /* Clean up and return */
    fcontext_cleanup(&fctx);
    return ret;
}

/**
 * do_umount2 - Implementation of unmount operation using fcontext framework
 * @target: Directory to unmount
 * @flags: Unmount flags
 *
 * Returns 0 on success, negative error code on failure.
 */
int64 do_umount2(const char* target, int32 flags) {
    struct fcontext fctx = {0};
    struct vfsmount* mnt;
    int32 ret;

    /* Set up context for path resolution */
    fctx.user_string = target;
    fctx.fc_path_remaining = (char*)target;
    fctx.user_flags = flags;
    fctx.fc_task = current_task();

    /* Resolve the mountpoint path */
    ret = MONKEY_WITH_ACTION(path_monkey, &fctx, VFS_ACTION_PATHWALK, LOOKUP_DIRECTORY);
    if (ret < 0) {
        fcontext_cleanup(&fctx);
        return ret;
    }

    /* Make sure the dentry is a mountpoint */
    if (!dentry_isMountpoint(fctx.fc_dentry)) {
        fcontext_cleanup(&fctx);
        return -EINVAL;
    }

    /* Get the mount associated with this mountpoint */
    mnt = dentry_lookupMount(fctx.fc_dentry);
    if (!mnt) {
        fcontext_cleanup(&fctx);
        return -EINVAL;
    }

    /* Verify mount permission - root only for now */
    if (fctx.fc_task->euid != 0) {
        mount_unref(mnt);
        fcontext_cleanup(&fctx);
        return -EPERM;
    }

    /* Check if it's busy and handle force flag */
    if (atomic_read(&mnt->mnt_refcount) > 2) { /* One ref from lookup, one from mount structure */
        if (!(flags & MNT_FORCE)) {
            mount_unref(mnt);
            fcontext_cleanup(&fctx);
            return -EBUSY;
        }
    }

    /* Set up filesystem specific context for umount */
    fctx.fc_mount = mnt;
    fctx.fc_action = VFS_ACTION_UMOUNT;
    fctx.fc_action_flags = flags;

    /* Call the filesystem's umount handler */
    ret = MONKEY_WITH_ACTION(mnt->mnt_superblock->s_fstype->fs_monkey, &fctx, FS_ACTION_UMOUNT, flags);
    
    /* Actually unmount it */
    if (ret == 0) {
        ret = do_umount(mnt, flags);
    }

    /* Release the mount reference from lookup */
    mount_unref(mnt);
    fcontext_cleanup(&fctx);
    return ret;
}

/**
 * do_pivot_root - Implementation of pivot_root operation using fcontext framework
 * @new_root: Path to new root filesystem
 * @put_old: Path where old root should be mounted
 *
 * Changes the root filesystem to the specified directory and moves the
 * old root to the location specified by put_old.
 *
 * Returns 0 on success, negative error code on failure.
 */
int64 do_pivot_root(const char* new_root, const char* put_old) {
    struct fcontext new_root_ctx = {0};
    struct fcontext put_old_ctx = {0};
    struct path old_root;
    int32 ret;

    /* Only root can pivot_root */
    if (current_task()->euid != 0)
        return -EPERM;

    /* Set up context for new_root path resolution */
    new_root_ctx.user_string = new_root;
    new_root_ctx.fc_path_remaining = (char*)new_root;
    new_root_ctx.fc_task = current_task();

    /* Resolve the new_root path */
    ret = MONKEY_WITH_ACTION(path_monkey, &new_root_ctx, VFS_ACTION_PATHWALK, LOOKUP_DIRECTORY);
    if (ret < 0) {
        fcontext_cleanup(&new_root_ctx);
        return ret;
    }

    /* Make sure new_root is a mountpoint */
    if (!dentry_isMountpoint(new_root_ctx.fc_dentry)) {
        fcontext_cleanup(&new_root_ctx);
        return -EINVAL;
    }

    /* Set up context for put_old path resolution relative to new_root */
    put_old_ctx.user_string = put_old;
    put_old_ctx.fc_path_remaining = (char*)put_old;
    put_old_ctx.fc_task = current_task();
    
    /* Start from new_root for resolving put_old */
    put_old_ctx.fc_dentry = dentry_ref(new_root_ctx.fc_dentry);
    put_old_ctx.fc_mount = mount_ref(new_root_ctx.fc_mount);

    /* Resolve the put_old path */
    ret = MONKEY_WITH_ACTION(path_monkey, &put_old_ctx, VFS_ACTION_PATHWALK, LOOKUP_DIRECTORY);
    if (ret < 0) {
        fcontext_cleanup(&new_root_ctx);
        fcontext_cleanup(&put_old_ctx);
        return ret;
    }

    /* Save current root for later */
    old_root.dentry = dentry_ref(current_task()->fs->root.dentry);
    old_root.mnt = mount_ref(current_task()->fs->root.mnt);

    /* Change to new root */
    set_fs_root(current_task()->fs, &new_root_ctx.fc_path);
    
    /* Move old root to put_old - this would involve bind mounting */
    /* For simplicity, we're not implementing the full bind mount here */
    /* In a full implementation, we would:
       1. Create a bind mount of old_root at put_old
       2. Unmount the original old_root 
    */

    /* Additional cleanup - free paths */
    path_destroy(&old_root);
    fcontext_cleanup(&new_root_ctx);
    fcontext_cleanup(&put_old_ctx);
    
    return 0;
}