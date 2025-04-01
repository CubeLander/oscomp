#ifndef _VFS_H_
#define _VFS_H_




#include <kernel/vfs/dentry.h>
#include <kernel/vfs/file.h>
#include <kernel/vfs/inode.h>
// #include <kernel/vfs/namespace.h>
#include <kernel/vfs/buffer_head.h>
#include <kernel/vfs/fiemap.h>
#include <kernel/vfs/fstype.h>
#include <kernel/vfs/io_vector.h>
#include <kernel/vfs/kiocb.h>
#include <kernel/vfs/path.h>
#include <kernel/vfs/superblock.h>
#include <kernel/vfs/vfsmount.h>
#include <kernel/vfs/fdtable.h>
#include <kernel/vfs/fs_struct.h>
#include <kernel/vfs/fcontext.h>
#include <kernel/vfs/dcache.h>

#include <kernel/types.h>
#include <kernel/util/list.h>
#include <kernel/util/qstr.h>

/* Forward declarations for VFS structures */
// struct file;
// struct inode;
// struct dentry;
// struct superblock;
struct vfsmount; /* Keep forward declaration for compatibility */
// struct path;
struct nameidata;

/* Path name length limits */
#define PATH_MAX 4096 /* Maximum path length */
#define NAME_MAX 255  /* Maximum filename length */

/* Seek types */
#define SEEK_SET 0 /* Set position from beginning of file */
#define SEEK_CUR 1 /* Set position from current */
#define SEEK_END 2 /* Set position from end of file */

/**
 * Directory entry in a directory listing
 */
struct dirent {
	uint64_t d_ino;          /* Inode number */
	uint64_t d_off;          /* Offset to next dirent */
	unsigned short d_reclen; /* Length of this dirent */
	unsigned char d_type;    /* File type */
	char d_name[];           /* File name (null-terminated) */
};

/* File types for d_type */
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12

/**
 * Filename lookup parameters
 */
struct nameidata {
	struct path path;    /* Path found so far */
	struct qstr last;    /* Last component */
	struct inode* inode; /* Current inode */
	uint32 flags;        /* Lookup flags */
	int32 last_type;     /* Last component type */
};

/*
 * VFS core API functions
 */
/* Initialization functions */
int32 vfs_init(void);


#define vfs_open file_open
#define vfs_close file_close
#define vfs_read file_read
#define vfs_write file_write
#define vfs_llseek file_llseek
#define vfs_readv file_readv
#define vfs_writev file_writev
#define vfs_fsync file_sync

// #define vfs_mkdir dentry_mkdir

struct vfsmount* vfs_kern_mount(struct fstype* type, int32 flags, const char* name, void* data);
struct dentry* vfs_mkdir(struct dentry* parent, const char* name, fmode_t mode);
struct dentry* vfs_mknod(struct dentry* parent, const char* name, mode_t mode, dev_t dev);


/*****上面是调试充分的，下面是不可靠的************************** */
/* File operations */

int32 vfs_stat(const char*, struct kstat*);
int32 vfs_statfs(struct path*, struct kstatfs*);
int32 vfs_utimes(const char*, struct timespec*, int32);

/* Directory operations */
// int32 vfs_mkdir(struct inode*, struct dentry*, fmode_t);
int32 vfs_unlink(struct inode*, struct dentry*);
int32 vfs_rename(struct inode*, struct dentry*, struct inode*, struct dentry*, uint32);

/* Link operations */
int32 vfs_link(struct dentry*, struct inode*, struct dentry*, struct inode**);
int32 vfs_symlink(struct inode*, struct dentry*, const char*);

/* Permission checking */
int32 vfs_permission(struct inode*, int32);
int32 vfs_path_lookup(struct dentry* base_dentry, struct vfsmount* base_mnt, const char* path_str, uint32 flags, struct path* result);

/* File mode checking helpers */
static inline int32 is_dir(fmode_t mode) { return (mode & S_IFMT) == S_IFDIR; }
static inline int32 is_file(fmode_t mode) { return (mode & S_IFMT) == S_IFREG; }
static inline int32 is_symlink(fmode_t mode) { return (mode & S_IFMT) == S_IFLNK; }






#endif /* _VFS_H_ */