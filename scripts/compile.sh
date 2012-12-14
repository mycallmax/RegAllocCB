#!/bin/sh
# Put all the *.c in the same directory
# Usage: ./compile.sh <output_name>
LLC=/team/cs-426-stu/liu187/build/Debug+Asserts/bin/llc
LLC_FLAGS="-regalloc=chaitin_briggs -debug -march=mips -print-machineinstrs"
for f in $(ls *.c); do
  b=${f%.c}
  /class/cs426/mips_linux_toolchain/bin/clang -I/class/cs426/mips_linux_toolchain/lib/gcc/mips-linux-gnu/4.4.6/include -I/class/cs426/mips_linux_toolchain/lib/gcc/mips-linux-gnu/4.4.6/include-fixed -I/class/cs426/mips_linux_toolchain/mips-linux-gnu/libc/usr/include  -ccc-host-triple mips-gnu-linux -ccc-clang-archs mips -Wimplicit-int -msoft-float -std=gnu89 -D_MIPS_SZPTR=32 -D_MIPS_SIM=_ABIO32 -emit-llvm -O3 -trigraphs -c $b.c -o $b.bc
	$LLC $LLC_FLAGS $b.bc -o $b.s
done
/class/cs426/mips_linux_toolchain/bin/mips-linux-gnu-gcc -mips32r2 -fPIC *.s -o $1
