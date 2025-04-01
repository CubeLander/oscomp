/**
 * @file kmalloc.h
 * @brief Kernel memory allocation interface
 *
 * Provides kmalloc/kfree interfaces for kernel dynamic memory allocation.
 * This implementation uses a slab allocator for small allocations and
 * page allocator for large allocations.
 */

#ifndef _KMALLOC_H
#define _KMALLOC_H
#include <kernel/types.h>

#ifndef _GFP_H
#define _GFP_H

/* Memory allocation flags */
#define __GFP_WAIT 0x0001    /* Can sleep */
#define __GFP_HIGH 0x0002    /* High priority allocation */
#define __GFP_IO 0x0004      /* Can start I/O */
#define __GFP_FS 0x0008      /* Can start filesystem s_operations */
#define __GFP_NOWARN 0x0010  /* Don't print allocation failure warnings */
#define __GFP_REPEAT 0x0020  /* Retry the allocation */
#define __GFP_NOFAIL 0x0040  /* Allocation cannot fail */
#define __GFP_NORETRY 0x0080 /* Don't retry if allocation fails */
#define __GFP_ZERO 0x0100    /* Zero the allocation */

/* Commonly used combinations */
#define GFP_KERNEL                                                             \
  (__GFP_WAIT | __GFP_IO | __GFP_FS) /* Normal kernel allocation */
#define GFP_ATOMIC 0                 /* Allocation cannot sleep */
#define GFP_USER (__GFP_WAIT | __GFP_IO | __GFP_FS) /* For processes */
#define GFP_HIGHUSER                                                           \
  (__GFP_WAIT | __GFP_IO | __GFP_FS) /* For user allocations */

#endif /* _GFP_H */

/**
 * @brief Initialize kernel memory allocation subsystem
 */
void kmem_init(void);

/**
 * @brief Allocate kernel memory
 *
 * @param size Size in bytes to allocate
 * @return void* Pointer to allocated memory, or NULL if failed
 */
void* kmalloc(size_t size);

/**
 * @brief Free previously allocated kernel memory
 *
 * @param ptr Pointer to memory allocated by kmalloc
 */
void kfree(void* ptr);

/**
 * @brief Allocate kernel memory and zero it
 *
 * @param size Size in bytes to allocate
 * @return void* Pointer to allocated and zeroed memory, or NULL if failed
 */
void* kzalloc(size_t size);

/**
 * @brief Resize an allocated memory block
 *
 * @param ptr Pointer to memory previously allocated by kmalloc, or NULL
 * @param new_size New size in bytes
 * @return void* Pointer to the resized memory block, or NULL if failed
 */
void* krealloc(void* ptr, size_t new_size);

/**
 * @brief Get the allocated size of a memory block
 *
 * @param ptr Pointer to memory allocated by kmalloc
 * @return size_t Size of the allocation in bytes
 */
size_t ksize(void* ptr);

/**
 * @brief Print memory allocator statistics
 */
void kmalloc_stats(void);

void* alloc_kernel_stack(void);

char *kstrdup(const char *s);
char *kstrndup(const char *s, size_t max, uint32 gfp);

#endif /* _KMALLOC_H */