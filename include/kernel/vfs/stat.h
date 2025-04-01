#ifndef _STAT_H
#define _STAT_H

#include "forward_declarations.h"
#include <kernel/vfs/path.h>



/* File dirent types (for directory entries) */
#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8
#define DT_LNK     10
#define DT_SOCK    12

/* Attribute flags for iattr */
#define ATTR_MODE     (1 << 0)
#define ATTR_UID      (1 << 1)
#define ATTR_GID      (1 << 2)
#define ATTR_SIZE     (1 << 3)
#define ATTR_ATIME    (1 << 4)
#define ATTR_MTIME    (1 << 5)
#define ATTR_CTIME    (1 << 6)
#define ATTR_ATIME_SET (1 << 7)
#define ATTR_MTIME_SET (1 << 8)
#define ATTR_FORCE    (1 << 9)

/**
 * struct kstat - Kernel file stat structure
 * Holds all the filesystem metadata information about a file
 */
struct kstat {
    uint64     dev;     /* Device ID containing file */
    uint64     ino;     /* File inode number */
    fmode_t      mode;    /* File mode and type */
    uint32     nlink;   /* Number of hard links */
    uid_t        uid;     /* User ID of owner */
    gid_t        gid;     /* Group ID of owner */
    uint64     rdev;    /* Device ID (if special file) */
    uint64     size;    /* File size in bytes */
    uint32     blksize; /* Block size for filesystem I/O */
    uint64     blocks;  /* Number of 512B blocks allocated */
    
    /* Time values with nanosecond precision */
    struct timespec atime; /* Last access time */
    struct timespec mtime; /* Last modification time */
    struct timespec ctime; /* Last status change time */
    struct timespec btime; /* Creation (birth) time */
};

/**
 * struct kstatfs - Internal kernel filesystem statistics
 * Used by the VFS layer for all filesystem operations
 */
struct kstatfs {
    uint64 f_type;    /* Type of filesystem */
    uint64 f_bsize;   /* Optimal transfer block size */
    uint64 f_blocks;  /* Total data blocks in filesystem */
    uint64 f_bfree;   /* Free blocks in filesystem */
    uint64 f_bavail;  /* Free blocks available to unprivileged user */
    uint64 f_files;   /* Total file nodes in filesystem */
    uint64 f_ffree;   /* Free file nodes in filesystem */
    uint64 f_namelen; /* Maximum length of filenames */
    uint64 f_frsize;  /* Fragment size */
    uint64 f_flags;   /* Mount flags */
};


/* Function prototypes */
int32 vfs_stat(const char* path, struct kstat* stat);
int32 vfs_statfs(struct path* path, struct kstatfs* kstatfs);
int32 vfs_utimes(const char* path, struct timespec* times, int32 flags);

#endif /* _STAT_H */