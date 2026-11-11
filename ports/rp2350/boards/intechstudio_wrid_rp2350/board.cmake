# flash size of the board in bytes. TODO: confirm
set(LD_FLASH_SIZE 2097152)

# flash reserved for the bootloader, in bytes (32 KB for now: 16 KB does not fit the full feature set)
if (NOT DEFINED LD_FLASH_BOOT_SIZE)
  set(LD_FLASH_BOOT_SIZE 32768)
endif ()

function(update_board TARGET)
  target_compile_definitions(${TARGET} INTERFACE
    BOARD_FLASH_SIZE=${LD_FLASH_SIZE}
    BOARD_FLASH_BOOT_SIZE=${LD_FLASH_BOOT_SIZE}
    )
endfunction()
