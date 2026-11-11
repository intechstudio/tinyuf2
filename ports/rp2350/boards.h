/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Ha Thach (tinyusb.org) for Adafruit Industries
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

#ifndef BOARDS_H_
#define BOARDS_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "board.h"

// Double tap of reset enters the bootloader. TinyUF2 keeps a 32 bit token in TINYUF2_DBL_TAP_REG, here a POWMAN
// scratch register (the same one in the bootloader and in applications, so an application can request the bootloader,
// see board_reboot_to_bootloader()). POWMAN scratch survives watchdog and software resets, but NOT the RUN pin:
// a RUN reset also resets POWMAN, and RAM is powered down by it. Only POWMAN's DOUBLE_TAP flag survives RUN, so
// boards.c arms that flag while the bootloader waits for the second tap and turns it into the token at the next boot.
#include "hardware/structs/powman.h"
#define TINYUF2_DBL_TAP_DFU     1
#define TINYUF2_DBL_TAP_REG     (powman_hw->scratch[0])

// Flash is memory mapped here, and UF2 files for this family use these absolute addresses
#define BOARD_FLASH_ADDR_ZERO   0x10000000

// The application starts right after the bootloader region (BOARD_FLASH_BOOT_SIZE, from board.cmake)
#define BOARD_FLASH_APP_START   (BOARD_FLASH_ADDR_ZERO + BOARD_FLASH_BOOT_SIZE)

#ifdef __cplusplus
 }
#endif

#endif /* BOARDS_H_ */
