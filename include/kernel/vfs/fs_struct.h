#ifndef _FS_STRUCT_H
#define _FS_STRUCT_H

#include <kernel/types.h>
#include <kernel/vfs.h>
#include <kernel/vfs/file.h>
#include <kernel/util/atomic.h>
#include <kernel/util/spinlock.h>



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
struct fs_struct *fs_struct_create(void);
struct fs_struct *copy_fs_struct(struct fs_struct *old_fs);
void fs_struct_unref(struct fs_struct *fs);
void set_fs_root(struct fs_struct *fs, const struct path *path);
void set_fs_pwd(struct fs_struct *fs, const struct path *path);


#endif