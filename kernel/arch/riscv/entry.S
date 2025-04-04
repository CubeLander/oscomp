#include <kernel/mm/memlayout.h>

	# qemu -kernel loads the kernel at 0x80000000
        # and causes each hart (i.e. CPU) to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.
.section .text
.global _entry
_entry:
        # set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        # sp = stack0 + ((hartid+1) * 4096)

        # status = Supervisor, a0 = hartid, a1 = dtb
        la sp, stack0

        li t0, KSTACKSIZE
        mv t1, a0
        addi t1, t1, 1
        mul t0, t0, t1
        add sp, sp, t0

        # jump to s_start() in start.c
        # pass a0 as hartid
        call s_start
spin:
        j spin

	// 下面的代码并非延迟槽，实际上，RISCV没有延迟槽
	nop
	nop
