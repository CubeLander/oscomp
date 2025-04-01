#include <kernel/mmu.h>
#include <kernel/sched.h>
#include <kernel/sprint.h>
#include <kernel/time.h>
#include <kernel/types.h>
#include <kernel/util.h>
#include <kernel/vfs.h>

/* Dentry cache hashtable */
struct hashtable dentry_hashtable;

/* 复合键结构 - 用于查找时构建临时键 */
struct dentry_key {
	struct dentry* parent;   /* 父目录项 */
	const struct qstr* name; /* 名称 */
};

/**
 * 从哈希节点获取dentry键
 */
void* dcache_getkey(struct list_head* node) {
	static struct dentry_key key;
	struct dentry* dentry = container_of(node, struct dentry, d_hashNode);

	key.parent = dentry->d_parent;
	key.name = dentry->d_name;

	return &key;
}

/**
 * 计算dentry复合键的哈希值
 */
static uint32 dcache_hash(const void* key) {
	const struct dentry_key* dkey = (const struct dentry_key*)key;
	uint32 hash;

	/* 结合父指针和名称哈希 */
	hash = (uint64)dkey->parent;
	hash = hash * 31 + dkey->name->hash;

	return hash;
}

/**
 * 比较两个dentry键是否相等
 */
static int32 dcache_equal(const void* k1, const void* k2) {
	const struct dentry_key* key1 = (const struct dentry_key*)k1;
	const struct dentry_key* key2 = (const struct dentry_key*)k2;

	/* 首先比较父节点 */
	if (key1->parent != key2->parent) return 0;

	/* 然后比较名称 */
	const struct qstr* name1 = key1->name;
	const struct qstr* name2 = key2->name;

	if (name1->len != name2->len) return 0;

	return !memcmp(name1->name, name2->name, name1->len);
}

struct dentry* dcache_lookup(struct dentry* parent, const struct qstr* name) {
	struct dentry_key key;
	key.parent = parent;
	key.name = name;

	struct list_node* node = hashtable_lookup(&dentry_hashtable, &key);
	if (node) { struct dentry* dentry = container_of(node, struct dentry, d_hashNode); 
		atomic_inc(&dentry->d_refcount);
		dentry->d_time = jiffies;
	}
	return NULL;
}


/**
 * 初始化dentry缓存
 */
int32 dcache_init(void) {
	sprint("Initializing dentry hashtable\n");

	/* 初始化dentry哈希表 */
	return hashtable_setup(&dentry_hashtable, 1024, /* 初始桶数 */
	                       75,                      /* 负载因子 */
	                       dcache_hash, dcache_getkey, dcache_equal);
}
