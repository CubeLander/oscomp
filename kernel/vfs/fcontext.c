#include <kernel/vfs.h>

/**
 * open_to_lookup_flags - Convert open() flags to lookup flags
 * @open_flags: Flags passed to open()
 *
 * Converts standard open() flags to corresponding lookup flags
 * used by the path walking code.
 *
 * Returns: lookup flags
 */
int32 open_to_lookup_flags(int32 open_flags) {
    int32 lookup_flags = 0;
    
    /* Handle creation flags */
    if (open_flags & O_CREAT)
        lookup_flags |= LOOKUP_CREATE;
    
    if ((open_flags & O_CREAT) && (open_flags & O_EXCL))
        lookup_flags |= LOOKUP_EXCL;
    
    /* Handle link following */
    if (!(open_flags & O_NOFOLLOW))
        lookup_flags |= LOOKUP_FOLLOW;
    
    /* Set directory vs file requirements */
    if (open_flags & O_DIRECTORY)
        lookup_flags |= LOOKUP_DIRECTORY;
    
    /* For truncate operations, we need to follow mountpoints */
    if (open_flags & O_TRUNC)
        lookup_flags |= LOOKUP_MOUNTPOINT;
    
    /* Track if we're doing an open operation */
    lookup_flags |= LOOKUP_OPEN;
    
    /* For atomicity in some operations */
    if ((open_flags & (O_CREAT|O_EXCL)) == (O_CREAT|O_EXCL))
        lookup_flags |= LOOKUP_REVAL;
    
    /* Allow down traversal from current dir */
    lookup_flags |= LOOKUP_DOWN;
    
    return lookup_flags;
}


/**
 * fcontext_cleanup - Release all resources associated with a file context
 * @fctx: File context to clean up
 *
 * Safely releases all non-NULL resources pointed to by the fcontext structure,
 * including dentries, mounts, files, and any dynamically allocated buffers.
 */
void fcontext_cleanup(struct fcontext* fctx) {
    if (!fctx)
        return;

    /* Clean up file reference */
    if (fctx->fc_file) {
        file_unref(fctx->fc_file);
        fctx->fc_file = NULL;
    }

    /* Clean up path components */
    if (fctx->fc_dentry) {
        dentry_unref(fctx->fc_dentry);
        fctx->fc_dentry = NULL;
    }
    
    if (fctx->fc_mount) {
        mount_unref(fctx->fc_mount);
        fctx->fc_mount = NULL;
    }

	if(fctx->user_buf){
		kfree(fctx->user_buf);
		fctx->user_buf = NULL;
	}


    /* Clean up string buffer if it was dynamically allocated 
     * Note: Only free fc_charbuf if it's not a substring of fc_path_remaining
     */
    if (fctx->fc_charbuf && (fctx->fc_charbuf < fctx->fc_path_remaining || 
        fctx->fc_charbuf > fctx->fc_path_remaining + strlen(fctx->fc_path_remaining))) {
        kfree(fctx->fc_charbuf);
        fctx->fc_charbuf = NULL;
    }


    /* Reset remaining fields */
    fctx->fc_strlen = 0;
    fctx->fc_hash = 0;
    fctx->user_buf_size = 0;
}