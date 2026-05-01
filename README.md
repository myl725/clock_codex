# clock_codex

`clock_codex` is the active rebuilt codebase for the ESP32-S3 smart terminal project.
It replaces the older `weather_clock_esp_idf` line and keeps the current architecture,
hardware notes, migration rules, and bring-up history in one repository.

## Current Focus

Current completed milestones:

- `LVGL 8.3.11` integrated
- `ILI9341` display migrated
- `XPT2046` touch migrated
- `WS2812` control path added
- `MAX98357A` audio playback bring-up completed
- `SD Audio` playback added for `WAV` and first-pass `MP3`

Current open direction:

- keep display/touch/lighting/audio playback stable
- improve MP3 compatibility for more files
- leave speech work paused until the current music path is settled

## Read This First

If you are resuming the project in a new chat or on a new machine, read these files first:

1. `docs/current-status.md`
2. `docs/architecture.md`
3. `docs/versioning.md`
4. `docs/runbook.md`
5. `docs/hardware-wiring.md`
6. `requirement.md`
7. `rules.md`

## Project Structure

- `src/`
  startup orchestration only
- `components/bsp`
  board-level pin and hardware configuration
- `components/display`
  display driver and LVGL display registration
- `components/touch`
  touch driver and LVGL input registration
- `components/ws2812`
  WS2812 low-level driver
- `components/audio_output`
  active `MAX98357A` I2S playback output path
- `components/audio_input`
  paused `INMP441` experiments kept for reference
- `components/app_services`
  lighting service, music playback service, and paused speech service code
- `components/app_ui`
  LVGL control screen for lighting and audio
- `docs/`
  project rules, status notes, runbook, and hardware documentation

## Build Status

The current mainline build is an LVGL control app with:

- `Lighting` tab for `WS2812`
- `Audio` tab for tone playback and `SD Audio` file playback

`pio run` succeeds on the current branch. Upload works when the correct ESP32-S3 serial port is
present and not occupied by another tool.

See `docs/current-status.md` and `docs/runbook.md` for the exact working state and debug flow.

## Versioning

This repository uses git as the default versioning method.

- `main`: latest reasonably stable line
- feature branches: larger experiments or isolated development

See `docs/versioning.md` for the working rules.
