#ifndef _DENTRY_H
#define _DENTRY_H

#include <kernel/fs/vfs/vfs.h>
#include <kernel/types.h>
#include <util/atomic.h>
#include <util/list.h>
#include <util/qstr.h>
#include <util/spinlock.h>
struct dentry_operations;
/*
 * Directory entry (dentry) structure
 *
 * A dentry is the glue that connects inodes and paths. Each path component
 * (directory or file name) is represented by a dentry.
 */
struct dentry {
	spinlock_t d_lock;   /* Protects dentry fields */
	atomic_t d_refcount; /* Reference count */

	/* RCU lookup touched fields */
	unsigned int d_flags;  /* Dentry flags */
	struct inode* d_inode; /* Associated inode */

	/* Lookup cache information */
	struct qstr* d_name;         /* Name of this dentry */
	struct list_node d_hashNode; /* Lookup hash list */

	struct dentry* d_parent;           /* Parent dentry */
	struct list_node d_parentListNode; /* Child entry in parent's list */

	/* Linked list of child dentries */
	struct list_head d_childList; /* Child dentries */

	/* Dentry usage management */

	/* Filesystem and operations */
	struct superblock* d_superblock; /* Superblock of file */

	/* D-cache management */
	unsigned long d_time; /* Revalidation time */
	void* d_fsdata;       /* Filesystem-specific data */

	struct list_node d_lruListNode;   /* 全局dentry的LRU链表，在引用计数归零时加入，便于复用 */
	                                  /* 只在内存压力时释放*/
	struct list_node d_aliasListNode; /* Inode 别名链表，用来维护硬链接 */

	/* Mount management */
	int d_mounted;            /* Mount count */
	struct path* d_automount; /* Automount point */

	const struct dentry_operations* d_operations; /* Dentry operations */
};

int init_dentry_hashtable(void);

/*dentry lru链表相关*/
void init_dentry_lruList(void);
unsigned int shrink_dentry_lru(unsigned int count);

/*dentry生命周期*/
struct dentry* dentry_locate(struct dentry* parent, const struct qstr* name, int is_dir, bool revalidate, bool alloc);
struct dentry* dentry_get(struct dentry* dentry); // 主要用来获取父节点
int dentry_put(struct dentry* dentry);
void dentry_prune(struct dentry *dentry);	/*unsafe*/
int dentry_delete(struct dentry *dentry);


int dentry_instantiate(struct dentry* dentry, struct inode* inode);

/*dentry标志判断*/
static inline bool dentry_isDir(const struct dentry* dentry);
static inline bool dentry_isSymlink(const struct dentry* dentry);
static inline bool dentry_isMountpoint(const struct dentry* dentry);

/*名字和目录操作*/
int dentry_rename(struct dentry* old_dentry, struct dentry* new_dentry);
/*符号链接支持*/
struct dentry* dentry_follow_link(struct dentry* link_dentry);
char* dentry_rawPath(struct dentry* dentry, char* buf, int buflen);
void dentry_prune(struct dentry *dentry); /*从父目录的子列表和哈希表中分离，但保留结构体和资源*/
bool is_mounted(struct dentry *dentry);

/*用于网络文件系统等需要验证缓存有效性的场景*/
int dentry_revalidate(struct dentry* dentry, unsigned int flags);


/*inode hook functions*/
int dentry_permission(struct dentry* dentry, int mask);
int dentry_getxattr(struct dentry* dentry, const char* name, void* value, size_t size);
int dentry_setxattr(struct dentry* dentry, const char* name, const void* value, size_t size, int flags);
int dentry_removexattr(struct dentry* dentry, const char* name);

/*
 * Operations that can be specialized for a particular dentry
 */
struct dentry_operations {
	/* Called to determine if dentry is still valid - important for NFS etc */
	int (*d_revalidate)(struct dentry*, unsigned int);

	/* Called to hash the dentry name for dcache */
	int (*d_hash)(const struct dentry*, struct qstr*);

	/* Called for name comparisons */
	int (*d_compare)(const struct dentry*, unsigned int, const char*, const struct qstr*);

	/* Called when dentry's reference count reaches 0 */
	int (*d_free)(const struct dentry*);

	/* Called to release dentry's inode */
	void (*d_inode_put)(struct dentry*, struct inode*);

	/* Called to create the relative path of a dentry */
	char* (*d_dname)(struct dentry*, char*, int);

	/* Called when a dentry is unhashed */
	void (*d_prune)(struct dentry*);
};

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

#endif /* _DENTRY_H */

// 可选：缓存一致性方法
// void d_rehash_subtree(struct dentry *dentry);
// void d_invalidate_subtree(struct dentry *dentry);

// 可选：快照方法
// struct dentry *d_snapshot(struct dentry *dentry);