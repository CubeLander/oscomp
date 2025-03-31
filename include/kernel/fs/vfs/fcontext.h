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
	const char* fc_filename;
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
	// 注意，这里的obj_path会随着fc_path_remaining的变化而变化
	// 直到*fc_path_remaining == 0

	// 谓语解释器
	int32 fc_action;
	int32 fc_action_flags;
	// 这两个字段是用户定义的，不能随便动。
	const int32 fc_flags;
	const mode_t fc_mode;


	// 宾语,作为过程的输入输出
	struct dentry* fc_sub_dentry;

	const char* fc_input_string;
	int32 fc_input_string_size;

	void* fc_buffer;
	int32 fc_buffer_size;

	dev_t fc_dev;

	// 属性上下文
	struct task_struct* fc_task;
	uid_t fc_uid;
	gid_t fc_gid;
	//struct namespace* fc_ns;
	//struct log;

	// 子任务派发和返回在栈上就行了
};

// clang-format off

#define MONKEY_WITH_ACTION(ctx, action_temp,flag_temp, task_block) do { \
	int32 __saved_action = (ctx)->fc_action;                    \
	int32 __saved_flags = (ctx)->fc_flags;                    \
	(ctx)->fc_action = (action_temp);                           \
	(ctx)->fc_action_flags = (flag_temp);                             \
	task_block;                                                 \
	(ctx)->fc_action = __saved_action;                          \
	(ctx)->fc_action_flags = __saved_flags;                        \
  } while (0)
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

// 注意，指定查找文件或目录的类型时，只能三选一




// clang-format on





enum monkey_action{
	VFS_ACTION_NONE = 0,
	VFS_ACTION_CREATE,
	VFS_ACTION_OPEN,
	VFS_ACTION_MKDIR,
	VFS_ACTION_RMDIR,
	VFS_ACTION_UNLINK,
	VFS_ACTION_SYMLINK,
	VFS_ACTION_RENAME,
	VFS_ACTION_LINK,
	VFS_ACTION_READLINK,
	VFS_ACTION_GETXATTR,
	VFS_ACTION_SETXATTR,
	VFS_ACTION_LISTXATTR,
	VFS_ACTION_REMOVEXATTR,
	VFS_ACTION_GETACL,
	VFS_ACTION_SETACL,
	VFS_ACTION_GETATTR,
	VFS_ACTION_SETATTR,
	VFS_ACTION_FIEMAP,
	VFS_ACTION_LOOKUP,	// 这个是一个伪系统调用
};