cmake_minimum_required(VERSION 3.22)

# open-lifu-transmitter-bl-updater
#
# A one-shot application for the Open-LIFU TRANSMITTER (STM32L443RC) that replaces the
# secure bootloader (pages 0-31) from inside the application slot, then resets into the
# new bootloader's DFU (USB on a master, I2C on a slave module). It targets research
# units on open-lifu-transmitter-bl 1.0.x (RDP level 0, no write protection) and is
# signed with the research key those bootloaders trust; the embedded bootloader (1.1.x
# research build) trusts the same key.
#
# Build inputs:
#   BL_IMAGE  the bootloader binary to embed (open-lifu-transmitter-bl.bin from its release)
#   BL_TAG    that bootloader's release tag (identification string and SBOM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

set(UPDATER_PRODUCT transmitter)
set(CMAKE_PROJECT_NAME open-lifu-${UPDATER_PRODUCT}-bl-updater)
project(${CMAKE_PROJECT_NAME} C ASM)

set(BL_IMAGE "" CACHE FILEPATH "Bootloader binary to embed (required)")
set(BL_TAG   "" CACHE STRING   "Bootloader release tag of BL_IMAGE")
if(NOT EXISTS "${BL_IMAGE}")
    message(FATAL_ERROR "BL_IMAGE not found: '${BL_IMAGE}'. Pass -DBL_IMAGE=<open-lifu-transmitter-bl.bin> -DBL_TAG=<tag>")
endif()
if(BL_TAG STREQUAL "")
    message(FATAL_ERROR "BL_TAG is required (the release tag of BL_IMAGE)")
endif()
file(SIZE   "${BL_IMAGE}" BL_IMAGE_SIZE)
file(SHA256 "${BL_IMAGE}" BL_IMAGE_SHA256)
if(BL_IMAGE_SIZE GREATER 65536 OR BL_IMAGE_SIZE LESS 1024)
    message(FATAL_ERROR "BL_IMAGE size ${BL_IMAGE_SIZE} B does not fit the 64 KB bootloader region")
endif()
get_filename_component(BL_IMAGE_ABS "${BL_IMAGE}" ABSOLUTE)
file(TO_CMAKE_PATH "${BL_IMAGE_ABS}" BL_IMAGE_ABS)

find_package(Git QUIET)
set(UPDATER_GIT_DESCRIBE "unknown")
set(UPDATER_GIT_SHA "unknown")
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --always
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                    OUTPUT_VARIABLE UPDATER_GIT_DESCRIBE OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                    OUTPUT_VARIABLE UPDATER_GIT_SHA OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(UPDATER_GIT_DESCRIBE STREQUAL "")
    set(UPDATER_GIT_DESCRIBE "unknown")
endif()

configure_file(${CMAKE_SOURCE_DIR}/include/updater_config.h.in ${CMAKE_BINARY_DIR}/generated/updater_config.h @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/src/bl_blob.s.in          ${CMAKE_BINARY_DIR}/generated/bl_blob.s          @ONLY)

add_executable(${CMAKE_PROJECT_NAME}
    src/startup.c
    src/board.c
    src/flash_ram.c
    src/main.c
    ${CMAKE_BINARY_DIR}/generated/bl_blob.s
)
set_source_files_properties(${CMAKE_BINARY_DIR}/generated/bl_blob.s PROPERTIES OBJECT_DEPENDS "${BL_IMAGE_ABS}")

# The RAM-resident code must not call anything in flash, including compiler-emitted
# memcpy/memset or jump tables (checked by scripts/check_ramfunc.sh).
set_source_files_properties(src/flash_ram.c PROPERTIES
    COMPILE_OPTIONS "-fno-builtin;-fno-tree-loop-distribute-patterns;-fno-jump-tables")

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    include cmsis/core cmsis/device ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE STM32L443xx $<$<CONFIG:Debug>:DEBUG>)

set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/linker/updater_slot.ld")
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -T "${LINKER_SCRIPT}" -Wl,--no-warn-rwx-segments)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES LINK_DEPENDS "${LINKER_SCRIPT}")
target_link_libraries(${CMAKE_PROJECT_NAME} ${TOOLCHAIN_LINK_LIBRARIES})

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    COMMENT "Updater image embedding open-lifu-transmitter-bl ${BL_TAG} (${BL_IMAGE_SIZE} B, sha256 ${BL_IMAGE_SHA256})"
)

message(STATUS "Updater    : ${CMAKE_PROJECT_NAME} ${UPDATER_GIT_DESCRIBE} (${UPDATER_GIT_SHA}), ${CMAKE_BUILD_TYPE}")
message(STATUS "Bootloader : ${BL_TAG} ${BL_IMAGE_ABS} (${BL_IMAGE_SIZE} B, sha256 ${BL_IMAGE_SHA256})")
