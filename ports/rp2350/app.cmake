include_guard(GLOBAL)

# applications are standalone pico-sdk projects: pico_sdk_init() must come after their project() and before their targets
enable_language(CXX)
if (NOT CMAKE_SIZE)
  string(REGEX REPLACE "gcc$" "size" CMAKE_SIZE "${CMAKE_C_COMPILER}")
endif ()
pico_sdk_init()

function(family_configure_app TARGET)
  family_configure_common(${TARGET})

  # boards.c is compiled into the application too, it drives the WS2812
  pico_generate_pio_header(${TARGET} ${FAMILY_PATH}/ws2812.pio)

  # link behind the bootloader: the flash region starts at the application start
  configure_file(${FAMILY_PATH}/linker/pico_flash_region_app.template.ld
    ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}/pico_flash_region.ld @ONLY)
  pico_add_linker_script_override_path(${TARGET} ${CMAKE_CURRENT_BINARY_DIR}/${TARGET} FILES pico_flash_region.ld)
endfunction()
