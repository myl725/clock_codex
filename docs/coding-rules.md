# clock_codex Coding Rules

## Purpose

This document defines coding conventions that keep `clock_codex` maintainable while the project is
rebuilt from the older ESP-IDF/LVGL codebase.

## Include Rules

- Use component public headers instead of cross-directory relative includes.
- Do not add includes such as `../../lib/...` or `../other_module/...` in new code.
- Keep private headers private to their component unless there is a deliberate public API reason.
- Prefer `#include "component_header.h"` for project headers and angle brackets for ESP-IDF/system headers.

## Naming Rules

- Use lowercase snake_case for C functions, variables, and file names.
- Use uppercase snake_case for macros and compile-time constants.
- Use short, explicit prefixes for exported symbols when ownership is not obvious.
- Name files after the capability they own, not after temporary experiments.

## Global State Rules

- Add global variables only when component lifetime truly requires them.
- Every global must have a clear owner component and a documented reason to exist.
- Keep hardware handles, queues, and synchronization objects inside the component that owns them.
- Do not let UI files access driver-owned globals directly.

## Task and Timer Rules

- Create long-lived FreeRTOS tasks only in `src` startup orchestration or inside the owning service module.
- Do not create long-lived background tasks from UI screen files or widget callbacks.
- LVGL timers that update page presentation may live in `app_ui`, but they must only drive UI behavior.
- If a task exists only to wrap a hardware feature, place its implementation in the service that owns that feature.

## Configuration Rules

- Put board pins, display dimensions, bus IDs, and calibration defaults in `board_config.h`.
- Put Wi-Fi credentials, weather settings, and feature-level app constants in `app_config.h`.
- Put ESP-IDF framework toggles in `sdkconfig.defaults`.
- Do not leave production values hardcoded in feature implementation files.

## Error Handling and Logging

- Use `ESP_ERROR_CHECK` for must-succeed init paths unless graceful recovery is intentional.
- Return `esp_err_t` or `bool` from component APIs when callers need to react to failure.
- Use module-specific log tags and keep them stable.
- Log state transitions and failures; avoid noisy per-loop logs in steady-state code.

## Memory and Resource Rules

- Prefer explicit init/deinit ownership for buses, channels, buffers, and mount points.
- Free or tear down resources in the same component that allocates them.
- Do not hide ownership transfers across layers.
- Avoid allocating large transient buffers on task stacks when heap or static storage is safer.

## UI Rules

- Keep UI code focused on presentation, event handling, and calling service APIs.
- Do not place raw networking, SPI, I2S, RMT, GPIO, or file-system transactions in UI files.
- If a page needs data, add or extend a service API instead of reaching into another module's globals.
- If a screen depends on periodic data, let the service own acquisition and the UI own presentation refresh.

## Review Checklist

Before considering a change complete, verify:

- no new cross-layer relative includes were introduced
- no business logic was pushed into `src/main.c`
- no new feature constants were hardcoded in `.c` files
- no UI file directly manipulates hardware-owned state
- new tasks, queues, and globals have a clear owner
