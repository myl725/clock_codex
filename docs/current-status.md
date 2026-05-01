# clock_codex Current Status

## Purpose

This document captures the current implementation state of `clock_codex` so a new Codex session,
another machine, or a future contributor can resume work quickly without relying on chat history.

Last updated for the milestone:

- LVGL bring-up completed
- ILI9341 display and XPT2046 touch migrated
- speech work paused
- WS2812 control path added
- MAX98357A audio output bring-up completed
- SD audio playback added for `WAV` and `MP3`

## What Is Working

- `LVGL 8.3.11` is integrated through PlatformIO `lib_deps`.
- The project boots with a working `lvgl_task`, `lv_tick_inc()`, and display flush loop.
- `ILI9341` display driver is migrated into `components/display`.
- `XPT2046` touch driver is migrated into `components/touch`.
- The current mainline app now boots into an LVGL control screen with `Lighting` and `Audio` tabs.
- Basic touch stability fixes are already applied:
  - signed coordinate math
  - screen-bound clamping
  - 3-sample median filtering
  - repeated init guards
- Governance documents and local skill are already present in:
  - `README.md`
  - `docs/architecture.md`
  - `docs/coding-rules.md`
  - `docs/migration-rules.md`
  - `docs/versioning.md`
  - `docs/runbook.md`
  - `docs/hardware-wiring.md`
  - `.codex/skills/clock-codex-governance/`

## Audio / Speech Status

Speech-related implementation is still kept in the repository for reference, but it is not part of the
current mainline build path.

Current state:

- `components/audio_input`
  - contains the previous `INMP441` experiments
- `components/app_services/app_speech_service.c`
  - contains the previous `esp-sr` offline wake-word / command-word attempt
- `components/audio_output`
  - now contains the active `MAX98357A` playback output path
  - defaults to `44.1kHz` for tone playback and can be reconfigured at runtime for file playback
- `components/app_services/app_music_service.c`
  - now contains the active tone-playback baseline plus `Tone / SD Audio` playback control
  - `SD Audio` mode now scans the SD root directory for playable `.wav` and `.mp3` files and lets the UI switch between them
- `components/app_services/app_music_wav.c`
  - now contains the first-pass `16-bit PCM WAV` reader for music playback
- `components/app_services/app_music_mp3.c`
  - now contains the first-pass streaming MP3 decoder path for SD music playback

Current decision:

- speech work is paused
- the mainline `src/main.c` no longer starts the speech stack
- the current mainline build focus is display/touch + WS2812 + local SD music playback
- the current WAV path expects `/sdcard/music.wav`
- SD WAV playback now assumes a dedicated SPI bus:
  - `SCK` on `GPIO18`
  - `MISO` on `GPIO19`
  - `MOSI` on `GPIO21`
  - `CS` on `GPIO38`
- the current SD music path supports `.wav` and `.mp3`
- `TEST3.MP3` now plays through the current MP3 path
- some MP3 files such as `TEST2.MP3` still fail early if the lightweight decoder cannot find a usable first audio frame

## Hardware Focus

Current active hardware focus:

- display: `ILI9341`
- touch: `XPT2046`
- WS2812: `GPIO46`, software path added for effect/color testing
- audio out: `MAX98357A` on `GPIO5/GPIO6/GPIO4`
- SD card: software path now targets dedicated SPI pins `GPIO18/GPIO19/GPIO21/GPIO38`

Paused hardware path:

- `INMP441`
- old speech experiments

## Current Build State

Current mainline build status:

- speech-related components are excluded from the current build path
- the active app is an LVGL control screen with lighting and SD audio controls
- `pio run` succeeds on the current branch
- `pio run -t upload` succeeds when `COM3` is free

Most recent successful mainline build characteristics:

- RAM about `27.7%`
- Flash about `35.6%`

Current upload note:

- upload works through `COM3` when the board is present and no other tool is holding the port

## Known Blockers

### 1. Some MP3 files are still not decoder-friendly

Current observation:

- `TEST3.MP3` is now stable enough to open and play
- `TEST2.MP3` still fails with an early `prime returned no audio` path in the lightweight MP3 decoder

That means the current `minimp3` integration is good enough for first-pass SD music playback, but it is not yet robust against every MP3 encoding variant.

### 2. Speech code is intentionally out of the mainline path

This is not a bug.

It is an explicit project decision for now:

- do not spend more time on `INMP441` bring-up in the current thread
- keep speech code only as reference
- move implementation attention to `WS2812`

## Recommended Next Steps

Resume work in this order:

1. reconnect the ESP32-S3 board and confirm `COM3`
2. upload the current display/touch/WS2812/audio mainline build
3. verify that:
   - display still works
   - touch still works
   - the WS2812 control tab appears correctly
   - effect switching works
   - color switching works
   - the audio tab can start and stop the tone cleanly
   - `SD Audio` can play known-good `WAV` and `MP3` files such as `TEST3.MP3`
4. improve MP3 compatibility:
   - keep current `TEST3.MP3` path stable
   - investigate why `TEST2.MP3` produces no usable priming frame
   - decide whether to keep tuning `minimp3` or replace it with a more tolerant decoder

## WS2812 Direction

Current implementation thread now includes:

- confirmed data pin: `GPIO46`
- clean `bsp` -> `ws2812` -> `app_services` -> `app_ui` control path
- test UI for:
  - effect selection
  - color selection
  - off / solid / blink / breathe / rainbow behavior
- clean `bsp` -> `audio_output` -> `app_services` -> `app_ui` path for audio
- test UI for:
  - 44.1kHz playback start/stop
  - runtime sample-rate reconfiguration for file playback
  - source switching between `Tone` and `SD Audio`
  - tone selection or SD file selection
- MAX98357A output validation

## Important Files

- `src/main.c`
- `components/bsp/include/board_config.h`
- `components/display/display_ili9341.c`
- `components/touch/touch_xpt2046.c`
- `platformio.ini`
- `sdkconfig.defaults`
- `partitions.csv`
- `tools/configure_upload.py`
- `tools/upload_via_flash_args.py`

Reference-only speech files kept in repo:

- `components/audio_input/audio_input.c`
- `components/app_services/app_speech_service.c`
- `components/app_ui/app_ui.c`

## Resume Notes

If a future session needs to continue from here, start by reading:

1. `docs/current-status.md`
2. `docs/architecture.md`
3. `docs/versioning.md`
4. `docs/runbook.md`
5. `docs/hardware-wiring.md`
6. `platformio.ini`
7. `components/bsp/include/board_config.h`

Then verify whether the intent is:

- keep display/touch stable
- or continue with the new `WS2812` task
