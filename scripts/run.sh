#!/bin/sh
# Usage: ./run.sh <executable> <arg1> <arg2> ... <argN>
/class/cs426/mips_linux_toolchain/bin/qemu-mips -L /class/cs426/mips_linux_toolchain/sysroot/ $@
