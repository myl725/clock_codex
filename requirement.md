# esp32-s3-smart-ambient-clock-requirements.md

# ESP32-S3 Smart Ambient Clock Project Requirements

## Summary

Build a new ESP32-S3 embedded application using `PlatformIO + ESP-IDF + LVGL` for a smart ambient desktop device. The product is not a multi-feature prototype. The first official version focuses on a polished core experience around:

- time display
- Wi‑Fi connectivity
- SNTP time sync
- weather display
- WS2812 lighting effects
- local persistent configuration
- first-boot setup flow

The new project is a fresh implementation. The old project is reference material, not an architectural base.

## Product Goal

Create a stable, atmosphere-first desktop device that:

- looks intentional and visually coherent
- provides reliable clock and weather information
- remains usable while offline
- is maintainable as a long-term embedded product
- can later accept extensions such as music or pomodoro without restructuring the core

## v1 Scope

### In Scope

- main home screen
- setup/config flow
- Wi‑Fi status
- SNTP sync
- weather fetch and display
- WS2812 lighting mode and brightness control
- local configuration persistence
- graceful offline degradation

### Out of Scope for v1 Core Delivery

- music playback
- pomodoro
- SD card media flows
- advanced personalization beyond basic preferences
- large feature menus or multi-mode product behavior

These features may be added later, but they must not drive the first implementation.

## Platform and Tooling

Use:

- VSCode + PlatformIO
- ESP-IDF framework
- LVGL for UI
- NVS for persistent configuration
- ESP-IDF native networking and system libraries

Do not redesign the project around Arduino-style dependencies.

## Hardware Assumptions

Assume the current hardware platform remains in use:

- ESP32-S3
- LCD display
- touch input
- Wi‑Fi
- WS2812 lighting
- optional DHT11 sensor support

The new design should preserve the ability to reuse known-good pin mappings, display parameters, touch parameters, and lighting/sensor low-level details from the old project where useful.

## System Architecture

Use a layered structure:

- `drivers`
- `services`
- `app`
- `ui`

### drivers

Responsible for:

- board-level definitions
- GPIO/RMT/SPI/I2C/I2S/display/touch/sensor/light low-level access
- peripheral initialization and raw device interaction

Not responsible for:

- UI logic
- network workflows
- weather parsing
- page state management

### services

Responsible for:

- configuration service
- network service
- time service
- weather service
- lighting service
- optional sensor service

Services own domain behavior, asynchronous work, retries, and state updates.

### app

Responsible for:

- boot flow
- central application state
- startup orchestration
- route decisions such as setup vs home

### ui

Responsible for:

- screen creation
- state rendering
- event forwarding
- route display

UI must not directly own network requests, sensor reads, or hardware control.

## UI Strategy

Continue using SquareLine Studio if it helps with layout and asset generation, but treat it as a layout generator only.

### Rules

- SquareLine output is generated code.
- Generated files should remain isolated.
- Custom behavior must live outside generated screen files.
- Presenters/controllers should bind state to widgets and forward UI events to services.

### UI Priorities

- atmosphere-first visual direction
- clear primary time display
- low-friction weather/status presentation
- minimal clutter
- gentle, intentional motion
- responsive interaction
- readable but not dashboard-like

## Boot Flow Requirements

On boot:

1. initialize core runtime
2. load persisted configuration
3. decide whether setup is required
4. if configuration is incomplete, show setup screen
5. if configuration is complete, show home screen immediately
6. continue Wi‑Fi, SNTP, and weather work asynchronously

Boot must not block the UI on weather success or full network readiness.

## Setup Flow Requirements

The setup flow must support at least:

- Wi‑Fi SSID
- Wi‑Fi password
- weather API key
- city
- basic brightness or theme preference
- setup-complete persistence

The setup flow should be simple and reliable. It does not need to be visually elaborate in v1.

## Offline and Degradation Requirements

When offline:

- clock must continue working
- lighting must continue working
- UI must remain responsive
- weather must show offline state or cached last-known data

Network or API failures must be treated as degradation, not fatal crashes.

## State Model Requirements

Define a central application state covering at least:

- boot stage
- config readiness
- network connectivity
- IP readiness
- time sync status
- weather validity
- weather snapshot
- weather last update
- lighting mode
- lighting brightness
- optional sensor snapshot

The UI must render from this state rather than from scattered globals.

## Configuration Requirements

Persist at least:

- Wi‑Fi SSID
- Wi‑Fi password
- weather API key
- city
- lighting brightness
- basic theme or visual preference
- setup-complete flag

Do not hardcode user credentials or weather API values in source.

## Concurrency Requirements

- Keep LVGL access confined to the UI execution context.
- Keep blocking work in services.
- Use queues, events, or controlled state transitions between tasks.
- Do not directly update LVGL from Wi‑Fi callbacks, HTTP callbacks, or sensor tasks.
- Do not run blocking network requests inside LVGL timers or UI callbacks.

## Migration Strategy

Use the old project only as a reference for:

- pin assignments
- display and touch parameters
- DHT11 implementation details if needed
- WS2812 implementation details if needed
- reusable images and fonts
- validated low-level hardware behavior

Do not copy the old architecture into the new codebase.

## Recommended Build Sequence

Implement in this order:

1. clean PlatformIO shell
2. minimal ESP-IDF boot and logs
3. LVGL and display bring-up
4. empty layered project structure
5. central app state and boot router
6. setup and home screen placeholders
7. configuration persistence
8. Wi‑Fi service
9. SNTP time service
10. weather service
11. lighting service
12. optional sensor integration

## Acceptance Criteria

The new v1 foundation is acceptable when:

- the project builds cleanly in PlatformIO
- boot reliably chooses setup vs home
- LVGL UI remains responsive
- no blocking network work runs in UI paths
- settings persist across reboot
- time and weather update asynchronously
- offline behavior degrades gracefully
- architecture boundaries remain intact
- SquareLine-generated files do not contain business logic

## Default Assumptions

- Keep `PlatformIO + ESP-IDF`
- Keep `LVGL`
- Keep SquareLine as a layout tool only
- Focus v1 on `lighting + time + weather`
- Defer music and pomodoro
- Use NVS for local persistent settings
- Optimize for long-term maintainability rather than fast feature stacking
