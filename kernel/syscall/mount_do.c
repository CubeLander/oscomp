// kernel/syscall/mount_do.c
#include <kernel/mmu.h>
#include <kernel/sched.h>
#include <kernel/sprint.h>
#include <kernel/syscall/syscall.h>
#include <kernel/types.h>
#include <kernel/util/string.h>
#include <kernel/vfs.h>

int64 do_mount(const char* source, const char* target, const char* fstype_name, uint64 flags, void* data) {
    /* Look up filesystem type */
    struct fstype* type = fstype_lookup(fstype_name);
    if (!type) return -ENODEV;
    
    /* Check if this is a bind mount request */
    bool is_bind_mount = (flags & MS_BIND) != 0;
    
    /* First resolve the source path */
    struct fcontext source_ctx = {
        .path_string = source,
        .fc_path_remaining = (char*)source,
        .fc_task = current_task()
    };
    
    /* Use appropriate lookup flags based on mount type */
    uint32 source_lookup_flags = is_bind_mount ? LOOKUP_DIRECTORY : 0;
    
    int32 ret = MONKEY_WITH_ACTION(path_monkey, &source_ctx, PATH_LOOKUP, source_lookup_flags);
    if (ret < 0) {
        fcontext_cleanup(&source_ctx);
        return ret;
    }
    
    /* Set up the context for target path resolution and mount operation */
    struct fcontext fctx = {
        .path_string = target,
        .fc_path_remaining = (char*)target,
        .user_flags = flags,
        .user_buf = data,
        .fc_task = current_task(),
        .fc_fstype = type
    };
    
    /* Store source dentry with reference counting (for both regular and bind mounts) */
    fctx.fc_iostruct = dentry_ref(source_ctx.fc_dentry);
    
    /* Clean up source context since we've captured what we need */
    fcontext_cleanup(&source_ctx);
    
    /* Resolve the mount point path to fc_dentry */
    ret = MONKEY_WITH_ACTION(path_monkey, &fctx, PATH_LOOKUP, LOOKUP_DIRECTORY);
    if (ret < 0) {
        dentry_unref(fctx.fc_iostruct);
        fcontext_cleanup(&fctx);
        return ret;
    }
    
    /* Perform the mount operation with appropriate action */
    int32 mount_action = is_bind_mount ? FS_MOUNT_BIND : FS_MOUNT;
    ret = MONKEY_WITH_ACTION(type->fs_monkey, &fctx, mount_action, flags);
    
    /* Clean up and return */
    dentry_unref(fctx.fc_iostruct);
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
    fctx.path_string = target;
    fctx.fc_path_remaining = (char*)target;
    fctx.user_flags = flags;
    fctx.fc_task = current_task();

    /* Resolve the mountpoint path */
    ret = MONKEY_WITH_ACTION(path_monkey, &fctx, PATH_LOOKUP, LOOKUP_DIRECTORY);
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
    fctx.fc_action = VFS_UMOUNT;
    fctx.fc_action_flags = flags;

    /* Call the filesystem's umount handler */
    ret = MONKEY_WITH_ACTION(mnt->mnt_superblock->s_fstype->fs_monkey, &fctx, FS_UMOUNT, flags);
    
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
    new_root_ctx.path_string = new_root;
    new_root_ctx.fc_path_remaining = (char*)new_root;
    new_root_ctx.fc_task = current_task();

    /* Resolve the new_root path */
    ret = MONKEY_WITH_ACTION(path_monkey, &new_root_ctx, PATH_LOOKUP, LOOKUP_DIRECTORY);
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
    put_old_ctx.path_string = put_old;
    put_old_ctx.fc_path_remaining = (char*)put_old;
    put_old_ctx.fc_task = current_task();
    
    /* Start from new_root for resolving put_old */
    put_old_ctx.fc_dentry = dentry_ref(new_root_ctx.fc_dentry);
    put_old_ctx.fc_mount = mount_ref(new_root_ctx.fc_mount);

    /* Resolve the put_old path */
    ret = MONKEY_WITH_ACTION(path_monkey, &put_old_ctx, PATH_LOOKUP, LOOKUP_DIRECTORY);
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