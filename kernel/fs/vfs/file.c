#include <kernel/fs/vfs/dentry.h>
#include <kernel/fs/vfs/file.h>
#include <kernel/fs/vfs/inode.h>
#include <kernel/fs/vfs/namespace.h>
#include <kernel/fs/vfs/path.h>
#include <kernel/fs/vfs/vfs.h>
#include <kernel/mm/kmalloc.h>
#include <kernel/sched/process.h>
#include <kernel/sched/sched.h>
#include <kernel/types.h>
#include <util/qstr.h>
#include <util/string.h>

#include <spike_interface/spike_utils.h>

static struct file* __file_alloc(struct dentry* dentry, fmode_t mode);
static struct file* __file_open(struct dentry* dentry, struct vfsmount* mnt, int flags, fmode_t mode);
static int __file_free(struct file* filp);

struct file* file_get(struct file* file) {
	if (!file)
		return NULL;
	if (atomic_read(&file->f_refcount) <= 0) {
		panic("file_get: f_refcount is already 0\n");
	}
	/* Increment reference count */
	atomic_inc(&file->f_refcount);
	return file;
}

/**
 * file_openPath - Open a file using a pre-resolved path
 * @path: Path to the file
 * @flags: Open flags
 * @mode: Creation mode
 */
struct file* file_openPath(const struct path* path, int flags, fmode_t mode) {
	if (!path || !path->dentry)
		return ERR_PTR(-EINVAL);

	return __file_open(path->dentry, path->mnt, flags, mode);
}

/**
 * file_open - Traditional file open with char* path
 * @filename: Path to file
 * @flags: Open flags
 * @mode: Creation mode
 */
struct file* file_open(const char* filename, int flags, fmode_t mode) {
	// struct qstr qname;
	struct file* file;

	if (!filename)
		return ERR_PTR(-EINVAL);

	/* Initialize qstr from string */
	// qstr_init_from_str(&qname, filename);

	/* Use qstr version */
	// file = file_open_qstr(&qname, flags, mode);
	/* Convert qstr to path */
	struct path path;
	int error = path_create(&filename, 0, &path);
	/* Handle O_CREAT flag if file doesn't exist */
	if (error == -ENOENT && (flags & O_CREAT)) {
		/* Would handle file creation here */
		/* For now, just return the error */
		return ERR_PTR(error);
	} else if (error) {
		return ERR_PTR(error);
	}
	/* Open the file */
	file = __file_open(path.dentry, path.mnt, flags, mode);
	/* Release path reference */
	path_destroy(&path);

	return file;
}

/**
 * __file_free - Close a file and clean up resources
 * @filp: Pointer to the file to close
 * @owner: Task that owns the file descriptor (can be NULL for kernel-owned
 * files)
 *
 * Closes a file, releases resources, updates task's file descriptor table
 * if owner is specified, and calls the file's release operation.
 *
 * Returns 0 on success or negative error code on failure.
 */
int __file_free(struct file* filp) {
	int error = 0;
	int fd = -1;

	if (!filp)
		return -EINVAL;

	/* Release the file's regular resources */
	if (filp->f_operations && filp->f_operations->release) {
		/* Call the file-specific release operation */
		error = filp->f_operations->release(filp->f_inode, filp);
	}

	/* Handle file-specific cleanup s_operations */
	if (filp->f_private) {
		/* Some file types might require special cleanup of f_private */
		/* e.g., for pipes, sockets, etc. */
		/* Implementation depends on file type */
	}

	/* Release associated resources */
	path_destroy(&filp->f_path);

	/* Release inode reference */
	if (filp->f_inode)
		inode_put(filp->f_inode);

	/* Log the file close if debugging enabled */
	if (fd >= 0) {
		/* debug_file_close(owner, fd); */
	}

	/* Free the file structure itself */
	kfree(filp);

	return error;
}

/**
 * __file_open - Common file opening logic
 * @dentry: Dentry of file to open
 * @mnt: Mount point
 * @flags: Open flags
 * @mode: Creation mode
 *
 * Internal helper function for all file_open variants.
 */
static struct file* __file_open(struct dentry* dentry, struct vfsmount* mnt, int flags, fmode_t mode) {
	struct file* file;
	struct inode* inode;
	int error = 0;
	fmode_t file_mode;

	/* Validate dentry has an inode */
	inode = dentry->d_inode;
	if (!inode)
		return ERR_PTR(-ENOENT);

	/* Check directory write permissions */
	if (S_ISDIR(inode->i_mode) && (flags & O_ACCMODE) != O_RDONLY)
		return ERR_PTR(-EISDIR);

	/* Convert flags to fmode_t */
	file_mode = FMODE_READ;
	if ((flags & O_ACCMODE) != O_RDONLY)
		file_mode |= FMODE_WRITE;

	/* Allocate and initialize file structure using our new function */
	file = __file_alloc(dentry, file_mode);
	if (IS_ERR(file))
		return file;

	/*initialization */
	atomic_set(&file->f_refcount, 1);
	file->f_path.dentry = get_dentry(dentry);
	file->f_path.mnt = get_mount(mnt);
	/* Set inode if available */
	assert(dentry->d_inode); // 我们的设计确保所有文件系统都有inode和对应的操作
	file->f_inode = inode_get(dentry->d_inode);
	file->f_operations = dentry->d_inode->i_fop;
	file->f_mode = file_mode;
	file->f_flags = flags;

	file->f_private = NULL;

	/* 设置预读参数 */
	file->f_read_ahead.start = 0;
	file->f_read_ahead.size = READ_AHEAD_DEFAULT;
	file->f_read_ahead.async_size = 0;
	file->f_read_ahead.ra_pages = READ_AHEAD_MAX;
	file->f_read_ahead.mmap_miss = 0;
	file->f_read_ahead.prev_pos = 0;

	/* Set position */
	if (flags & O_APPEND)
		file->f_pos = inode->i_size;

	/* Call open method if available */
	if (file->f_operations && file->f_operations->open) {
		error = file->f_operations->open(inode, file);
		if (error) {
			/* Clean up on error */
			path_destroy(&file->f_path);
			inode_put(file->f_inode);
			kfree(file);
			return ERR_PTR(error);
		}
	}

	return file;
}

/**
 * __file_alloc - Allocatesa new file structure
 *
 * This function allocates a new file structure.
 * The caller is responsible for initializing the file structure.
 *
 * Returns a pointer to the allocated file structure, or an ERR_PTR on error.
 */
static struct file* __file_alloc(struct dentry* dentry, fmode_t mode) {
	struct file* file;

	if (!dentry)
		return ERR_PTR(-EINVAL);

	/* Allocate file structure */
	file = kmalloc(sizeof(struct file));
	if (!file)
		return ERR_PTR(-ENOMEM);

	/* Initialize to zeros */
	memset(file, 0, sizeof(struct file));
	spinlock_init(&file->f_lock);

	return file;
}

/**
 * file_denyWrite
 * @file: File pointer
 */
int file_denyWrite(struct file* file) {
	if (!file) {
		return -EINVAL;
	}
	if (!file->f_inode || atomic_read(&file->f_refcount) <= 0)
		return -EBADF;

	/* Deny write access by setting the mode to read-only */
	spinlock_lock(&file->f_lock);
	file->f_mode &= ~FMODE_WRITE;
	spinlock_unlock(&file->f_lock);
	return 0;
}

/**
 * file_allowWrite
 * @file: File pointer
 *
 * Allows write access to the file by setting the mode to read-write.
 * Returns 0 on success or negative error code on failure.
 */
int file_allowWrite(struct file* file) {
	if (!file) {
		return -EINVAL;
	}
	if (!file->f_inode || atomic_read(&file->f_refcount) <= 0)
		return -EBADF;

	/* Allow write access by setting the mode to read-write */
	spinlock_lock(&file->f_lock);
	file->f_mode |= FMODE_WRITE;
	spinlock_unlock(&file->f_lock);
	return 0;
}

inline bool file_readable(struct file* file) {
	if (!file || !file->f_inode || atomic_read(&file->f_refcount) <= 0)
		return false;
	return (file->f_mode & FMODE_READ) != 0;
}

inline bool file_writable(struct file* file) {
	if (!file || !file->f_inode || atomic_read(&file->f_refcount) <= 0)
		return false;
	return (file->f_mode & FMODE_WRITE) != 0;
}

/**
 * file_put - Decrease reference count on a file
 * @file: File pointer
 * @owner: Task that owns the file descriptor (can be NULL)
 *
 * Decrements the reference count on a file and releases
 * it if the count reaches zero, passing owner information
 * for proper cleanup.
 */
void file_put(struct file* file) {
	if (!file)
		return;

	if (atomic_dec_and_test(&file->f_refcount)) {
		/* Reference count reached zero, close the file */
		__file_free(file);
	} else {
		panic("file_put: f_refcount is already 0\n");
		/* This should not happen, as we expect the reference count
		 * to be managed properly by the caller */
	}
}


ssize_t file_read(struct file* file, char* buf, size_t count, loff_t* pos) {
    struct kiocb kiocb;
    ssize_t ret;
    
    if (!file)
        return -EINVAL;
    
    /* Initialize the kiocb with the file */
    init_kiocb(&kiocb, file);
    
    /* If a specific position was provided, use it */
    if (pos && *pos != file->f_pos) {
        kiocb_set_pos(&kiocb, *pos);
        kiocb.ki_flags |= KIOCB_NOUPDATE_POS; /* Don't update file position */
    }
    
    /* Perform the read operation */
    ret = kiocb_read(&kiocb, buf, count);
    
    /* Update the position pointer if provided */
    if (pos)
        *pos = kiocb.ki_pos;
    
    return ret;
}



ssize_t file_write(struct file* file, const char* buf, size_t count, loff_t* pos) {
    struct kiocb kiocb;
    ssize_t ret;
    
    if (!file)
        return -EINVAL;
    
    /* Initialize the kiocb with the file */
    init_kiocb(&kiocb, file);
    
    /* If a specific position was provided, use it */
    if (pos && *pos != file->f_pos) {
        kiocb_set_pos(&kiocb, *pos);
        kiocb.ki_flags |= KIOCB_NOUPDATE_POS; /* Don't update file position */
    }
    
    /* Perform the write operation */
    ret = kiocb_write(&kiocb, buf, count);
    
    /* Update the position pointer if provided */
    if (pos)
        *pos = kiocb.ki_pos;
    
    return ret;
}


/**
 * file_llseek - Repositions the file offset 
 * @file: File to seek within
 * @offset: File offset to seek to
 * @whence: Type of seek (SEEK_SET, SEEK_CUR, SEEK_END)
 *
 * This function updates a file's position according to the whence parameter:
 * - SEEK_SET: Sets position to offset from start of file
 * - SEEK_CUR: Sets position to current position plus offset
 * - SEEK_END: Sets position to end of file plus offset
 *
 * Returns the new file position on success, or a negative error code on failure.
 */
loff_t file_llseek(struct file* file, loff_t offset, int whence) {
    //struct kiocb kiocb;
    loff_t new_pos;
    
    /* Basic validation */
    if (!file || !file->f_inode)
        return -EINVAL;
    
    /* Check if file is seekable */
    if (!(file->f_mode & FMODE_LSEEK))
        return -ESPIPE;
    
    /* Initialize kiocb */
    //init_kiocb(&kiocb, file);
	// The only potential usage would be if the file system's specific llseek operation needed it
    
    /* Call file-specific llseek operation if available */
    if (file->f_operations && file->f_operations->llseek)
        return file->f_operations->llseek(file, offset, whence);
    
    /* Generic implementation for simple files */
    spinlock_lock(&file->f_lock);
    
    switch (whence) {
    case SEEK_SET:
        new_pos = offset;
        break;
    case SEEK_CUR:
        new_pos = file->f_pos + offset;
        break;
    case SEEK_END:
        new_pos = file->f_inode->i_size + offset;
        break;
    default:
        spinlock_unlock(&file->f_lock);
        return -EINVAL;
    }
    
    /* Validate the resulting position */
    if (new_pos < 0) {
        spinlock_unlock(&file->f_lock);
        return -EINVAL;
    }
    
    /* Update file position */
    file->f_pos = new_pos;
    spinlock_unlock(&file->f_lock);
    
    return new_pos;
}

/**
 * file_sync - Synchronize a file's in-core state with storage
 * @file: File to synchronize
 * @datasync: Only flush user data if non-zero (not metadata)
 *
 * This function flushes all dirty pages associated with the file to disk
 * and updates the on-disk file metadata. If datasync is non-zero, only
 * the file data is synced (not metadata).
 *
 * Returns 0 on success or a negative error code on failure.
 */
int file_sync(struct file* file, int datasync) {
    struct inode* inode;
    int ret = 0;

    /* Basic validation */
    if (!file)
        return -EINVAL;

    inode = file->f_inode;
    if (!inode)
        return -EINVAL;

    /* Call file-specific fsync operation if available */
    if (file->f_operations && file->f_operations->fsync) {
        /* Use the file's fsync operation with full file range */
        //struct kiocb kiocb;
        //init_kiocb(&kiocb, file);	
		// currently unused
        return file->f_operations->fsync(file, 0, INT64_MAX, datasync);
    }

    /* Generic implementation - sync the inode */
    if (datasync)
        ret = inode_sync(inode, 1); /* Only sync data blocks */
    else
        ret = inode_sync_metadata(inode, 1); /* Sync data and metadata */

    return ret;
}

/**
 * file_readv - Read data from a file into multiple buffers
 * @file: File to read from
 * @vec: Array of io_vector structures
 * @vlen: Number of io_vector structures
 * @pos: Position in file to read from (updated on return)
 */
ssize_t file_readv(struct file* file, const struct io_vector* vec, unsigned long vlen, loff_t* pos) {
    struct kiocb kiocb;
    struct io_vector_iterator iter;
    ssize_t ret;

    if (!file || !vec || !pos)
        return -EINVAL;

    /* Initialize kiocb */
    init_kiocb(&kiocb, file);
    kiocb.ki_pos = *pos;
    
    /* If we're using a position different from the file's current position */
    if (*pos != file->f_pos)
        kiocb.ki_flags |= KIOCB_NOUPDATE_POS;

    /* Setup the io_vector iterator */
    ret = setup_io_vector_iterator(&iter, vec, vlen);
    if (ret < 0)
        return ret;

    /* Use optimized read_iter if available */
    if (likely(file->f_operations && file->f_operations->read_iter)) {
        ret = file->f_operations->read_iter(&kiocb, &iter);
    } else if (file->f_operations && file->f_operations->read) {
        /* Fall back to sequential reads */
        ret = 0;
        for (unsigned long i = 0; i < vlen; i++) {
            ssize_t bytes;
            bytes = file->f_operations->read(file, vec[i].iov_base, vec[i].iov_len, &kiocb.ki_pos);
            if (bytes < 0) {
                if (ret == 0)
                    ret = bytes;
                break;
            }
            ret += bytes;
            if (bytes < vec[i].iov_len)
                break; /* Short read */
        }
    } else {
        ret = -EINVAL;
    }

    /* Update the position for the caller */
    if (ret > 0)
        *pos = kiocb.ki_pos;

    return ret;
}



/**
 * file_writev - Write data from multiple buffers to a file
 * @file: File to write to
 * @vec: Array of io_vector structures
 * @vlen: Number of io_vector structures
 * @pos: Position in file to write to (updated on return)
 */
ssize_t file_writev(struct file* file, const struct io_vector* vec, unsigned long vlen, loff_t* pos) {
    struct kiocb kiocb;
    struct io_vector_iterator iter;
    ssize_t ret;

    if (!file || !vec || !pos)
        return -EINVAL;

    /* Initialize kiocb */
    init_kiocb(&kiocb, file);
    kiocb.ki_pos = *pos;
    
    /* If we're using a position different from the file's current position */
    if (*pos != file->f_pos)
        kiocb.ki_flags |= KIOCB_NOUPDATE_POS;

    /* Setup the io_vector iterator */
    ret = setup_io_vector_iterator(&iter, vec, vlen);
    if (ret < 0)
        return ret;

    /* Handle append mode */
    if (file->f_flags & O_APPEND) {
        kiocb.ki_flags |= KIOCB_APPEND;
        kiocb.ki_pos = file->f_inode->i_size;
    }

    /* Use optimized write_iter if available */
    if (file->f_operations && file->f_operations->write_iter) {
        ret = file->f_operations->write_iter(&kiocb, &iter);
    } else if (file->f_operations && file->f_operations->write) {
        /* Fall back to sequential writes */
        ret = 0;
        for (unsigned long i = 0; i < vlen; i++) {
            ssize_t bytes;
            bytes = file->f_operations->write(file, vec[i].iov_base, vec[i].iov_len, &kiocb.ki_pos);
            if (bytes < 0) {
                if (ret == 0)
                    ret = bytes;
                break;
            }
            ret += bytes;
            if (bytes < vec[i].iov_len)
                break; /* Short write */
        }
    } else {
        ret = -EINVAL;
    }

    /* Update the position for the caller */
    if (ret > 0) {
        *pos = kiocb.ki_pos;
        
        /* Mark the inode as dirty if write was successful */
        inode_setDirty(file->f_inode);
    }

    return ret;
}