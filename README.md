## 主线任务

### 对POSIX支持的远期目标
- [ ] 完善文件局部锁（需要进程的阻塞和事件机制支持）
- [ ] 在dentry.c中做完备的跨挂载点支持
- [ ] dentry的符号链接解析器需要重做，（需要完善的path作为支持）
- [ ] 实现通用的事件队列，并监听dentry的变化
- [ ] 在task_struct和scheduler中加入进程cpu亲和性
- [ ] 重写进程信号量
- [ ] 信号量的队列需要用list重写
- [ ] 实现io多路复用
- [ ] 在fs_struct中初始化mnt_ns


### 目前正在着手的任务
- [x] 在内核页表中分配一整段虚拟内存
- [x] 完善inode  
- [x] 重写inode hash
- [ ] 在路径遍历中处理挂载点
- [ ] 尚未实现vfs文件系统、设备、虚拟标准输入输出表述符、主要系统调用。
- [ ] 在file_open中做文件创建
- [ ] 在`__file_free`中处理f_private字段
- [x] 用嵌入式哈希表重写inode hash
- [x] 实现ext4 inode虚函数表
- [ ] 在初始化ext4 mount时传入ext4全局锁
- [ ] 用riscv-linux-gnu编译lwext4
- [ ] 在lwext4静态库编译中修改malloc为kmalloc
- [ ] 提供一个通用的s_device_id获取方法
- [ ] 在fs-specific动作中设置i_new
- [ ] 在generic_shutdown_super中做inode_free
- [ ] 在vfs_mkdir中，考虑current_task->umask
- [ ] 制作并加载initramfs
- [x] 在superblock中处理ino = 0的acquire请求
- [ ] 补充kernel/console
- [ ] 重写进程信号量和进程信号函数
- [ ] 在dcache中删除dentry
- [ ] 创建成功后再插入哈希表



### 可选任务
- [ ] 在vfs中实现读写锁
- [ ] 在I_FREEING时释放inode
- [ ] 实现懒挂载
- [ ] dentry的快照与缓存一致性
- [ ] 加入一个现代的debugger
- [ ] 在kmalloc中加入gfp_flags
- [ ] 重写上下文服务程序
- [ ] 改造ext4 mount锁（目前是所有mount共享的全局锁）
- [ ] ext4_sync_fs中加入wait支持
- [ ] block_device类异步IO
- [ ] 加入kobject模块
- [ ] 实现namespace虚拟化
- [ ] 在copy_to_user中，检查pagefault disabled
- [ ] 在access_ok中验证用户指针
- [ ] 完善sys_mount中的文件系统特定数据
- [ ] 在kernel blockdevice创建和销毁时，创建和销毁ext4_blockdevice
- [ ] 实现fs_specific_mount fallback
- [ ] 支持negative dentry
- [ ] 在superblock中加入配额quota
- [ ] 在superblock中做active refcount
- [ ] 优化vfs_path_lookup
- [ ] 加入dentry_automount特性
- [ ] A more complex data structure for multiple mount tracking (rather than a simple counter)

## 小任务
- [ ] 把readp和writep分离出去
- [x] file_llseek中没有用到kiocb
- [x] 把命名标准写成文档
- [ ] 为每个cpu准备一个idle task
- [ ] 在user_stack创建和销毁的时候，需要对栈顶-16
- [x] 实现基数树
- [x] 在struct file中，只保留path字段
- [x] 重做使用链表处理碰撞的哈希表
- [x] 实现dentry lru机制
- [x] 设计一个dentry专用的哈希表
- [x] 在dentry中额外维护成员表
- [x] fdtable的size要能动态扩展
- [x] 在kstack启用和销毁的时候预先-16
- [x] 需要实现pid的分配
- [ ] 在inode_create中匹配成员字段
