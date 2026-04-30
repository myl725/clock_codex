# clock_codex

`clock_codex` is the rebuilt main project for the ESP32-S3 smart terminal.
It replaces the older `weather_clock_esp_idf` project as the active codebase and keeps the new
architecture, documentation, and migration rules in one place.

## Current Focus

The project is currently in bring-up and migration mode.

Completed milestones:

- `LVGL 8.3.11` integrated
- `ILI9341` display migrated
- `XPT2046` touch migrated
- `INMP441` speech demo skeleton integrated

Current main open direction:

- stabilize the current offline speech demo path
- or move toward a networked `ESP采音 -> Wi-Fi/WebSocket -> upper computer/cloud ASR -> text return` architecture

## Read This First

If you are starting a new session, read these files in order:

1. `docs/current-status.md`
2. `docs/architecture.md`
3. `docs/versioning.md`
4. `docs/runbook.md`
5. `docs/hardware-wiring.md`

## Project Structure

- `src/`
  startup orchestration only
- `components/bsp`
  board-level pin and hardware configuration
- `components/display`
  display driver and LVGL display registration
- `components/touch`
  touch driver and LVGL input registration
- `components/audio_input`
  `INMP441` I2S microphone capture
- `components/app_services`
  speech and future service orchestration
- `components/app_ui`
  LVGL screens and presentation logic
- `docs/`
  project rules, state, runbook, and hardware notes

## Build Status

The project can currently link to `firmware.elf`.
There are still known build-chain issues around:

- PlatformIO size checking
- `esp-sr` model packaging on this Windows environment
- upload depending on the correct serial port being present

See `docs/current-status.md` and `docs/runbook.md` for the exact workaround flow.

## Versioning

This repository uses git as the default versioning method.

- main branch: latest reasonably stable line
- feature branches: larger experimental changes

See `docs/versioning.md` for the working rules.
