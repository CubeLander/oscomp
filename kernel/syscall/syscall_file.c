#include <kernel/fs/vfs/vfs.h>
#include <kernel/proc_file.h>

#include <kernel/sched/process.h>
#include <spike_interface/spike_utils.h>

// int sys_fstat(uint64 fd, uint64 stat)
// {

// 	return do_stat(fd,user_va_to_pa(CURRENT->pagetable,(void*)stat));
// }