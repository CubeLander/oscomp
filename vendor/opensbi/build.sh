#!/bin/bash
make clean
#make PLATFORM=generic PLATFORM_RISCV_XLEN=64 PLATFORM_RISCV_LP64=y CROSS_COMPILE=riscv64-linux-gnu- CFLAGS="-O0 -g3" -j$(nproc)
make PLATFORM=generic CROSS_COMPILE=riscv64-linux-gnu- DEBUG=1 -j$(nproc)
# 在opensbi的makefile中加入了两个DEBUG标志