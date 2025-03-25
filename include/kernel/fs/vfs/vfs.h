#ifndef _VFS_H_
#define _VFS_H_

#include <kernel/fs/vfs/dentry.h>
#include <kernel/fs/vfs/file.h>
#include <kernel/fs/vfs/inode.h>
#include <kernel/fs/vfs/namespace.h>
#include <kernel/fs/vfs/path.h>
#include <kernel/fs/vfs/superblock.h>
#include <kernel/fs/vfs/kiocb.h>
#include <kernel/fs/vfs/io_vector.h>
#include <kernel/fs/vfs/fstype.h>
#include <kernel/fs/vfs/address_space.h>
#include <kernel/fs/vfs/writeback.h>

#include <kernel/types.h>
#include <util/list.h>
#include <util/qstr.h>

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

/* File open flags */
#define O_RDONLY 00
#define O_WRONLY 01
#define O_RDWR 02
#define O_CREAT 0100
#define O_EXCL 0200
#define O_NOCTTY 0400
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_NONBLOCK 04000
#define O_DIRECTORY 0200000

/* Seek types */
#define SEEK_SET 0 /* Set position from beginning of file */
#define SEEK_CUR 1 /* Set position from current */
#define SEEK_END 2 /* Set position from end of file */

/**
 * Directory entry in a directory listing
 */
struct dirent {
	uint64_t d_ino;		 /* Inode number */
	uint64_t d_off;		 /* Offset to next dirent */
	unsigned short d_reclen; /* Length of this dirent */
	unsigned char d_type;	 /* File type */
	char d_name[];		 /* File name (null-terminated) */
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
 * Directory context for readdir operations
 */
struct dir_context {
	int (*actor)(struct dir_context*, const char*, int, loff_t, uint64_t,
		     unsigned);
	loff_t pos; /* Current position in directory */
};
/**
 * Internal kernel filesystem statistics
 * Used by the VFS layer for all filesystem operations
 */
struct kstatfs {
	uint64_t f_type;    /* Type of filesystem */
	uint64_t f_bsize;   /* Optimal transfer block size */
	uint64_t f_blocks;  /* Total data blocks in filesystem */
	uint64_t f_bfree;   /* Free blocks in filesystem */
	uint64_t f_bavail;  /* Free blocks available to unprivileged user */
	uint64_t f_files;   /* Total file nodes in filesystem */
	uint64_t f_ffree;   /* Free file nodes in filesystem */
	uint64_t f_namelen; /* Maximum length of filenames */
	uint64_t f_frsize;  /* Fragment size */
	uint64_t f_flags;   /* Mount flags */
};

/**
 * Filename lookup parameters
 */
struct nameidata {
	struct path path;    /* Path found so far */
	struct qstr last;    /* Last component */
	struct inode* inode; /* Current inode */
	unsigned int flags;  /* Lookup flags */
	int last_type;	     /* Last component type */
};

/*
 * VFS core API functions
 */
/* Initialization functions */
int vfs_init(void);

#define vfs_read file_read
#define vfs_write file_write
#define vfs_llseek file_llseek
#define vfs_readv file_readv
#define vfs_writev file_writev
#define vfs_fsync file_sync


struct vfsmount* vfs_kern_mount(struct fsType* type, int flags,
				const char* name, void* data);

/* File operations */

int vfs_stat(const char*, struct kstat*);
int vfs_statfs(struct path*, struct kstatfs*);
int vfs_utimes(const char*, struct timespec*, int);

/* Directory operations */
int vfs_mkdir(struct inode*, struct dentry*, fmode_t);
int vfs_rmdir(struct inode*, struct dentry*);
int vfs_unlink(struct inode*, struct dentry*);
int vfs_rename(struct inode*, struct dentry*, struct inode*, struct dentry*,
	       unsigned int);
int iterate_dir(struct file*, struct dir_context*);

/* Link operations */
int vfs_link(struct dentry*, struct inode*, struct dentry*, struct inode**);
int vfs_symlink(struct inode*, struct dentry*, const char*);

/* Permission checking */
int vfs_permission(struct inode*, int);

/* File mode checking helpers */
static inline int is_dir(fmode_t mode) { return (mode & S_IFMT) == S_IFDIR; }
static inline int is_file(fmode_t mode) { return (mode & S_IFMT) == S_IFREG; }
static inline int is_symlink(fmode_t mode) { return (mode & S_IFMT) == S_IFLNK; }

#endif /* _VFS_H_ */