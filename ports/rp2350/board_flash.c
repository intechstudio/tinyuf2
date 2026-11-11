// RP2350 flash access through the bootrom flash API (pico-sdk hardware_flash).
//
// TinyUF2 hands over 256 byte UF2 payloads in any order and neither erases nor range checks them, so this file:
// - refuses everything below the application start (the bootloader) and above the flash,
// - erases a 4 KB sector only when a row to write is not blank, and writes back the rows of that sector known to be good.
#include "board_api.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/bootrom.h"
#include "boot/picobin.h"
#include "boot/picoboot_constants.h"

//--------------------------------------------------------------------+
// MACRO TYPEDEF CONSTANT ENUM
//--------------------------------------------------------------------+

// flash is erased in sectors and programmed in pages, a UF2 payload is one page
#define ERASE_SIZE   FLASH_SECTOR_SIZE
#define PROGRAM_SIZE FLASH_PAGE_SIZE

enum {
  BLOCK_COUNT    = BOARD_FLASH_SIZE / ERASE_SIZE,
  ROWS_PER_BLOCK = ERASE_SIZE / PROGRAM_SIZE
};
_Static_assert(ROWS_PER_BLOCK <= 16, "good_rows is a 16 bit mask");

// rows of each block known to hold their data, written or found identical since the flash was initialized.
// They are written back when the block has to be erased.
static uint16_t good_rows[BLOCK_COUNT];
// holds the rows to keep while their block is erased
static uint8_t block_cache[ERASE_SIZE] __attribute__((aligned(4)));

//--------------------------------------------------------------------+
// Internal Helper
//--------------------------------------------------------------------+
static bool is_blank(uint32_t addr, uint32_t size) {
  const volatile uint32_t* flash = (const volatile uint32_t*) addr;
  for (uint32_t i = 0; i < size / sizeof(uint32_t); i++) {
    if (flash[i] != 0xffffffff) {
      return false;
    }
  }
  return true;
}

static bool is_equal(uint32_t addr, const uint8_t* data, uint32_t size) {
  return memcmp((const void*) addr, data, size) == 0;
}

static bool is_valid_row(uint32_t addr, uint32_t len) {
  return len == PROGRAM_SIZE && (addr % PROGRAM_SIZE) == 0 && addr >= BOARD_FLASH_APP_START &&
         addr <= BOARD_FLASH_ADDR_ZERO + BOARD_FLASH_SIZE - len;
}

// The bootrom flash functions take the flash out of XIP mode, so no code or interrupt handler may run from flash
// meanwhile: keep interrupts off for the duration.
static void flash_erase(uint32_t block) {
  const uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(block * ERASE_SIZE, ERASE_SIZE);
  restore_interrupts(ints);
}

static void flash_program(uint32_t offset, const uint8_t* src, uint32_t len) {
  const uint32_t ints = save_and_disable_interrupts();
  flash_range_program(offset, src, len);
  restore_interrupts(ints);
}

// Erase a block, writing back the rows to keep
static bool flash_erase_block_keep_rows(uint32_t block, uint32_t keep) {
  const uint32_t base = BOARD_FLASH_ADDR_ZERO + block * ERASE_SIZE;

  for (uint32_t row = 0; row < ROWS_PER_BLOCK; row++) {
    if (keep & (1u << row)) {
      memcpy(block_cache + row * PROGRAM_SIZE, (const void*) (base + row * PROGRAM_SIZE), PROGRAM_SIZE);
    }
  }

  flash_erase(block);
  TUF2_VERIFY(is_blank(base, ERASE_SIZE));

  for (uint32_t row = 0; row < ROWS_PER_BLOCK; row++) {
    if (keep & (1u << row)) {
      flash_program(block * ERASE_SIZE + row * PROGRAM_SIZE, block_cache + row * PROGRAM_SIZE, PROGRAM_SIZE);
    }
  }
  good_rows[block] = (uint16_t) keep;
  return true;
}

//--------------------------------------------------------------------+
// Board API
//--------------------------------------------------------------------+
void board_flash_init(void) {
}

uint32_t board_flash_size(void) {
  return BOARD_FLASH_SIZE;
}

void board_flash_read(uint32_t addr, void* buffer, uint32_t len) {
  memcpy(buffer, (const void*) addr, len);
}

void board_flash_flush(void) {
  // rows are programmed as they arrive
}

bool board_flash_write(uint32_t addr, void const* data, uint32_t len) {
  TUF2_VERIFY(is_valid_row(addr, len));

  const uint32_t offset  = addr - BOARD_FLASH_ADDR_ZERO;
  const uint32_t block   = offset / ERASE_SIZE;
  const uint16_t row_bit = (uint16_t) (1u << ((offset % ERASE_SIZE) / PROGRAM_SIZE));

  // a UF2 block written twice, or the flash already holding the data
  if (is_equal(addr, data, len)) {
    good_rows[block] |= row_bit;
    return true;
  }

  if (!is_blank(addr, len)) {
    TUF2_VERIFY(flash_erase_block_keep_rows(block, good_rows[block] & ~row_bit));
  }

  flash_program(offset, data, len);
  TUF2_VERIFY(is_equal(addr, data, len));
  good_rows[block] |= row_bit;
  return true;
}

void board_flash_erase_app(void) {
  // erasing the first block of the app is enough to invalidate it
  const uint32_t block = (BOARD_FLASH_APP_START - BOARD_FLASH_ADDR_ZERO) / ERASE_SIZE;
  flash_erase(block);
  good_rows[block] = 0;
}

bool board_flash_protect_bootloader(bool protect) {
  // M6: flash chip block protect. Until then writes below BOARD_FLASH_APP_START are refused by board_flash_write()
  (void) protect;
  return false;
}

#ifdef TINYUF2_SELF_UPDATE

static bool is_all_ff(const uint8_t* data, uint32_t size) {
  for (uint32_t i = 0; i < size; i++) {
    if (data[i] != 0xff) {
      return false;
    }
  }
  return true;
}

static bool is_new_bootloader_valid(const uint8_t* bootloader_bin, uint32_t bootloader_len) {
  // at least larger than the vector table, and must leave the application untouched
  if (bootloader_len < 1024 || bootloader_len > BOARD_FLASH_BOOT_SIZE) {
    return false;
  }

  const uint32_t* word  = (const uint32_t*) (uintptr_t) bootloader_bin;
  const uint32_t  sp    = word[0];
  const uint32_t  entry = word[1];

  // 1st word is the stack pointer (must be in SRAM), the 2nd the reset handler: a Thumb address inside the image
  if ((sp & 0xff000003u) != 0x20000000u) {
    return false;
  }
  if ((entry & 1u) == 0 || entry < BOARD_FLASH_ADDR_ZERO || entry >= BOARD_FLASH_ADDR_ZERO + bootloader_len) {
    return false;
  }

  // the bootrom only boots an image with an IMAGE_DEF block, which it looks for in the first 4 KB
  const uint32_t search_words = (bootloader_len < 4096 ? bootloader_len : 4096) / sizeof(uint32_t);
  for (uint32_t i = 0; i < search_words; i++) {
    if (word[i] == PICOBIN_BLOCK_MARKER_START) {
      return true;
    }
  }
  return false;
}

// Fill block_cache with what a block of the bootloader area must hold: its part of the new bootloader, then blank
static uint32_t bootloader_block_fill(uint32_t block, const uint8_t* bootloader_bin, uint32_t bootloader_len) {
  const uint32_t offset = block * ERASE_SIZE;
  uint32_t       size   = 0;
  if (offset < bootloader_len) {
    size = ((bootloader_len - offset) < ERASE_SIZE) ? (bootloader_len - offset) : ERASE_SIZE;
  }

  memset(block_cache, 0xff, sizeof(block_cache));
  memcpy(block_cache, bootloader_bin + offset, size);
  return BOARD_FLASH_ADDR_ZERO + offset;
}

// Make a block of the bootloader area hold its part of the new bootloader and nothing else
static bool bootloader_block_update(uint32_t block, const uint8_t* bootloader_bin, uint32_t bootloader_len) {
  const uint32_t base = bootloader_block_fill(block, bootloader_bin, bootloader_len);
  if (is_equal(base, block_cache, ERASE_SIZE)) {
    return true;
  }

  if (!is_blank(base, ERASE_SIZE)) {
    flash_erase(block);
    TUF2_VERIFY(is_blank(base, ERASE_SIZE));
  }

  for (uint32_t row = 0; row < ROWS_PER_BLOCK; row++) {
    if (!is_all_ff(block_cache + row * PROGRAM_SIZE, PROGRAM_SIZE)) {
      flash_program(block * ERASE_SIZE + row * PROGRAM_SIZE, block_cache + row * PROGRAM_SIZE, PROGRAM_SIZE);
    }
  }
  return is_equal(base, block_cache, ERASE_SIZE);
}

// Runs from RAM: erases the first block of the application, which is the code that is running, then reboots.
// Interrupts are off, and nothing in flash may be called once the erase has started.
static void __no_inline_not_in_flash_func(erase_app_start_and_reboot)(uint32_t offset) {
  flash_range_erase(offset, ERASE_SIZE);
  rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_NORMAL, 10, 0, 0);
  while (1) {}
}

void board_self_update(const uint8_t* bootloader_bin, uint32_t bootloader_len) {
  // On failure this application stays in place so the update can be tried again
  if (!is_new_bootloader_valid(bootloader_bin, bootloader_len)) {
    TUF2_LOG1("Invalid bootloader image\r\n");
    return;
  }

  // The IMAGE_DEF the bootrom boots from sits in block 0. Invalidate it first and rewrite it last, so that a power
  // loss halfway never leaves a bootable mix of old and new code: the bootrom then falls back to its USB drive.
  const uint32_t base0 = bootloader_block_fill(0, bootloader_bin, bootloader_len);
  if (!is_equal(base0, block_cache, ERASE_SIZE)) {
    flash_erase(0);
  }

  // keep writing until flash contents matches the new bootloader data, the rest of the bootloader area blank
  const uint32_t blocks  = BOARD_FLASH_BOOT_SIZE / ERASE_SIZE;
  bool           updated = false;
  for (uint32_t retry = 0; retry < 3 && !updated; retry++) {
    updated = true;
    for (uint32_t i = blocks; i > 0 && updated; i--) {
      updated = bootloader_block_update(i - 1, bootloader_bin, bootloader_len);
    }
  }

  if (!updated) {
    TUF2_LOG1("Bootloader update failed\r\n");
    return;
  }

  // self-destruct so the new bootloader does not run this application again
  save_and_disable_interrupts();
  erase_app_start_and_reboot(BOARD_FLASH_APP_START - BOARD_FLASH_ADDR_ZERO);
}
#endif
