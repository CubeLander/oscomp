#ifndef _KIOCB_H
#define _KIOCB_H

#include <kernel/types.h>
#include <kernel/fs/vfs/file.h>

/**
 * struct kiocb - Kernel I/O control block
 * @ki_filp: File for the I/O
 * @ki_pos: Current file position
 * @ki_flags: Operation flags
 *
 * This is a simplified version of the Kernel I/O Control Block,
 * focused on synchronous operations only. It provides a clean
 * abstraction for VFS I/O operations.
 */
struct kiocb {
    struct file *ki_filp;      /* File pointer */
    loff_t ki_pos;             /* File position */
    int ki_flags;              /* Operation flags */
};

/* Initialization and position */
void init_kiocb(struct kiocb *kiocb, struct file *file);
void kiocb_set_pos(struct kiocb *kiocb, loff_t pos);

/* I/O operations */
ssize_t kiocb_read(struct kiocb *kiocb, char *buf, size_t len);
ssize_t kiocb_write(struct kiocb *kiocb, const char *buf, size_t len);

/* Flag operations */
int kiocb_is_direct(const struct kiocb *kiocb);
int kiocb_is_append(const struct kiocb *kiocb);
int kiocb_is_nonblock(const struct kiocb *kiocb);

/* KIOCB operation flags */
#define KIOCB_APPEND        (1 << 0)    /* File opened in append mode */
#define KIOCB_DIRECT        (1 << 1)    /* Direct I/O - bypass page cache */
#define KIOCB_NONBLOCK      (1 << 2)    /* Non-blocking mode */
#define KIOCB_NOUPDATE_POS  (1 << 3)    /* Don't update file position */

#endif /* _KIOCB_H */