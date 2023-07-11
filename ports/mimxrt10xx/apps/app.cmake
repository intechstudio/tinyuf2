set(APPLICATION_ADDR 0x6000C000)

# override bin hex output
function(family_add_bin_hex TARGET)
  add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary -R .flash_config -R .ivt $<TARGET_FILE:${TARGET}> $<TARGET_FILE_DIR:${TARGET}>/${TARGET}.bin
    COMMAND ${CMAKE_OBJCOPY} -Oihex -R .flash_config -R .ivt $<TARGET_FILE:${TARGET}> $<TARGET_FILE_DIR:${TARGET}>/${TARGET}.hex
    VERBATIM)
endfunction()

function(configure_app TARGET)
  target_link_options(${TARGET} PUBLIC
    "LINKER:--script=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../linker/${MCU_VARIANT}_ram.ld"
    "LINKER:--script=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/memory.ld"
    "LINKER:--script=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../linker/common.ld"
    )

  family_add_uf2(${TARGET} ${UF2_FAMILY_ID})
  family_flash_uf2(${TARGET} ${UF2_FAMILY_ID})
endfunction()
