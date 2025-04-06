/*
 * Supervisor-mode startup codes
 */

#include <kernel/boot/dtb.h>
#include <kernel/device/sbi.h>
#include <kernel/elf.h>
#include <kernel/mmu.h>
#include <kernel/riscv.h>
#include <kernel/sched.h>
#include <kernel/syscall/syscall.h>
#include <kernel/types.h>
#include <kernel/util.h>
#include <kernel/vfs.h>

#define __boot_code __attribute__((section(".boot_text")));
#define __boot_data __attribute__((section(".boot_data")));	


// 分配 (NCPU + 1) 个保护页 + NCPU 个实际栈页
__attribute__((aligned(PAGE_SIZE))) char stack0[PAGE_SIZE * (NCPU + 1 + NCPU)];

__attribute__((aligned(PAGE_SIZE))) char emergency_stack_top[PAGE_SIZE];

void setup_stack_guard_pages(void) {
	pgt_map_pages(g_kernel_pagetable,(vaddr_t)0,(paddr_t)0, PAGE_SIZE, 0);
	// 设置空指针保护页
    // 计算保护页的起始地址
    for (int i = 0; i <= NCPU; i++) {
        void *guard_page = stack0 + (i * 2 - 1) * (PAGE_SIZE);
        
        // 标记为不可访问
        pgt_map_pages(g_kernel_pagetable,(vaddr_t)guard_page,(paddr_t)guard_page, PAGE_SIZE, 0); // 无权限
    }
	kprintf("setup_stack_guard_pages: complete\n");
    
    // 刷新TLB
    flush_tlb();
}

static void kernel_vm_init(void) {
	kprintf("kernel_vm_init: start, membase = %lx, memsize=%lx\n",memInfo.start, memInfo.size);
	// extern struct mm_struct init_mm;
	//  映射内核代码段和只读段
	g_kernel_pagetable = (pagetable_t)alloc_page()->paddr;
	// init_mm.pagetable = g_kernel_pagetable;
	//  之后它会被加入内核的虚拟空间，先临时用一个页
	memset(g_kernel_pagetable, 0, PAGE_SIZE);

	extern char _ftext[], _etext[], _fdata[], _end[];
	// kprintf("_etext=%lx,_ftext=%lx\n", _etext, _ftext);

	pgt_map_pages(g_kernel_pagetable, (uint64)_ftext, (uint64)_ftext, (uint64)(_etext - _ftext), prot_to_type(PROT_READ | PROT_EXEC, 0));

	// 映射内核HTIF段
	pgt_map_pages(g_kernel_pagetable, (uint64)_etext, (uint64)_etext, (uint64)(_fdata - _etext), prot_to_type(PROT_READ | PROT_WRITE, 0));

	// 映射内核数据段
	pgt_map_pages(g_kernel_pagetable, (uint64)_fdata, (uint64)_fdata, (uint64)(_end - _fdata), prot_to_type(PROT_READ | PROT_WRITE, 0));
	// 映射内核数据段
	pgt_map_pages(g_kernel_pagetable, (uint64)_fdata, (uint64)_fdata, (uint64)(_end - _fdata), prot_to_type(PROT_READ | PROT_WRITE, 0));

	// 对于剩余的物理内存空间做直接映射
	pgt_map_pages(g_kernel_pagetable, (uint64)_end, (uint64)_end, DRAM_BASE + memInfo.size - (uint64)_end, prot_to_type(PROT_READ | PROT_WRITE, 0));
	// // satp不通过这层映射找g_kernel_pagetable，但是为了维护它，也需要做一个映射
	// pgt_map_pages(g_kernel_pagetable, (uint64)g_kernel_pagetable,
	//               (uint64)g_kernel_pagetable, PAGE_SIZE,
	//               prot_to_type(PROT_READ | PROT_WRITE, 0));
	setup_stack_guard_pages();
	// 映射内核栈
	// pgt_map_pages(init_mm.pagetable, (uint64)init_mm.pagetable, )

	// pagetable_dump(g_kernel_pagetable);

	// // 6. 映射MMIO区域（如果有需要）
	// // 例如UART、PLIC等外设的内存映射IO区域

	kprintf("kern_vm_init: complete\n");
}

/**
 * setup_init_fds - Set up standard file descriptors for init process
 * @init_task: Pointer to the init task structure
 *
 * Sets up standard input, output, and error for the init process.
 * This must be called before the init process starts executing.
 *
 * Returns: 0 on success, negative error code on failure
 */
int32 setup_init_fds(struct task_struct* init_task) {
	int32 fd, console_fd;
	struct task_struct* saved_task = current_task();

	// Temporarily set current to init task
	set_current_task(init_task);

	// Set up stdin, stdout, stderr
	for (fd = 0; fd < 3; fd++) {
		if (fd != do_open("/dev/console", O_RDWR, 0)) {
			kprintf("Failed to open /dev/console for fd %d\n", fd);
			set_current_task(saved_task);
			return -1;
		}
	}

	// Restore original task
	set_current_task(saved_task);
	return 0;
}

int32 create_init_process(void) {
	struct task_struct* init_task;
	int32 error = -1;

	// Create the init process task structure
	init_task = alloc_process(); // No parent for init
	if (!init_task) return -ENOMEM;

	// Set up process ID and other basic attributes
	init_task->pid = 1;
	init_task->parent = NULL; // No parent

	setup_init_fds(init_task);
	// Load the init binary
	error = load_init_binary(init_task, "/sbin/init");
	if (error) goto fail_exec;

	// Add to scheduler queue and start running
	insert_to_ready_queue(init_task);

	// we should never reach here.
	return 0;

fail_exec:
	// Clean up file descriptors
	for (int fd = 0; fd < init_task->fdtable->max_fds; fd++) {
		if (init_task->fdtable->fd_array[fd]) fdtable_closeFd(init_task->fdtable, fd);
	}
fail_fds:
	fs_struct_unref(init_task->fs);
fail_fs:
	free_process(init_task);
	return error;
}

void start_trap() { while (1); }

struct task_struct boot_task;


struct trapframe boot_trapframe;
// 这个boot_trapframe应该给每个核都发一个
void boot_trap_setup(void){
	current_percpu[read_tp()] = &boot_task;
	boot_task.trapframe = &boot_trapframe;

	extern char smode_trap_vector[];
	write_csr(sstatus, read_csr(sstatus) | SSTATUS_SIE);
	write_csr(stvec, (uint64)smode_trap_vector);
	write_csr(sscratch, (uint64)&boot_trapframe);
	uint64 ksp = read_reg(sp);
	boot_trapframe.kernel_sp = ROUNDUP(ksp, PAGE_SIZE);
	boot_trapframe.kernel_schedule = (uint64)schedule;

	return;
}


//
// s_start: S-mode entry point of riscv-pke OS kernel.
//
volatile static int32 sig = 1;
volatile static int counter = 0;

void early_boot(uintptr_t hartid, uintptr_t dtb) {
	write_tp(hartid);
	boot_trap_setup();
	// 最重要！先把中断服务程序挂上去，不然崩溃都不知道怎么死的。

	if (hartid == 0) {
		// spike_file_init(); //TODO: 将文件系统迁移到 QEMU
		// init_dtb(dtb);
		parseDtb(dtb);
	}
	if (NCPU > 1) sync_barrier(&counter, NCPU);
	write_csr(sie, read_csr(sie) | SIE_SEIE | SIE_STIE); // 不启用核间中断（暂时） TODO

	// init timing. added @lab1_3
	// lab1_challenge1 为了调试便利，禁用了外部时钟中断：
	// timerinit(hartid);

	// switch to supervisor mode (S mode) and jump to s_start(), i.e., set pc to
	// mepc
	// asm volatile("mret");
	// 在内核初始化早期添加
	// kprintf("Current privilege level: %ld\n", (read_csr(mstatus) >> 11) & 3);
	// kprintf("stvec: 0x%lx\n", read_csr(stvec));
	// kprintf("medeleg: 0x%lx\n", read_csr(medeleg));
	// kprintf("mideleg: 0x%lx\n", read_csr(mideleg));
	// kprintf("sstatus: 0x%lx\n", read_csr(sstatus));
	// kprintf("sie: 0x%lx\n", read_csr(sie));
	// kprintf("In m_start, hartid:%d\n", hartid);

	//write_csr(stvec, (uint64)start_trap);

	extern void init_idle_task(void);

	kprintf("Enter supervisor mode...\n");
	write_csr(satp, 0);

	if (hartid == 0) {
		init_page_manager();
		kernel_vm_init();
		pagetable_activate(g_kernel_pagetable);
		boot_trapframe.kernel_satp = MAKE_SATP(g_kernel_pagetable);
		create_init_mm();
		kmem_init();
		init_scheduler();

		init_idle_task();
		// kmalloc在形式上需要使用init_mm的“用户虚拟空间分配器”
		// 所以我们在启用kmalloc之前，需要先初始化0号进程

		// init_scheduler();
		vfs_init();
		sig = 0;
	} else {
		while (sig) {
		}
		pagetable_activate(g_kernel_pagetable);
	}

	// sync_barrier(&sync_counter, NCPU);

	//  写入satp寄存器并刷新tlb缓存
	//    从这里开始，所有内存访问都通过MMU进行虚实转换

	kprintf("Switch to user mode...\n");
	// the application code (elf) is first loaded into memory, and then put into
	// execution added @lab3_1
	create_init_process();
	schedule();
	// we should never reach here.
	return;
}

// This is a dummy file for RISC-V architecture detection
// Save this file as dummy_riscv.c in your project root

int main() { return 0; }

// Compile with:
// riscv64-unknown-elf-gcc -o dummy_riscv dummy_riscv.c