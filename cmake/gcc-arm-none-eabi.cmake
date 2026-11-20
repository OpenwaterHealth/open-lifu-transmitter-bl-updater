set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

set(CMAKE_C_COMPILER_ID GNU)
set(TOOLCHAIN_PREFIX                arm-none-eabi-)
set(CMAKE_C_COMPILER                ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_ASM_COMPILER              ${CMAKE_C_COMPILER})
set(CMAKE_OBJCOPY                   ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_SIZE                      ${TOOLCHAIN_PREFIX}size)
set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C       ".elf")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# STM32L443: Cortex-M4. Soft-float: the updater has no floating point and needs no FPU set-up.
set(TARGET_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=soft")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${TARGET_FLAGS} -Wall -Wextra -fdata-sections -ffunction-sections")
set(CMAKE_ASM_FLAGS "${TARGET_FLAGS} -x assembler-with-cpp")
set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_C_FLAGS_RELEASE "-Os -g0")

# No C library start-up (src/startup.c); libgcc only for compiler helpers.
set(CMAKE_EXE_LINKER_FLAGS "${TARGET_FLAGS} -nostartfiles -nostdlib -Wl,-Map=${CMAKE_PROJECT_NAME}.map -Wl,--gc-sections -Wl,--print-memory-usage")
set(TOOLCHAIN_LINK_LIBRARIES "gcc")
