
#define ERROR_KERNEL_STACK 0x1001
#define SBI_LEGACY_PUTCHAR_NUM 0x01
#define SBI_LEGACY_GETCHAR_NUM 0x02
#define SSTATUS_SIE 0x02 
.section trapsec
.globl trap_sec_start
trap_sec_start:



#
# When a trap (e.g., a syscall from User mode in this lab) happens and the computer
# enters the Supervisor mode, the computer will continue to execute the following
# function (smode_trap_vector) to actually handle the trap.
#
# NOTE: sscratch points to the trapframe of current process before entering
# smode_trap_vector. It is done by reture_to_user function (defined below) when
# scheduling a user-mode application to run.
#
.globl smode_trap_vector
.align 4
smode_trap_vector:
	# 首先关中断。 
	csrrc t0, sstatus, SSTATUS_SIE
    # 首先保存关键CSR到一些临时寄存器
    ; csrr     s1, sepc    # 保存sepc到s1
    ; csrr     s2, scause  # 保存scause到s2
    ; csrr     s3, stval   # 保存stval到s3
    ; csrr     s4, sstatus # 保存sstatus到s4
    
    # csrrw    a0, sscratch, a0


		# sscratch临时保存了用户中断上下文在内核中的位置。
    # swap a0 and sscratch, so that points a0 to the trapframe of current process
    csrrw a0, sscratch, a0


    # 检查 t6(a0) 是否在有效地址范围内
    ; li        t0, 0x80000000         # 假设这是内核地址空间的下界
    ; bltu      a0, t0, stack_error    # 如果 a0 < 下界，跳转到错误处理
    ; li        t0, 0x88000000         # 假设这是内核地址空间的上界
    ; bgeu      a0, t0, stack_error    # 如果 a0 >= 上界，跳转到错误处理
	; 	# t0-t6都是临时寄存器，操作系统或应用程序都不需要保护其中的内容。
    # 这个t6寄存器在用户程序中用不上，所以可以用来做汇编的临时变量，作为保存所有寄存器的内存地址。
    addi t6, a0 , 0

    # store_all_registers is a macro defined in util/load_store.S, it stores contents
    # of all general purpose registers into a piece of memory started from [t6].
    sd ra, 0(t6)
    sd sp, 8(t6)
    sd gp, 16(t6)
    sd tp, 24(t6)
    sd t0, 32(t6)
    sd t1, 40(t6)
    sd t2, 48(t6)
    sd s0, 56(t6)
    sd s1, 64(t6)
    sd a0, 72(t6)
    sd a1, 80(t6)
    sd a2, 88(t6)
    sd a3, 96(t6)
    sd a4, 104(t6)
    sd a5, 112(t6)
    sd a6, 120(t6)
    sd a7, 128(t6)
    sd s2, 136(t6)
    sd s3, 144(t6)
    sd s4, 152(t6)
    sd s5, 160(t6)
    sd s6, 168(t6)
    sd s7, 176(t6)
    sd s8, 184(t6)
    sd s9, 192(t6)
    sd s10, 200(t6)
    sd s11, 208(t6)
    sd t3, 216(t6)
    sd t4, 224(t6)
    sd t5, 232(t6)
    sd t6, 240(t6)

    # come back to save a0 register before entering trap handling in trapframe
    # [t0]=[sscratch]
    csrr t0, sscratch
    sd t0, 72(a0)

    # use the "user kernel" stack (whose pointer stored in p->trapframe->kernel_sp)
    ld sp, 248(a0)

    # load the address of smode_trap_handler() from p->trapframe->kernel_trap
    ld t0, 256(a0)

    # restore kernel page table from p->trapframe->kernel_satp. added @lab2_1
    ld t1, 272(a0)
    csrw satp, t1
    sfence.vma zero, zero
	# 存完状态以后再开中断
	call smode_trap_handler
    # jump to smode_trap_handler() that is defined in kernel/trap.c
    # call t0
#
# return from Supervisor mode to User mode, transition is made by using a trapframe,
# which stores the context of a user application.
# return_to_user() takes one parameter, i.e., the pointer (a0 register) pointing to a
# trapframe (defined in kernel/trapframe.h) of the process.
#
.globl return_to_user
return_to_user:
    # a0: TRAPFRAME
    # a1: user page table, for satp.

    # switch to the user page table. added @lab2_1
    csrw satp, a1
    sfence.vma zero, zero

    # [sscratch]=[a0], save a0 in sscratch, so sscratch points to a trapframe now.
    csrw sscratch, a0

    # let [t6]=[a0]
    addi t6, a0, 0

    # restore_all_registers is a assembly macro defined in util/load_store.S.
    # the macro restores all registers from trapframe started from [t6] to all general
    # purpose registers, so as to resort the execution of a process.
    ld ra, 0(t6)
    ld sp, 8(t6)
    ld gp, 16(t6)
    ld tp, 24(t6)
    ld t0, 32(t6)
    ld t1, 40(t6)
    ld t2, 48(t6)
    ld s0, 56(t6)
    ld s1, 64(t6)
    ld a0, 72(t6)
    ld a1, 80(t6)
    ld a2, 88(t6)
    ld a3, 96(t6)
    ld a4, 104(t6)
    ld a5, 112(t6)
    ld a6, 120(t6)
    ld a7, 128(t6)
    ld s2, 136(t6)
    ld s3, 144(t6)
    ld s4, 152(t6)
    ld s5, 160(t6)
    ld s6, 168(t6)
    ld s7, 176(t6)
    ld s8, 184(t6)
    ld s9, 192(t6)
    ld s10, 200(t6)
    ld s11, 208(t6)
    ld t3, 216(t6)
    ld t4, 224(t6)
    ld t5, 232(t6)
    ld t6, 240(t6) 

    # return to user mode and user pc.
    sret




# 栈错误处理
stack_error:
    # 切换到紧急栈
    la        sp, emergency_stack_top
    
    # 输出错误头部
    la        a2, error_header
    call      print_string
    
    # 直接输出错误代码（硬编码为KERNEL_STACK_ERROR）
    li        a2, ERROR_KERNEL_STACK
    call      print_hex
    
    # 输出失效地址信息
    la        a2, addr_msg
    call      print_string
    mv        a2, t6  # 直接使用t6而不是a1
    call      print_hex
    
    # 输出sepc
    la        a2, sepc_msg
    call      print_string
    mv        a2, s1     # 使用之前保存的sepc值
    call      print_hex
    
    # 输出scause
    la        a2, scause_msg
    call      print_string
    mv        a2, s2     # 使用之前保存的scause值
    call      print_hex
    
    # 输出stval
    la        a2, stval_msg
    call      print_string
    csrr      a2, stval
    call      print_hex
    
    # 输出sstatus
    la        a2, sstatus_msg
    call      print_string
    csrr      a2, sstatus
    call      print_hex
    
    # 输出结束信息并停止系统
    la        a2, fatal_end_msg
    call      print_string
    j         system_halt



# 致命错误报告函数
# 参数: a0 = 错误代码, a1 = 出错地址
report_fatal_error:
    # 输出错误头部
    la        a2, error_header
    call      print_string
    
    # 输出错误代码
    mv        a2, a0
    call      print_hex
    
    # 输出出错地址
    la        a2, addr_msg
    call      print_string
    mv        a2, a1
    call      print_hex
    
    # 保存并获取关键寄存器值
    csrr      a2, sepc
    sd        a2, 40(sp)           # 保存 sepc 到栈上
    
    # 输出sepc
    la        a2, sepc_msg
    call      print_string
    mv        a2, s1     # 使用之前保存的sepc值
    call      print_hex
    
    # 输出 scause
    la        a2, sepc_msg
    call      print_string
    mv        a2, s2     # 使用之前保存的sepc值
    call      print_hex
    
    # 输出 stval
    la        a2, sepc_msg
    call      print_string
    mv        a2, s3     # 使用之前保存的sepc值
    call      print_hex
    
    # 输出 sstatus
    la        a2, sepc_msg
    call      print_string
    mv        a2, s4     # 使用之前保存的sepc值
    call      print_hex
    
    # 输出当前线程信息（如果可能）
    # 可扩展更多信息...
    
    # 输出结束消息
    la        a2, fatal_end_msg
    call      print_string
    
    # 返回调用者
    ret
# 系统停止函数
system_halt:
    # 禁用所有中断
    csrci     sstatus, 2     # 清除 SIE 位
    
    # 输出关机消息
    la        a2, halt_msg
    call      print_string
    
halt_loop:
    wfi                      # 等待中断（省电）
    j         halt_loop      # 永远循环

# ===== 辅助函数 =====

# 打印字符串函数
# 参数: a2 = 字符串指针
print_string:
    mv        t0, a2
str_loop:
    lb        t1, 0(t0)
    beqz      t1, str_done
    mv        a0, t1
    
    # 保存寄存器
    addi      sp, sp, -32
    sd        a0, 0(sp)
    sd        a1, 8(sp)
    sd        a2, 16(sp)
    sd        ra, 24(sp)
    
    # 调用 SBI_PUTCHAR
    mv        a0, t1
    li        a1, 0
    li        a2, 0
    li        a7, SBI_LEGACY_PUTCHAR_NUM
    ecall
    
    # 恢复寄存器
    ld        ra, 24(sp)
    ld        a2, 16(sp)
    ld        a1, 8(sp)
    ld        a0, 0(sp)
    addi      sp, sp, 32
    
    addi      t0, t0, 1
    j         str_loop
str_done:
    ret

# 打印16进制数函数
# 参数: a2 = 要打印的值
print_hex:
    # 保存寄存器
    addi      sp, sp, -64
    sd        ra, 0(sp)
    sd        a0, 8(sp)
    sd        a1, 16(sp)
    sd        a2, 24(sp)
    sd        a7, 32(sp)
    sd        t0, 40(sp)
    sd        t1, 48(sp)
    sd        t2, 56(sp)
    
    # 输出 "0x" 前缀
    li        a0, '0'
    li        a1, 0
    li        a2, 0
    li        a7, 0x01 # SBI_LEGACY_PUTCHAR_NUM
    ecall
    
    li        a0, 'x'
    li        a1, 0
    li        a2, 0
    li        a7, 0x01 # SBI_LEGACY_PUTCHAR_NUM
    ecall
    
    # 恢复值
    ld        a2, 24(sp)
    
    # 输出16个十六进制位
    li        t0, 60           # 移位计数
hex_loop:
    srl       t1, a2, t0       # 右移获取当前4位
    andi      t1, t1, 0xf      # 掩码，只保留低4位
    
    # 转换为ASCII字符
    li        t2, 10
    blt       t1, t2, digit    # 如果 < 10，直接转成数字
    addi      t1, t1, 'a' - 10 # 否则转成a-f
    j         print_digit
digit:
    addi      t1, t1, '0'      # 转成数字0-9
    
print_digit:
    # 保存/恢复临时寄存器
    sd        t0, 40(sp)
    sd        t1, 48(sp)
    sd        t2, 56(sp)
    
    # 输出字符
    mv        a0, t1
    li        a1, 0
    li        a2, 0
    li        a7, SBI_LEGACY_PUTCHAR_NUM
    ecall
    
    # 恢复临时寄存器
    ld        t0, 40(sp)
    ld        t1, 48(sp)
    ld        t2, 56(sp)
    
    addi      t0, t0, -4       # 移位计数减4
    bltz      t0, hex_done     # 如果小于0，结束
    j         hex_loop         # 否则继续循环
    
hex_done:
    # 恢复所有寄存器
    ld        t2, 56(sp)
    ld        t1, 48(sp)
    ld        t0, 40(sp)
    ld        a7, 32(sp)
    ld        a2, 24(sp)
    ld        a1, 16(sp)
    ld        a0, 8(sp)
    ld        ra, 0(sp)
    addi      sp, sp, 64
    ret

# ===== 字符串常量 =====
.section .rodata
error_header:
    .asciz "\r\n\r\n***** KERNEL FATAL ERROR *****\r\nError Code: "
addr_msg:
    .asciz "\r\nFault Address: "
sepc_msg:
    .asciz "\r\nSEPC: "
scause_msg:
    .asciz "\r\nSCAUSE: "
stval_msg:
    .asciz "\r\nSTVAL: "
sstatus_msg:
    .asciz "\r\nSSTATUS: "
fatal_end_msg:
    .asciz "\r\n********************************\r\n"
halt_msg:
    .asciz "\r\nSystem halted. Reset required.\r\n"