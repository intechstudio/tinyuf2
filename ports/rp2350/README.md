# TinyUF2 for RP2350

Port for the RP2350 (Arm Cortex-M33, secure) built on [pico-sdk](https://github.com/raspberrypi/pico-sdk). The bootloader runs
behind the RP2350 bootrom, which finds its `IMAGE_DEF` in flash and starts it: the bootrom USB bootloader (BOOTSEL) stays
available as recovery whatever is in flash.

Boards:

| Board | Notes |
| --- | --- |
| `intechstudio_grid_rp2350` | 16 WS2812 on GPIO6 (PIO0), volume `GRIDRP2350` |

## Build

pico-sdk is not a submodule: `tools/get_deps.py` fetches the pinned version into `lib/mcu/raspberrypi/pico-sdk`.

```
python3 tools/get_deps.py rp2350
cmake -B build -G Ninja -DBOARD=intechstudio_grid_rp2350 ports/rp2350
cmake --build build
```

Use another pico-sdk with `-DPICO_SDK_PATH=<path>` or the `PICO_SDK_PATH` environment variable. The build also needs a host
compiler, for the SDK's `pioasm`.

## Flash layout

| Address | Content |
| --- | --- |
| `0x10000000` | TinyUF2, 32 KB region (`LD_FLASH_BOOT_SIZE` in `board.cmake`) |
| `0x10008000` | Application |

The application must be linked at `0x10008000`, which `app.cmake` does for the applications built here. A UF2 file of an
application uses the family ID `0xe48bff59` (`RP2350_ARM_S`) and absolute flash addresses. To create one from a `.bin`:

```
uf2conv.py -c -b 0x10008000 -f 0xe48bff59 firmware.bin
```

The bootloader refuses UF2 blocks below the application start or past the end of the flash, and erases a 4 KB sector only
when a block to write does not fit in blank flash, writing back the blocks of that sector it knows to be good.

The bootloader image is about 17 KB, so the region could shrink: 16 KB was not reached with pico-sdk, TinyUSB's mass storage
class and the flash driver. What was tried: `pico_minimize_runtime()` and replacing `irq_add_shared_handler()` (both used,
about 3.4 KB saved) and LTO (does not link with the Arm GNU 13.3 and Debian 14.2 toolchains).

## Flashing the bootloader

Hold BOOTSEL while plugging in, then copy `_bin/<board>/tinyuf2.uf2` to the BOOTSEL drive. Updating a running bootloader is done with
the self-update below. SWD flash targets are not set up.

## Entering the bootloader

- Double tap reset: the LEDs show purple for 0.5 s after reset, a second reset within that time stays in the bootloader.
- The bootloader also stays when the application is missing or invalid: the stack pointer in the first vector must point into
  SRAM and the reset vector must be a Thumb address inside the application area.
- From an application: `board_reboot_to_bootloader()`.

The double tap uses the `DOUBLE_TAP` flag of POWMAN, armed from the start of the bootloader until it starts the application or
enters DFU mode. A RUN pin reset powers down RAM and also resets the POWMAN scratch registers, which are only kept over watchdog
and software resets: the application request therefore uses `powman_hw->scratch[0]`, the reset button the flag.

## Self-update

`update-tinyuf2.uf2` is built next to the bootloader in `_bin/<board>/apps`. Copy it to the bootloader drive: it runs from the
application area, rewrites the bootloader and erases itself, then the new bootloader starts. The application area is
overwritten, so copy the application again afterwards.

The bootloader's first sector holds the `IMAGE_DEF` the bootrom looks for. The update erases it first and writes it last, so an
interrupted update leaves no bootable mix of old and new code: the bootrom then starts its USB bootloader, from where
`tinyuf2.uf2` can be copied again. An image is only written when its vector table is valid, it fits the boot area and holds an
`IMAGE_DEF` start marker in its first 4 KB.

## Not implemented

- Write protection: `board_flash_protect_bootloader()` does nothing. UF2 writes cannot reach the bootloader, but the flash
  itself is not protected against an application. It needs the block protect bits of the flash chip, whose type is not known yet.
- `BOARD_FLASH_SIZE` in `board.cmake` is the assumed 2 MB, to be confirmed for the board.
- Applications for the RISC-V cores and the non-secure Arm family are not supported.

## Notes

- pico-sdk's own TinyUSB support is switched off in `family.cmake`: its glue defines functions with the names of this repo's
  `family_support.cmake` and replaces them. TinyUF2 builds `lib/tinyusb`, and CMake prints a warning that SDK USB support is unavailable.
- `pico_override_flash_size()` must not be used for the boot region: it also defines `PICO_FLASH_SIZE_BYTES`, and the SDK's
  `flash_range_erase()`/`flash_range_program()` assert on every offset beyond it.
- Flash writes keep interrupts off, as the flash leaves XIP mode meanwhile.
