# 修改: 切换到 RISC-V Linux GNU 交叉编译工具链
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)
set(CMAKE_C_COMPILER riscv64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER riscv64-linux-gnu-g++)
set(CMAKE_ASM_COMPILER riscv64-linux-gnu-gcc)

# 设置编译标志，使其与内核编译标志兼容
# 修改 CMAKE_C_FLAGS，添加更多限制标准库的标志
set(CMAKE_C_FLAGS "-Wall -fno-builtin -nostdlib -nodefaultlibs -nostartfiles -mcmodel=medany -g -Og -std=gnu99 -fno-delete-null-pointer-checks")

# 项目声明
project(lwext4 C)
cmake_minimum_required(VERSION 3.4)

# 设置构建类型为静态库
set(BUILD_SHARED_LIBS OFF)

include_directories(include)
include_directories(${PROJECT_BINARY_DIR}/include)

# 移除不需要的块设备，使用 none 类型
set(BLOCKDEV_TYPE none)

# 配置选项
add_definitions(-DCONFIG_USE_DEFAULT_CONFIG=0)
add_definitions(-DVERSION="${VERSION}")
# 添加适用于 RISC-V 的特殊定义
add_definitions(-DCONFIG_HAVE_OWN_OFLAGS=1)
add_definitions(-DCONFIG_BLOCK_DEV_CACHE_SIZE=0)
add_definitions(-DCONFIG_BLOCK_DEV_ENABLE_STATS=0)
# 添加以下定义来禁用标准库依赖
add_definitions(-DCONFIG_HAVE_OWN_ERRNO=0)
add_definitions(-DCONFIG_DEBUG_ASSERT=0)
add_definitions(-DCONFIG_HAVE_OWN_ASSERT=0)
add_definitions(-DCONFIG_DEBUG_PRINTF=0)
add_definitions(-DCONFIG_DEBUG=0)
# 禁用fortify source
add_definitions(-U_FORTIFY_SOURCE)
add_definitions(-D_FORTIFY_SOURCE=0)

# 禁用不需要的功能
option(LWEXT4_BUILD_EXAMPLES "Build examples" OFF)
option(LWEXT4_BUILD_TESTS "Build tests" OFF)

# ==============================================================================================

# 仅构建核心源代码
add_subdirectory(src)

# 配置警告选项
if (NOT CMAKE_COMPILER_IS_GNUCC)
    set_target_properties(lwext4 PROPERTIES COMPILE_FLAGS "")
else()
    set_target_properties(lwext4 PROPERTIES COMPILE_FLAGS "-Wall -Wextra")
endif()

# 指定输出静态库的位置和名称
set_target_properties(lwext4 PROPERTIES 
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    OUTPUT_NAME "lwext4"
)

# DISTRIBUTION 相关设置保持不变
set(CPACK_PACKAGE_VERSION_MAJOR "${VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${VERSION_PATCH}")
set(CPACK_SOURCE_GENERATOR "TBZ2")
set(CPACK_SOURCE_PACKAGE_FILE_NAME
  "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(CPACK_SOURCE_IGNORE_FILES
"/build" ".git")
include(CPack)

add_custom_target(dist COMMAND ${CMAKE_MAKE_PROGRAM} package_source)