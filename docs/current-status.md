# clock_codex Current Status

## Purpose

This document captures the current implementation state of `clock_codex` so a new Codex session,
another machine, or a future contributor can resume work quickly without relying on chat history.

Last updated for the milestone:

- LVGL bring-up completed
- ILI9341 display and XPT2046 touch migrated
- INMP441 speech demo skeleton integrated

## What Is Working

- `LVGL 8.3.11` is integrated through PlatformIO `lib_deps`.
- The project boots with a working `lvgl_task`, `lv_tick_inc()`, and display flush loop.
- `ILI9341` display driver is migrated into `components/display`.
- `XPT2046` touch driver is migrated into `components/touch`.
- Basic touch stability fixes are already applied:
  - signed coordinate math
  - screen-bound clamping
  - 3-sample median filtering
  - repeated init guards
- Governance documents and local skill are already present in:
  - `docs/architecture.md`
  - `docs/coding-rules.md`
  - `docs/migration-rules.md`
  - `docs/versioning.md`
  - `.codex/skills/clock-codex-governance/`

## Audio / Speech Status

The repository already contains a first speech-oriented implementation:

- `components/audio_input`
  - I2S RX capture for `INMP441`
  - sample rate: `16kHz`
  - input path based on reused old MAX98357 I2S pins
- `components/app_services/app_speech_service.c`
  - integrated with `esp-sr`
  - currently designed as an offline wake word + command-word demo
  - not full free-form speech-to-text
- `components/app_ui/app_ui.c`
  - speech demo screen
  - shows state, wake hint, recognition text, detail, and input level

Current command-word demo assumptions:

- wake word: `嗨乐鑫`
- command phrases:
  - `你好，小钟`
  - `显示时间`
  - `进入测试`
  - `结束测试`

## Hardware Pin Map

Current board-level audio mapping in `components/bsp/include/board_config.h`:

- `GPIO5` -> I2S `BCLK`
- `GPIO6` -> I2S `WS/LRCL`
- `GPIO4` -> I2S `DIN` from `INMP441 SD`
- `3.3V` -> `INMP441 VDD`
- `GND` -> `INMP441 GND`
- `INMP441 L/R` -> `GND`

This means the current implementation expects the microphone on the left slot.

Important hardware note:

- These pins reuse the old MAX98357 interface.
- If MAX98357 is still physically attached to the same lines, it may interpret bus activity as playback data.
- For clean microphone verification, disconnect or disable the amplifier side during testing.

## Current Build State

Code status is split into two layers:

- The source code compiles and links successfully far enough to produce `firmware.elf`.
- `esp-sr` model packing also works if invoked with the PlatformIO Python interpreter.

Verified generated artifacts:

- `bootloader.bin`
- `partitions.bin`
- `firmware.elf`
- `srmodels/srmodels.bin` after manual model packaging

Manual model packaging command that worked:

```powershell
& 'C:\Users\PC\.platformio\penv\Scripts\python.exe' `
  'C:\Users\PC\Desktop\clock_codex\managed_components\espressif__esp-sr\model\movemodel.py' `
  -d1 'C:\Users\PC\Desktop\clock_codex\sdkconfig.esp32-s3-devkitm-1' `
  -d2 'C:\Users\PC\Desktop\clock_codex\managed_components\espressif__esp-sr' `
  -d3 'C:\Users\PC\Desktop\clock_codex\.pio\build\esp32-s3-devkitm-1'
```

Manual firmware image generation command that worked:

```powershell
& 'C:\Users\PC\.platformio\penv\Scripts\python.exe' `
  'C:\Users\PC\.platformio\packages\tool-esptoolpy\esptool.py' `
  --chip esp32s3 elf2image --flash_mode dio --flash_freq 80m --flash_size 16MB `
  -o 'C:\Users\PC\Desktop\clock_codex\.pio\build\esp32-s3-devkitm-1\firmware.bin' `
  'C:\Users\PC\Desktop\clock_codex\.pio\build\esp32-s3-devkitm-1\firmware.elf'
```

## Known Blockers

### 1. PlatformIO size check is still wrong

PlatformIO is still enforcing a `1MB` app-size limit even though:

- flash is configured as `16MB`
- the custom partition table uses:
  - `factory @ 0x20000 size 0x400000`
  - `model @ 0x420000 size 0x600000`

Observed symptom:

- `checkprogsize` fails with `maximum allowed (1048576 bytes)`

Current workaround:

- build/link to `firmware.elf`
- generate `firmware.bin` manually
- upload using the custom `flash_args` based uploader

### 2. `esp-sr` model packaging assumes `python` exists in PATH

`movemodel.py` is invoked by build scripts through bare `python`, which fails on this machine.

Observed symptom:

- `Python was not found but can be installed from the Microsoft Store`

Current workaround:

- call the script using `C:\Users\PC\.platformio\penv\Scripts\python.exe`

### 3. Upload cannot continue unless the board serial port is present

At the latest check, Windows only showed:

- `COM1`

The board port that had worked earlier was not present, so final upload could not be completed in that session.

## Recommended Next Steps

Resume work in this order:

1. reconnect the ESP32-S3 board and confirm the real serial port
2. finish the upload path for the current speech demo build
3. verify that:
   - display still works
   - touch still works
   - INMP441 capture does not crash the system
4. decide the speech direction:
   - keep offline wake word + command words with `esp-sr`
   - or switch to `ESP采音 -> Wi-Fi/WebSocket -> upper-computer/cloud ASR -> return text`

## Architecture Decision for Speech

Current recommendation for product-like free speech transcription:

- do not force full speech-to-text on ESP32-S3 alone
- keep ESP32-S3 responsible for:
  - audio capture
  - wake UI / listening UI
  - Wi-Fi transport
  - result display
- move free-form ASR and LLM logic to an upper computer or server

Suggested component split if the project moves to a xiaozhi-style architecture:

- `audio_input`
  - raw microphone capture
- `speech_transport`
  - WebSocket transport over Wi-Fi
- `app_services`
  - session state machine, JSON parse, text/result storage
- `app_ui`
  - listening / partial text / final text display

## Important Files

- `src/main.c`
- `components/bsp/include/board_config.h`
- `components/display/display_ili9341.c`
- `components/touch/touch_xpt2046.c`
- `components/audio_input/audio_input.c`
- `components/app_services/app_speech_service.c`
- `components/app_ui/app_ui.c`
- `platformio.ini`
- `sdkconfig.defaults`
- `partitions.csv`
- `tools/configure_upload.py`
- `tools/upload_via_flash_args.py`

## Resume Notes

If a future session needs to continue from here, start by reading:

1. `docs/current-status.md`
2. `docs/architecture.md`
3. `docs/versioning.md`
4. `components/app_services/app_speech_service.c`
5. `platformio.ini`

Then verify whether the intent is:

- offline command recognition demo
- or networked speech recognition / LLM integration
