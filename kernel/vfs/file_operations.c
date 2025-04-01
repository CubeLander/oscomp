#include <kernel/types.h>
#include <kernel/vfs.h>
#include <kernel/mm/kmalloc.h>
#include <kernel/device/block_device.h>
#include <kernel/device/char_device.h>
#include <kernel/sprint.h>
#include <kernel/types.h>



static int32 bdFile_open(struct inode* inode, struct file* file)
{
    struct block_device* bdev;
    int32 ret = 0;
    
    if (!inode || !file)
        return -EINVAL;
        
    /* Get block device from inode */
    bdev = blockdevice_lookup(inode->i_rdev);
    if (!bdev)
        return -ENXIO;
        
    /* Open the device */
    ret = blockdevice_open(bdev, file->f_flags);
    if (ret) {
        /* Failed to open - release reference */
        blockdevice_unref(bdev);
        return ret;
    }
    
    /* Store in file f_private */
    file->f_private = bdev;
    return 0;
}



static int32 bdFile_release(struct inode* inode, struct file* file)
{
    struct block_device* bdev = file->f_private;
    
    if (!bdev)
        return 0;
        
    /* Close the device */
    blockdevice_close(bdev);
    
    /* Release reference obtained in open */
    blockdevice_unref(bdev);
    file->f_private = NULL;
    
    return 0;
}



static loff_t bdFile_llseek(struct file* file, loff_t offset, int32 whence)
{
    struct block_device* bdev = file->f_private;
    loff_t size, max_size, new_offset;
    
    if (!bdev)
        return -EINVAL;
        
    /* Get device size */
    size = bdev->bd_block_size;
    max_size = size;
    
    /* Handle different seek types */
    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
        case SEEK_CUR:
            new_offset = file->f_pos + offset;
            break;
        case SEEK_END:
            new_offset = size + offset;
            break;
        default:
            return -EINVAL;
    }
    
    /* Check bounds */
    if (new_offset < 0 || new_offset > max_size)
        return -EINVAL;
        
    /* Update position */
    file->f_pos = new_offset;
    return new_offset;
}

static ssize_t bdFile_read(struct file* file, char* buf, size_t count, loff_t* pos)
{
    struct block_device* bdev = file->f_private;
    uint32 block_size, start_block, end_block;
    ssize_t total_read = 0;
    char* buffer;
    int32 ret;
    
    if (!bdev || !buf || !pos)
        return -EINVAL;
        
    /* Block device must have read operation */
    if (!bdev->bd_ops || !bdev->bd_ops->read_blocks)
        return -EINVAL;
        
    /* Check EOF */
    if (*pos >= bdev->bd_block_size)
        return 0;
        
    /* Limit count to device size */
    if (*pos + count > bdev->bd_block_size)
        count = bdev->bd_block_size - *pos;
        
    /* Calculate block information */
    block_size = bdev->bd_block_size;
    start_block = *pos / block_size;
    end_block = (*pos + count + block_size - 1) / block_size;
    
    /* Allocate temporary buffer if needed for unaligned reads */
    if ((*pos % block_size) || (count % block_size)) {
        buffer = kmalloc((end_block - start_block) * block_size);
        if (!buffer)
            return -ENOMEM;
            
        /* Read blocks into buffer */
        ret = bdev->bd_ops->read_blocks(bdev, buffer, start_block, end_block - start_block);
        if (ret < 0) {
            kfree(buffer);
            return ret;
        }
        
        /* Copy data to user buffer */
        memcpy(buf, buffer + (*pos % block_size), count);
        kfree(buffer);
    } else {
        /* Aligned read - read directly into user buffer */
        ret = bdev->bd_ops->read_blocks(bdev, buf, start_block, (count / block_size));
        if (ret < 0)
            return ret;
    }
    
    /* Update position */
    *pos += count;
    return count;
}

static ssize_t bdFile_write(struct file* file, const char* buf, size_t count, loff_t* pos)
{
    struct block_device* bdev = file->f_private;
    uint32 block_size, start_block, end_block;
    ssize_t total_written = 0;
    char* buffer;
    int32 ret;
    
    if (!bdev || !buf || !pos)
        return -EINVAL;
        
    /* Block device must have write operation */
    if (!bdev->bd_ops || !bdev->bd_ops->write_blocks)
        return -EINVAL;
        
    /* Check EOF and read-only */
    if (*pos >= bdev->bd_block_size)
        return 0;
    if (file->f_flags & O_RDONLY)
        return -EBADF;
        
    /* Limit count to device size */
    if (*pos + count > bdev->bd_block_size)
        count = bdev->bd_block_size - *pos;
        
    /* Calculate block information */
    block_size = bdev->bd_block_size;
    start_block = *pos / block_size;
    end_block = (*pos + count + block_size - 1) / block_size;
    
    /* Handle unaligned writes */
    if ((*pos % block_size) || (count % block_size)) {
        buffer = kmalloc((end_block - start_block) * block_size);
        if (!buffer)
            return -ENOMEM;
            
        /* Read existing blocks for partial updates */
        ret = bdev->bd_ops->read_blocks(bdev, buffer, start_block, end_block - start_block);
        if (ret < 0) {
            kfree(buffer);
            return ret;
        }
        
        /* Copy user data to buffer */
        memcpy(buffer + (*pos % block_size), buf, count);
        
        /* Write blocks back */
        ret = bdev->bd_ops->write_blocks(bdev, buffer, start_block, end_block - start_block);
        kfree(buffer);
        
        if (ret < 0)
            return ret;
    } else {
        /* Aligned write - write directly from user buffer */
        ret = bdev->bd_ops->write_blocks(bdev, buf, start_block, (count / block_size));
        if (ret < 0)
            return ret;
    }
    
    /* Update position */
    *pos += count;
    return count;
}

static int64 bdFile_ioctl(struct file* file, uint32 cmd, uint64 arg)
{
    struct block_device* bdev = file->f_private;
    int64 ret = -ENOTTY;
    
    if (!bdev)
        return -EINVAL;
        
    /* Handle standard block device ioctls */
    switch (cmd) {
        case BLKGETSIZE:   /* Get device size in sectors */
            return bdev->bd_block_size / 512;
            
        case BLKGETSIZE64: /* Get device size in bytes */
            return bdev->bd_block_size;
            
        case BLKSSZGET:    /* Get block size */
            return bdev->bd_block_size;
            
        /* Add more standard ioctls as needed */
    }
    
    /* Call device-specific ioctl if available */
    if (bdev->bd_ops && bdev->bd_ops->ioctl)
        ret = bdev->bd_ops->ioctl(bdev, cmd, arg);
        
    return ret;
}

/* 
 * Character Device File Operations 
 */

static int32 chFile_open(struct inode* inode, struct file* file)
{
    struct char_device* cdev;
    int32 ret = 0;
    
    if (!inode)
        return -EINVAL;
        
    /* Get character device from inode */
    cdev = cdev_get(inode->i_rdev);
    if (!cdev)
        return -ENXIO;
        
    /* Store in file f_private */
    file->f_private = cdev;
    
    /* Call device open method if available */
    if (cdev->ops && cdev->ops->open)
        ret = cdev->ops->open(cdev, file);
        
    if (ret)
        file->f_private = NULL;
        
    return ret;
}

static int32 chFile_release(struct inode* inode, struct file* file)
{
    struct char_device* cdev = file->f_private;
    int32 ret = 0;
    
    if (!cdev)
        return 0;
        
    /* Call device close method if available */
    if (cdev->ops && cdev->ops->release)
        ret = cdev->ops->release(cdev, file);
        
    /* Release reference to device */
    cdev_put(cdev);
    file->f_private = NULL;
    
    return ret;
}

static ssize_t chFile_read(struct file* file, char* buf, size_t count, loff_t* pos)
{
    struct char_device* cdev = file->f_private;
    
    if (!cdev || !buf)
        return -EINVAL;
        
    /* Character device must have read operation */
    if (!cdev->ops || !cdev->ops->read)
        return -EINVAL;
        
    /* Call device-specific read */
    return cdev->ops->read(cdev, file, buf, count, pos);
}

static ssize_t chFile_write(struct file* file, const char* buf, size_t count, loff_t* pos)
{
    struct char_device* cdev = file->f_private;
    
    if (!cdev || !buf)
        return -EINVAL;
        
    /* Character device must have write operation */
    if (!cdev->ops || !cdev->ops->write)
        return -EINVAL;
        
    /* Call device-specific write */
    return cdev->ops->write(cdev, file, buf, count, pos);
}

static loff_t chFile_llseek(struct file* file, loff_t offset, int32 whence)
{
    struct char_device* cdev = file->f_private;
    
    if (!cdev)
        return -EINVAL;
        
    /* Use device-specific seek if available */
    if (cdev->ops && cdev->ops->llseek)
        return cdev->ops->llseek(cdev, file, offset, whence);
        
    /* By default, many character devices don't support seeking */
    return -ESPIPE;
}

static int64 chFile_ioctl(struct file* file, uint32 cmd, uint64 arg)
{
    struct char_device* cdev = file->f_private;
    
    if (!cdev)
        return -EINVAL;
        
    /* Call device-specific ioctl if available */
    if (cdev->ops && cdev->ops->ioctl)
        return cdev->ops->ioctl(cdev, file, cmd, arg);
        
    return -ENOTTY;
}

/*
 * FIFO (Named Pipe) File Operations
 */

static int32 fifoFile_open(struct inode* inode, struct file* file)
{
    /* Implementation depends on pipe buffer structure */
    sprint("FIFO open: not fully implemented\n");
    return 0;
}

static int32 fifoFile_release(struct inode* inode, struct file* file)
{
    /* Release pipe resources */
    sprint("FIFO release: not fully implemented\n");
    return 0;
}

static ssize_t fifoFile_read(struct file* file, char* buf, size_t count, loff_t* pos)
{
    /* Read from pipe buffer with blocking semantics */
    sprint("FIFO read: not fully implemented\n");
    return -ENOSYS;
}

static ssize_t fifoFile_write(struct file* file, const char* buf, size_t count, loff_t* pos)
{
    /* Write to pipe buffer with blocking semantics */
    sprint("FIFO write: not fully implemented\n");
    return -ENOSYS;
}

static int64 fifoFile_ioctl(struct file* file, uint32 cmd, uint64 arg)
{
    /* Handle pipe-specific ioctls */
    return -ENOTTY;
}

/*
 * Socket File Operations
 */

static int32 socketFile_open(struct inode* inode, struct file* file)
{
    /* Socket opening is special and usually done via socket() syscall */
    sprint("Socket open: not fully implemented\n");
    return -ENXIO;
}

static int32 socketFile_release(struct inode* inode, struct file* file)
{
    /* Close the socket */
    sprint("Socket release: not fully implemented\n");
    return 0;
}

static ssize_t socketFile_read(struct file* file, char* buf, size_t count, loff_t* pos)
{
    /* Socket read is typically done via recv() */
    sprint("Socket read: not fully implemented\n");
    return -ENOSYS;
}

static ssize_t socketFile_write(struct file* file, const char* buf, size_t count, loff_t* pos)
{
    /* Socket write is typically done via send() */
    sprint("Socket write: not fully implemented\n");
    return -ENOSYS;
}

static int64 socketFile_ioctl(struct file* file, uint32 cmd, uint64 arg)
{
    /* Socket ioctls for connection management */
    return -ENOTTY;
}

/* 
 * Define file operation tables for each device type
 */

const struct file_operations bdFile_operations = {
    .open = bdFile_open,
    .release = bdFile_release,
    .read = bdFile_read,
    .write = bdFile_write,
    .llseek = bdFile_llseek,
    .unlocked_ioctl = bdFile_ioctl,
};

const struct file_operations chFile_operations = {
    .open = chFile_open,
    .release = chFile_release,
    .read = chFile_read,
    .write = chFile_write,
    .llseek = chFile_llseek,
    .unlocked_ioctl = chFile_ioctl,
};

const struct file_operations fifoFile_operations = {
    .open = fifoFile_open,
    .release = fifoFile_release,
    .read = fifoFile_read,
    .write = fifoFile_write,
    .unlocked_ioctl = fifoFile_ioctl,
};

const struct file_operations socketFile_operations = {
    .open = socketFile_open,
    .release = socketFile_release,
    .read = socketFile_read,
    .write = socketFile_write,
    .unlocked_ioctl = socketFile_ioctl,
};