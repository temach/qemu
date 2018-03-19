Directory with test files for qemu global register allocation algorithm.

The files are in assembler. Target is arm.
To compile install linaro toolchain, then
"arm-linux-gnueabihf-gcc -static -g -Wall -nostdlib -nostartfiles -x assembler loop-example.asm -o arm-hw"

Compile qemu linux-user-arm and then just to run it use
"../bin/debug/native/arm-linux-user/qemu-arm -d in_asm,cpu,op_opt,out_asm arm-hw 2> arm_static_debug.log"

Or to step the qemu source code in gdb, then use
"gdb --args ../bin/debug/native/arm-linux-user/qemu-arm -d op_opt arm-hw"
and break on tcg_gen_code.

For bigger picture of arm/gnu assembler
https://stackoverflow.com/questions/43574163/why-is-gnu-as-syntax-different-between-x86-and-arm/43576532#43576532
