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

## Audio Output Wiring

Current audio output target:

- `MAX98357A`

Current project note:

- speech / microphone work is paused
- current active audio path is playback output only

Current I2S mapping:

- `GPIO5` -> `MAX98357A BCLK`
- `GPIO6` -> `MAX98357A LRC`
- `GPIO4` -> `MAX98357A DIN`
- `MAX98357A OUT+/-` -> speaker

Current software expectation:

- tone playback defaults to `44.1kHz`
- file playback can reconfigure the output rate to match supported SD audio files such as `48kHz MP3`
- I2S standard TX mode
- `MCLK` is unused

## Paused Microphone Note

The earlier `INMP441` experiment reused the same `GPIO5/GPIO6/GPIO4` I2S group.

That means:

- the current music path and the old microphone path should not be assumed active at the same time
- if `INMP441` is still attached on the same lines, review wiring before revisiting speech

## WS2812 Wiring

Current LED target:

- `WS2812`

Current note:

- `GPIO46` -> `WS2812 DIN`
- current software assumes `10` LEDs in `board_config.h`
- once the real LED count is confirmed, this document and `board_config.h` should be updated together

## SD Card Wiring

Current software assumption:

- SD card uses a dedicated SPI bus from the display/touch path
- `GPIO19` -> SD `MISO`
- `GPIO21` -> SD `MOSI`
- `GPIO18` -> SD `SCK`
- `GPIO38` -> SD `CS`

Current software expectation:

- mount point: `/sdcard`
- demo WAV path: `/sdcard/music.wav`
- supported first-pass formats:
  - `16-bit PCM WAV`, mono or stereo
  - `MP3` through the current lightweight decoder path

Current note:

- SD no longer shares the LCD/touch SPI line group
- software now assumes dedicated `SPI3_HOST` for the card
- current known-good MP3 test file is `TEST3.MP3`
- some MP3 files such as `TEST2.MP3` still fail early in decoder priming and need later follow-up

## Software Source Of Truth

When in doubt, confirm the latest mapping in:

- `components/bsp/include/board_config.h`

Do not update wiring assumptions in code without updating this document.
