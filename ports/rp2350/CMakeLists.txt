cmake_minimum_required(VERSION 3.20)

# family.cmake (included by family_support.cmake) pulls in pico_sdk_init.cmake, which must happen before project()
include(${CMAKE_CURRENT_LIST_DIR}/../family_support.cmake)

project(tinyuf2 C CXX ASM)
set(CMAKE_EXECUTABLE_SUFFIX .elf)

# pico-sdk's toolchain file does not set CMAKE_SIZE, which family_configure_common runs after the build
if (NOT CMAKE_SIZE)
  string(REGEX REPLACE "gcc$" "size" CMAKE_SIZE "${CMAKE_C_COMPILER}")
endif ()

pico_sdk_init()

#------------------------------------
# TinyUF2
#------------------------------------
add_executable(tinyuf2
  board_flash.c
  boards.c
  ${TOP}/lib/tinyusb/src/portable/raspberrypi/rp2040/dcd_rp2040.c
  ${TOP}/lib/tinyusb/src/portable/raspberrypi/rp2040/rp2040_usb.c
  )

# the WS2812 PIO program
pico_generate_pio_header(tinyuf2 ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)

# The bootloader links into the first LD_FLASH_BOOT_SIZE bytes of flash only. pico_override_flash_size() cannot be used
# for that: it also defines PICO_FLASH_SIZE_BYTES, which makes flash_range_erase/program hard_assert() on every offset
# beyond the bootloader, i.e. on every application write. So only the linker region is overridden.
set(PICO_FLASH_SIZE_BYTES_STRING "${LD_FLASH_BOOT_SIZE}")
configure_file(${PICO_SDK_PATH}/src/rp2_common/pico_standard_link/pico_flash_region.template.ld
  ${CMAKE_CURRENT_BINARY_DIR}/tinyuf2/pico_flash_region.ld)
pico_add_linker_script_override_path(tinyuf2 ${CMAKE_CURRENT_BINARY_DIR}/tinyuf2 FILES pico_flash_region.ld)

# drop the SDK runtime parts the bootloader does not use (default alarm pool, printf, float/double, panic, ...)
pico_minimize_runtime(tinyuf2)

# see __wrap_irq_add_shared_handler() in boards.c
target_link_options(tinyuf2 PUBLIC "LINKER:--wrap=irq_add_shared_handler")

# the RP2040 and RP2350 share one TinyUSB device driver (OPT_MCU_RP2040)
family_configure_tinyuf2(tinyuf2 OPT_MCU_RP2040)
family_gen_uf2(tinyuf2 ${UF2_FAMILY_ID})

#------------------------------------
# Application
#------------------------------------
family_add_app(blinky ${TOP}/apps/blinky)
family_add_app(update-tinyuf2 ${TOP}/apps/self_update SELF_UPDATE)
