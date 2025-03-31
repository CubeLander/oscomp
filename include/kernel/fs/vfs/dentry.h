#pragma once

#include "forward_declarations.h"
#include <kernel/util.h>
//#include <kernel/vfs.h>
#include <kernel/fs/vfs/inode.h>

struct dentry_operations;
struct iattr;
/*
 * Directory entry (dentry) structure
 *
 * A dentry is the glue that connects inodes and paths. Each path component
 * (directory or file name) is represented by a dentry.
 */
struct dentry {
	spinlock_t d_lock;
	// 需要优化为读写锁
	atomic_t d_refcount;

	uint32 d_flags;
	struct inode* d_inode;
	struct vfsmount* d_mount;
	struct qstr* d_name;
	struct list_node d_hashNode;

	struct dentry* d_parent;
	struct list_node d_parentListNode;

	struct list_head d_childList;

	uint64 d_time;
	// d_time 记录这条路径（dentry）最后一次被成功验证的时间戳。

	struct list_node d_lruListNode;   /* 全局dentry的LRU链表，在引用计数归零时加入，便于复用 */
	                                  /* 只在内存压力时释放*/
	struct list_node d_inodeListNode; /* Inode 别名链表，用来维护硬链接 */
};


int32 init_dentry_hashtable(void);
struct dentry* dentry_mkdir(struct dentry* parent, const char* name, fmode_t mode);
struct dentry* dentry_acquire(struct dentry* parent, const struct qstr* name, int32 is_dir, bool revalidate, bool alloc);
struct dentry* dentry_ref(struct dentry* dentry);
int32 dentry_unref(struct dentry* dentry);
struct dentry* dentry_mknod(struct dentry* parent, const char* name, mode_t mode, dev_t dev);
struct vfsmount* dentry_lookupMountpoint(struct dentry* dentry);


/*上面是仔细调过的，下面是没调的*/


/*dentry lru链表相关*/
void init_dentry_lruList(void);
uint32 shrink_dentry_lru(uint32 count);

/*dentry生命周期*/


struct dentry* dentry_lookup(struct dentry* parent, const struct qstr* name);

void dentry_prune(struct dentry* dentry); /*unsafe*/
int32 dentry_delete(struct dentry* dentry);

int32 dentry_instantiate(struct dentry* dentry, struct inode* inode);

/*名字和目录操作*/
int32 dentry_rename(struct dentry* old_dentry, struct dentry* new_dentry);

/*符号链接支持*/
// struct dentry* dentry_follow_link(struct dentry* link_dentry);
char* dentry_allocRawPath(struct dentry* dentry);
void dentry_prune(struct dentry* dentry); /*从父目录的子列表和哈希表中分离，但保留结构体和资源*/

/*用于网络文件系统等需要验证缓存有效性的场景*/
int32 dentry_revalidate(struct dentry* dentry, uint32 flags);

int32 setattr_prepare(struct dentry* dentry, struct iattr* attr);
int32 notify_change(struct dentry* dentry, struct iattr* attr);

/*inode hook functions*/
int32 dentry_permission(struct dentry* dentry, int32 mask);
//int32 dentry_getxattr(struct dentry* dentry, const char* name, void* value, size_t size);
//int32 dentry_setxattr(struct dentry* dentry, const char* name, const void* value, size_t size, int32 flags);
//int32 dentry_removexattr(struct dentry* dentry, const char* name);

/*
 * Dentry state flags
 */
#define DCACHE_DISCONNECTED 0x0001   /* Dentry is disconnected from the FS tree */
#define DCACHE_OP_HASH 0x0002        /* Has a custom hash operation */
#define DCACHE_OP_COMPARE 0x0004     /* Has a custom compare operation */
#define DCACHE_OP_REVALIDATE 0x0008  /* Has a revalidate operation */
#define DCACHE_OP_DELETE 0x0010      /* Has a delete operation */
#define DCACHE_REFERENCED 0x0040     /* Recently used */
#define DCACHE_RCUACCESS 0x0080      /* RCU-enabled */
#define DCACHE_NEGATIVE 0x0100       /* Negative dentry (no inode exists) */
#define DCACHE_FALLTHRU 0x0200       /* Used for lookup fallthrough */
#define DCACHE_NEED_AUTOMOUNT 0x1000 /* Automount point */
#define DCACHE_MOUNTED 0x2000        /* Is a mountpoint */
#define DCACHE_HASHED 0x4000         /* In dentry hash table */
#define DCACHE_IN_LRU 0x8000         /* Dentry在LRU列表中 */

static inline bool dentry_isDir(const struct dentry* dentry) {
	if (!dentry || !dentry->d_inode) return false;
	return S_ISDIR(dentry->d_inode->i_mode);
}

static inline bool dentry_isSymlink(const struct dentry* dentry) {
	if (!dentry || !dentry->d_inode) return false;
	return S_ISLNK(dentry->d_inode->i_mode);
}

static inline bool dentry_isFile(const struct dentry* dentry) {
	if (!dentry || !dentry->d_inode) return false;
	return S_ISREG(dentry->d_inode->i_mode);
}

static inline bool dentry_isMountpoint(const struct dentry* dentry) {
	if (!dentry) return false;
	return (dentry->d_flags & DCACHE_MOUNTED) != 0;
}

static inline bool dentry_isNegative(const struct dentry* dentry) {
	if (!dentry) return false;
	return (dentry->d_flags & DCACHE_NEGATIVE) != 0;
}

bool dentry_isEmptyDir(struct dentry* dentry);


int32 dentry_monkey(struct fcontext* fctx);

// 可选：缓存一致性方法
// void d_rehash_subtree(struct dentry *dentry);
// void d_invalidate_subtree(struct dentry *dentry);

// 可选：快照方法
// struct dentry *d_snapshot(struct dentry *dentry);