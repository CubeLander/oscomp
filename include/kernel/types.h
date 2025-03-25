#ifndef _TYPES_H_
#define _TYPES_H_
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* 关于指针和数据类型的说明
 * uint64: 切实的数据值，不可能用作内存/内存运算
 *
 * 由于我们特殊的内核空间布局结构（物理内存总是会在内核页表中留一份直接映射）
 * 所以说paddr_t可以代表整个的内核物理地址空间+内核虚拟地址空间
 * paddr_t: 内核物理地址和内核虚拟地址（参与运算）
 * vaddr_t：用户虚拟地址（参与运算）
 *
 * kptr_t: 内核空间指针
 * uptr_t: 用户空间指针
 *
 *
 * 所以说，内核的文档中的概念，需要严格区分指针和地址。
 */

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef unsigned long long uint64;

typedef signed char int8;
typedef signed short int16;
typedef signed int int32;
typedef signed long long int64;

#define INT64_MAX 0x7fffffffffffffffLL
#define INT64_MIN 0x8000000000000000LL

#define INT32_MAX 0x7fffffff
#define INT32_MIN 0x80000000

#define UINT32_MAX 0xffffffff
#define UINT32_MIN 0x0

#define UINT64_MAX 0xffffffffffffffffUL
#define UINT64_MIN 0x0UL

#ifndef bool
typedef int bool;
#define true 1
#define false 0
#endif

#define ERR_PTR(err) ((void *)((intptr_t)(err)))
#define PTR_ERR(ptr) ((intptr_t)(ptr))          // 从“错误指针”提取出原始错误码
#define IS_ERR(ptr)  ((uintptr_t)(ptr) > (uintptr_t)-4096)  // 判断一个指针是不是“错误指针”

// 方便的宏定义
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ROUNDUP(a, b) ((((a) - 1) / (b) + 1) * (b))
#define ROUNDDOWN(a, b) ((a) / (b) * (b))

#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define __user
// depreciated 用来标记用户的虚拟内存地址
typedef uint64 uaddr;

// Physical/virtual addresses (64-bit on your architecture)
typedef uint64 paddr_t; // Physical address
typedef uint64 vaddr_t; // Virtual address
typedef uint64 time64_t; // 64-bit time value

// User/kernel space pointers (for clarity in interfaces)
typedef void* kptr_t;	     // Kernel pointer
typedef void* __user uptr_t; // User space pointer

// fs相关
#define MOUNT_DEFAULT 0
#define MOUNT_AS_ROOT 1

#define MASK_FILEMODE 0x003

/* Basic time types and structures */

/* 
 * Time value with nanosecond resolution
 * Represents time as seconds and nanoseconds since the Epoch (1970-01-01 00:00:00 +0000)
 */
struct timespec {
    time_t  tv_sec;     /* seconds */
    long    tv_nsec;    /* nanoseconds */
};

// timespec64 structure if not already defined
struct timespec64 {
    time64_t tv_sec;        /* seconds */
    long     tv_nsec;       /* nanoseconds */
};

/* 
 * Time value with microsecond resolution
 * Used in many system calls that pre-date nanosecond precision
 */
struct timeval {
    time_t      tv_sec;     /* seconds */
    suseconds_t tv_usec;    /* microseconds */
};

/*
 * POSIX-compatible time zone structure
 */
struct timezone {
    int tz_minuteswest;     /* minutes west of Greenwich */
    int tz_dsttime;         /* type of DST correction */
};

/*
 * Timer interval specification
 */
struct itimerspec {
    struct timespec it_interval;    /* timer interval */
    struct timespec it_value;       /* initial expiration */
};

//vma
/* Virtual memory fault return type */
typedef unsigned int vm_fault_t;
/* VM fault status codes */
#define VM_FAULT_OOM        0x000001
#define VM_FAULT_SIGBUS     0x000002
#define VM_FAULT_MAJOR      0x000004
#define VM_FAULT_WRITE      0x000008  /* Write access, not read */
#define VM_FAULT_HWPOISON   0x000010
#define VM_FAULT_RETRY      0x000020
#define VM_FAULT_NOPAGE     0x000040  /* No page was found */
#define VM_FAULT_LOCKED     0x000080
#define VM_FAULT_DONE_COW   0x000100
#define VM_FAULT_NEEDDSYNC  0x000200

// file system type
/*
 * File type and permission bits
 */

#define MAX_FILE_NAME_LEN 256
typedef uint64 loff_t;

typedef uint64_t sector_t; /* 64-bit sector number */
/* File permissions and type mode */
typedef unsigned int __poll_t;


struct dir {
	char name[MAX_FILE_NAME_LEN];
	int inum;
};

struct istat {
	int st_inum;
	int st_size;
	int st_type;
	int st_nlinks;
	int st_blocks;
};

/* Mount flags */
#define MS_RDONLY 1        // Mount read-only
#define MS_NOSUID 2        // Ignore suid and sgid bits
#define MS_NODEV 4         // Disallow access to device special files
#define MS_NOEXEC 8        // Disallow program execution
#define MS_SYNCHRONOUS 16  // Writes are synced at once
#define MS_REMOUNT 32      // Remount with different flags
#define MS_MANDLOCK 64     // Allow mandatory locks on this FS
#define MS_DIRSYNC 128     // Directory modifications are synchronous
#define MS_NOATIME 1024    // Do not update access times
#define MS_NODIRATIME 2048 // Do not update directory access times

/*
 * File mode fmode_t flags
 */
typedef uint32 fmode_t;
/* Access modes */
#define FMODE_READ (1U << 0)  /* File is open for reading */
#define FMODE_WRITE (1U << 1) /* File is open for writing */
#define FMODE_EXEC (1U << 5)  /* File is executable */

/* Seeking flags */
#define FMODE_LSEEK (1U << 2)  /* File is seekable */
#define FMODE_PREAD (1U << 3)  /* File supports pread */
#define FMODE_PWRITE (1U << 4) /* File supports pwrite */

/* Special access flags */
#define FMODE_ATOMIC_POS (1U << 12) /* File needs atomic access to position */
#define FMODE_RANDOM (1U << 13)     /* File will be accessed randomly */
#define FMODE_PATH (1U << 14)       /* O_PATH flag - minimal file access */
#define FMODE_STREAM (1U << 16)     /* File is stream-like */

/* Permission indicators */
#define FMODE_WRITER (1U << 17)    /* Has write access to underlying fs */
#define FMODE_CAN_READ (1U << 18)  /* Has read methods */
#define FMODE_CAN_WRITE (1U << 19) /* Has write methods */

/* State flags */
#define FMODE_OPENED (1U << 20)  /* File has been opened */
#define FMODE_CREATED (1U << 21) /* File was created */

/* Optimization flags */
#define FMODE_NOWAIT (1U << 22)      /* Return -EAGAIN if I/O would block */
#define FMODE_CAN_ODIRECT (1U << 24) /* Supports direct I/O */
#define FMODE_BUF_RASYNC (1U << 28)  /* Supports async buffered reads */
#define FMODE_BUF_WASYNC (1U << 29)  /* Supports async buffered writes */



/* File permission and type bits */
typedef unsigned short umode_t;


/*fstype.h*/
/* Filesystem type flags */
#define FS_REQUIRES_DEV 0x01       /* Filesystem requires a block device */
#define FS_BINARY_MOUNTDATA 0x02   /* Binary mount data instead of text */
#define FS_HAS_SUBTYPE 0x04        /* Subtype field valid */
#define FS_USERNS_MOUNT 0x08       /* Can be mounted in userns */
#define FS_RENAME_DOES_D_MOVE 0x20 /* FS will handle d_move in rename */

/* Capability flags */
#define FS_CAP_CASE_INSENSITIVE 1
#define FS_CAP_ATOMIC_RENAME 2

#endif
