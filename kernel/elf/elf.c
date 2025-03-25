/*
 * RISC-V OS Kernel
 * 现代化的ELF加载器实现，用于加载和解析可执行文件
 * 使用内核标准文件接口
 */

#include <kernel/elf.h>

#include <kernel/fs/vfs/vfs.h>
#include <kernel/mm/kmalloc.h>
#include <kernel/mm/mm_struct.h>
#include <kernel/mm/mmap.h>
#include <kernel/mm/vma.h>
#include <kernel/proc_file.h>
#include <kernel/riscv.h>
#include <kernel/sched/process.h>
#include <kernel/trapframe.h>

#include <spike_interface/spike_utils.h>
#include <util/string.h>

/**
 * ELF加载器的上下文信息
 * 用于保存加载过程中的必要数据
 */
typedef struct elf_context {
  int fd;                   // 文件描述符
  struct task_struct *proc; // 目标进程
  elf_header ehdr;          // ELF头部
  uint64 entry_point;       // 入口点
} elf_context;

static int init_elf_context(elf_context *ctx, int fd, struct task_struct *proc);
static void setup_global_pointer(elf_context *ctx);

static int load_segment(elf_context *ctx, elf_prog_header *ph);

static int validate_elf_header(elf_header *ehdr);
static int elf_setup_vma(struct mm_struct *mm, uint64 ph_vaddr, uint64 ph_memsz,
                         uint32 ph_flags);
static ssize_t elf_read_at(elf_context *ctx, void* dest, size_t size,
                           off_t offset);
static int load_debug_information(elf_context *ctx);
static int load_elf_binary(elf_context *ctx);



/**
 * 从文件加载ELF可执行文件到进程
 * 对外的主要接口函数
 *
 * @param proc 目标进程
 * @param filename ELF文件名
 */
void load_elf_from_file(struct task_struct *proc, char *filename) {
  elf_context ctx;

  sprint("load_elf_from_file: Loading application: %s\n", filename);

  // 使用内核标准文件接口打开ELF文件
  int fd = do_open(filename, O_RDONLY);
  if (fd < 0) {
    panic("Failed to open application file: %s (error %d)\n", filename, fd);
  }
  sprint("load_elf_from_file: do_open ended.\n");

  // 确保进程有有效的内存布局
  if (unlikely(!proc->mm)) {
    proc->mm = user_alloc_mm();
    if (!proc->mm) {
      do_close(fd);
      panic("Failed to create memory layout for process\n");
    }
  }
  sprint("load_elf_from_file: process has mm_struct.\n");

  // 初始化ELF上下文
  if (init_elf_context(&ctx, fd, proc) != 0) {
    do_close(fd);
    panic("Failed to initialize ELF context\n");
  }

  // 加载ELF二进制文件
  if (load_elf_binary(&ctx) != 0) {
    do_close(fd);
    panic("Failed to load ELF binary\n");
  }

  // 如果需要，加载调试信息
  load_debug_information(&ctx);
  sprint("load_elf_from_file: load debug information\n");
  // 关闭文件
  do_close(fd);

  sprint(
      "Application loaded successfully, entry point (virtual address): 0x%lx\n",
      proc->trapframe->epc);
}

/**
 * 加载ELF文件的符号表
 * 用于调试和错误回溯
 *
 * @param filename ELF文件名
 * @return 0表示成功，非0表示失败
 */
int load_elf_symbols(char *filename) {
  int fd = do_open(filename, O_RDONLY);
  if (fd < 0) {
    sprint("Failed to open file for symbols: %s (error %d)\n", filename, fd);
    return -1;
  }

  elf_context ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.fd = fd;

  // 读取ELF头
  if (elf_read_at(&ctx, &ctx.ehdr, sizeof(elf_header), 0) !=
      sizeof(elf_header)) {
    sprint("Failed to read ELF header for symbols\n");
    do_close(fd);
    return -1;
  }

  if (validate_elf_header(&ctx.ehdr) != 0) {
    do_close(fd);
    return -1;
  }

  // 此处可以实现符号表加载逻辑
  // ...

  do_close(fd);
  return 0;
}

/**
 * 根据程序计数器值查找函数名
 * 用于调试和错误回溯
 *
 * @param epc 程序计数器值（函数地址）
 * @return 函数名，如果未找到则返回NULL
 */
char *locate_function_name(uint64 epc) {
  // 默认实现 - 直接返回占位符
  // 实际实现需要搜索已加载的符号表
  static char unknown[] = "unknown_function";
  return unknown;
}

static int elf_setup_vma(struct mm_struct *mm, uint64 ph_vaddr, uint64 ph_memsz,
                         uint32 ph_flags) {
  // 确定VMA类型和权限
  enum vma_type vma_type;
  int prot = 0;
  uint64 vma_flags = 0;
  // 设置权限和段类型
  if (ph_flags & SEGMENT_READABLE) {
    prot |= PROT_READ;
    vma_flags |= VM_READ;
  }
  if (ph_flags & SEGMENT_WRITABLE) {
    prot |= PROT_WRITE;
    vma_flags |= VM_WRITE;
  }
  if (ph_flags & SEGMENT_EXECUTABLE) {
    prot |= PROT_EXEC;
    vma_type = VMA_TEXT;
    vma_flags |= VM_EXEC;
  } else { // 数据段
    vma_type = VMA_DATA;
  }

  // 手动创建VMA以管理该段
  struct vm_area_struct *vma =
      vm_area_setup(mm, ph_vaddr, ph_memsz, vma_type, prot, vma_flags);
  if (unlikely(!vma)) {
    sprint("Failed to create VMA for segment\n");
    return -1;
  }
  int ret = populate_vma(vma, ph_vaddr, ph_memsz, prot);
  if (ret != 0) {
    sprint("Failed to populate VMA: errno = %d\n", ret);
    return ret;
  }
  return 0;
}

/**
 * 从文件的指定偏移读取数据
 *
 * @param ctx ELF上下文
 * @param dest 目标缓冲区
 * @param size 读取字节数
 * @param offset 文件偏移
 * @return 实际读取的字节数
 */
static ssize_t elf_read_at(elf_context *ctx, void *dest, size_t size,
                           off_t offset) {
  // 保存当前文件位置
  int current_pos = do_lseek(ctx->fd, 0, SEEK_CUR);
  if (current_pos < 0) {
    sprint("Failed to get current file position\n");
    return -1;
  }

  // 设置新的文件位置
  if (do_lseek(ctx->fd, offset, SEEK_SET) < 0) {
    sprint("Failed to seek to offset %ld\n", offset);
    return -1;
  }

  // 读取数据
  ssize_t bytes_read = do_read(ctx->fd, dest, size);

  // 恢复文件位置
  do_lseek(ctx->fd, current_pos, SEEK_SET);

  return bytes_read;
}

/**
 * 验证ELF头信息是否有效
 *
 * @param ehdr ELF头部
 * @return 0表示有效，非0表示无效
 */
static int validate_elf_header(elf_header *ehdr) {
  // 验证魔数
  if (ehdr->magic != ELF_MAGIC) {
    sprint("Invalid ELF magic number: 0x%x\n", ehdr->magic);
    return -1;
  }

  // 验证架构（RISC-V 64位）
  if (ehdr->machine != 0xf3) { // EM_RISCV
    sprint("Unsupported architecture: 0x%x\n", ehdr->machine);
    return -1;
  }

  // 验证类型（可执行文件）
  if (ehdr->type != 2) { // ET_EXEC
    sprint("Not an executable file: %d\n", ehdr->type);
    return -1;
  }

  return 0;
}

/**
 * 加载ELF段到进程内存
 *
 * @param ctx ELF上下文
 * @param ph 程序头
 * @return 0表示成功，非0表示失败
 */
static int load_segment(elf_context *ctx, elf_prog_header *ph) {
  struct task_struct *proc = ctx->proc;
  struct mm_struct *mm = proc->mm;
  int ret;

  // 只加载可加载的段
  if (ph->type != ELF_PROG_LOAD) {
    return 0;
  }
  // 验证段信息
  if (ph->memsz < ph->filesz) {
    sprint("Invalid segment: memory size < file size\n");
    return -1;
  }
  // 检查地址溢出
  if (ph->vaddr + ph->memsz < ph->vaddr) {
    sprint("Segment address overflow\n");
    return -1;
  }
  sprint("Loading segment: vaddr=0x%lx, size=0x%lx, flags=0x%x\n", ph->vaddr,
         ph->memsz, ph->flags);
  ret = elf_setup_vma(mm, ph->vaddr, ph->memsz, ph->flags);
  if (ret != 0) {
    sprint("Failed to setup VMA for segment\n");
    return ret;
  }

  // 计算所需页数（向上取整）
  uint64 num_pages = (ph->memsz + PAGE_SIZE - 1) / PAGE_SIZE;
  for (uint64 i = 0; i < num_pages; i++) {
    vaddr_t vaddr = ph->vaddr + i * PAGE_SIZE;
    // 内核实际读elf的物理地址
		paddr_t pa = lookup_pa(proc->mm->pagetable, vaddr);
    // 计算这一页需要从文件中加载的字节数
    size_t page_offset = i * PAGE_SIZE;
    uint64 file_offset = ph->off + page_offset;
    uint64 bytes_to_copy = 0;

    // 确定需要复制多少数据
    if (page_offset < ph->filesz) {
      bytes_to_copy = MIN(PAGE_SIZE, ph->filesz - page_offset);

      // 从文件读取数据到分配的物理页
      if (elf_read_at(ctx, (void*)pa, bytes_to_copy, file_offset) != bytes_to_copy) {
        sprint("Failed to read segment data\n");
        return -1;
      }

      // 如果此页有剩余部分，需要清零（.bss段的一部分）
      if (bytes_to_copy < PAGE_SIZE) {
        memset((char *)pa + bytes_to_copy, 0, PAGE_SIZE - bytes_to_copy);
      }
    } else {
      // 这一页完全是bss段，清零整个页
      memset((void*)pa, 0, PAGE_SIZE);
    }
  }

  return 0;
}

/**
 * 查找并设置.sdata段的全局指针
 * RISC-V中gp寄存器通常指向.sdata段+0x800
 *
 * @param ctx ELF上下文
 */
static void setup_global_pointer(elf_context *ctx) {
  elf_header *ehdr = &ctx->ehdr;

  // 读取节头字符串表
  elf_sect_header shstr_section;
  if (elf_read_at(ctx, &shstr_section, sizeof(shstr_section),
                  ehdr->shoff + ehdr->shstrndx * sizeof(elf_sect_header)) !=
      sizeof(shstr_section)) {
    sprint("Failed to read section header string table\n");
    return;
  }

  // 分配缓冲区存储节名字符串
  char *shstr_buffer = (char *)kmalloc(shstr_section.size);
  if (!shstr_buffer) {
    sprint("Failed to allocate section name buffer\n");
    return;
  }

  // 读取节名字符串表
  if (elf_read_at(ctx, shstr_buffer, shstr_section.size,
                  shstr_section.offset) != shstr_section.size) {
    sprint("Failed to read section names\n");
    kfree(shstr_buffer);
    return;
  }

  // 遍历所有节头，查找.sdata段
  for (int i = 0; i < ehdr->shnum; i++) {
    elf_sect_header sh;
    uint64 sh_offset = ehdr->shoff + i * sizeof(elf_sect_header);

    if (elf_read_at(ctx, &sh, sizeof(sh), sh_offset) != sizeof(sh)) {
      sprint("Failed to read section header %d\n", i);
      continue;
    }

    // 检查节名是否为.sdata
    if (sh.name < shstr_section.size &&
        strcmp(shstr_buffer + sh.name, ".sdata") == 0) {
      // 找到.sdata节，设置gp寄存器
      ctx->proc->trapframe->regs.gp = sh.addr + 0x800;
      sprint("Found .sdata section at 0x%lx, setting gp to 0x%lx\n", sh.addr,
             ctx->proc->trapframe->regs.gp);
      break;
    }
  }

  kfree(shstr_buffer);
}

/**
 * 初始化ELF加载上下文
 *
 * @param ctx 要初始化的上下文
 * @param fd 文件描述符
 * @param proc 目标进程
 * @return 0表示成功，非0表示失败
 */
static int init_elf_context(elf_context *ctx, int fd,
                            struct task_struct *proc) {
  memset(ctx, 0, sizeof(elf_context));
  ctx->fd = fd;
  ctx->proc = proc;

  // 读取ELF头
  if (elf_read_at(ctx, &ctx->ehdr, sizeof(ctx->ehdr), 0) != sizeof(ctx->ehdr)) {
    sprint("Failed to read ELF header\n");
    return -1;
  }

  // 验证ELF头
  if (validate_elf_header(&ctx->ehdr) != 0) {
    return -1;
  }

  ctx->entry_point = ctx->ehdr.entry;
  return 0;
}

/**
 * 加载调试信息（若需要）
 *
 * @param ctx ELF上下文
 * @return 0表示成功，非0表示失败
 */
static int load_debug_information(elf_context *ctx) {
  // 这里可以实现加载调试信息的功能
  return 0;
}


/**
 * 加载ELF文件到进程
 *
 * @param ctx ELF上下文
 * @return 0表示成功，非0表示失败
 */
static int load_elf_binary(elf_context *ctx) {
  elf_header *ehdr = &ctx->ehdr;
  elf_prog_header ph;

  // 遍历程序头表，加载各个段
  for (int i = 0; i < ehdr->phnum; i++) {
    uint64 ph_offset = ehdr->phoff + i * sizeof(ph);

    // 读取程序头
    if (elf_read_at(ctx, &ph, sizeof(ph), ph_offset) != sizeof(ph)) {
      sprint("Failed to read program header %d\n", i);
      return -1;
    }

    // 加载段
    if (load_segment(ctx, &ph) != 0) {
      sprint("Failed to load segment %d\n", i);
      return -1;
    }
  }

  // 设置全局指针
  setup_global_pointer(ctx);

  // 设置进程入口点
  ctx->proc->trapframe->epc = ctx->entry_point;

  sprint("ELF loaded successfully, entry point: 0x%lx\n", ctx->entry_point);
  return 0;
}