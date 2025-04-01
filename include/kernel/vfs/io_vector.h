#ifndef _IO_VECTOR_H
#define _IO_VECTOR_H

#include "forward_declarations.h"

/**
 * struct io_vector - Describes a memory buffer for vectored I/O
 * @iov_base: Starting address of buffer
 * @iov_len: Size of buffer in bytes
 */
struct io_vector {
    void *iov_base;      /* Starting address */
    size_t iov_len;      /* Number of bytes to transfer */
};


// Memory management
void io_vector_init(struct io_vector *vec, void *base, size_t len);
int32 io_vector_allocate(struct io_vector *vec, size_t size);
void io_vector_free(struct io_vector *vec);

// Basic I/O
ssize_t io_vector_read_from(struct io_vector *vec, struct file *file, loff_t *pos);
ssize_t io_vector_write_to(struct io_vector *vec, struct file *file, loff_t *pos);
// State queries
size_t io_vector_length(const struct io_vector *vec);
void *io_vector_base(const struct io_vector *vec);

/**
 * struct io_vector_iterator - Iterator for working with I/O vectors
 */
struct io_vector_iterator {
	struct io_vector *iov_base;  /* 数组基地址 */
    uint64 index;         /* 当前索引 */
    uint64 nr_segs;  /* Number of segments */
    size_t iov_offset;   /* Offset within current io_vector */
    size_t count;        /* Total bytes remaining */
};




// Iterator operations
int32 setup_io_vector_iterator(struct io_vector_iterator *iter, const struct io_vector *vec, uint64 vlen);
size_t io_vector_iterator_copy_from(struct io_vector_iterator *iter, void *kaddr, size_t len);
size_t io_vector_iterator_copy_to(struct io_vector_iterator *iter, const void *kaddr, size_t len);
void io_vector_iterator_advance(struct io_vector_iterator *iter, size_t bytes);
void io_vector_iterator_rewind(struct io_vector_iterator *iter);


uint64 io_vector_segment_count(const struct io_vector_iterator *iter);
size_t io_vector_remaining(const struct io_vector_iterator *iter);



#endif /* _IO_VECTOR_H */




// 解耦为单独的 `IOVector` 类是个好主意。这个类应该提供以下方法：

// ## IOVector 类核心方法

// 1. **内存管理方法**
//    - `allocate(size_t size)` - 分配指定大小的缓冲区
//    - `attach(void* buffer, size_t size)` - 关联外部缓冲区
//    - `release()` - 释放资源

// 2. **I/O 操作方法**
//    - `readFrom(struct file* file, loff_t* pos)` - 从文件读取数据
//    - `writeTo(struct file* file, loff_t* pos)` - 写入数据到文件
//    - `copy(IOVector* dest)` - 在向量间复制数据

// 3. **迭代器支持**
//    - `createIterator()` - 创建新迭代器
//    - `resetIterator(IOVectorIterator* iter)` - 重置迭代器

// 4. **状态查询**
//    - `getTotalSize()` - 返回总数据大小
//    - `getSegmentCount()` - 返回段数量
//    - `isEmpty()` - 检查是否为空

// 这种设计实现了关注点分离，让向量化 I/O 的实现独立于文件抽象，并可复用于其他 I/O 场景。