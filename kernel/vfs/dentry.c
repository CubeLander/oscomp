#include <kernel/mmu.h>
#include <kernel/sched.h>
#include <kernel/sprint.h>
#include <kernel/time.h>
#include <kernel/types.h>
#include <kernel/util.h>
#include <kernel/vfs.h>

static struct dentry* __dentry_alloc(struct dentry* parent, const struct qstr* name);
static void __dentry_free(struct dentry* dentry);

static struct dentry* virtual_root_dentry = NULL;

struct dentry* get_virtual_root_dentry(void) {
    if (!virtual_root_dentry) {
        /* Initialize the virtual root dentry */
        virtual_root_dentry = kzalloc(sizeof(struct dentry));
        if (!virtual_root_dentry) {
            return NULL; /* Memory allocation failed */
        }
        
        /* Set up minimal required fields */
        atomic_set(&virtual_root_dentry->d_refcount, 1);
        virtual_root_dentry->d_parent = virtual_root_dentry; /* Self-referential */
        virtual_root_dentry->d_flags = DCACHE_MOUNTED; /* Always treated as a mount point */
        
        /* Create a name for the virtual root */
        struct qstr* root_name = kzalloc(sizeof(struct qstr));
        if (!root_name) {
            kfree(virtual_root_dentry);
            virtual_root_dentry = NULL;
            return NULL;
        }
        
        root_name->name = kstrdup("");  /* Empty string for the virtual root */
        root_name->len = 0;
        root_name->hash = 0;
        virtual_root_dentry->d_name = root_name;
        
        /* Initialize lists */
        INIT_LIST_HEAD(&virtual_root_dentry->d_childList);
        
        /* No inode attached - this is purely a virtual node */
    }
    
    return dentry_ref(virtual_root_dentry);
}




struct dentry* dentry_ref(struct dentry* dentry) {
	if (!dentry) return NULL;

	atomic_inc(&dentry->d_refcount);
	return dentry;
}

/**
 * 释放dentry引用
 */
int32 dentry_unref(struct dentry* dentry) {
	if (!dentry) return -EINVAL;
	if (atomic_read(&dentry->d_refcount) <= 0) return -EINVAL;
	/* 如果引用计数降为0 */
	if (atomic_dec_and_test(&dentry->d_refcount)) {
		dentry_free(dentry);
		return 1;
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

		/* 添加到父节点的子列表 */
		spinlock_lock(&parent->d_lock);
		list_add(&dentry->d_parentListNode, &parent->d_childList);
		spinlock_unlock(&parent->d_lock);
	}

	return dentry;
}

/**
 * 标记 dentry 为删除状态并处理相关的 inode
 * 不要求引用计数为 0，因为这可能是对仍在使用的文件执行 unlink
 *
 * @param dentry: 要标记为删除的 dentry
 * @return: 成功返回 0，失败返回错误码
 */
int32 dentry_delete(struct dentry* dentry) {
	if (!dentry) return -EINVAL;

	struct inode* inode = dentry->d_inode;
	if (inode) {
		inode_unref(inode);
		if (atomic_dec_and_test(&inode->i_nlink)) {
			// 如果引用计数降为 0，表示没有硬链接了
			// 这里可以选择直接释放 inode 或者标记为删除状态
			spinlock_lock(&inode->i_lock);
			inode->i_state |= I_FREEING;
			spinlock_unlock(&inode->i_lock);
		}
	}

	// 从目录树分离 dentry
	dentry_prune(dentry);

	// 注意：不需要显式释放 inode，
	// 当 dentry 的引用计数降为 0 时，会调用 dentry_put，
	// 这会最终减少 inode 的引用计数

	return 0;
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

int32 dentry_monkey_lookup(struct fcontext* fctx) {
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

	/* 3. 如果需要创建新dentry */
	next_dentry = __dentry_alloc(parent, &qname);
	if (next_dentry) {
		next_dentry->d_flags |= DCACHE_NEGATIVE;

		/* 更新fctx */
		dentry_unref(fctx->fc_dentry);
		fctx->fc_dentry = next_dentry;
		return 0;
	}
	return -ENOMEM;
}

int32 dentry_monkey(struct fcontext* fctx) {
	if (fctx->fc_action >= VFS_MAX) return -EINVAL;
	monkey_intent_handler_t handler = dentry_intent_table[fctx->fc_action];
	if (!handler) return -ENOTSUP;

	return handler(fctx);
}
// clang-format off
monkey_intent_handler_t dentry_intent_table[VFS_MAX] = {
    [DENTRY_LOOKUP] = dentry_monkey_lookup, 	// 处理fc_string的路径字符串，并继续执行path_walk

};
// clang-format on