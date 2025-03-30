// monkeyos/include/kernel/fs/io_context.h
#ifndef MONKEY_IO_CONTEXT_H
#define MONKEY_IO_CONTEXT_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/fs/vfs_types.h>
#include <kernel/fs/dentry.h>
#include <kernel/fs/inode.h>
#include <kernel/fs/superblock.h>
#include <kernel/fs/file.h>
#include <kernel/task/task.h>

/*
 * IO action type - what the current task wants to do
 */
typedef enum io_action_type {
    IOACT_READ,
    IOACT_WRITE,
    IOACT_MKDIR,
    IOACT_UNLINK,
    IOACT_LOOKUP,
    IOACT_CREATE,
    IOACT_RENAME,
    IOACT_OPEN,
    IOACT_SYMLINK,
    IOACT_FSYNC,
    IOACT_IOCTL,
    IOACT_GENERIC_META,
    IOACT_INVALID
} io_action_type_t;

/*
 * IO task bits - what behaviors this request expects the system to perform
 */
#define IOTASK_RESOLVE_PATH     0x001
#define IOTASK_LOCK_INODE       0x002
#define IOTASK_LOAD_BLOCKS      0x004
#define IOTASK_CACHE_CHECK      0x008
#define IOTASK_TRIGGER_IO       0x010
#define IOTASK_FSYNC_ON_EXIT    0x020
#define IOTASK_LOG_ACTION       0x040
#define IOTASK_NOTIFY_WATCHER   0x080

/*
 * IO context - a complete representation of a filesystem request
 */
typedef struct io_context {
    // 行为类型与语义标记
    io_action_type_t action;
    uint32_t flags;      // open flags, e.g. O_DIRECT, O_SYNC
    uint32_t task_bits;  // IOTASK_* bitmap to indicate expected behavior

    // 任务发起方
    struct task_struct *task;  // current task

    // 资源引用（自动维护引用计数）
    struct dentry *target_dentry;
    struct dentry *parent_dentry;
    struct inode *target_inode;
    struct inode *parent_inode;
    struct superblock *sb;
    struct file *file;

    // 通用读写参数
    void *rw_buf;
    size_t rw_len;
    loff_t rw_pos;

    // 创建相关参数
    mode_t create_mode;

    // rename 使用
    const char *rename_newname;
    struct dentry *rename_newparent;

    // ioctl 参数
    uint32_t ioctl_cmd;
    uint64_t ioctl_arg;

    // 通用返回值容器
    ssize_t result_size;
    int32_t result_code;

    // 可扩展字段
    void *fs_private;
} io_context_t;

#endif // MONKEY_IO_CONTEXT_H
