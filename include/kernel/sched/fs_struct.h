#ifndef _FS_STRUCT_H
#define _FS_STRUCT_H

#include <kernel/types.h>
#include <kernel/fs/vfs/vfs.h>
#include <kernel/fs/vfs/file.h>
#include <util/atomic.h>
#include <util/spinlock.h>



/**
 * Filesystem information for a process
 */
struct fs_struct {
	struct path root;          /* Root directory */
	struct path pwd;           /* Current working directory */
	struct mnt_namespace* mnt_ns;  /* Mount namespace */
	spinlock_t lock;           /* Lock for pwd/root */
	atomic_t count;            /* Reference count */
};

/* Filesystem info management */
struct fs_struct *setup_fs_struct(void);
struct fs_struct *copy_fs_struct(struct fs_struct *old_fs);
void put_fs_struct(struct fs_struct *fs);
void set_fs_root(struct fs_struct *fs, const struct path *path);
void set_fs_pwd(struct fs_struct *fs, const struct path *path);


#endif