#include <kernel/mmu.h>
#include <kernel/sched.h>
#include <kernel/sprint.h>
#include <kernel/time.h>
#include <kernel/types.h>
#include <kernel/util.h>
#include <kernel/vfs.h>

/* Dentry cache hashtable */
static struct hashtable dentry_hashtable;

static void* __dentry_get_key(struct list_head* node);
static uint32 __dentry_hashfunction(const void* key);
static inline int32 __dentry_hash(struct dentry* dentry);
static int32 __dentry_key_equals(const void* k1, const void* k2);
static struct dentry* __dentry_alloc(struct dentry* parent, const struct qstr* name);
static void __dentry_free(struct dentry* dentry);
static struct dentry* __find_in_lru_list(struct dentry* parent, const struct qstr* name);
static struct dentry* __dentry_lookupHash(struct dentry* parent, const struct qstr* name);

/* 复合键结构 - 用于查找时构建临时键 */
struct dentry_key {
	struct dentry* parent;   /* 父目录项 */
	const struct qstr* name; /* 名称 */
};

static struct list_head g_dentry_lru_list; /* 全局LRU链表，用于dentry的复用 */
static uint32 g_dentry_lru_count = 0;      /* 当前LRU链表中的dentry数量 */
static spinlock_t g_dentry_lru_list_lock;

void init_dentry_lruList(void) {
	/* 初始化全局LRU链表 */
	INIT_LIST_HEAD(&g_dentry_lru_list);
	spinlock_init(&g_dentry_lru_list_lock);
	g_dentry_lru_count = 0;
}

/**
 * 强化版dentry获取接口 - 集成查找与创建功能
 *
 * @param parent: 父dentry
 * @param name: 需要查找的名称
 * @param is_dir: 文件类型筛选器:
 *               -1: 不指定类型(只匹配名字)
 *                0: 仅匹配文件
 *                1: 仅匹配目录
 * @param revalidate: 是否需要重新验证找到的dentry
 * @param alloc: 未找到时是否创建新dentry
 * @return: 匹配的dentry，引用计数加1，未找到或不符合要求时返回NULL
 */
struct dentry* dentry_acquire(struct dentry* parent, const struct qstr* name, int32 is_dir, bool revalidate, bool alloc) {
	struct dentry* dentry = NULL;
	bool type_match = true;

	unlikely_if(!parent || !name || !name->name) return ERR_PTR(-EINVAL);
	struct inode* dir_inode = parent->d_inode;

	/* 1. 先尝试查找已有的dentry */
	dentry = dentry_lookup(parent, &name);

	/* 3. 如果找到，检查类型是否匹配 */
	if (dentry && is_dir != -1) {
		/* 获取inode并检查是否为目录 */
		if (dentry->d_inode) {
			bool is_directory = S_ISDIR(dentry->d_inode->i_mode);
			type_match = (is_dir == 1) ? is_directory : !is_directory;
		} else {
			/* 负向dentry，无法确定类型 */
			type_match = false;
		}

		if (!type_match) {
			/* 类型不匹配，放弃此dentry */
			dentry_unref(dentry);
			dentry = NULL;
		}
	}

	/* 4. 如果没找到或类型不匹配，尝试从LRU列表复用 */
	if (!dentry) {
		dentry = __find_in_lru_list(parent, &tmp_name);

		/* 如果在LRU中找到可复用的dentry */
		if (dentry) {
			/* 检查类型是否匹配 */
			if (is_dir != -1 && dentry->d_inode) {
				bool is_directory = S_ISDIR(dentry->d_inode->i_mode);
				type_match = (is_dir == 1) ? is_directory : !is_directory;

				if (!type_match) {
					/* 类型不匹配，不使用此dentry */
					__dentry_free(dentry); /* 直接释放而不回到LRU */
					dentry = NULL;
				} else {
					/* 重置dentry状态 */
					atomic_set(&dentry->d_refcount, 1);

					/* 重新添加到哈希表 */
					if (!(dentry->d_flags & DCACHE_HASHED)) {
						int32 ret = __dentry_hash(dentry);
						if (ret == 0) { dentry->d_flags |= DCACHE_HASHED; }
					}
				}
			}
		}
	}

	/* 5. 如果依然没有找到且允许创建，则创建新dentry */
	if (!dentry && alloc) {
		/* 创建新dentry */
		dentry = __dentry_alloc(parent, &tmp_name);
		if (dentry) {
			/* 添加到哈希表 */
			int32 ret = __dentry_hash(dentry);
			if (ret == 0) { dentry->d_flags |= DCACHE_HASHED; }

			/* 标记为负dentry */
			dentry->d_flags |= DCACHE_NEGATIVE;
		}
	}

	return dentry;
}

struct dentry* dentry_ref(struct dentry* dentry) {
	if (!dentry) return NULL;

	atomic_inc(&dentry->d_refcount);
	return dentry;
}

/**
 * 初始化dentry缓存
 */
int32 init_dentry_hashtable(void) {
	sprint("Initializing dentry hashtable\n");

	/* 初始化dentry哈希表 */
	return hashtable_setup(&dentry_hashtable, 1024, /* 初始桶数 */
	                       75,                      /* 负载因子 */
	                       __dentry_hashfunction, __dentry_get_key, __dentry_key_equals);
}

/**
 * 释放dentry引用
 */
int32 dentry_unref(struct dentry* dentry) {
	if (!dentry) return -EINVAL;
	if (atomic_read(&dentry->d_refcount) <= 0) return -EINVAL;
	/* 如果引用计数降为0 */
	if (atomic_dec_and_test(&dentry->d_refcount)) {
		/* 如果是未使用但已哈希的dentry，加入LRU列表而非释放 */
		/* 加入LRU列表的代码，需要全局LRU列表的锁 */
		spinlock_lock(&g_dentry_lru_list_lock);
		list_add(&dentry->d_lruListNode, &g_dentry_lru_list);
		dentry->d_flags |= DCACHE_IN_LRU;
		g_dentry_lru_count++;
		spinlock_unlock(&g_dentry_lru_list_lock);
		/* 注意：这里不需要释放dentry，因为它会在内存压力时被回收 */
	}
	return 0;
}

/**
 * 从缓存中删除dentry
 */
static void __dentry_free(struct dentry* dentry) {
	if (!dentry) return;

	spinlock_lock(&dentry->d_lock);

	/* 从哈希表中移除 */
	if (dentry->d_flags & DCACHE_HASHED) {
		hashtable_remove(&dentry_hashtable, &dentry->d_hashNode);
		dentry->d_flags &= ~DCACHE_HASHED;
	}

	/* 从父节点的子列表中移除 */
	if (!list_empty(&dentry->d_parentListNode)) {
		list_del(&dentry->d_parentListNode);
		INIT_LIST_HEAD(&dentry->d_parentListNode);
	}

	/* 从LRU列表中移除 */
	if (!list_empty(&dentry->d_lruListNode)) {
		list_del(&dentry->d_lruListNode);
		INIT_LIST_HEAD(&dentry->d_lruListNode);
	}

	/* 从inode的别名列表中移除 */
	if (dentry->d_inode && !list_empty(&dentry->d_inodeListNode)) {
		spinlock_lock(&dentry->d_inode->i_dentryList_lock);
		list_del_init(&dentry->d_inodeListNode);
		spinlock_unlock(&dentry->d_inode->i_dentryList_lock);
	}

	spinlock_unlock(&dentry->d_lock);

	/* 释放inode引用 */
	if (dentry->d_inode) {
		inode_unref(dentry->d_inode);
		dentry->d_inode = NULL;
	}

	/* 释放父引用 */
	if (dentry->d_parent && dentry->d_parent != dentry) {
		dentry_unref(dentry->d_parent);
		dentry->d_parent = NULL;
	}

	/* 释放名称 */
	if (dentry->d_name) {
		kfree(dentry->d_name);
		dentry->d_name = NULL;
	}

	/* 释放dentry结构 */
	kfree(dentry);
}

/**
 * 从LRU列表中释放指定数量的dentry
 * 通常在内存压力大时调用
 *
 * @param count: 要释放的dentry数量，0表示全部释放
 * @return: 实际释放的数量
 */
uint32 shrink_dentry_lru(uint32 count) {
	struct dentry* dentry;
	struct list_head *pos, *n;
	uint32 freed = 0;

	spinlock_lock(&g_dentry_lru_list_lock);

	/* 如果count为0，释放全部 */
	if (count == 0) count = g_dentry_lru_count;

	list_for_each_safe(pos, n, &g_dentry_lru_list) {
		if (freed >= count) break;

		dentry = container_of(pos, struct dentry, d_lruListNode);

		/* 从LRU列表移除 */
		list_del_init(&dentry->d_lruListNode);
		dentry->d_flags &= ~DCACHE_IN_LRU;
		g_dentry_lru_count--;

		/* 释放dentry */
		__dentry_free(dentry);

		freed++;
	}

	spinlock_unlock(&g_dentry_lru_list_lock);
	return freed;
}

/**
 * 将dentry与inode关联
 *
 * @param dentry: 要关联的dentry
 * @param inode: 要关联的inode，NULL表示创建负向dentry（未实现）
 * @return: 成功返回0，失败返回错误码
 */
int32 dentry_instantiate(struct dentry* dentry, struct inode* inode) {
	if (!dentry || !inode) return -EINVAL;

	spinlock_lock(&dentry->d_lock);

	/* 如果已有inode，先解除关联 */
	if (dentry->d_inode) {
		/* 从inode的别名列表中移除 */
		if (!list_empty(&dentry->d_inodeListNode)) {
			spinlock_lock(&dentry->d_inode->i_dentryList_lock);
			list_del_init(&dentry->d_inodeListNode);
			spinlock_unlock(&dentry->d_inode->i_dentryList_lock);
		}
		inode_unref(dentry->d_inode);
		dentry->d_inode = NULL;
	}

	/* 增加inode引用计数 */
	inode = inode_ref(inode);
	dentry->d_inode = inode;

	/* 添加到inode的别名列表 */
	spinlock_lock(&inode->i_dentryList_lock);
	list_add(&dentry->d_inodeListNode, &inode->i_dentryList);
	spinlock_unlock(&inode->i_dentryList_lock);

	spinlock_unlock(&dentry->d_lock);
	return 0;
}

/**
 * 从哈希节点获取dentry键
 */
static void* __dentry_get_key(struct list_head* node) {
	static struct dentry_key key;
	struct dentry* dentry = container_of(node, struct dentry, d_hashNode);

	key.parent = dentry->d_parent;
	key.name = dentry->d_name;

	return &key;
}

/**
 * 计算dentry复合键的哈希值
 */
static uint32 __dentry_hashfunction(const void* key) {
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
static int32 __dentry_key_equals(const void* k1, const void* k2) {
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

/* 保留现有函数，但将创建函数改为内部使用 */
static struct dentry* __dentry_alloc(struct dentry* parent, const struct qstr* name) {
	struct dentry* dentry;

	if (!name || !name->name) return NULL;

	/* 分配dentry结构 */
	dentry = kmalloc(sizeof(struct dentry));
	if (!dentry) return NULL;

	/* 初始化基本字段 */
	memset(dentry, 0, sizeof(struct dentry));
	spinlock_init(&dentry->d_lock);
	atomic_set(&dentry->d_refcount, 1);
	INIT_LIST_HEAD(&dentry->d_childList);
	INIT_LIST_HEAD(&dentry->d_lruListNode);
	INIT_LIST_HEAD(&dentry->d_inodeListNode);
	INIT_LIST_HEAD(&dentry->d_hashNode);

	/* 复制name */
	dentry->d_name = qstr_create_with_length(name->name, name->len);

	/* 设置父节点关系 */
	dentry->d_parent = parent ? dentry_ref(parent) : dentry; /* 根目录是自己的父节点 */

	if (parent) {
		dentry->d_superblock = parent->d_superblock;

		/* 添加到父节点的子列表 */
		spinlock_lock(&parent->d_lock);
		list_add(&dentry->d_parentListNode, &parent->d_childList);
		spinlock_unlock(&parent->d_lock);
	}

	return dentry;
}

/**
 * __find_in_lru_list - 在LRU列表中查找可复用的dentry
 * 高效的实现，使用全局dentry哈希表和IN_LRU标志位
 *
 * @parent: 父目录项
 * @name: 名称
 * @return: 找到的dentry或NULL
 */
static struct dentry* __find_in_lru_list(struct dentry* parent, const struct qstr* name) {
	struct dentry_key key;

	if (!parent || !name) return NULL;

	/* 构造查询键 */
	key.parent = parent;
	key.name = (struct qstr*)name;

	/* 使用全局dentry哈希表直接查找 */
	struct dentry* dentry = __dentry_lookupHash(parent, name);
	if (dentry) {
		spinlock_lock(&g_dentry_lru_list_lock);

		/* 检查此dentry是否在LRU列表中 */
		if (dentry->d_flags & DCACHE_IN_LRU) {
			/* 从LRU列表移除 */
			list_del_init(&dentry->d_lruListNode);
			dentry->d_flags &= ~DCACHE_IN_LRU;
			g_dentry_lru_count--;
		} else {
			/* 不在LRU列表中，无法重用 */
			dentry = NULL;
		}

		spinlock_unlock(&g_dentry_lru_list_lock);
	}

	return dentry;
}

/**
 * dentry_rename - Rename a dentry (update parent and/or name)
 * @old_dentry: Source dentry to be renamed
 * @new_dentry: Target dentry containing new parent and name information
 *
 * Updates a dentry's parent and name, maintaining hash table integrity.
 * Performs proper locking and reference counting on the parent dentries.
 *
 * Returns: 0 on success, negative error code on failure
 */
int32 dentry_rename(struct dentry* old_dentry, struct dentry* new_dentry) {
	int32 error = 0;

	if (!old_dentry || !new_dentry) return -EINVAL;

	/* Don't rename to self */
	if (old_dentry == new_dentry) return 0;

	/* Lock dentries in address order to prevent deadlocks */
	if (old_dentry < new_dentry) {
		spinlock_lock(&old_dentry->d_lock);
		spinlock_lock(&new_dentry->d_lock);
	} else {
		spinlock_lock(&new_dentry->d_lock);
		spinlock_lock(&old_dentry->d_lock);
	}

	/* Remove from hash table first */
	if (old_dentry->d_flags & DCACHE_HASHED) {
		hashtable_remove(&dentry_hashtable, &old_dentry->d_hashNode);
		old_dentry->d_flags &= ~DCACHE_HASHED;
	}

	/* Handle parent change if needed */
	if (old_dentry->d_parent != new_dentry->d_parent) {
		/* Remove from old parent's child list */
		list_del(&old_dentry->d_parentListNode);

		/* Update parent reference */
		struct dentry* old_parent = old_dentry->d_parent;
		old_dentry->d_parent = dentry_ref(new_dentry->d_parent);

		/* Add to new parent's child list */
		spinlock_lock(&new_dentry->d_parent->d_lock);
		list_add(&old_dentry->d_parentListNode, &new_dentry->d_parent->d_childList);
		spinlock_unlock(&new_dentry->d_parent->d_lock);

		/* Release reference to old parent */
		dentry_unref(old_parent);
	}

	/* Update name */
	if (old_dentry->d_name) { kfree(old_dentry->d_name); }
	old_dentry->d_name = qstr_create_with_length(new_dentry->d_name->name, new_dentry->d_name->len);

	/* Re-hash the dentry with new parent/name */
	error = __dentry_hash(old_dentry);
	if (error == 0) {
		old_dentry->d_flags |= DCACHE_HASHED;
	} else {
		/* If insertion failed, try to restore old state as much as possible */
		error = -EBUSY;
	}

	/* Unlock in reverse order */
	if (old_dentry < new_dentry) {
		spinlock_unlock(&new_dentry->d_lock);
		spinlock_unlock(&old_dentry->d_lock);
	} else {
		spinlock_unlock(&old_dentry->d_lock);
		spinlock_unlock(&new_dentry->d_lock);
	}

	return error;
}
/**
 * dentry_revalidate - 重新验证dentry的有效性
 * @dentry: 要验证的dentry
 * @flags: 验证标志位
 *
 * 检查一个dentry是否仍然有效，尤其是网络文件系统中的dentry。
 * 如果dentry的文件系统有自定义验证方法，则调用它。
 *
 * 返回: 有效返回1，无效返回0，错误返回负值
 */
int32 dentry_revalidate(struct dentry* dentry, uint32 flags) {
	if (!dentry) return -EINVAL;

	/* 如果dentry没有d_operations或d_revalidate函数，默认认为有效 */
	if (!dentry->d_operations || !dentry->d_operations->d_revalidate) return 1;

	/* 调用文件系统特定的验证方法 */
	return dentry->d_operations->d_revalidate(dentry, flags);
}

/**
 * dentry_follow_link - Follow a symbolic link to its target
 * @link_dentry: The symbolic link dentry to follow
 *
 * This function resolves a symbolic link to its target by reading
 * the link content and traversing the path it points to.
 * It handles both absolute and relative paths correctly and limits
 * the recursion depth to prevent infinite loops.
 *
 * Return: The target dentry with increased reference count, or ERR_PTR on error
 */
// struct dentry *dentry_follow_link(struct dentry *link_dentry)
// {
//     struct dentry *target_dentry = NULL;
//     char *link_value = NULL;
//     int32 res, link_len;
//     int32 max_loops = 8; /* Maximum symlink recursion depth */
//     bool is_absolute;

//     /* Validate input parameters */
//     if (!link_dentry || !link_dentry->d_inode)
//         return ERR_PTR(-EINVAL);

//     /* Ensure it's a symlink */
//     if (!S_ISLNK(link_dentry->d_inode->i_mode))
//         return ERR_PTR(-EINVAL);

//     /* Allocate buffer for link content */
//     link_value = kmalloc(PATH_MAX);
//     if (!link_value)
//         return ERR_PTR(-ENOMEM);

//     /* Get a reference to the original link */
//     struct dentry *current_dentry = dentry_ref(link_dentry);

//     /* Track the starting point for relative path resolution */
//     struct dentry *base_dir = dentry_ref(link_dentry->d_parent);

//     while (max_loops-- > 0) {
//         /* Read the link content */
//         if (!current_dentry->d_inode->i_op || !current_dentry->d_inode->i_op->readlink) {
//             res = -EINVAL;
//             goto out_error;
//         }

//         link_len = current_dentry->d_inode->i_op->readlink(current_dentry, link_value, PATH_MAX - 1);
//         if (link_len < 0) {
//             res = link_len;
//             goto out_error;
//         }

//         /* Ensure the string is null-terminated */
//         link_value[link_len] = '\0';

//         /* Determine if the path is absolute or relative */
//         is_absolute = (link_value[0] == '/');

//         /* Release current dentry before resolving the next path */
//         dentry_unref(current_dentry);
//         current_dentry = NULL;

//         /* Parse the link path */
//         struct path link_path;
//         struct nameidata nd;

//         /* Pseudocode: Initialize nameidata with appropriate context */
//         /*
//          * nameidata_init(&nd);
//          * nd.path.dentry = is_absolute ? root_dentry : base_dir;
//          * nd.path.mnt = current->fs->root.mnt;
//          * nd.flags = LOOKUP_FOLLOW;
//          */

//         /* Pseudocode: Path lookup - would be implemented via path_lookup() in a real system */
//         /*
//          * res = path_lookup(link_value, &nd);
//          * if (res) goto out_error;
//          * link_path = nd.path;
//          */

//         /* For demonstration purposes, we'll use the existing path_create function */
//         uint32 lookup_flags = LOOKUP_FOLLOW;
//         if (!is_absolute) {
//             /* Pseudocode: For relative paths, we need to start from base_dir */
//             /*
//              * // Real implementation would use something like:
//              * res = vfs_path_lookup(base_dir, base_dir->d_sb->s_root.mnt,
//              *                      link_value, lookup_flags, &link_path);
//              */

//             /* Since we're using path_create, we need to construct the full path */
//             char full_path[PATH_MAX*2];
//             char base_path[PATH_MAX];

//             /* Get the path of the parent directory */
//             dentry_allocRawPath(base_dir, base_path, PATH_MAX);

//             /* Construct full path by concatenating parent path and link content */
//             if (strlen(base_path) + strlen(link_value) + 2 > PATH_MAX*2) {
//                 res = -ENAMETOOLONG;
//                 goto out_error;
//             }

//             /* Handle special case when base_path is root */
//             if (strcmp(base_path, "/") == 0)
//                 snprintf(full_path, PATH_MAX*2, "/%s", link_value);
//             else
//                 snprintf(full_path, PATH_MAX*2, "%s/%s", base_path, link_value);

//             res = path_create(full_path, lookup_flags, &link_path);
//         } else {
//             /* Absolute path - just use path_create */
//             res = path_create(link_value, lookup_flags, &link_path);
//         }

//         if (res) {
//             goto out_error;
//         }

//         /* Get a reference to the resolved dentry */
//         current_dentry = dentry_ref(link_path.dentry);

//         /* Clean up the path */
//         /*
//          * Pseudocode: Real implementation would have a proper path_put() function
//          * path_put(&link_path);
//          */
//         path_destroy(&link_path);

//         /* Check if the target exists */
//         if (!current_dentry->d_inode) {
//             res = -ENOENT;
//             goto out_error;
//         }

//         /* If not a symlink, we're done */
//         if (!S_ISLNK(current_dentry->d_inode->i_mode)) {
//             target_dentry = current_dentry;
//             current_dentry = NULL; /* Prevent it from being released */
//             break;
//         }

//         /* For another symlink, update the base_dir for relative path resolution */
//         dentry_unref(base_dir);
//         base_dir = dentry_ref(current_dentry->d_parent);
//     }

//     /* Check for too many levels of symlinks */
//     if (max_loops < 0) {
//         res = -ELOOP;
//         goto out_error;
//     }

//     /* Success - free resources and return target */
//     kfree(link_value);
//     if (base_dir)
//         dentry_unref(base_dir);

//     return target_dentry;

// out_error:
//     /* Clean up on error */
//     kfree(link_value);
//     if (current_dentry)
//         dentry_unref(current_dentry);
//     if (base_dir)
//         dentry_unref(base_dir);
//     return ERR_PTR(res);
// }


/**
 * 从目录树中剥离 dentry
 * 注意：此函数通常不应直接调用，除非明确了解其后果
 * 应该优先使用 dentry_delete 而非直接调用此函数
 */
void dentry_prune(struct dentry* dentry) {
	if (!dentry) return;

	spinlock_lock(&dentry->d_lock);

	/* 调用文件系统特定的修剪方法 */
	if (dentry->d_operations && dentry->d_operations->d_prune) dentry->d_operations->d_prune(dentry);

	/* 从哈希表中移除 */
	if (dentry->d_flags & DCACHE_HASHED) {
		hashtable_remove(&dentry_hashtable, &dentry->d_hashNode);
		dentry->d_flags &= ~DCACHE_HASHED;
	}

	/* 从父目录的子列表中移除 */
	if (dentry->d_parent && dentry->d_parent != dentry && !list_empty(&dentry->d_parentListNode)) {
		list_del(&dentry->d_parentListNode);
		INIT_LIST_HEAD(&dentry->d_parentListNode);
	}

	/* 标记为已修剪状态 */
	dentry->d_flags |= DCACHE_DISCONNECTED;

	spinlock_unlock(&dentry->d_lock);

	/* 注意：不直接释放dentry，仍然依赖标准引用计数机制 */
}

/**
 * 标记 dentry 为删除状态并处理相关的 inode
 * 不要求引用计数为 0，因为这可能是对仍在使用的文件执行 unlink
 *
 * @param dentry: 要标记为删除的 dentry
 * @return: 成功返回 0，失败返回错误码
 */
int32 dentry_delete(struct dentry* dentry) {
	struct inode* inode;

	if (!dentry) return -EINVAL;

	inode = dentry->d_inode;
	if (!inode) return -ENOENT; // 没有关联的 inode

	spinlock_lock(&inode->i_lock);

	// 减少硬链接计数
	if (inode->i_nlink > 0) inode->i_nlink--;

	// 如果是最后一个硬链接，标记 inode 为删除状态
	if (inode->i_nlink == 0) inode->i_state |= I_FREEING;

	spinlock_unlock(&inode->i_lock);

	// 从目录树分离 dentry
	dentry_prune(dentry);

	// 注意：不需要显式释放 inode，
	// 当 dentry 的引用计数降为 0 时，会调用 dentry_put，
	// 这会最终减少 inode 的引用计数

	return 0;
}

/**
 * 检查一个dentry是否为挂载点
 *
 * @param dentry: 要检查的dentry
 * @return: 如果dentry是挂载点返回true，否则返回false
 */
bool is_mounted(struct dentry* dentry) {
	if (!dentry) return false;

	/* 最快速的检查方法 - 检查DCACHE_MOUNTED标志 */
	if (dentry->d_flags & DCACHE_MOUNTED) return true;

	/*
	 * 注意：在完整的实现中，可能还需要执行其他检查，
	 * 例如查询全局的mount列表，确保标志是最新的。
	 * 但这取决于挂载系统的具体实现和更新策略。
	 */

	return false;
}

/**
 * setattr_prepare - Check if attribute change is allowed
 * @dentry: dentry of the inode to change
 * @attr: attributes to change
 *
 * Validates that the requested attribute changes are allowed
 * based on permissions and constraints.
 *
 * Returns 0 if the change is allowed, negative error code otherwise.
 */
int32 setattr_prepare(struct dentry* dentry, struct iattr* attr) {
	struct inode* inode = dentry->d_inode;
	int32 error = 0;

	if (!inode) return -EINVAL;

	/* Check for permission to change attributes */
	if (attr->ia_valid & ATTR_MODE) {
		error = inode_checkPermission(inode, MAY_WRITE);
		if (error) return error;
	}

	/* Check if user can change ownership */
	if (attr->ia_valid & (ATTR_UID | ATTR_GID)) {
		/* Only root can change ownership */
		if (current_task()->euid != 0) return -EPERM;
	}

	/* Check if size can be changed */
	if (attr->ia_valid & ATTR_SIZE) {
		error = inode_checkPermission(inode, MAY_WRITE);
		if (error) return error;

		/* Cannot change size of directories */
		if (S_ISDIR(inode->i_mode)) return -EISDIR;
	}

	return 0;
}

/**
 * dentry_lookup - Find dentry in the dentry cache
 * @parent: Parent directory dentry
 * @name: Name to look up in the parent directory
 *
 * This function searches for a dentry with the given name under the specified
 * parent directory in the dentry cache. If found, increases its reference count.
 *
 * Return: Found dentry with increased refcount, or NULL if not found
 */
struct dentry* dentry_lookup(struct dentry* parent, const struct qstr* name) {
	struct dentry* dentry = NULL;

	if (!parent || !name || !name->name) return NULL;

	/* Look up the dentry in the hash table */
	dentry = __dentry_lookupHash(parent, name);

	/* If found, increase the reference count */
	if (dentry) {
		/* Check if the dentry is in LRU list - can't use directly if it is */
		if (dentry->d_flags & DCACHE_IN_LRU) {
			spinlock_lock(&g_dentry_lru_list_lock);

			/* Double-check under lock */
			if (dentry->d_flags & DCACHE_IN_LRU) {
				/* Remove from LRU list */
				list_del_init(&dentry->d_lruListNode);
				dentry->d_flags &= ~DCACHE_IN_LRU;
				g_dentry_lru_count--;

				/* Reset reference count */
				atomic_set(&dentry->d_refcount, 1);
			} else {
				/* Someone else removed it from LRU, increment ref count */
				atomic_inc(&dentry->d_refcount);
			}

			spinlock_unlock(&g_dentry_lru_list_lock);
		} else {
			/* Normal case: just increment ref count */
			atomic_inc(&dentry->d_refcount);
		}
		extern uint64 jiffies;
		/* Update access time for LRU algorithm */
		dentry->d_time = jiffies;

		/* Set referenced flag for page replacement algorithms */
		dentry->d_flags |= DCACHE_REFERENCED;
	}

	return dentry;
}

static struct dentry* __dentry_lookupHash(struct dentry* parent, const struct qstr* name) {
	struct dentry_key key;
	key.parent = parent;
	key.name = name;

	struct list_node* node = hashtable_lookup(&dentry_hashtable, &key);
	if (node)
		return container_of(node, struct dentry, d_hashNode);
	else
		return NULL;
}

static inline int32 __dentry_hash(struct dentry* dentry) { return hashtable_insert(&dentry_hashtable, &dentry->d_hashNode); }

/**
 * dentry_isEmptyDir - Check if a directory is empty
 * @dentry: The directory entry to check
 *
 * Returns true if the directory contains no entries other than "." and ".."
 * Returns false if the dentry is invalid, not a directory, or contains entries
 */
bool dentry_isEmptyDir(struct dentry* dentry) {
	// Verify the dentry is valid and is a directory
	if (!dentry || !dentry_isDir(dentry)) return false;

	// Empty directories have no child entries in d_childList
	// (note: "." and ".." special entries aren't included in d_childList)
	return list_empty(&dentry->d_childList);
}


struct vfsmount* dentry_lookupMount(struct dentry* dentry) {
	if (!dentry) return NULL;

	/* Simply return the direct mount reference if this is a mountpoint */
	if (dentry->d_flags & DCACHE_MOUNTED) { return mount_ref(dentry->d_mount); }

	return NULL;
}

static int dentry_isMismatch(struct dentry* dentry, int64 lookup_flags) {
	if (!dentry) return -EINVAL;

	/* 检查dentry是否有inode */
	if (!dentry->d_inode) return -ENOENT;

	/* 检查类型是否匹配 */
	if ((lookup_flags & LOOKUP_DIRECTORY) && dentry_isDir(dentry) == false) return -ENOTDIR;
	if ((lookup_flags & LOOKUP_MONKEY_SYMLINK) && dentry_isSymlink(dentry) == false) return -EINVAL;
	if ((lookup_flags & LOOKUP_MONKEY_FILE) && dentry_isFile(dentry) == false) return -EINVAL;

	return 0;
}

int32 dentry_monkey_pathwalk(struct fcontext* fctx) {
	int error;
	struct dentry* parent = fctx->fc_dentry;

	if (!dentry_isDir(parent)) {
		/* 不是目录，返回错误 */
		return -ENOTDIR;
	}

	if (!parent->d_inode) {
		/* dentry没有inode，返回错误 */
		return -ENOENT;
	}

	struct qstr qname;
	qname.name = fctx->fc_path_remaining;
	qname.len = strlen(qname.name);
	qname.hash = full_name_hash(qname.name, qname.len);

	struct dentry* next_dentry = NULL;

	/* 1. 先尝试查找已有的dentry */
	next_dentry = dentry_lookup(parent, &qname);
	if (PTR_IS_ERR(next_dentry)) { return PTR_ERR(next_dentry); }

	if (next_dentry) {
		error = dentry_isMismatch(next_dentry, fctx->fc_action_flags);
		if (error) {
			/* 如果dentry不匹配，则返回错误 */
			dentry_unref(next_dentry);
			return error;
		} else {
			/* 成功找到匹配的dentry，直接更新fctx */
			dentry_unref(fctx->fc_dentry); // 释放旧dentry引用
			fctx->fc_dentry = next_dentry; // 更新为新dentry
			return 0;
		}
	}

	/* 2. 尝试从LRU列表复用 */
	next_dentry = __find_in_lru_list(parent, &qname);
	if (next_dentry) {
		error = dentry_isMismatch(next_dentry, fctx->fc_action_flags);
		if (error) {
			/* 不匹配，不使用 */
			__dentry_free(next_dentry);
			next_dentry = NULL;
		} else {
			/* 成功找到匹配的dentry，直接更新fctx */
			dentry_unref(fctx->fc_dentry);
			fctx->fc_dentry = next_dentry;
			return 0;
		}
	}

	/* 3. 如果需要创建新dentry */
	next_dentry = __dentry_alloc(parent, &qname);
	if (next_dentry) {
		/* 添加到哈希表 */
		int32 ret = __dentry_hash(next_dentry);
		if (ret == 0) { next_dentry->d_flags |= DCACHE_HASHED; }
		/* 标记为负向dentry */
		next_dentry->d_flags |= DCACHE_NEGATIVE;

		/* 更新fctx */
		dentry_unref(fctx->fc_dentry);
		fctx->fc_dentry = next_dentry;
		return 0;
	}
	return -ENOMEM;
}

int32 dentry_monkey(struct fcontext* fctx) {
	// if (fctx->fc_action >= VFS_ACTION_MAX) return -EINVAL;

	// monkey_intent_handler_t handler = dentry_intent_table[fctx->fc_action];
	// if (!handler) return -ENOTSUP;

	return dentry_monkey_pathwalk(fctx);
}
// clang-format off
monkey_intent_handler_t dentry_intent_table[VFS_ACTION_MAX] = {
    [VFS_ACTION_PATHWALK] = dentry_monkey_pathwalk, 	// 处理fc_string的路径字符串，并继续执行path_walk
	[VFS_ACTION_CREATE] = dentry_monkey, 
	[VFS_ACTION_MKDIR] = dentry_monkey,
    [VFS_ACTION_RMDIR] = dentry_monkey,         
	[VFS_ACTION_UNLINK] = dentry_monkey, 
	[VFS_ACTION_RENAME] = dentry_monkey,
	[VFS_ACTION_MKNOD] = dentry_monkey_mknod,

};
// clang-format on