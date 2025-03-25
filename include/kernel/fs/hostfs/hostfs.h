#ifndef _HOSTFS_H_
#define _HOSTFS_H_
#include <kernel/fs/vfs/vfs.h>

#define HOSTFS_TYPE 1

// root directory
#define H_ROOT_DIR "./hostfs"

// hostfs utility functin declarations
int register_hostfs();
struct device *init_host_device(char *name);
void get_path_string(char *path, struct dentry *dentry);
struct inode *hostfs_alloc_vinode(struct superblock *sb);
int hostfs_write_back_vinode(struct inode *vinode);
int hostfs_update_vinode(struct inode *vinode);

// hostfs interface function declarations
ssize_t hostfs_read(struct inode *f_inode, char *r_buf, ssize_t len,
                    int *offset);
ssize_t hostfs_write(struct inode *f_inode, const char *w_buf, ssize_t len,
                     int *offset);
struct inode *hostfs_lookup(struct inode *parent, struct dentry *sub_dentry);
struct inode *hostfs_create(struct inode *parent, struct dentry *sub_dentry);
int hostfs_lseek(struct inode *f_inode, ssize_t new_offset, int whence,
                  int *offset);
int hostfs_link(struct inode *parent, struct dentry *sub_dentry, struct inode *link_node);
int hostfs_unlink(struct inode *parent, struct dentry *sub_dentry, struct inode *unlink_node);
int hostfs_hook_open(struct inode *f_inode, struct dentry *dentry);
int hostfs_hook_close(struct inode *f_inode, struct dentry *dentry);
int hostfs_readdir(struct inode *dir_vinode, struct dir *dir, int *offset);
struct inode *hostfs_mkdir(struct inode *parent, struct dentry *sub_dentry);
struct superblock *hostfs_get_superblock(struct device *dev);

extern const struct inode_operations hostfs_node_ops;

#endif
