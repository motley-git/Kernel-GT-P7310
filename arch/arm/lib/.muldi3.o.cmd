cmd_arch/arm/lib/muldi3.o := /home/cainm/workspace/platform/android-sdk-linux_x86/android-ndk-r5c/toolchains/arm-eabi-4.4.0/prebuilt/linux-x86/bin/arm-eabi-gcc -Wp,-MD,arch/arm/lib/.muldi3.o.d  -nostdinc -isystem /home/cainm/workspace/platform/android-sdk-linux_x86/android-ndk-r5c/toolchains/arm-eabi-4.4.0/prebuilt/linux-x86/bin/../lib/gcc/arm-eabi/4.4.0/include -I/home/cainm/workspace/7310/kernel/arch/arm/include -Iinclude  -include include/generated/autoconf.h -D__KERNEL__ -mlittle-endian -Iarch/arm/mach-tegra/include -D__ASSEMBLY__ -mabi=aapcs-linux -mno-thumb-interwork -funwind-tables  -D__LINUX_ARM_ARCH__=7 -march=armv7-a  -include asm/unified.h -msoft-float -gdwarf-2        -c -o arch/arm/lib/muldi3.o arch/arm/lib/muldi3.S

deps_arch/arm/lib/muldi3.o := \
  arch/arm/lib/muldi3.S \
  /home/cainm/workspace/7310/kernel/arch/arm/include/asm/unified.h \
    $(wildcard include/config/arm/asm/unified.h) \
    $(wildcard include/config/thumb2/kernel.h) \
  include/linux/linkage.h \
  include/linux/compiler.h \
    $(wildcard include/config/trace/branch/profiling.h) \
    $(wildcard include/config/profile/all/branches.h) \
    $(wildcard include/config/enable/must/check.h) \
    $(wildcard include/config/enable/warn/deprecated.h) \
  /home/cainm/workspace/7310/kernel/arch/arm/include/asm/linkage.h \

arch/arm/lib/muldi3.o: $(deps_arch/arm/lib/muldi3.o)

$(deps_arch/arm/lib/muldi3.o):
