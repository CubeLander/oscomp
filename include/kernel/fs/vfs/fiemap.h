#ifndef _FIEMAP_H
#define _FIEMAP_H
#include "forward_declarations.h"
// 用来支持对于文件在磁盘中的表示查询

/* Structure for the fiemap extent information */
struct fiemap_extent_info {
    uint32 fi_flags;         /* Flags for the current operation */
    uint32 fi_extents_mapped; /* Number of extents mapped so far */
    uint32 fi_extents_max;    /* Maximum number of extents to map */
    struct fiemap_extent *fi_extents_start; /* Pointer to array of extents */
};

/* Structure for individual extent information */
struct fiemap_extent {
    uint64 fe_logical;  /* Logical offset in file in bytes */
    uint64 fe_physical; /* Physical offset on disk in bytes */
    uint64 fe_length;   /* Length in bytes */
    uint64 fe_reserved64[2];
    uint64 fe_flags;    /* Flags for this extent */
    uint64 fe_reserved[3];
};

/* Fiemap flags */
#define FIEMAP_FLAG_SYNC        0x0001 /* Sync file data before map */
#define FIEMAP_FLAG_XATTR       0x0002 /* Map extended attribute tree */
#define FIEMAP_FLAG_CACHE       0x0004 /* Request caching of the extents */

/* Fiemap extent flags */
#define FIEMAP_EXTENT_LAST          0x0001 /* Last extent in file */
#define FIEMAP_EXTENT_UNKNOWN       0x0002 /* Data location unknown */
#define FIEMAP_EXTENT_DELALLOC      0x0004 /* Location still pending */
#define FIEMAP_EXTENT_ENCODED       0x0008 /* Data is encoded */
#define FIEMAP_EXTENT_DATA_ENCRYPTED 0x0080 /* Data is encrypted */
#define FIEMAP_EXTENT_NOT_ALIGNED   0x0100 /* Extent offsets may not be block aligned */
#define FIEMAP_EXTENT_DATA_INLINE   0x0200 /* Data mixed with metadata */
#define FIEMAP_EXTENT_DATA_TAIL     0x0400 /* Multiple files in block */
#define FIEMAP_EXTENT_UNWRITTEN     0x0800 /* Space allocated, but no data */
#define FIEMAP_EXTENT_MERGED        0x1000 /* File does not natively support extents */



#endif