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
