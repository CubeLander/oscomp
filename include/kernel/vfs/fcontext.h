#pragma once
#include "forward_declarations.h"

// Welcome to the Monkey Republic.
//
// This fcontext represents a civic action request.
// Each monkey module (path, dentry, inode, device...) acts as a civil servant
// executing its part of the task.
// The system is composed by trust, local validation, and inter-monkey diplomacy.
//
// No monarchs, no magic. Just reliable behavior.

struct fcontext{
	// 主语和主语路径解释
	const char* path_string;
	char* fc_path_remaining;
	int32 fc_fd;
	struct file* fc_file;
	union {
		struct path fc_path;
		struct {
			struct dentry* fc_dentry;
			struct vfsmount* fc_mount;
		};
	};
	union {
		struct qstr fc_string;
		struct {
			char *fc_charbuf;
			uint32 fc_strlen;
			uint32 fc_hash;
		};
	};
	struct fstype* fc_fstype;
	struct superblock* fc_superblock;
	// 注意，这里的obj_path会随着fc_path_remaining的变化而变化
	// 直到*fc_path_remaining == 0

	// 谓语解释器
	int32 fc_action;
	int32 fc_action_flags;

	// 这两个字段是用户定义的，不能随便动。
	int32 user_flags;
	mode_t user_mode;


	// 宾语,作为过程的输入输出
	void* fc_iostruct;	
	// 保存内部临时意图调用的输出结构体
	// 如何解释和使用这个指针，取决于fc_action
	// 就像 syscall 的 rax，但更自由、更通用、更具意图性

	void* user_buf;
	int32 user_buf_size;

	// 属性上下文
	struct task_struct* fc_task;
	uid_t fc_uid;
	gid_t fc_gid;
	//struct namespace* fc_ns;
	//struct log;

	// 子任务派发和返回在栈上就行了
};

#define fc_is_last (*fc_path_remaining == 0)

typedef int32 (*monkey_intent_handler_t)(struct fcontext* fctx);
void fcontext_cleanup(struct fcontext* fctx);

// clang-format off

#define MONKEY_WITH_ACTION(monkey_fn, ctx, action_temp, flag_temp) ({  \
    int32 __saved_action = (ctx)->fc_action;                           \
    int32 __saved_flags  = (ctx)->fc_action_flags;                     \
    (ctx)->fc_action      = (action_temp);                             \
    (ctx)->fc_action_flags = (flag_temp);                              \
    int32 __ret = monkey_fn(ctx);                                      \
    (ctx)->fc_action      = __saved_action;                            \
    (ctx)->fc_action_flags = __saved_flags;                            \
    __ret;                                                             \
})
// clang-format on

// clang-format off
/* pathwalk mode */
#define LOOKUP_FOLLOW			0x0001	/* follow links at the end */
#define LOOKUP_DIRECTORY		0x0002	/* require a directory */
#define LOOKUP_AUTOMOUNT		0x0004  /* force terminal automount */
#define LOOKUP_EMPTY			0x4000	/* accept empty path [user_... only] */
#define LOOKUP_DOWN				0x8000	/* follow mounts in the starting point */
#define LOOKUP_MOUNTPOINT		0x0080	/* follow mounts in the end */

#define LOOKUP_REVAL			0x0020	/* tell ->d_revalidate() to trust no cache */
#define LOOKUP_RCU				0x0040	/* RCU pathwalk mode; semi-internal */

/* These tell filesystem methods that we are dealing with the final component... */
#define LOOKUP_OPEN				0x0100	/* ... in open */
#define LOOKUP_CREATE			0x0200	/* ... in object creation */
#define LOOKUP_EXCL				0x0400	/* ... in exclusive creation */
#define LOOKUP_RENAME_TARGET	0x0800	/* ... in destination of rename() */

/* internal use only */
#define LOOKUP_PARENT			0x0010

/* Scoping flags for lookup. */
#define LOOKUP_NO_SYMLINKS		0x010000 /* No symlink crossing. */
#define LOOKUP_NO_MAGICLINKS	0x020000 /* No nd_jump_link() crossing. */
#define LOOKUP_NO_XDEV			0x040000 /* No mountpoint crossing. */
#define LOOKUP_BENEATH			0x080000 /* No escaping from starting point. */
#define LOOKUP_IN_ROOT			0x100000 /* Treat dirfd as fs root. */
#define LOOKUP_CACHED			0x200000 /* Only do cached lookup */

#define LOOKUP_MONKEY_FILE 		0x4000000000000000 /* require a regular file */
#define LOOKUP_MONKEY_SYMLINK	0x8000000000000000 /* require a symlink */

#define MOUNT_ROOTFS 	0x00000001 /* mount as rootfs */

// 注意，指定查找文件或目录的类型时，只能三选一

// clang-format on

#define VFS_ACTION_MAX 100

enum monkey_action{
	VFS_NONE = 0,
	VFS_CREATE,
	VFS_OPEN,
	VFS_CLOSE,
	VFS_MKDIR,
	VFS_MKNOD,
	VFS_RMDIR,
	VFS_UNLINK,
	VFS_SYMLINK,
	VFS_RENAME,
	VFS_LINK,
	VFS_READLINK,
	VFS_GETXATTR,
	VFS_SETXATTR,
	VFS_LISTXATTR,
	VFS_REMOVEXATTR,
	VFS_GETACL,
	VFS_SETACL,
	VFS_GETATTR,
	VFS_SETATTR,
	VFS_FIEMAP,
	VFS_LOOKUP,	// 这个是一个伪系统调用
	VFS_READ,
	VFS_WRITE,
	VFS_UMOUNT,

	PATH_LOOKUP,

	// fs_monkey需要回应这些意图
	FS_INITFS,
	FS_EXITFS,
	FS_MOUNT,
	FS_MOUNT_BIND,
	FS_UMOUNT,
	FS_CREATE_SB,

	FD_OPEN,
	FD_CLOSE,


	INODE_READ,
	INODE_WRITE,
	INODE_LSEEK,
	INODE_SETXATTR,
	INODE_REMOVEXATTR,
	INODE_LISTXATTR,
	INODE_GETXATTR,

	DENTRY_LOOKUP,

    SB_ALLOC_INODE = 300,
    SB_DESTROY_INODE,
    SB_WRITE_INODE,
    SB_EVICT_INODE,
    SB_SYNC_FS,
    SB_STATFS,
    SB_PUT_SUPER,

};