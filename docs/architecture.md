# clock_codex Architecture Rules

## Purpose

This document defines the stable architecture boundaries for the `clock_codex` ESP-IDF project.
Use it as the source of truth when adding modules, migrating old code from `weather_clock_esp_idf`,
or reviewing whether a change belongs in the correct layer.

## Layer Model

The project is organized around six responsibilities:

- `src/`
  Own startup orchestration only. Keep `app_main()` thin and limited to init order, task bootstrap,
  and top-level lifecycle control.
- `components/bsp`
  Own board-specific configuration and low-level shared hardware definitions such as GPIO mapping,
  SPI host choice, display size, and default touch calibration values.
- `components/display`
  Own display driver integration, LVGL display registration, frame flush, and display bus details.
- `components/touch`
  Own touch controller integration, coordinate conversion, calibration handling, and LVGL indev registration.
- `components/app_services`
  Own service-facing APIs that expose capabilities to the UI and later coordinate Wi-Fi, SNTP,
  weather, DHT11, WS2812, SD, and MP3 modules.
- `components/app_ui`
  Own LVGL screens, assets, event callbacks, and UI state updates.

## Dependency Direction

Only allow these dependency directions:

- `src` -> `bsp`, `display`, `touch`, `app_services`, `app_ui`
- `app_ui` -> `app_services`
- `app_services` -> `bsp`, hardware-specific driver modules, ESP-IDF services
- `display` -> `bsp`, LVGL, ESP-IDF drivers
- `touch` -> `bsp`, LVGL, ESP-IDF drivers

Do not allow these reverse dependencies:

- `app_services` -> `app_ui`
- `display` -> `app_ui`
- `touch` -> `app_ui`
- hardware/driver modules -> `app_ui`

## Ownership Rules

Assign each concern to one owner layer:

- startup order, task creation policy, and global init sequence: `src`
- board pins, bus identifiers, calibration defaults, hardware constants: `bsp`
- display flush and display bus transactions: `display`
- touch sampling and coordinate mapping: `touch`
- external capabilities and module orchestration: `app_services`
- screen widgets, transitions, labels, timers tied to page presentation: `app_ui`

If a change spans multiple layers, split it so each layer keeps its own responsibilities.

## Initialization Rules

For phase 1, keep the init order fixed:

1. `nvs_flash_init()`
2. `lv_init()`
3. `bsp_display_init()` or `display_init()` wrapper
4. `bsp_touch_init()` or `touch_init()` wrapper
5. `app_ui_init()`
6. create `lvgl_task`

Do not start Wi-Fi, DHT11, WS2812, SD, or audio tasks in phase 1 unless the milestone explicitly adds them.

## UI and Service Boundary

Treat the UI as a client of services:

- UI may request time, weather, temperature, music control, or LED updates through `app_services`.
- UI may not access SPI, I2S, RMT, GPIO, or raw ESP-IDF driver objects directly.
- UI may not read or write driver-owned globals such as raw queues, bus handles, or sensor structs.

In early migration, service stubs are allowed and preferred over direct coupling.

## Configuration Boundary

Split configuration by intent:

- `board_config.h`
  Board pins, SPI host selection, display geometry, calibration defaults.
- `app_config.h`
  Project-level app configuration such as Wi-Fi credentials, weather location, API keys, feature flags.
- `sdkconfig.defaults`
  ESP-IDF capability toggles and framework-level options.

Do not hardcode these values inside feature `.c` files except for short-lived debug experiments that are removed before merge.

## Migration Placement Guide

When migrating code from the old project, place it by behavior, not by old folder name:

- `lv_port_disp*` -> `components/display`
- `touch_indev*` -> `components/touch`
- SquareLine/LVGL screens, fonts, images -> `components/app_ui`
- Wi-Fi, SNTP, weather, DHT11, WS2812, SD/MP3 control facades -> `components/app_services`
- board constants and shared hardware mapping -> `components/bsp`

Do not preserve old relative include structure if it conflicts with these ownership rules.
