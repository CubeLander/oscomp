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
__boot_code void early_sync_barrier(volatile int32* counter, int32 all);
__boot_code void early_write_tp(uint64 x);
__boot_code void* early_memset(void* dest, int byte, size_t len);
__boot_code int32 eayly_map_pages(pagetable_t pagetable, uint64 va, uint64 pa, uint64 size, int32 perm);

extern char _ftext[], _etext[], _fdata[], _end[], _boot_end[];

// 分配 (NCPU + 1) 个保护页 + NCPU 个实际栈页
__boot_data __attribute__((aligned(PAGE_SIZE))) char stack0[PAGE_SIZE * (NCPU + 1 + NCPU)];

__boot_code static void kernel_vm_init(void) {
	// extern struct mm_struct init_mm;
	//  映射内核代码段和只读段

	void* pagetable_phaddr = _boot_end;
	// init_mm.pagetable = pagetable_phaddr;
	//  之后它会被加入内核的虚拟空间，先临时用一个页
	early_memset(pagetable_phaddr, 0, PAGE_SIZE);

	// kprintf("_etext=%lx,_ftext=%lx\n", _etext, _ftext);

	eayly_map_pages(pagetable_phaddr, (uint64)_ftext, (uint64)_ftext, (uint64)(_etext - _ftext), prot_to_type(PROT_READ | PROT_EXEC, 0));

	// 映射内核HTIF段
	eayly_map_pages(pagetable_phaddr, (uint64)_etext, (uint64)_etext, (uint64)(_fdata - _etext), prot_to_type(PROT_READ | PROT_WRITE, 0));

	// 映射内核数据段
	eayly_map_pages(pagetable_phaddr, (uint64)_fdata, (uint64)_fdata, (uint64)(_end - _fdata), prot_to_type(PROT_READ | PROT_WRITE, 0));
	// 映射内核数据段
	eayly_map_pages(pagetable_phaddr, (uint64)_fdata, (uint64)_fdata, (uint64)(_end - _fdata), prot_to_type(PROT_READ | PROT_WRITE, 0));

	// 对于剩余的物理内存空间做直接映射
	eayly_map_pages(pagetable_phaddr, (uint64)_end, (uint64)_end, DRAM_BASE + memInfo.size - (uint64)_end, prot_to_type(PROT_READ | PROT_WRITE, 0));
	// // satp不通过这层映射找pagetable_phaddr，但是为了维护它，也需要做一个映射
	// eayly_map_pages(pagetable_phaddr, (uint64)pagetable_phaddr,
	//               (uint64)pagetable_phaddr, PAGE_SIZE,
	//               prot_to_type(PROT_READ | PROT_WRITE, 0));
	// 映射内核栈
	// eayly_map_pages(init_mm.pagetable, (uint64)init_mm.pagetable, )

	// pagetable_dump(pagetable_phaddr);

	// // 6. 映射MMIO区域（如果有需要）
	// // 例如UART、PLIC等外设的内存映射IO区域
}

void start_trap() { while (1); }
struct trapframe boot_trapframe;
// 这个boot_trapframe应该给每个核都发一个
__boot_code void boot_trap_setup(void) { return; }

//
// s_start: S-mode entry point of riscv-pke OS kernel.
//
volatile static int32 sig = 1;
volatile static int counter = 0;

void early_boot(uintptr_t hartid, uintptr_t dtb) {
	write_tp(hartid);
	write_csr(sstatus, read_csr(sstatus) | SSTATUS_SIE);
	write_csr(stvec, (uint64)start_trap);
	write_csr(sscratch, (uint64)&boot_trapframe);
	// 最重要！先把中断服务程序挂上去，不然崩溃都不知道怎么死的。

	if (hartid == 0) {
		// spike_file_init(); //TODO: 将文件系统迁移到 QEMU
		// init_dtb(dtb);
		write_csr(satp, 0);
		kernel_vm_init();
	}
	if (NCPU > 1) early_sync_barrier(&counter, NCPU);
	write_csr(sie, read_csr(sie) | SIE_SEIE | SIE_STIE); // 不启用核间中断（暂时） TODO
	return;
}

__boot_code void early_write_tp(uint64 x) { asm volatile("mv tp, %0" : : "r"(x)); }

__boot_code void early_sync_barrier(volatile int32* counter, int32 all) {

	int32 local;

	asm volatile("amoadd.w %0, %2, (%1)\n" : "=r"(local) : "r"(counter), "r"(1) : "memory");

	if (local + 1 < all) {
		do {
			asm volatile("lw %0, (%1)\n" : "=r"(local) : "r"(counter) : "memory");
		} while (local < all);
	}
}

__boot_code void* early_memset(void* dest, int byte, size_t len) {
	if ((((uintptr_t)dest | len) & (sizeof(uintptr_t) - 1)) == 0) {
		uintptr_t word = byte & 0xFF;
		word |= word << 8;
		word |= word << 16;
		word |= word << 16 << 16;

		int32* d = dest;
		while ((uintptr_t)d < (uintptr_t)(dest + len)) *d++ = word;
	} else {
		char* d = dest;
		while (d < (char*)(dest + len)) *d++ = byte;
	}
	return dest;
}


__boot_code int32 eayly_map_pages(pagetable_t pagetable, uint64 va, uint64 pa, uint64 size, int32 perm) {
	// size可以不对齐
	size = ROUNDUP(size, PAGE_SIZE);
	// kprintf("eayly_map_pages: start\n");
	for (uint64 off = 0; off < size; off += PAGE_SIZE) {
		early_map_page(pagetable, va + off, pa + off, perm);
	}
	// kprintf("eayly_map_pages: complete\n");

	return 0;
}



__boot_code int32 early_map_page(pagetable_t pagetable, vaddr_t va, paddr_t pa, int32 perm) {
	if (pagetable == NULL) {
		return -1;
	}
	uint64 aligned_va = ROUNDDOWN(va, PAGE_SIZE);
	uint64 aligned_pa = ROUNDDOWN(pa, PAGE_SIZE);

	// 检查虚拟地址是否有效
	if (aligned_va >= MAXVA) {
		return -1;
	}

	// 查找页表项，必要时分配页表
	pte_t* pte = early_page_walk(pagetable, aligned_va, 1);
	if (pte == NULL) {
		return -1;
	}

	// 检查是否已映射
	if (*pte & PTE_V) {
		// 页已映射，可能需要更新权限
		if (PTE2PA(*pte) == aligned_pa) {
			// 同一物理页，只更新权限
			*pte = PA2PPN(aligned_pa) | perm | PTE_V;
		} else {
			// 映射到不同物理页，报错
			return -1;
		}
	} else {
		// 创建新映射
		//kprintf("create page=%lx perm: %lx\n", aligned_pa, perm);
		*pte = PA2PPN(aligned_pa) | perm | PTE_V;
	}

	return 0;
}



/**
 * 在页表中查找页表项
 */
__boot_code pte_t* early_page_walk(pagetable_t pagetable, uint64 va, int32 alloc) {
	if (pagetable == NULL) {
		return NULL;
	}

	// 检查虚拟地址是否有效
	if (va >= MAXVA) {

		return NULL;
	}

	// starting from the page directory
	pagetable_t pt = pagetable;

	for (int32 level = 2; level > 0; level--) {

		pte_t* pte = pt + PX(level, va);

		if (*pte & PTE_V) {

			pt = (pagetable_t)PTE2PA(*pte);
		} else {

			if (alloc && ((pt = (pte_t*)alloc_page()->paddr) != 0)) {
				memset(pt, 0, PAGE_SIZE);
				// writes the physical address of newly allocated page to pte, to
				// establish the page table tree.

				*pte = PA2PPN(pt) | PTE_V;
			} else {
				return 0;

			} // returns NULL, if alloc == 0, or no more physical page remains
		}
	}

	// return a PTE which contains phisical address of a page
	return pt + PX(0, va);
}
