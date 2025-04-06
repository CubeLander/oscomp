/* See LICENSE for license details. */

OUTPUT_ARCH( "riscv" )

ENTRY( _entry )

SECTIONS
{
  /*--------------------------------------------------------------------*/
  /* Boot code and data (低地址区域，直接映射)                          */
  /*--------------------------------------------------------------------*/

  /* 从SBI加载内核的位置开始 */
  . = 0x80200000;
  
  /* 为早期页表分配预留额外的物理页 */
  _early_boot_page_pool = .;
  . += 0x10000;  /* 预留16页 = 64KB，可以根据需求调整 */
  _early_boot_page_pool_end = .;
  /* 启动代码区域 */
  .__boot_code : {
    ___boot_code_start = .;
    *(.text.entry)  /* 入口点代码放在启动代码中 */
    *(.__boot_code)   /* 添加的__boot_code段 */
    *(.__boot_code.*)
    . = ALIGN(16);
    ___boot_code_end = .;
  }
  
  /* 启动数据区域 */
  .boot_data : {
    _boot_data_start = .;
    *(.boot_data)   /* 添加的boot_data段 */
    *(.boot_data.*)
    *(.boot_rodata) /* 启动期间只读数据 */
    *(.boot_rodata.*)
    . = ALIGN(16);
    _boot_data_end = .;
  }
  
  /* 记录低地址区域结束位置 */
  _boot_end = .;
  
  /*--------------------------------------------------------------------*/
  /* 内核主体代码和数据 (高地址区域)                                    */
  /*--------------------------------------------------------------------*/
  
/* 切换到高地址区域 */
. = 0xff200000;

/* 记录高地址区域内核代码起始位置 */
_kernel_virt_start = .;

/* 为早期引导页表预留空间 (4KB = 一页) */
. = ALIGN(0x1000);  /* 确保页对齐 */
_early_boot_pagetable = .;
. += 0x1000;  /* 预留一页 */
_early_boot_pagetable_end = .;

/* 内核代码从页表后开始 */
. = ALIGN(0x1000);
_kernel_code_start = .;

/* 记录物理地址和虚拟地址的偏移量，用于地址转换 */
_kernel_phys_virt_offset = _kernel_code_start - _boot_end;
  
  /* 记录高地址区域内核代码起始位置 */
  _kernel_virt_start = .;
  
  /* 记录物理地址和虚拟地址的偏移量，用于地址转换 */
  _kernel_phys_virt_offset = . - _boot_end;
  
  /* Code and read-only segment */
  _ftext = .;
  
  /* text: Program code section */
  .text : AT(_boot_end) {
    . = ALIGN(4); /* 4 字节对齐 */
    *(.text)
    *(.text.*)
    *(.gnu.linkonce.t.*)
    . = ALIGN(0x1000);

    _trap_sec_start = .;
    *(trapsec)
    . = ALIGN(0x1000);
  /*   ASSERT(. - _trap_sec_start == 0x1000, "error: trap section larger than one page");   */
  }

  /* rodata: Read-only data */
  .rodata : {
    *(.rdata)
    *(.rodata)
    *(.rodata.*)
    *(.gnu.linkonce.r.*)
  }

  /* End of code and read-only segment */
  . = ALIGN(0x1000);
  _etext = .;


  /*--------------------------------------------------------------------*/
  /* Initialized data segment                                           */
  /*--------------------------------------------------------------------*/

  /* Start of initialized data segment */
  . = ALIGN(16);
  _fdata = .;

  /* data: Writable data */
  .data : {
    *(.data)
    *(.data.*)
    *(.srodata*)
    *(.gnu.linkonce.d.*)
    *(.comment)
  }

  /* End of initialized data segment */
  . = ALIGN(16);
  _edata = .;

  /*--------------------------------------------------------------------*/
  /* Uninitialized data segment                                         */
  /*--------------------------------------------------------------------*/

  /* Start of uninitialized data segment */
  . = .;
  _fbss = .;

  /* bss: Uninitialized writeable data section */
  . = .;
  _bss_start = .;
  .bss : {
    *(.bss)
    *(.bss.*)
    *(.sbss*)
    *(.gnu.linkonce.b.*)
    *(COMMON)
  }

  . = ALIGN(0x1000);
  _end = .;
  
  /* 记录虚拟地址结束位置 */
  _kernel_virt_end = .;
  
  /* 记录物理内存中内核结束位置（供内存管理使用） */
  _kernel_phys_end = (_end - _kernel_phys_virt_offset);
}