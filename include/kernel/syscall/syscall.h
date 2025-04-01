#ifndef _SYSCALL_H_
#define _SYSCALL_H_

#include <kernel/types.h>

/* Include syscall numbers */
#include "syscall_ids.h"

/**
 * System call wrapper type - all syscall wrappers must match this signature
 * They receive raw int64 arguments and return int64
 */
typedef int64 (*syscall_func_t)(int64, int64, int64, int64, int64, int64);

/**
 * System call entry in dispatch table
 */
struct syscall_entry {
	syscall_func_t func; /* The wrapper function pointer */
	const char* name;    /* Name for debugging */
	uint8_t num_args;    /* Number of arguments (for validation) */
};

/* Syscall dispatcher */
int64 syscall_entry(int64 syscall_num, int64 a0, int64 a1, int64 a2, int64 a3, int64 a4, int64 a5);

/* File-related syscalls */
int64 sys_open(const char* pathname, int32 flags, mode_t mode);
int64 sys_close(int32 fd);
int64 sys_read(int32 fd, void* buf, size_t count);
int64 sys_write(int32 fd, const void* buf, size_t count);
int64 sys_lseek(int32 fd, off_t offset, int32 whence);
int64 sys_mount(const char* source, const char* target, const char* fstype, uint64 flags, const void* data);

/* Process-related syscalls */
int64 sys_exit(int32 status);
int64 sys_getpid(void);
int64 sys_getppid(void);
int64 sys_clone(uint64 flags, uint64 stack, uint64 ptid, uint64 tls, uint64 ctid);
int64 sys_execve(const char* filename, const char* const argv[], const char* const envp[]);
//int64 sys_wait4(pid_t pid, int32* wstatus, int32 options, struct rusage* rusage);

/* Memory-related syscalls */
int64 sys_brk(void* addr);
int64 sys_mmap(void* addr, size_t length, int32 prot, int32 flags, int32 fd, off_t offset);
int64 sys_munmap(void* addr, size_t length);
int64 sys_mprotect(void* addr, size_t len, int32 prot);
int64 sys_mremap(void* old_address, size_t old_size, size_t new_size, int32 flags, void* new_address);

/* Time-related syscalls */
int64 sys_time(time_t* tloc);
int64 sys_nanosleep(const struct timespec* req, struct timespec* rem);
int64 sys_gettimeofday(struct timeval* tv, struct timezone* tz);
int64 sys_clock_gettime(clockid_t clk_id, struct timespec* tp);

/* Misc syscalls */
int64 sys_getrandom(void* buf, size_t buflen, uint32 flags);
int64 sys_yield(void);

#endif /* _SYSCALL_H_ */