cmake_minimum_required(VERSION 3.10)
project(busybox C)

# BusyBox构建配置
set(BUSYBOX_INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/install)

# 设置RISCV交叉编译
set(CROSS_COMPILE "riscv64-linux-gnu-")
set(ARCH "riscv")

include(ExternalProject)

# 创建一个脚本来执行配置步骤
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/configure_busybox.sh
"#!/bin/bash
set -e
cd ${CMAKE_CURRENT_SOURCE_DIR}
make defconfig
# 设置交叉编译前缀
sed -i 's/CONFIG_CROSS_COMPILER_PREFIX=\"\"/CONFIG_CROSS_COMPILER_PREFIX=\"riscv64-linux-gnu-\"/' .config
# 设置静态构建
sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
# 删除和添加TC配置
sed -i '/CONFIG_TC=/d' .config 
echo 'CONFIG_TC=n' >> .config
# 禁用硬件加速
sed -i 's/.*CONFIG_SHA1_HWACCEL.*/# CONFIG_SHA1_HWACCEL is not set/' .config
sed -i 's/.*CONFIG_SHA256_HWACCEL.*/# CONFIG_SHA256_HWACCEL is not set/' .config
")

# 创建一个脚本用于创建符号链接
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/create_symlinks.sh
"#!/bin/bash
set -e
INSTALL_DIR=\"${BUSYBOX_INSTALL_DIR}\"
mkdir -p \"$INSTALL_DIR/bin\"
mkdir -p \"$INSTALL_DIR/sbin\"
mkdir -p \"$INSTALL_DIR/usr/bin\"
mkdir -p \"$INSTALL_DIR/usr/sbin\"

# 创建busybox程序的各种applet符号链接
if [ -f \"$INSTALL_DIR/bin/busybox\" ]; then
    cd \"$INSTALL_DIR/bin\"
    ./busybox --list | xargs -I {} ln -sf busybox {}
fi
")

# 创建一个脚本来设置基本配置文件
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/create_configs.sh
"#!/bin/bash
set -e
INSTALL_DIR=\"${BUSYBOX_INSTALL_DIR}\"
mkdir -p \"$INSTALL_DIR/etc\"
mkdir -p \"$INSTALL_DIR/etc/init.d\"

# 创建基本配置文件
echo 'root::0:0:root:/root:/bin/sh' > \"$INSTALL_DIR/etc/passwd\"
echo 'root:x:0:' > \"$INSTALL_DIR/etc/group\"
echo '::sysinit:/etc/init.d/rcS' > \"$INSTALL_DIR/etc/inittab\"
echo '::respawn:-/bin/sh' >> \"$INSTALL_DIR/etc/inittab\"
echo '::restart:/sbin/init' >> \"$INSTALL_DIR/etc/inittab\"
echo '::shutdown:/bin/umount -a -r' >> \"$INSTALL_DIR/etc/inittab\"

# 创建启动脚本
echo '#!/bin/sh' > \"$INSTALL_DIR/etc/init.d/rcS\"
echo 'mount -t proc none /proc' >> \"$INSTALL_DIR/etc/init.d/rcS\"
echo 'mount -t sysfs none /sys' >> \"$INSTALL_DIR/etc/init.d/rcS\"
echo 'mount -t devtmpfs none /dev' >> \"$INSTALL_DIR/etc/init.d/rcS\"
chmod +x \"$INSTALL_DIR/etc/init.d/rcS\"
")

# 设置脚本权限
execute_process(COMMAND chmod +x ${CMAKE_CURRENT_BINARY_DIR}/configure_busybox.sh)
execute_process(COMMAND chmod +x ${CMAKE_CURRENT_BINARY_DIR}/create_symlinks.sh)
execute_process(COMMAND chmod +x ${CMAKE_CURRENT_BINARY_DIR}/create_configs.sh)

# 配置BusyBox构建 - 使用脚本来避免sed表达式问题
ExternalProject_Add(busybox_build
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    
    # 使用脚本进行配置
    CONFIGURE_COMMAND ${CMAKE_CURRENT_BINARY_DIR}/configure_busybox.sh
    
    # 清理并构建BusyBox
    BUILD_COMMAND make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} clean
                 COMMAND make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -j$(nproc)
    
    # 安装到指定目录
    INSTALL_COMMAND make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} 
                     CONFIG_PREFIX=${BUSYBOX_INSTALL_DIR} install
    
    BUILD_ALWAYS FALSE  # 只在源文件更改时重新构建
    BUILD_IN_SOURCE 1   # 在源目录构建
)

# 创建符号链接和配置文件
add_custom_command(
    TARGET busybox_build
    POST_BUILD
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/create_symlinks.sh
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/create_configs.sh
    COMMENT "Creating BusyBox symlinks and configuration files"
)

# 导出安装目录路径，以便在父CMakeLists.txt中使用
set(BUSYBOX_INSTALL_DIR ${BUSYBOX_INSTALL_DIR} PARENT_SCOPE)

# 添加清理目标
add_custom_target(clean_busybox
    COMMAND make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} clean
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${BUSYBOX_INSTALL_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Cleaning BusyBox build directories"
)