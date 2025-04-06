#include <kernel/mm/memlayout.h>
//#include <kernel/riscv.h>

	# qemu -kernel loads the kernel at 0x80000000
        # and causes each hart (i.e. CPU) to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.
#define PAGE_SIZE 4096
.section .text.entry
.global _entry
_entry:
    la sp, stack0

    # 每个CPU的栈是: 保护页 + 实际栈页
    # CPU 0: 保护页0, 栈页0
    # CPU 1: 保护页1, 栈页1
    # 但保护页是共享的: 栈页0也用保护页1
    
    # 先计算到第一个保护页之后
	li t0, 4096
    add sp, sp, t0
    
    # 然后计算当前CPU的偏移
    add t0, t0, t0  # 每CPU两页(保护页+栈页)
    mul t0, t0, a0
    add sp, sp, t0
    
    # 此时sp指向栈页的末尾
    
    # 接下来跳转到s_start
	call early_boot
	
    call s_start
spin:
        j spin

	// 下面的代码并非延迟槽，实际上，RISCV没有延迟槽
	nop
	nop
