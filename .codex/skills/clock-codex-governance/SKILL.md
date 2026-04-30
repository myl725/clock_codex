---
name: clock-codex-governance
description: Governance summary for the clock_codex ESP-IDF project. Use when adding, migrating, reviewing, or refactoring code in clock_codex so changes follow the repo's architecture, coding, and migration rules instead of reintroducing old cross-layer coupling.
---

# Clock Codex Governance

## Overview

Keep clock_codex changes aligned with the repo's written governance. This skill is only the execution
summary. Treat the documents under `docs/` as the full source of truth.

## Required Reads

Read these files before making non-trivial changes in `clock_codex`:

- `docs/architecture.md`
- `docs/coding-rules.md`
- `docs/migration-rules.md` when moving code from `weather_clock_esp_idf`

If the task is tiny and obviously local, skim the relevant document first and then proceed.

## Core Boundary

Follow this dependency model:

- `src` owns startup orchestration
- `app_ui` depends on `app_services`
- `app_services` may depend on BSP and hardware-facing modules
- driver and hardware-facing modules must not depend on `app_ui`

Do not reintroduce direct UI access to SPI, I2S, RMT, GPIO, sensor structs, or hardware-owned queues.

## Placement Rules

Place new work by responsibility:

- startup and init order -> `src`
- board constants and pin maps -> `components/bsp`
- display driver integration -> `components/display`
- touch driver integration -> `components/touch`
- UI screens, assets, and callbacks -> `components/app_ui`
- Wi-Fi, SNTP, weather, DHT11, WS2812, SD/MP3 control APIs -> `components/app_services`

If a file mixes multiple responsibilities, split it instead of preserving the old shape.

## Prohibited Changes

Do not:

- add cross-directory relative includes such as `../` or `../../`
- hardcode Wi-Fi credentials, API keys, or board pins in feature `.c` files
- push feature logic into `src/main.c`
- let UI files read or write driver-owned globals directly
- copy generated directories such as `.pio/` or `build/` from the old project

## Migration Checklist

When migrating a module from `weather_clock_esp_idf`:

1. move it into the correct new component, not the old folder shape
2. remove relative includes
3. move constants into `board_config.h`, `app_config.h`, or `sdkconfig.defaults`
4. replace direct UI-to-driver coupling with a service API or a stub
5. verify compile, boot stability, and one key interaction

## Self-Check Before Finishing

Before wrapping up a task, confirm:

- the change respects `docs/architecture.md`
- new config is not buried in implementation files
- globals, queues, and tasks have a clear owner
- UI stayed presentation-focused
- the touched module still has a clean migration path for later milestones
