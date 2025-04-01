#ifndef _FSTYPE_H
#define _FSTYPE_H

#include "forward_declarations.h"
#include <kernel/util/list.h>
#include <kernel/util/spinlock.h>

/* File system types */
struct fstype {
	const char* fs_name;
	int32 fs_flags;

	struct list_node fs_globalFsListNode;

	struct list_head fs_list_superblock;
	spinlock_t fs_list_superblock_lock;
	/* Add to struct fstype */
	uint64 fs_capabilities; /* Capabilities like case sensitivity */

	int32 (*fs_monkey)(struct fcontext* fctx);
};

// int32 parse_mount_options(const char* options, struct fs_mount_context* parsed);

int32 fstype_register_all(void);
int32 fstype_register(struct fstype*);
int32 fstype_unregister(struct fstype*);

struct superblock* fstype_mount(struct fstype* type, int32 flags, dev_t dev_id, void* fs_data);
int32 fstype_fill_sb(struct fstype* type, struct superblock* sb, void* data, int32 flags);
struct fstype* fstype_lookup(const char* name);

/* Filesystem type flags */
#define FS_REQUIRES_DEV 0x01       /* Filesystem requires a block device */
#define FS_BINARY_MOUNTDATA 0x02   /* Binary mount data instead of text */
#define FS_HAS_SUBTYPE 0x04        /* Subtype field valid */
#define FS_USERNS_MOUNT 0x08       /* Can be mounted in userns */
#define FS_RENAME_DOES_D_MOVE 0x20 /* FS will handle d_move in rename */

/* Capability flags */
#define FS_CAP_CASE_INSENSITIVE 1
#define FS_CAP_ATOMIC_RENAME 2

#endif