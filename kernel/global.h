#ifndef _GLOBAL_H_
#define _GLOBAL_H_

#include "config.h"
#include "process.h"
#include "vmm.h"
#include "semaphore.h"

//Two functions defined in kernel/usertrap.S
extern char smode_trap_vector[];
extern void return_to_user(trapframe *, uint64 satp);

// trap_sec_start points to the beginning of S-mode trap segment (i.e., the entry point
// of S-mode trap vector).
extern char trap_sec_start[];

// process pool. added @lab3_1
extern process procs[NPROC];

extern process* ready_queue;

// current points to the currently running user-mode application.
extern process* current[NCPU];

extern heap_block kernel_heap_head;

// 信号灯库
extern semaphore sem_pool[NSEM];

#endif