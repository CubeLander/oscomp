cmake_minimum_required(VERSION 3.10)
project(musl C)

# 设置输出目录
set(MUSL_OUTPUT_DIR ${CMAKE_BINARY_DIR}/lib)

# 从原来的构建命令转换
include(ExternalProject)

# 获取处理器核心数
execute_process(COMMAND nproc OUTPUT_VARIABLE NUM_PROCESSORS OUTPUT_STRIP_TRAILING_WHITESPACE)

# 创建简化的构建脚本，直接编译到目标位置
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/build_musl.sh
"#!/bin/bash
set -ex

# 进入源码目录
cd \"${CMAKE_CURRENT_SOURCE_DIR}\"

# 确保输出目录存在
mkdir -p \"${MUSL_OUTPUT_DIR}\"

# 配置musl - 注意：我们仍然需要一个临时安装目录来获取头文件
TEMP_INSTALL_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/temp_install\"
mkdir -p \"$TEMP_INSTALL_DIR\"

# 配置musl
echo \"配置 musl...\"
./configure \\
  --prefix=\"$TEMP_INSTALL_DIR\" \\
  --target=riscv64-unknown-elf \\
  --disable-shared \\
  CFLAGS=\"-g3 -O0 -fno-omit-frame-pointer -mabi=lp64d\" \\
  CC=riscv64-unknown-elf-gcc

# 构建musl
echo \"构建 musl...\"
make clean
make -j${NUM_PROCESSORS}

# 直接复制库文件到目标位置而不是安装
echo \"复制 musl 库到目标位置...\"
cp lib/libc.a \"${MUSL_OUTPUT_DIR}/libc.a\"

# 安装头文件到临时目录
make install

echo \"musl构建完成\"
")

# 设置脚本权限
execute_process(COMMAND chmod +x ${CMAKE_CURRENT_BINARY_DIR}/build_musl.sh)

# 简化ExternalProject配置，只使用一个脚本处理全部步骤
ExternalProject_Add(musl_build
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    DOWNLOAD_COMMAND ""
    CONFIGURE_COMMAND ""  # 不使用单独的配置命令
    BUILD_COMMAND ${CMAKE_CURRENT_BINARY_DIR}/build_musl.sh  # 使用我们的构建脚本
    INSTALL_COMMAND ""  # 不使用单独的安装命令
    BUILD_IN_SOURCE 0  # 不使用build-in-source，我们的脚本自己会进入源码目录
    LOG_BUILD TRUE     # 记录构建日志
)

# 创建一个接口库，以便在CMake中使用
add_library(musl_lib INTERFACE)
add_dependencies(musl_lib musl_build)

# 设置包含目录和库路径
target_include_directories(musl_lib INTERFACE 
    ${CMAKE_CURRENT_BINARY_DIR}/temp_install/include
)

target_link_directories(musl_lib INTERFACE 
    ${MUSL_OUTPUT_DIR}
)

# 将头文件安装到主项目的include目录
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/temp_install/include/
        DESTINATION ${CMAKE_SOURCE_DIR}/include/musl
        FILES_MATCHING PATTERN "*.h")

# 添加自定义目标来执行清理
add_custom_target(clean_musl
    COMMAND rm -rf ${CMAKE_CURRENT_BINARY_DIR}/temp_install
    COMMAND rm -f ${MUSL_OUTPUT_DIR}/libc.a
    COMMAND cd ${CMAKE_CURRENT_SOURCE_DIR} && make clean
    COMMENT "Cleaning musl build directories"
)

# 导出输出目录
set(MUSL_OUTPUT_DIR ${MUSL_OUTPUT_DIR} PARENT_SCOPE)