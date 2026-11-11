#ifndef BOARD_H_
#define BOARD_H_

//--------------------------------------------------------------------+
// Neopixel
//--------------------------------------------------------------------+

// Number of neopixels
#define NEOPIXEL_NUMBER     16
// Brightness from 1 to 255
#define NEOPIXEL_BRIGHTNESS 0x08

// WS2812 data pin (GPIO number), no supply enable pin on this board
#define NEOPIXEL_PIN        6

// BOARD_FLASH_SIZE and BOARD_FLASH_BOOT_SIZE come from board.cmake

//--------------------------------------------------------------------+
// USB UF2
//--------------------------------------------------------------------+

#define USB_VID          0x2e8a
#define USB_PID          0x1145
#define USB_MANUFACTURER "Intech Studio"
#define USB_PRODUCT      "Wrid RP2350 Bootloader"

#define UF2_PRODUCT_NAME USB_PRODUCT
#define UF2_BOARD_ID     "RP2350-WRID-v0"
#define UF2_VOLUME_LABEL "WRIDRP2350"
#define UF2_INDEX_URL    "https://github.com/intechstudio/grid-fw/"

#endif
