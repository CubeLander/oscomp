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
	int (*fs_register)(struct fsType* fs);

	int (*fs_unregister)(struct fsType* fs);
};

// struct fs_mount_context {
// 	char* mount_options;
// 	unsigned long flags;
// 	void* fs_specific_data;
// };

//int parse_mount_options(const char* options, struct fs_mount_context* parsed);

int fsType_register_all(void);
int fsType_register(struct fsType* fs);
int fsType_unregister(struct fsType* fs);

struct superblock* fsType_acquireSuperblock(struct fsType* type, dev_t dev_id, void* fs_data);
struct superblock* fsType_createMount(struct fsType* type, int flags, const char* dev_name, void* data);
struct fsType* fsType_lookup(const char* name);



const char* fsType_error_string(int error_code);


#endif