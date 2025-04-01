#include <kernel/vfs.h>


/**
 * Generic filesystem intent handlers for different operations
 */
static int32 fs_intent_mount(struct fcontext* fctx);
static int32 fs_intent_umount(struct fcontext* fctx);
static int32 fs_intent_initfs(struct fcontext* fctx);
static int32 fs_intent_exitfs(struct fcontext* fctx);

/**
 * Filesystem intent table - maps action IDs to handler functions
 */
static monkey_intent_handler_t fs_intent_table[VFS_ACTION_MAX] = {
    [FS_ACTION_MOUNT]  = fs_intent_mount,
    [FS_ACTION_UMOUNT] = fs_intent_umount,
    [FS_ACTION_INITFS] = fs_intent_initfs,
    [FS_ACTION_EXITFS] = fs_intent_exitfs,
    /* Additional handlers can be added here */
};

/**
 * fs_monkey - Generic filesystem context handler
 * @fctx: Filesystem context
 *
 * Dispatches filesystem operations to the appropriate handler
 * based on the action specified in the context.
 *
 * Returns 0 on success, negative error code on failure.
 */
int32 fs_monkey(struct fcontext* fctx) {
    if (!fctx || !fctx->fc_fstype) {
        return -EINVAL;
    }

    /* Validate action is within range */
    if (fctx->fc_action >= VFS_ACTION_MAX || fctx->fc_action < 0) {
        return -EINVAL;
    }

    /* Get the handler for this action */
    monkey_intent_handler_t handler = fs_intent_table[fctx->fc_action];
    
    /* Call the handler if available, otherwise return error */
    if (handler) {
        return handler(fctx);
    }
    
    return -ENOSYS; /* Function not implemented */
}