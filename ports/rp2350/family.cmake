include_guard(GLOBAL)

#------------------------------------
# Config
#------------------------------------

# RP2350_ARM_S_FAMILY_ID in pico-sdk's boot/uf2.h
set(UF2_FAMILY_ID 0xe48bff59)

set(PICO_PLATFORM rp2350)
# uf2 is made by family_gen_uf2, not by the SDK
set(PICO_NO_UF2 1)
# nothing here post-processes with picotool, and without an installed one the SDK would fetch and build it on every build
set(PICO_NO_PICOTOOL 1)
# the bootloader has no secure gateway functions, so build without -mcmse (as the RP2350 bootrom itself does)
set(PICO_NO_CMSE 1)

# pico-sdk: -DPICO_SDK_PATH, else the environment, else the copy tools/get_deps.py fetches
if (NOT PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
  set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif ()
if (NOT PICO_SDK_PATH)
  set(PICO_SDK_PATH ${TOP}/lib/mcu/raspberrypi/pico-sdk)
endif ()
if (NOT EXISTS ${PICO_SDK_PATH}/pico_sdk_init.cmake)
  message(FATAL_ERROR "pico-sdk not found at ${PICO_SDK_PATH}: run python3 tools/get_deps.py rp2350, "
                      "or pass -DPICO_SDK_PATH=<pico-sdk>")
endif ()

# pico-sdk's tinyusb glue includes TinyUSB's own family_support.cmake, whose functions (family_configure_common ...)
# would replace TinyUF2's. TinyUF2 builds lib/tinyusb itself, so point the SDK at a path without hw/bsp/rp2040:
# it then skips its TinyUSB support (and prints a warning saying so).
set(PICO_TINYUSB_PATH ${CMAKE_BINARY_DIR}/no-sdk-tinyusb)

include(${CMAKE_CURRENT_LIST_DIR}/boards/${BOARD}/board.cmake)

# pico-sdk selects the toolchain, no CMAKE_TOOLCHAIN_FILE here
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

#------------------------------------
# BOARD_TARGET
#------------------------------------
function(family_add_board_target BOARD_TARGET)
  if (TARGET ${BOARD_TARGET})
    return()
  endif ()

  add_library(${BOARD_TARGET} INTERFACE)
  target_include_directories(${BOARD_TARGET} INTERFACE
    ${CMAKE_CURRENT_FUNCTION_LIST_DIR}
    ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/boards/${BOARD}
    )
  target_link_libraries(${BOARD_TARGET} INTERFACE
    pico_runtime
    pico_standard_link
    hardware_clocks
    hardware_flash
    hardware_gpio
    hardware_irq
    hardware_pio
    hardware_resets
    hardware_structs
    pico_sync
    )

  update_board(${BOARD_TARGET})

  target_compile_definitions(${BOARD_TARGET} INTERFACE
    BOARD_UF2_FAMILY_ID=${UF2_FAMILY_ID}
    )
endfunction()
