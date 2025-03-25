#include <kernel/fs/vfs/kiocb.h>
#include <kernel/fs/vfs/file.h>
#include <util/spinlock.h>

/**
 * init_kiocb - Initialize a kernel I/O control block
 * @kiocb: The kiocb to initialize
 * @file: The file associated with this I/O operation
 *
 * Sets up the kiocb structure with the given file, initializing
 * all fields to appropriate default values.
 * kiocb一般不需要分配内存，一般都直接声明在栈上。
 */
void init_kiocb(struct kiocb *kiocb, struct file *file)
{
    if (!kiocb || !file)
        return;
        
    kiocb->ki_filp = file;
    kiocb->ki_pos = file->f_pos;
    kiocb->ki_flags = 0;
    
    /* Set flags based on file mode */
    if (file->f_flags & O_APPEND)
        kiocb->ki_flags |= KIOCB_APPEND;
    if (file->f_flags & O_DIRECT)
        kiocb->ki_flags |= KIOCB_DIRECT;
    if (file->f_flags & O_NONBLOCK)
        kiocb->ki_flags |= KIOCB_NONBLOCK;
}

/**
 * kiocb_set_pos - Set the position for a kiocb
 * @kiocb: The kiocb to modify
 * @pos: The new position
 *
 * Sets the current position for I/O operations with this kiocb.
 */
void kiocb_set_pos(struct kiocb *kiocb, loff_t pos)
{
    if (kiocb)
        kiocb->ki_pos = pos;
}

/**
 * kiocb_read - Read data using a kiocb
 * @kiocb: The kiocb describing the I/O
 * @buf: Buffer to read into
 * @len: Maximum number of bytes to read
 *
 * Performs a read operation starting at the kiocb's position.
 * Returns the number of bytes read or a negative error code.
 */
ssize_t kiocb_read(struct kiocb *kiocb, char *buf, size_t len)
{
    struct file *file;
    ssize_t ret;
    
    if (!kiocb || !buf || !len)
        return -EINVAL;
    
    file = kiocb->ki_filp;
    if (!file)
        return -EBADF;
    
    /* Check if file is readable */
    if (!file_readable(file))
        return -EBADF;
    
    /* If we have a file-specific read method, use it */
    if (file->f_operations && file->f_operations->read) {
        ret = file->f_operations->read(file, buf, len, &kiocb->ki_pos);
    } else {
        ret = -EINVAL;
    }
    
    /* Update file position if read was successful */
    if (ret > 0 && !(kiocb->ki_flags & KIOCB_NOUPDATE_POS))
        file->f_pos = kiocb->ki_pos;
    
    return ret;
}

/**
 * kiocb_write - Write data using a kiocb
 * @kiocb: The kiocb describing the I/O
 * @buf: Buffer to write from
 * @len: Number of bytes to write
 *
 * Performs a write operation starting at the kiocb's position.
 * For append mode, the position is set to the end of the file.
 * Returns the number of bytes written or a negative error code.
 */
ssize_t kiocb_write(struct kiocb *kiocb, const char *buf, size_t len)
{
    struct file *file;
    ssize_t ret;
    
    if (!kiocb || !buf || !len)
        return -EINVAL;
    
    file = kiocb->ki_filp;
    if (!file)
        return -EBADF;
    
    /* Check if file is writable */
    if (!file_writable(file))
        return -EBADF;
    
    /* Handle append mode */
    if (kiocb->ki_flags & KIOCB_APPEND) {
        spinlock_lock(&file->f_lock);
        kiocb->ki_pos = file->f_inode->i_size;
        spinlock_unlock(&file->f_lock);
    }
    
    /* If we have a file-specific write method, use it */
    if (file->f_operations && file->f_operations->write) {
        ret = file->f_operations->write(file, buf, len, &kiocb->ki_pos);
    } else {
        ret = -EINVAL;
    }
    
    /* Update file position if write was successful */
    if (ret > 0 && !(kiocb->ki_flags & KIOCB_NOUPDATE_POS))
        file->f_pos = kiocb->ki_pos;
    
    /* Mark inode as dirty if write was successful */
    if (ret > 0 && file->f_inode)
        inode_setDirty(file->f_inode);
    
    return ret;
}

/**
 * kiocb_is_direct - Check if kiocb is for direct I/O
 * @kiocb: The kiocb to check
 *
 * Returns true if this is a direct I/O operation.
 */
int kiocb_is_direct(const struct kiocb *kiocb)
{
    return kiocb && (kiocb->ki_flags & KIOCB_DIRECT);
}

/**
 * kiocb_is_append - Check if kiocb is for append mode
 * @kiocb: The kiocb to check
 *
 * Returns true if this is an append operation.
 */
int kiocb_is_append(const struct kiocb *kiocb)
{
    return kiocb && (kiocb->ki_flags & KIOCB_APPEND);
}

/**
 * kiocb_is_nonblock - Check if kiocb is for non-blocking I/O
 * @kiocb: The kiocb to check
 *
 * Returns true if this is a non-blocking I/O operation.
 */
int kiocb_is_nonblock(const struct kiocb *kiocb)
{
    return kiocb && (kiocb->ki_flags & KIOCB_NONBLOCK);
}