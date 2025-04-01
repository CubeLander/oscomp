#include <kernel/util.h>
#include <kernel/vfs.h>

#define RAMFS_MAGIC 0x534d4152 /* "SMAR" */

/**
 * Handlers for superblock operations
 */
static int32 ramfs_intent_alloc_inode(struct fcontext* fctx);
static int32 ramfs_intent_destroy_inode(struct fcontext* fctx);
static int32 ramfs_intent_write_inode(struct fcontext* fctx);
static int32 ramfs_intent_evict_inode(struct fcontext* fctx);
static int32 ramfs_intent_sync_fs(struct fcontext* fctx);
static int32 ramfs_intent_statfs(struct fcontext* fctx);
static int32 ramfs_intent_put_super(struct fcontext* fctx);

/**
 * ramfs_monkey - ramfs-specific context handler
 * @fctx: Filesystem context
 *
 * Dispatches ramfs operations to the appropriate handler
 * based on the action specified in the context.
 *
 * Returns 0 on success, negative error code on failure.
 */
int32 ramfs_monkey(struct fcontext* fctx) {
	if (!fctx) {
		return -EINVAL;
	}

	/* Validate action is within range */
	if (fctx->fc_action >= VFS_ACTION_MAX || fctx->fc_action < 0) {
		return -EINVAL;
	}

	/* Get the handler for this action */
	monkey_intent_handler_t handler = ramfs_intent_table[fctx->fc_action];

	/* Call the handler if available, otherwise return error */
	if (handler) {
		return handler(fctx);
	}

	return -ENOSYS; /* Function not implemented */
}

/**
 * Static ramfs type instance - no need for superblock_operations anymore
 */
static struct fstype ramfs_fs_type = {
    .fs_name = "ramfs",
    .fs_flags = 0, /* No special flags needed for ramfs */
    .fs_monkey = ramfs_monkey,
    .fs_capabilities = 0,
};

/**
 * register_ramfs - Register the ramfs filesystem
 *
 * Initializes and registers the ramfs filesystem type.
 *
 * Returns 0 on success, negative error code on failure.
 */
int32 register_ramfs(void) {
	/* Initialize ramfs-specific fields */
	spin_lock_init(&ramfs_fs_type.fs_list_superblock_lock);
	INIT_LIST_HEAD(&ramfs_fs_type.fs_list_superblock);
	/* Register with the VFS */
	return fstype_register(&ramfs_fs_type);
}

/**
 * ramfs的挂载处理函数
 */
static int32 ramfs_intent_mount(struct fcontext* fctx) {
    struct fstype* type = fctx->fc_fstype;
    const char* source = (const char*)fctx->user_buf;
    void* data = fctx->fc_iostruct;
    uint64 flags = fctx->user_flags;
    bool is_rootfs = (flags & MOUNT_ROOTFS) != 0;  // 使用挂载标志判断是否为根文件系统
    
    /* 在context中设置设备ID */
    fctx->io_dev = 0; // 对于ramfs，设备ID为0
    
    /* 使用intent系统创建superblock */
    int32 ret = MONKEY_WITH_ACTION(ramfs_monkey, fctx, FS_ACTION_CREATESUPERBLOCK, 0);
    if (ret < 0) {
        return ret;
    }
    
    /* 从context获取创建的superblock */
    struct superblock* sb = fctx->fc_superblock;
    
    if (is_rootfs) {
        /* 对于根文件系统，创建特殊的根挂载点 */
        struct vfsmount* root_mnt = superblock_acquireMount(sb, flags, source);
        if (!root_mnt) {
            dentry_unref(sb->s_root);
            kfree(sb);
            return -EINVAL;
        }
        
        /* 根挂载点的特殊设置 */
        root_mnt->mnt_root = sb->s_root;
        root_mnt->mnt_path.dentry = dentry_ref(sb->s_root);
        root_mnt->mnt_path.mnt = NULL; /* 根挂载点没有父挂载点 */
        
        /* 设置全局根挂载点 (假设有这样的设置函数) */
        set_root_mnt(root_mnt);
        
        /* 将挂载点存储在context中供调用者使用 */
        fctx->fc_mount = root_mnt;
    } else {
        /* 普通挂载点处理 */
        struct dentry* target_dentry = fctx->fc_dentry;
        
        /* 检查挂载点是否有效 */
        if (!target_dentry || !target_dentry->d_inode || 
            !S_ISDIR(target_dentry->d_inode->i_mode)) {
            dentry_unref(sb->s_root);
            kfree(sb);
            return -ENOTDIR; /* 挂载点必须是一个目录 */
        }
        
        /* 创建挂载点 */
        struct vfsmount* mnt = superblock_acquireMount(sb, flags, source);
        if (!mnt) {
            dentry_unref(sb->s_root);
            kfree(sb);
            return -EINVAL;
        }
        
        /* 设置挂载点标志位 */
        target_dentry->d_flags |= DCACHE_MOUNTED;
        
        /* 设置挂载点路径 */
        mnt->mnt_path.dentry = dentry_ref(target_dentry);
        mnt->mnt_path.mnt = fctx->fc_mount ? mount_ref(fctx->fc_mount) : NULL;
        
        /* 将挂载点存储在context中供调用者使用 */
        fctx->fc_mount = mnt;
    }
    
    return 0;
}



/**
 * Implementation of superblock operation handlers using the intent system
 */

static int32 ramfs_intent_alloc_inode(struct fcontext* fctx) {
	struct superblock* sb = fctx->fc_superblock;

	/* Allocate a new inode */
	struct inode* inode = kzalloc(sizeof(struct inode));
	if (!inode) {
		return -ENOMEM;
	}

	/* Initialize inode */
	atomic_set(&inode->i_refcount, 1);
	inode->i_superblock = sb;
	inode->i_ino = atomic64_inc_return(&sb->s_next_ino);
	atomic_inc(&sb->s_ninodes);

	/* Initialize locks and lists */
	spin_lock_init(&inode->i_lock);
	INIT_LIST_HEAD(&inode->i_dentryList);
	spin_lock_init(&inode->i_dentryList_lock);

	/* Add to superblock's inode list */
	spin_lock(&sb->s_list_all_inodes_lock);
	list_add(&inode->i_s_list_node, &sb->s_list_all_inodes);
	spin_unlock(&sb->s_list_all_inodes_lock);

	/* Store the result in the context */
	fctx->fc_iostruct = inode;

	return 0;
}

static int32 ramfs_intent_destroy_inode(struct fcontext* fctx) {
	struct inode* inode = fctx->fc_dentry->d_inode;

	/* Remove from superblock's inode list */
	spin_lock(&inode->i_superblock->s_list_all_inodes_lock);
	list_del(&inode->i_s_list_node);
	spin_unlock(&inode->i_superblock->s_list_all_inodes_lock);

	/* Free inode memory */
	kfree(inode);

	return 0;
}

static int32 ramfs_intent_write_inode(struct fcontext* fctx) {
	/* For ramfs, all data is in memory, no need to write to disk */
	return 0;
}

static int32 ramfs_intent_evict_inode(struct fcontext* fctx) {
	struct inode* inode = fctx->fc_dentry->d_inode;

	/* Clear inode data */
	inode->i_size = 0;

	/* Free any memory used for data */
	if (inode->i_fs_info) {
		kfree(inode->i_fs_info);
		inode->i_fs_info = NULL;
	}

	/* Return inode to clean state */
	inode->i_state &= ~(I_DIRTY | I_DIRTY_SYNC | I_DIRTY_DATASYNC);

	return 0;
}

static int32 ramfs_intent_sync_fs(struct fcontext* fctx) {
	/* For ramfs, all data is in memory, nothing to sync */
	return 0;
}

static int32 ramfs_intent_statfs(struct fcontext* fctx) {
	struct kstatfs* buf = (struct kstatfs*)fctx->fc_iostruct;

	if (!buf) {
		return -EINVAL;
	}

	/* Fill in the statistics */
	buf->f_type = RAMFS_MAGIC;
	buf->f_bsize = PAGE_SIZE;
	buf->f_namelen = NAME_MAX;

	/* For ramfs, report "infinite" space (limited by system RAM) */
	buf->f_blocks = 0;
	buf->f_bfree = 0;
	buf->f_bavail = 0;
	buf->f_files = atomic_read(&fctx->fc_dentry->d_inode->i_superblock->s_ninodes);
	buf->f_ffree = UINT64_MAX - buf->f_files;

	return 0;
}

static int32 ramfs_intent_put_super(struct fcontext* fctx) {
	struct superblock* sb = (struct superblock*)fctx->fc_iostruct;

	/* Free superblock resources */
	if (sb->s_root) {
		dentry_unref(sb->s_root);
	}

	kfree(sb);

	return 0;
}

/**
 * Adapter function to bridge between superblock_operations and the intent system
 * This would be used where the VFS code still expects to call s_operations methods
 */
static struct inode* ramfs_adapter_alloc_inode(struct superblock* sb) {
	/* Create a temporary context for the operation */
	struct fcontext ctx = {0};
	ctx.fc_fstype = sb->s_fstype;
	ctx.fc_iostruct = sb;

	/* Call through our intent system */
	int32 ret = MONKEY_WITH_ACTION(ramfs_monkey, &ctx, SB_ACTION_ALLOC_INODE, 0);
	if (ret < 0) {
		return NULL;
	}

	return (struct inode*)ctx.fc_iostruct;
}

/**
 * ramfs_intent_create_superblock - 创建一个新的ramfs superblock
 * @fctx: 文件系统上下文
 *
 * 创建并初始化一个新的ramfs superblock。
 * 创建的superblock被存储在fctx->fc_superblock中。
 *
 * 返回0表示成功，负数表示错误代码。
 */
static int32 ramfs_intent_create_superblock(struct fcontext* fctx) {
    struct fstype* type = fctx->fc_fstype;
    dev_t dev_id = fctx->io_dev;
    
    /* 创建新的superblock */
    struct superblock* sb = kzalloc(sizeof(struct superblock));
    if (!sb) {
        return -ENOMEM;
    }
    
    /* 初始化superblock */
    sb->s_blocksize = PAGE_SIZE;
    sb->s_blocksize_bits = PAGE_SHIFT;
    sb->s_magic = RAMFS_MAGIC;
    sb->s_time_granularity = 1;
    sb->s_fstype = type;
    sb->s_device_id = dev_id;
    
    /* 初始化链表 */
    INIT_LIST_HEAD(&sb->s_list_mounts);
    INIT_LIST_HEAD(&sb->s_list_all_inodes);
    INIT_LIST_HEAD(&sb->s_list_clean_inodes);
    INIT_LIST_HEAD(&sb->s_list_dirty_inodes);
    INIT_LIST_HEAD(&sb->s_list_io_inodes);
    
    /* 初始化锁 */
    spin_lock_init(&sb->s_lock);
    spin_lock_init(&sb->s_list_mounts_lock);
    spin_lock_init(&sb->s_list_all_inodes_lock);
    spin_lock_init(&sb->s_list_inode_states_lock);
    
    /* 初始化原子变量 */
    atomic_set(&sb->s_refcount, 1);
    atomic_set(&sb->s_ninodes, 0);
    atomic64_set(&sb->s_next_ino, 1);
    
    /* 创建根inode */
    struct inode* root_inode = NULL;
    struct fcontext alloc_inode_ctx = {
        .fc_fstype = type,
        .fc_superblock = sb,
    };
    
    /* 使用intent系统分配inode */
    int32 ret = MONKEY_WITH_ACTION(ramfs_monkey, &alloc_inode_ctx, SB_ACTION_ALLOC_INODE, 0);
    if (ret < 0) {
        kfree(sb);
        return ret;
    }
    
    root_inode = (struct inode*)alloc_inode_ctx.fc_iostruct;
    if (!root_inode) {
        kfree(sb);
        return -ENOMEM;
    }
    
    /* 初始化根inode */
    root_inode->i_mode = S_IFDIR | 0755;
    root_inode->i_uid = 0;
    root_inode->i_gid = 0;
    root_inode->i_ino = 1;
    
    /* 创建文件系统根dentry */
    struct dentry* root_dentry = kzalloc(sizeof(struct dentry));
    if (!root_dentry) {
        inode_unref(root_inode);
        kfree(sb);
        return -ENOMEM;
    }
    
    /* 初始化根dentry */
    atomic_set(&root_dentry->d_refcount, 1);
    root_dentry->d_inode = root_inode;
    root_dentry->d_parent = root_dentry; /* 根是自己的父节点 */
    
    /* 创建一个名为"/" 的qstr */
    struct qstr* root_name = kzalloc(sizeof(struct qstr));
    if (!root_name) {
        dentry_unref(root_dentry);
        inode_unref(root_inode);
        kfree(sb);
        return -ENOMEM;
    }
    
    root_name->name = kstrdup("/");
    root_name->len = 1;
    root_name->hash = hash_string("/",0);
    root_dentry->d_name = root_name;
    
    /* 初始化子目录列表 */
    INIT_LIST_HEAD(&root_dentry->d_childList);
    
    /* 设置superblock的根 */
    sb->s_root = root_dentry;
    
    /* 将创建的superblock存储在context中 */
    fctx->fc_superblock = sb;
    
    return 0;
}

/**
 * ramfs_intent_table - Maps action IDs to ramfs-specific handlers
 */
static monkey_intent_handler_t ramfs_intent_table[VFS_ACTION_MAX] = {
    /* Common filesystem operations */
    [FS_ACTION_MOUNT] = ramfs_intent_mount,
    [FS_ACTION_UMOUNT] = ramfs_intent_umount,
    [FS_ACTION_INITFS] = ramfs_intent_initfs,
    [FS_ACTION_EXITFS] = ramfs_intent_exitfs,
	[FS_ACTION_CREATESUPERBLOCK] = ramfs_intent_create_superblock,

    /* Superblock operations */
    [SB_ACTION_ALLOC_INODE] = ramfs_intent_alloc_inode,
    [SB_ACTION_DESTROY_INODE] = ramfs_intent_destroy_inode,
    [SB_ACTION_WRITE_INODE] = ramfs_intent_write_inode,
    [SB_ACTION_EVICT_INODE] = ramfs_intent_evict_inode,
    [SB_ACTION_SYNC_FS] = ramfs_intent_sync_fs,
    [SB_ACTION_STATFS] = ramfs_intent_statfs,
    [SB_ACTION_PUT_SUPER] = ramfs_intent_put_super,

    /* Add more handlers as needed */
};