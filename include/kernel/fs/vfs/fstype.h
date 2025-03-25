#ifndef _FSTYPE_H
#define _FSTYPE_H

#include <kernel/types.h>
#include <util/list.h>
#include <util/spinlock.h>

/* File system types */
struct fsType {
	const char* fs_name;
	int fs_flags;

	struct list_node fs_globalFsListNode;

	struct list_head fs_list_sb;
	spinlock_t fs_list_s_lock;
	/* Add to struct fsType */
	unsigned long fs_capabilities; /* Capabilities like case sensitivity */

	int (*fs_fill_sb)(struct superblock* sb, void* data, int silent);
	struct superblock* (*fs_mount_sb)(struct fsType*, int, const char* mount_path, void*);
	void (*fs_kill_sb)(struct superblock*);

	int (*fs_init)(void);  /* Called during registration */
	void (*fs_exit)(void); /* Called during unregistration */
};

// struct fs_mount_context {
// 	char* mount_options;
// 	unsigned long flags;
// 	void* fs_specific_data;
// };

#define fsType_fill_sb(type, sb, data, silent) ((type)->fs_fill_sb ? (type)->fs_fill_sb(sb, data, silent) : -ENOSYS)
#define fsType_mount_sb(type, flags, mount_path, data) ((type)->fs_mount_sb ? (type)->fs_mount_sb(type, flags, mount_path, data) : NULL)
#define fsType_kill_sb(type, sb) ((type)->fs_kill_sb ? (type)->fs_kill_sb(sb) : (void)0)
#define fsType_init(type) ((type)->fs_init ? (type)->fs_init() : 0)
#define fsType_exit(type) ((type)->fs_exit ? (type)->fs_exit() : (void)0)

//int parse_mount_options(const char* options, struct fs_mount_context* parsed);

int fsType_register_all(void);
int fsType_register(struct fsType*);
int fsType_unregister(struct fsType*);

struct superblock* fsType_acquireSuperblock(struct fsType* type, dev_t dev_id, void* fs_data);
struct superblock* fsType_createMount(struct fsType* type, int flags, const char* dev_name, void* data);
struct fsType* fsType_lookup(const char* name);

/* Filesystem type flags */
#define FS_REQUIRES_DEV 0x01       /* Filesystem requires a block device */
#define FS_BINARY_MOUNTDATA 0x02   /* Binary mount data instead of text */
#define FS_HAS_SUBTYPE 0x04        /* Subtype field valid */
#define FS_USERNS_MOUNT 0x08       /* Can be mounted in userns */
#define FS_RENAME_DOES_D_MOVE 0x20 /* FS will handle d_move in rename */

/* Capability flags */
#define FS_CAP_CASE_INSENSITIVE 1
#define FS_CAP_ATOMIC_RENAME 2

const char* fsType_error_string(int error_code);

#endif