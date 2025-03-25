#include <kernel/fs/vfs/dentry.h>
#include <kernel/fs/vfs/inode.h>
#include <kernel/mm/kmalloc.h>
#include <kernel/time.h>
#include <util/hashtable.h>
#include <util/string.h>

#include <spike_interface/spike_utils.h>
#include <errno.h>


/* Dentry cache hashtable */
static struct hashtable dentry_hashtable;

static void* __dentry_get_key(struct list_head* node);
static unsigned int __dentry_hash(const void* key, unsigned int size);
static int __dentry_key_equals(const void* k1, const void* k2);
static struct dentry* __dentry_alloc(struct dentry* parent, const struct qstr* name);
static void __dentry_free(struct dentry* dentry);
static struct dentry* __find_in_lru_list(struct dentry* parent, const struct qstr* name);

/* 复合键结构 - 用于查找时构建临时键 */
struct dentry_key {
	struct dentry* parent; /* 父目录项 */
	struct qstr* name;     /* 名称 */
};

static struct list_head g_dentry_lru_list;  /* 全局LRU链表，用于dentry的复用 */
static unsigned int g_dentry_lru_count = 0; /* 当前LRU链表中的dentry数量 */
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
struct dentry* dentry_locate(struct dentry* parent, const struct qstr* name, int is_dir, bool revalidate, bool alloc) {
	struct dentry* dentry = NULL;
	bool type_match = true;

	if (!parent || !name || !name->name)
		return NULL;

	/* 确保名称有哈希值 */
	struct qstr tmp_name = *name;
	if (!tmp_name.hash)
		tmp_name.hash = full_name_hash(tmp_name.name, tmp_name.len);

	/* 1. 先尝试查找已有的dentry */
	dentry = d_lookup(parent, &tmp_name);

	/* 2. 如果找到但需要重新验证 */
	if (dentry && revalidate && dentry->d_operations && dentry->d_operations->d_revalidate) {
		if (!dentry->d_operations->d_revalidate(dentry, 0)) {
			/* 验证失败，放弃此dentry */
			dentry_put(dentry);
			d_drop(dentry);
			dentry = NULL;
		}
	}

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
			dentry_put(dentry);
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
						int ret = hashtable_insert(&dentry_hashtable, &dentry->d_hashNode);
						if (ret == 0) {
							dentry->d_flags |= DCACHE_HASHED;
						}
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
			int ret = hashtable_insert(&dentry_hashtable, &dentry->d_hashNode);
			if (ret == 0) {
				dentry->d_flags |= DCACHE_HASHED;
			}

			/* 标记为负dentry */
			dentry->d_flags |= DCACHE_NEGATIVE;
		}
	}

	return dentry;
}

struct dentry* dentry_get(struct dentry* dentry){
	if (!dentry)
		return NULL;

	atomic_inc(&dentry->d_refcount);
	return dentry;
}

/**
 * 初始化dentry缓存
 */
int init_dentry_hashtable(void) {
	sprint("Initializing dentry hashtable\n");

	/* 初始化dentry哈希表 */
	return hashtable_setup(&dentry_hashtable, 1024, /* 初始桶数 */
	                       75,                      /* 负载因子 */
	                       __dentry_hash, __dentry_get_key, __dentry_key_equals);
}

/**
 * 释放dentry引用
 */
int dentry_put(struct dentry* dentry) {
	if (!dentry)
		return -EINVAL;
	if (atomic_read(&dentry->d_refcount) <= 0)
		return -EINVAL;
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
	if (!dentry)
		return;

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
	if (dentry->d_inode && !list_empty(&dentry->d_aliasListNode)) {
		spinlock_lock(&dentry->d_inode->i_dentryList_lock);
		list_del_init(&dentry->d_aliasListNode);
		spinlock_unlock(&dentry->d_inode->i_dentryList_lock);
	}

	spinlock_unlock(&dentry->d_lock);

	/* 释放inode引用 */
	if (dentry->d_inode) {
		inode_put(dentry->d_inode);
		dentry->d_inode = NULL;
	}

	/* 释放父引用 */
	if (dentry->d_parent && dentry->d_parent != dentry) {
		dentry_put(dentry->d_parent);
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
unsigned int shrink_dentry_lru(unsigned int count) {
	struct dentry* dentry;
	struct list_head *pos, *n;
	unsigned int freed = 0;

	spinlock_lock(&g_dentry_lru_list_lock);

	/* 如果count为0，释放全部 */
	if (count == 0)
		count = g_dentry_lru_count;

	list_for_each_safe(pos, n, &g_dentry_lru_list) {
		if (freed >= count)
			break;

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
int dentry_instantiate(struct dentry* dentry, struct inode* inode) {
	if (!dentry || !inode)
		return -EINVAL;

	spinlock_lock(&dentry->d_lock);

	/* 如果已有inode，先解除关联 */
	if (dentry->d_inode) {
		/* 从inode的别名列表中移除 */
		if (!list_empty(&dentry->d_aliasListNode)) {
			spinlock_lock(&dentry->d_inode->i_dentryList_lock);
			list_del_init(&dentry->d_aliasListNode);
			spinlock_unlock(&dentry->d_inode->i_dentryList_lock);
		}

		/* 减少inode引用 */
		inode_put(dentry->d_inode);
		dentry->d_inode = NULL;
	}

	/* 增加inode引用计数 */
	inode = iget(inode);
	dentry->d_inode = inode;

	/* 添加到inode的别名列表 */
	spinlock_lock(&inode->i_dentryList_lock);
	list_add(&dentry->d_aliasListNode, &inode->i_dentryList);
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
static unsigned int __dentry_hash(const void* key, unsigned int size) {
	const struct dentry_key* dkey = (const struct dentry_key*)key;
	unsigned int hash;

	/* 结合父指针和名称哈希 */
	hash = (unsigned long)dkey->parent;
	hash = hash * 31 + dkey->name->hash;

	return hash;
}

/**
 * 比较两个dentry键是否相等
 */
static int __dentry_key_equals(const void* k1, const void* k2) {
	const struct dentry_key* key1 = (const struct dentry_key*)k1;
	const struct dentry_key* key2 = (const struct dentry_key*)k2;

	/* 首先比较父节点 */
	if (key1->parent != key2->parent)
		return 0;

	/* 然后比较名称 */
	const struct qstr* name1 = key1->name;
	const struct qstr* name2 = key2->name;

	if (name1->len != name2->len)
		return 0;

	return !memcmp(name1->name, name2->name, name1->len);
}


/**
 * dentry_isDir - 检查 dentry 是否为目录
 * @dentry: 要检查的 dentry
 *
 * 通过检查 dentry 关联的 inode 的模式，判断其是否为目录。
 *
 * 返回: 如果是目录则返回 true，否则返回 false
 */
static inline bool dentry_isDir(const struct dentry *dentry)
{
    if (!dentry || !dentry->d_inode)
        return false;
        
    return S_ISDIR(dentry->d_inode->i_mode);
}

/**
 * dentry_isSymlink - 检查 dentry 是否为符号链接
 * @dentry: 要检查的 dentry
 *
 * 通过检查 dentry 关联的 inode 的模式，判断其是否为符号链接。
 *
 * 返回: 如果是符号链接则返回 true，否则返回 false
 */
static inline bool dentry_isSymlink(const struct dentry *dentry)
{
    if (!dentry || !dentry->d_inode)
        return false;
        
    return S_ISLNK(dentry->d_inode->i_mode);
}

/**
 * dentry_isMountpoint - 检查 dentry 是否为挂载点
 * @dentry: 要检查的 dentry
 *
 * 通过检查 dentry 的标志位，判断其是否为挂载点。
 * 挂载点通常会设置 DCACHE_MOUNTED 标志。
 *
 * 返回: 如果是挂载点则返回 true，否则返回 false
 */
static inline bool dentry_isMountpoint(const struct dentry *dentry)
{
    if (!dentry)
        return false;
        
    return (dentry->d_flags & DCACHE_MOUNTED) != 0;
}

/* 保留现有函数，但将创建函数改为内部使用 */
static struct dentry* __dentry_alloc(struct dentry* parent, const struct qstr* name) {
	struct dentry* dentry;

	if (!name || !name->name)
		return NULL;

	/* 分配dentry结构 */
	dentry = kmalloc(sizeof(struct dentry));
	if (!dentry)
		return NULL;

	/* 初始化基本字段 */
	memset(dentry, 0, sizeof(struct dentry));
	spinlock_init(&dentry->d_lock);
	atomic_set(&dentry->d_refcount, 1);
	INIT_LIST_HEAD(&dentry->d_childList);
	INIT_LIST_HEAD(&dentry->d_lruListNode);
	INIT_LIST_HEAD(&dentry->d_aliasListNode);
	INIT_LIST_HEAD(&dentry->d_hashNode);

	/* 复制name */
	dentry->d_name = qstr_create_with_length(name->name, name->len);

	/* 设置父节点关系 */
	dentry->d_parent = parent ? dentry_get(parent) : dentry; /* 根目录是自己的父节点 */

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
static struct dentry* __find_in_lru_list(struct dentry* parent, const struct qstr* name)
{
    struct dentry* dentry = NULL;
    struct dentry_key key;
    
    if (!parent || !name)
        return NULL;
    
    /* 构造查询键 */
    key.parent = parent;
    key.name = (struct qstr*)name;
    
    /* 使用全局dentry哈希表直接查找 */
    dentry = hashtable_lookup(&dentry_hashtable, &key);
    
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
 * dentry_rename - 重命名dentry
 * @old_dentry: 源dentry
 * @new_dentry: 目标dentry
 *
 * 执行dentry重命名操作，包括更新哈希表和父子关系。
 * 此操作需谨慎，因为它会更改哈希表中的键。
 *
 * 返回: 成功返回0，失败返回错误码
 */
int dentry_rename(struct dentry *old_dentry, struct dentry *new_dentry)
{
    int error = 0;
    
    if (!old_dentry || !new_dentry)
        return -EINVAL;
    
    /* 不能重命名为自身 */
    if (old_dentry == new_dentry)
        return 0;
    
    /* 锁定两个dentry - 按地址顺序加锁避免死锁 */
    if (old_dentry < new_dentry) {
        spinlock_lock(&old_dentry->d_lock);
        spinlock_lock(&new_dentry->d_lock);
    } else {
        spinlock_lock(&new_dentry->d_lock);
        spinlock_lock(&old_dentry->d_lock);
    }
    
    /* 从哈希表中移除旧dentry */
    if (old_dentry->d_flags & DCACHE_HASHED) {
        hashtable_remove(&dentry_hashtable, &old_dentry->d_hashNode);
        old_dentry->d_flags &= ~DCACHE_HASHED;
    }
    
    /* 如果目标dentry已存在且已哈希，从哈希表移除 */
    if (new_dentry->d_flags & DCACHE_HASHED) {
        hashtable_remove(&dentry_hashtable, &new_dentry->d_hashNode);
        new_dentry->d_flags &= ~DCACHE_HASHED;
    }
    
    /* 更新旧dentry的父和名称 */
    if (old_dentry->d_parent != new_dentry->d_parent) {
        /* 从旧父移除 */
        spinlock_lock(&old_dentry->d_parent->d_lock);
        list_del(&old_dentry->d_parentListNode);
        spinlock_unlock(&old_dentry->d_parent->d_lock);
        
        /* 添加到新父 */
        spinlock_lock(&new_dentry->d_parent->d_lock);
        list_add(&old_dentry->d_parentListNode, &new_dentry->d_parent->d_childList);
        spinlock_unlock(&new_dentry->d_parent->d_lock);
        
        /* 更新父引用 */
        struct dentry *old_parent = old_dentry->d_parent;
        old_dentry->d_parent = get_dentry(new_dentry->d_parent);
        dentry_put(old_parent);
    }
	kfree(old_dentry->d_name);
	old_dentry->d_name = qstr_create_with_len(new_dentry->d_name->name, new_dentry->d_name->len);
    
    

    
    
    if (hashtable_insert(&dentry_hashtable, old_dentry) == 0) {
        old_dentry->d_flags |= DCACHE_HASHED;
    } else {
        error = -EBUSY;
    }
    
out:
    spinlock_unlock(&new_dentry->d_lock);
    spinlock_unlock(&old_dentry->d_lock);
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
int dentry_revalidate(struct dentry *dentry, unsigned int flags)
{
    if (!dentry)
        return -EINVAL;
    
    /* 如果dentry没有d_operations或d_revalidate函数，默认认为有效 */
    if (!dentry->d_operations || !dentry->d_operations->d_revalidate)
        return 1;
    
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
struct dentry *dentry_follow_link(struct dentry *link_dentry)
{
    struct dentry *target_dentry = NULL;
    char *link_value = NULL;
    int res, link_len;
    int max_loops = 8; /* Maximum symlink recursion depth */
    bool is_absolute;
    
    /* Validate input parameters */
    if (!link_dentry || !link_dentry->d_inode)
        return ERR_PTR(-EINVAL);
    
    /* Ensure it's a symlink */
    if (!S_ISLNK(link_dentry->d_inode->i_mode))
        return ERR_PTR(-EINVAL);
    
    /* Allocate buffer for link content */
    link_value = kmalloc(PATH_MAX);
    if (!link_value)
        return ERR_PTR(-ENOMEM);
    
    /* Get a reference to the original link */
    struct dentry *current_dentry = dentry_get(link_dentry);
    
    /* Track the starting point for relative path resolution */
    struct dentry *base_dir = dentry_get(link_dentry->d_parent);
    
    while (max_loops-- > 0) {
        /* Read the link content */
        if (!current_dentry->d_inode->i_op || !current_dentry->d_inode->i_op->readlink) {
            res = -EINVAL;
            goto out_error;
        }
        
        link_len = current_dentry->d_inode->i_op->readlink(current_dentry, link_value, PATH_MAX - 1);
        if (link_len < 0) {
            res = link_len;
            goto out_error;
        }
        
        /* Ensure the string is null-terminated */
        link_value[link_len] = '\0';
        
        /* Determine if the path is absolute or relative */
        is_absolute = (link_value[0] == '/');
        
        /* Release current dentry before resolving the next path */
        dentry_put(current_dentry);
        current_dentry = NULL;
        
        /* Parse the link path */
        struct path link_path;
        struct nameidata nd;
        
        /* Pseudocode: Initialize nameidata with appropriate context */
        /*
         * nameidata_init(&nd);
         * nd.path.dentry = is_absolute ? root_dentry : base_dir;
         * nd.path.mnt = current->fs->root.mnt;
         * nd.flags = LOOKUP_FOLLOW;
         */
        
        /* Pseudocode: Path lookup - would be implemented via path_lookup() in a real system */
        /*
         * res = path_lookup(link_value, &nd);
         * if (res) goto out_error;
         * link_path = nd.path;
         */
        
        /* For demonstration purposes, we'll use the existing path_create function */
        unsigned int lookup_flags = LOOKUP_FOLLOW;
        if (!is_absolute) {
            /* Pseudocode: For relative paths, we need to start from base_dir */
            /*
             * // Real implementation would use something like:
             * res = vfs_path_lookup(base_dir, base_dir->d_sb->s_root.mnt,
             *                      link_value, lookup_flags, &link_path);
             */
            
            /* Since we're using path_create, we need to construct the full path */
            char full_path[PATH_MAX*2];
            char base_path[PATH_MAX];
            
            /* Get the path of the parent directory */
            dentry_rawPath(base_dir, base_path, PATH_MAX);
            
            /* Construct full path by concatenating parent path and link content */
            if (strlen(base_path) + strlen(link_value) + 2 > PATH_MAX*2) {
                res = -ENAMETOOLONG;
                goto out_error;
            }
            
            /* Handle special case when base_path is root */
            if (strcmp(base_path, "/") == 0)
                snprintf(full_path, PATH_MAX*2, "/%s", link_value);
            else
                snprintf(full_path, PATH_MAX*2, "%s/%s", base_path, link_value);
            
            res = path_create(full_path, lookup_flags, &link_path);
        } else {
            /* Absolute path - just use path_create */
            res = path_create(link_value, lookup_flags, &link_path);
        }
        
        if (res) {
            goto out_error;
        }
        
        /* Get a reference to the resolved dentry */
        current_dentry = dentry_get(link_path.dentry);
        
        /* Clean up the path */
        /*
         * Pseudocode: Real implementation would have a proper path_put() function
         * path_put(&link_path);
         */
        path_destroy(&link_path);
        
        /* Check if the target exists */
        if (!current_dentry->d_inode) {
            res = -ENOENT;
            goto out_error;
        }
        
        /* If not a symlink, we're done */
        if (!S_ISLNK(current_dentry->d_inode->i_mode)) {
            target_dentry = current_dentry;
            current_dentry = NULL; /* Prevent it from being released */
            break;
        }
        
        /* For another symlink, update the base_dir for relative path resolution */
        dentry_put(base_dir);
        base_dir = dentry_get(current_dentry->d_parent);
    }
    
    /* Check for too many levels of symlinks */
    if (max_loops < 0) {
        res = -ELOOP;
        goto out_error;
    }
    
    /* Success - free resources and return target */
    kfree(link_value);
    if (base_dir)
        dentry_put(base_dir);
    
    return target_dentry;

out_error:
    /* Clean up on error */
    kfree(link_value);
    if (current_dentry)
        dentry_put(current_dentry);
    if (base_dir)
        dentry_put(base_dir);
    return ERR_PTR(res);
}



/**
 * dentry_rawPath - 构造dentry的完整路径
 * @dentry: 要构造路径的dentry
 * @buf: 输出缓冲区
 * @buflen: 缓冲区长度
 *
 * 从给定的dentry构造完整路径名，结果放入buf中。
 * 路径是从根目录到dentry的完整路径。
 *
 * 返回: 成功返回缓冲区中路径的起始地址，失败返回NULL
 */
char *dentry_rawPath(struct dentry *dentry, char *buf, int buflen)
{
    if (!dentry || !buf || buflen <= 0)
        return NULL;
    
    /* 保留最后一个字符用于NULL结尾 */
    char *end = buf + buflen - 1;
	*end = '\0';
    char *start = end;
    struct dentry *d = dentry_get(dentry);
    /* 回溯构建路径 */
    while (d) {
        /* 获取父dentry，需要处理根目录的情况 */
        spin_lock(&d->d_lock);
        
        /* 如果是根目录，特殊处理 */
        if (d == d->d_parent) {
            /* 根目录路径为"/" */
            if (start == end)
                --start;
            *start = '/';
            spin_unlock(&d->d_lock);
            dentry_put(d);
            break;
        }
        
        /* 获取名称长度 */
        int name_len = d->d_name->len;
        /* 检查空间是否足够 */
        if (start - buf < name_len + 1) {
            spin_unlock(&d->d_lock);
            dentry_put(d);
            return NULL; /* 缓冲区太小 */
        }
        
        /* 为当前组件添加目录分隔符 */
        --start;
        *start = '/';
        
        /* 复制名称 */
        start -= name_len;
        memcpy(start, d->d_name->name, name_len);
        
        /* 增加父dentry引用并释放当前锁 */
        spin_unlock(&d->d_lock);
        
        /* 释放当前dentry引用，移动到父级 */\
		struct dentry* parent = d->d_parent;
        dentry_put(d);
        d = dentry_get(parent);
    }
    
    /* 如果路径为空（非常奇怪的情况），返回根路径 */
    if (start == end) {
        --start;
        *start = '/';
    }
    
    return start;
}


/**
 * dentry_permission - 检查dentry对象的权限
 * @dentry: 要检查权限的dentry对象
 * @mask: 请求的权限掩码(MAY_READ、MAY_WRITE、MAY_EXEC等)
 *
 * 检查给定dentry对象是否具有请求的权限，首先尝试调用
 * 文件系统特定的permission操作，如果不存在则回退到
 * 通用的inode_permission检查。
 *
 * 返回: 如果有权限返回0，否则返回负错误码
 */
int dentry_permission(struct dentry *dentry, int mask)
{
    struct inode *inode;
    
    if (!dentry)
        return -EINVAL;
    
    inode = dentry->d_inode;
    if (!inode)
        return -ENOENT;  /* 负向dentry没有inode */
    
    /* 回退到inode权限检查 */
    return inode_checkPermission(inode, mask);
}

/**
 * dentry_getxattr - 获取dentry对象的扩展属性
 * @dentry: 要操作的dentry对象
 * @name: 扩展属性名称
 * @value: 输出缓冲区，用于存储属性值
 * @size: 缓冲区大小
 *
 * 获取dentry对象的指定扩展属性值。如果value为NULL或size为0，
 * 则只返回属性值的长度而不复制数据。
 *
 * 返回: 成功返回属性值的长度，错误返回负错误码
 */
int dentry_getxattr(struct dentry *dentry, const char *name, void *value, size_t size)
{
    struct inode *inode;
    
    if (!dentry || !name)
        return -EINVAL;
    
    inode = dentry->d_inode;
    if (!inode)
        return -ENOENT;
    
    /* 检查inode是否支持getxattr操作 */
    if (!inode->i_op || !inode->i_op->getxattr)
        return -EOPNOTSUPP;
    
    /* 调用inode的getxattr方法 */
    return inode->i_op->getxattr(dentry, name, value, size);
}

/**
 * dentry_setxattr - 设置dentry对象的扩展属性
 * @dentry: 要操作的dentry对象
 * @name: 扩展属性名称
 * @value: 属性值
 * @size: 属性值的大小
 * @flags: 设置标志(如XATTR_CREATE、XATTR_REPLACE)
 *
 * 为dentry对象设置指定的扩展属性。可以指定以下标志：
 * - XATTR_CREATE: 仅当属性不存在时创建
 * - XATTR_REPLACE: 仅当属性已存在时替换
 *
 * 返回: 成功返回0，错误返回负错误码
 */
int dentry_setxattr(struct dentry *dentry, const char *name, const void *value, size_t size, int flags)
{
    struct inode *inode;
    
    if (!dentry || !name || (!value && size > 0))
        return -EINVAL;
    
    inode = dentry->d_inode;
    if (!inode)
        return -ENOENT;
    
    /* 检查写入权限 */
    int err = inode_checkPermission(inode, MAY_WRITE);
    if (err)
        return err;
    
    /* 检查inode是否支持setxattr操作 */
    if (!inode->i_op || !inode->i_op->setxattr)
        return -EOPNOTSUPP;
    
    /* 调用inode的setxattr方法 */
    err = inode->i_op->setxattr(dentry, name, value, size, flags);
    
    if (err == 0) {
        /* 属性修改，标记inode为脏 */
        inode_setDirty(inode);
        
        /* 如果文件系统支持，更新ctime */
        inode->i_ctime = current_time(inode->i_superblock);
    }
    
    return err;
}


/**
 * dentry_removexattr - 移除dentry对象的扩展属性
 * @dentry: 要操作的dentry对象
 * @name: 要移除的扩展属性名称
 *
 * 移除dentry对象的指定扩展属性。
 *
 * 返回: 成功返回0，错误返回负错误码
 */
int dentry_removexattr(struct dentry *dentry, const char *name)
{
    struct inode *inode;
    int error;
    
    if (!dentry || !name)
        return -EINVAL;
    
    inode = dentry->d_inode;
    if (!inode)
        return -ENOENT;
    
    /* 检查写入权限 */
    error = inode_checkPermission(inode, MAY_WRITE);
    if (error)
        return error;
    
    /* 检查inode是否支持removexattr操作 */
    if (!inode->i_op || !inode->i_op->removexattr)
        return -EOPNOTSUPP;
    
    /* 调用inode的removexattr方法 */
    error = inode->i_op->removexattr(dentry, name);
    
    if (error == 0) {
        /* 属性修改，标记inode为脏 */
        inode_setDirty(inode);
        
        /* 如果文件系统支持，更新ctime */
        inode->i_ctime = current_time(inode->i_superblock);
    }
    
    return error;
}


/**
 * 从目录树中剥离 dentry
 * 注意：此函数通常不应直接调用，除非明确了解其后果
 * 应该优先使用 dentry_delete 而非直接调用此函数
 */
void dentry_prune(struct dentry *dentry) {
    if (!dentry)
        return;
    
    spinlock_lock(&dentry->d_lock);
    
    /* 调用文件系统特定的修剪方法 */
    if (dentry->d_operations && dentry->d_operations->d_prune)
        dentry->d_operations->d_prune(dentry);
    
    /* 从哈希表中移除 */
    if (dentry->d_flags & DCACHE_HASHED) {
        hashtable_remove(&dentry_hashtable, &dentry->d_hashNode);
        dentry->d_flags &= ~DCACHE_HASHED;
    }
    
    /* 从父目录的子列表中移除 */
    if (dentry->d_parent && dentry->d_parent != dentry && 
        !list_empty(&dentry->d_parentListNode)) {
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
int dentry_delete(struct dentry *dentry) {
    struct inode *inode;
    
    if (!dentry)
        return -EINVAL;
    
    inode = dentry->d_inode;
    if (!inode)
        return -ENOENT;  // 没有关联的 inode
    
    spinlock_lock(&inode->i_lock);
    
    // 减少硬链接计数
    if (inode->i_nlink > 0)
        inode->i_nlink--;
    
    // 如果是最后一个硬链接，标记 inode 为删除状态
    if (inode->i_nlink == 0)
        inode->i_state |= I_FREEING;
    
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
    if (!dentry)
        return false;
    
    /* 最快速的检查方法 - 检查DCACHE_MOUNTED标志 */
    if (dentry->d_flags & DCACHE_MOUNTED)
        return true;
    
    /* 
     * 注意：在完整的实现中，可能还需要执行其他检查，
     * 例如查询全局的mount列表，确保标志是最新的。
     * 但这取决于挂载系统的具体实现和更新策略。
     */
    
    return false;
}