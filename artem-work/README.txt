Directory with test files for qemu global register allocation algorithm.

The files are in assembler. Target is arm.
download pre-compiled linaro arm toolchain from here
"https://releases.linaro.org/components/toolchain/binaries/latest/arm-linux-gnueabihf/gcc-linaro-7.2.1-2017.11-x86_64_arm-linux-gnueabihf.tar.xz"

To compile the arm binary add toolchain's "/bin" to your PATH and run
"arm-linux-gnueabihf-gcc -static -g -Wall -nostdlib -nostartfiles -x assembler loop-example.asm -o arm-hw"

Compile qemu linux-user-arm with the following configuration (to enable gdb debug later)
"../../../configure --enable-debug-tcg --enable-debug --disable-strip --target-list=arm-linux-user"

and then just to run our simple arm program use
"../bin/debug/native/arm-linux-user/qemu-arm -d in_asm,cpu,op_opt,out_asm arm-hw 2> arm_static_debug.log"

Or to step through the qemu source code in gdb use
"gdb --args ../bin/debug/native/arm-linux-user/qemu-arm -d op_opt arm-hw"
and break on tcg_gen_code.

For bigger picture of arm/gnu assembler
https://stackoverflow.com/questions/43574163/why-is-gnu-as-syntax-different-between-x86-and-arm/43576532#43576532
