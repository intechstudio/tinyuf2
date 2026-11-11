/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 Ha Thach for Adafruit Industries
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "board_api.h"

#include "pico/bootrom.h"
#include "boot/picoboot_constants.h"

#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/resets.h"
#include "hardware/structs/scb.h"
#include "hardware/pio.h"
#include "hardware/structs/systick.h"
#include "hardware/timer.h"

#include "ws2812.pio.h"

//--------------------------------------------------------------------+
// Neopixel (WS2812 driven by PIO)
//--------------------------------------------------------------------+
#define NEOPIXEL_PIO      pio0
#define NEOPIXEL_SM       0
#define NEOPIXEL_BIT_HZ   800000u
#define NEOPIXEL_BIT_PIO_CYCLES (ws2812_T1 + ws2812_T2 + ws2812_T3)
// the line must stay low this long after a frame to latch it (datasheet minimum is 280 us)
#define NEOPIXEL_RESET_US 300u

static void neopixel_init(void) {
  // the only program in this PIO: load it at offset 0 directly instead of through pio_add_program() (saves ~300 bytes)
  const uint offset = 0;
  for (uint i = 0; i < count_of(ws2812_program_instructions); i++) {
    NEOPIXEL_PIO->instr_mem[offset + i] = ws2812_program_instructions[i];
  }
  pio_sm_config c = ws2812_program_get_default_config(offset);
  sm_config_set_sideset_pins(&c, NEOPIXEL_PIN);
  // shift left, autopull at 24 bits: one GRB pixel per FIFO word
  sm_config_set_out_shift(&c, false, true, 24);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

  // integer clock divider: sys_clk / (bit rate * PIO cycles per bit), 8 bit fraction
  const uint32_t target = NEOPIXEL_BIT_HZ * NEOPIXEL_BIT_PIO_CYCLES;
  const uint32_t sys_hz = clock_get_hz(clk_sys);
  sm_config_set_clkdiv_int_frac8(&c, (uint16_t) (sys_hz / target), (uint8_t) (((sys_hz % target) * 256u) / target));

  pio_gpio_init(NEOPIXEL_PIO, NEOPIXEL_PIN);
  pio_sm_set_consecutive_pindirs(NEOPIXEL_PIO, NEOPIXEL_SM, NEOPIXEL_PIN, 1, true);
  pio_sm_init(NEOPIXEL_PIO, NEOPIXEL_SM, offset, &c);
  pio_sm_set_enabled(NEOPIXEL_PIO, NEOPIXEL_SM, true);
}

static inline uint8_t apply_brightness(uint8_t value) {
  return (uint8_t) (((uint32_t) value * NEOPIXEL_BRIGHTNESS) / 255u);
}

void board_rgb_write(uint8_t const rgb[]) {
  static uint32_t last_end = 0;

  // neopixel color order is GRB, left aligned in the 24 bit shift
  const uint32_t grb = ((uint32_t) apply_brightness(rgb[1]) << 24) | ((uint32_t) apply_brightness(rgb[0]) << 16) |
                       ((uint32_t) apply_brightness(rgb[2]) << 8);

  // the previous frame is latched once the line has been low for the reset time
  while ((time_us_32() - last_end) < NEOPIXEL_RESET_US) {}

  for (uint i = 0; i < NEOPIXEL_NUMBER; i++) {
    pio_sm_put_blocking(NEOPIXEL_PIO, NEOPIXEL_SM, grb);
  }

  // wait until the last word has left the FIFO; the reset time above covers the word still being shifted out
  while (!pio_sm_is_tx_fifo_empty(NEOPIXEL_PIO, NEOPIXEL_SM)) {}
  last_end = time_us_32();
}

// single color LED is not present on this board
void board_led_write(uint32_t state) {
  (void) state;
}

//--------------------------------------------------------------------+
// Board
//--------------------------------------------------------------------+
#ifndef BUILD_APPLICATION
// POWMAN's DOUBLE_TAP flag is kept across a RUN pin reset. It is set from the start of the bootloader until the
// application is started or DFU mode is entered, so a second reset in that window is seen at the next boot.
static inline void double_tap_arm(void) {
  hw_set_bits(&powman_hw->chip_reset, POWMAN_CHIP_RESET_DOUBLE_TAP_BITS | POWMAN_PASSWORD_BITS);
}

static inline void double_tap_disarm(void) {
  hw_clear_bits(&powman_hw->chip_reset, POWMAN_CHIP_RESET_DOUBLE_TAP_BITS | POWMAN_PASSWORD_BITS);
}
#endif

void board_init(void) {
  neopixel_init();

#ifndef BUILD_APPLICATION
  // reset while the previous boot was waiting for a second tap: ask main() for DFU mode
  if (powman_hw->chip_reset & POWMAN_CHIP_RESET_DOUBLE_TAP_BITS) {
    TINYUF2_DBL_TAP_REG = DBL_TAP_MAGIC;
  }
  double_tap_arm();
#endif
}

void board_teardown(void) {
#ifndef BUILD_APPLICATION
  double_tap_disarm();
#endif
  board_timer_stop();
  // leave the application a clean start: no pending SysTick, no enabled or pending interrupts, PIO0 in reset state
  scb_hw->icsr = M33_ICSR_PENDSTCLR_BITS;
  for (uint i = 0; i < count_of(nvic_hw->icer); i++) {
    nvic_hw->icer[i] = 0xffffffffu;
    nvic_hw->icpr[i] = 0xffffffffu;
  }
  reset_block_mask(RESETS_RESET_PIO0_BITS);
  unreset_block_mask_wait_blocking(RESETS_RESET_PIO0_BITS);
}

void board_dfu_init(void) {
#ifndef BUILD_APPLICATION
  double_tap_disarm();
#endif
  // clk_usb is set up by the SDK runtime init; TinyUSB's dcd_init() resets and configures the USB controller
}

void board_reset(void) {
  // let the bootrom do a normal reboot; a bare watchdog trigger does not reset anything unless PSM_WDSEL is set up
  rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_NORMAL, 10, 0, 0);
  while (1) {}
}

void board_dfu_complete(void) {
  board_reset();
}

// The application is valid when its vector table looks sane: the initial stack pointer is in SRAM and the reset
// handler is a Thumb address inside the application area. Erased flash (0xffffffff) fails the stack pointer check.
bool board_app_valid(void) {
  const uint32_t* vector = (const uint32_t*) BOARD_FLASH_APP_START;
  const uint32_t  sp     = vector[0];
  const uint32_t  entry  = vector[1];

  return (sp & 0xff000003u) == 0x20000000u && (entry & 1u) != 0 && entry > BOARD_FLASH_APP_START &&
         entry < BOARD_FLASH_ADDR_ZERO + BOARD_FLASH_SIZE;
}

void board_app_jump(void) {
  const uint32_t* vector = (const uint32_t*) BOARD_FLASH_APP_START;

  // exceptions before the application installs its own vector table must reach the application's handlers
  scb_hw->vtor = BOARD_FLASH_APP_START;
  __dsb();
  __isb();

  // same as a reset: stack pointer from the first vector, then the reset handler from the second
  __asm volatile("msr msp, %0" : : "r"(vector[0]) : "memory");
  ((void (*)(void)) vector[1])();
  while (1) {}
}

// for applications: come back in the bootloader instead of running the application again
void board_reboot_to_bootloader(void) {
  TINYUF2_DBL_TAP_REG = DBL_TAP_MAGIC;
  board_reset();
}

// The serial number is the chip's 64 bit device id, as given by the bootrom (same bytes as pico_get_unique_board_id())
uint8_t board_usb_get_serial(uint8_t serial_id[16]) {
  const rom_get_sys_info_fn get_sys_info = (rom_get_sys_info_fn) rom_func_lookup(ROM_FUNC_GET_SYS_INFO);
  uint32_t words[9];
  if (get_sys_info == NULL || get_sys_info(words, 9, SYS_INFO_CHIP_INFO) != 4) {
    return 0;
  }

  // words[0] flags the returned items, then package_sel, then the device id in words[2] and words[3]
  const uint8_t* bytes = (const uint8_t*) words;
  for (uint i = 0; i < 8; i++) {
    serial_id[i] = bytes[15 - i];
  }
  return 8;
}

//--------------------------------------------------------------------+
// Timer
//--------------------------------------------------------------------+
void board_timer_start(uint32_t ms) {
  systick_hw->rvr = (clock_get_hz(clk_sys) / 1000u) * ms - 1u;
  systick_hw->cvr = 0;
  // enable, interrupt, processor clock
  systick_hw->csr = 0x7u;
}

void board_timer_stop(void) {
  systick_hw->csr = 0;
}

// pico-sdk vector table name for SysTick
void isr_systick(void);
void isr_systick(void) {
  board_timer_handler();
}

int board_uart_write(void const* buf, int len) {
  (void) buf;
  (void) len;
  return 0;
}

//--------------------------------------------------------------------+
// USB Interrupt Handler
//--------------------------------------------------------------------+

// TinyUSB's RP2040/RP2350 device driver registers its USB IRQ with irq_add_shared_handler(), which links in the SDK's
// shared handler list (about 700 bytes). Nothing else shares USBCTRL_IRQ in the bootloader, so install it as the
// exclusive handler instead (see --wrap in CMakeLists.txt).
void __wrap_irq_add_shared_handler(uint num, irq_handler_t handler, uint8_t order_priority);
void __wrap_irq_add_shared_handler(uint num, irq_handler_t handler, uint8_t order_priority) {
  (void) order_priority;
  irq_set_exclusive_handler(num, handler);
}
