/*
 * Utility functions for trap handling in Supervisor mode.
 */

#include <kernel/strap.h>
#include <kernel/mmu.h>
#include <kernel/time.h>
#include <kernel/sched.h>
#include <kernel/riscv.h>
#include <kernel/util.h>
#include <kernel/syscall/syscall.h>
#include <kernel/sprint.h>

//
// handling the syscalls. will call syscall_entry() defined in kernel/syscall.c
//
static void handle_syscall(struct trapframe *tf) {

  tf->epc += 4;


  tf->regs.a0 = syscall_entry(tf->regs.a7, tf->regs.a0, tf->regs.a1, tf->regs.a2, tf->regs.a3,
                           tf->regs.a4, tf->regs.a5);
}

//
// global variable that store the recorded "ticks". added @lab1_3

//
// added @lab1_3
//
void handle_mtimer_trap() {
  sprint("Ticks %d\n", jiffies);
  int32 hartid = read_tp();
  if(hartid == 0){
	jiffies++;
  }
  CURRENT->tick_count++;

  write_csr(sip, read_csr(sip) & ~SIP_SSIP);
}

/**
 * 处理用户空间页错误
 * 包含了原有的栈自动增长和COW功能，以及新的VMA管理功能
 * 
 * @param mcause 错误原因
 * @param sepc 错误发生时的程序计数器
 * @param stval 访问时导致页错误的虚拟地址
 */
void handle_user_page_fault(uint64 mcause, uint64 sepc, uint64 stval) {
	struct task_struct *proc = CURRENT;
	uint64 addr = stval;
	
	sprint("sepc=%lx, handle_page_fault: %lx\n", sepc, addr);
	
	// 标记访问类型
	int32 fault_prot = 0;
	if (mcause == CAUSE_LOAD_PAGE_FAULT)
			fault_prot |= PROT_READ;
	else if (mcause == CAUSE_STORE_PAGE_FAULT)
			fault_prot |= PROT_WRITE;
	else if (mcause == CAUSE_FETCH_PAGE_FAULT)
			fault_prot |= PROT_EXEC;
	
	// 处理基于 VMA 的内存管理
	if (proc->mm) {
			// 查找地址所在的VMA
			struct vm_area_struct *vma = find_vma(proc->mm, addr);
			
			if (vma) {
					// 确认访问权限
					if ((fault_prot & vma->vm_prot) != fault_prot) {
							sprint("权限不足: 需要 %d, VMA允许 %d\n", fault_prot, vma->vm_prot);
							goto error;
					}
					
					// 页地址对齐
					uint64 page_va = ROUNDDOWN(addr, PAGE_SIZE);
					
					// 计算页索引
					int32 page_idx = (page_va - vma->vm_start) / PAGE_SIZE;
					if (page_idx < 0 || page_idx >= vma->page_count) {
							sprint("页索引越界: %d\n", page_idx);
							goto error;
					}
					
					// // 如果页已存在，检查是否需要COW
					// if (vma->pages[page_idx]) {
					// 		// 检查是否是COW页
					// 		pte_t *pte = page_walk(proc->pagetable, page_va, 0);
					// 		if (pte && (*pte & PTE_V) && ((*pte & PTE_W) == 0) && (*pte & PTE_R) && mcause == CAUSE_STORE_PAGE_FAULT) {
					// 				// 写时复制
					// 				struct page *old_page = vma->pages[page_idx];
					// 				struct page *new_page = alloc_page();
					// 				if (!new_page) goto error;
									
					// 				// 复制页内容
					// 				uint64 old_pa = page_to_addr(old_page);
					// 				uint64 new_pa = page_to_addr(new_page);
					// 				memcpy(new_pa, old_pa, PAGE_SIZE);
									
					// 				// 更新映射
					// 				user_vm_unmap(proc->pagetable, page_va, PAGE_SIZE, 0);
					// 				user_vm_map(proc->pagetable, page_va, PAGE_SIZE, (uint64)new_pa, 
					// 									 prot_to_type(vma->vm_prot, 1));
									
					// 				// 更新页结构
					// 				vma->pages[page_idx] = new_page;
					// 				new_page->paddr = (uint64)page_va;
					// 				put_page(old_page);
									
					// 				return;
					// 		} else {
					// 				// 页已分配但可能未映射，确保映射正确
					// 				uint64 pa = page_to_addr(vma->pages[page_idx]);
					// 				user_vm_map(proc->pagetable, page_va, PAGE_SIZE, (uint64)pa,
					// 									prot_to_type(vma->vm_prot, 1));
					// 				return;
					// 		}
					// }
					
					// // 分配新页并映射
					// uint64 page_addr = mm_alloc_page(proc, page_va, vma->vm_prot);
					// if (page_addr) return;
			}
	}
	
	// // 如果没有找到VMA或VMA处理失败，回退到原有处理逻辑
	// if (mcause == CAUSE_STORE_PAGE_FAULT) {
	// 		// 检查是否是栈扩展
	// 		if (stval >= proc->user_stack_bottom - PAGE_SIZE) {
	// 				proc->user_stack_bottom -= PAGE_SIZE;
	// 				uint64 pa = Alloc_page();
	// 				user_vm_map(proc->pagetable, proc->user_stack_bottom, PAGE_SIZE,
	// 									(uint64)pa, prot_to_type(PROT_WRITE | PROT_READ, 1));
	// 				proc->mapped_info[STACK_SEGMENT].va = proc->user_stack_bottom;
	// 				proc->mapped_info[STACK_SEGMENT].npages++;
	// 				return;
	// 		}
			
	// 		// 检查是否是COW页
	// 		pte_t *pte = page_walk(proc->pagetable, stval, 0);
	// 		if (pte && ((uint64)*pte & PTE_W) == 0 && (*pte & PTE_X) == 0) {
	// 				uint64 page_va = stval & (~0xfff);
	// 				uint64 newpa = (uint64)Alloc_page();
	// 				memcpy((uint64 )newpa, (uint64 )PTE2PA(*pte), PAGE_SIZE);
	// 				user_vm_unmap(proc->pagetable, page_va, PAGE_SIZE, 0);
	// 				user_vm_map(proc->pagetable, page_va, PAGE_SIZE, newpa, 
	// 									 prot_to_type(PROT_WRITE | PROT_READ, 1));
	// 				return;
	// 		}
	// }
	
error:
	// 不能处理的页错误
	sprint("无法处理的页错误: addr=%lx, mcause=%lx\n", stval, mcause);
	panic("This address is not available!");
}

//
// implements round-robin scheduling. added @lab3_3
//
void rrsched() {
  // TODO (lab3_3): implements round-robin scheduling.
  // hint: increase the tick_count member of current process by one, if it is
  // bigger than TIME_SLICE_LEN (means it has consumed its time slice), change
  // its state into READY, place it in the rear of ready queue, and finally
  // schedule next process to run.
  // panic( "You need to further implement the timer handling in lab3_3.\n" );
  int32 hartid = read_tp();
  if (CURRENT->tick_count >= TIME_SLICE_LEN) {
    CURRENT->tick_count = 0;
    //sys_user_yield();
  }
}

//
// kernel/smode_trap.S will pass control to smode_trap_handler, when a trap
// happens in S-mode.
//
void smode_trap_handler(void) {
  int32 hartid = read_tp();
  // make sure we are in User mode before entering the trap handling.
  // we will consider other previous case in lab1_3 (interrupt).
  if ((read_csr(sstatus) & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  assert(CURRENT);
  // save user process counter.
  CURRENT->trapframe->epc = read_csr(sepc);

  // if the cause of trap is syscall from user application.
  // read_csr() and CAUSE_USER_ECALL are macros defined in kernel/riscv.h
  uint64 cause = read_csr(scause);

  // use switch-case instead of if-else, as there are many cases since lab2_3.
  switch (cause) {
  case CAUSE_USER_ECALL:
    handle_syscall(CURRENT->trapframe);
    // sprint("coming back from syscall\n");
    break;
  case CAUSE_MTIMER_S_TRAP:
    handle_mtimer_trap();
    // invoke round-robin scheduler. added @lab3_3
    rrsched();
    break;
  case CAUSE_STORE_PAGE_FAULT:
  case CAUSE_LOAD_PAGE_FAULT:
    // the address of missing page is stored in stval
    // call handle_user_page_fault to process page faults
    handle_user_page_fault(cause, read_csr(sepc), read_csr(stval));
    break;
  default:
    sprint("smode_trap_handler(): unexpected scause %p\n", read_csr(scause));
    sprint("            sepc=%p stval=%p\n", read_csr(sepc), read_csr(stval));
    panic("unexpected exception happened.\n");
    break;
  }
  // sprint("calling switch_to, current = 0x%x\n", current);
  // continue (come back to) the execution of current process.
  switch_to(CURRENT);
}
