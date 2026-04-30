# clock_codex Hardware Wiring

## Purpose

This document records the hardware connections assumed by the current `clock_codex` codebase.
Use it to avoid guessing pin mappings during bring-up, migration, or debugging on a new machine.

## Board Assumption

Current target:

- `ESP32-S3`
- `N16R8` style module assumption
- PlatformIO environment: `esp32-s3-devkitm-1`

## Display Wiring

Current display target:

- `ILI9341`

Board-level mapping from `components/bsp/include/board_config.h`:

- `GPIO13` -> LCD `MISO`
- `GPIO11` -> LCD `MOSI`
- `GPIO12` -> LCD `CLK`
- `GPIO10` -> LCD `CS`
- `GPIO17` -> LCD `DC`
- `GPIO16` -> LCD `RST`

Current display bus host:

- `SPI2_HOST`

Display geometry:

- `320 x 240`

## Touch Wiring

Current touch target:

- `XPT2046`

Board-level mapping:

- shared SPI bus with display
- `GPIO8` -> touch `CS`

Notes:

- touch currently uses software calibration values stored in `board_config.h`
- touch is integrated as an LVGL pointer input device

## Audio / Microphone Wiring

Current microphone target:

- `INMP441`

Current I2S mapping:

- `GPIO5` -> `INMP441 SCK / BCLK`
- `GPIO6` -> `INMP441 WS / LRCL`
- `GPIO4` -> `INMP441 SD / DOUT`
- `3.3V` -> `INMP441 VDD`
- `GND` -> `INMP441 GND`
- `INMP441 L/R` -> `GND`

Current software expectation:

- sample rate: `16kHz`
- microphone data read from the left slot
- `MCLK` is unused

## Reused MAX98357 Interface Note

The current microphone experiment intentionally reuses the earlier MAX98357-related I2S pins.

That means:

- the pin group is electrically reused
- the old amplifier path and microphone path should not both be assumed active at the same time without review

If MAX98357 is still attached:

- it may interpret I2S traffic in unintended ways
- it may output noise or interfere with clean microphone testing

For clean `INMP441` verification:

- disconnect the amplifier side
- or make sure it is not actively participating during microphone bring-up

## Software Source Of Truth

When in doubt, confirm the latest mapping in:

- `components/bsp/include/board_config.h`

Do not update wiring assumptions in code without updating this document.
