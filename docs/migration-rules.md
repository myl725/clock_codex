# clock_codex Migration Rules

## Purpose

This document governs how code is moved from `weather_clock_esp_idf` into `clock_codex`.
The goal is not to copy the old structure verbatim. The goal is to preserve working behavior while
removing the coupling that made the old project hard to extend.

## Migration Priorities

Migrate in this order unless a specific task says otherwise:

1. display
2. touch
3. LVGL/UI assets and screen framework
4. service stubs
5. time/SNTP
6. Wi-Fi/weather
7. DHT11
8. WS2812
9. SD/MP3/I2S

This keeps the platform bootable and debuggable at each milestone.

## Required Cleanup Before Landing Old Code

Before migrated code is accepted into `clock_codex`, do these cleanups:

1. remove cross-directory relative includes
2. remove hardcoded credentials, keys, or board constants from `.c` files
3. remove direct UI-to-driver coupling
4. move raw hardware configuration into `board_config.h` or `app_config.h`
5. rename files or symbols if ownership is unclear in the new structure

Do not defer these cleanups if they would preserve the exact coupling we are trying to eliminate.

## Allowed Migration Patterns

These patterns are allowed and encouraged:

- copy working driver logic, then wrap it behind a smaller component API
- keep temporary service stubs while real modules are not yet reconnected
- split one old file into multiple new files when it mixes UI, service, and driver concerns
- keep behavior-compatible defaults if they help the new project boot sooner

## Forbidden Migration Patterns

Do not do the following:

- copy the old folder tree directly into the new project
- preserve `../` or `../../` include chains
- let `app_ui` include old hardware module headers directly
- hardcode Wi-Fi SSID, password, API keys, or pin maps inside migrated feature files
- move old debug code, commented experiments, or dead helpers unless they are still needed
- add new functionality while a module is still being structurally cleaned up

## Validation Rules Per Module

Each migrated module must pass three checks before the next module starts:

1. it compiles inside `clock_codex`
2. startup remains stable and the device does not crash during bring-up
3. the key user-visible interaction for that module works, or a deliberate stub is in place

Examples:

- display: screen initializes and shows a valid frame
- touch: tap input reaches LVGL and drives at least one interaction
- UI: screens load without missing assets or unresolved symbols
- service stub: UI receives placeholder data instead of dereferencing old globals

## Special Rules for Old Project Artifacts

Do not migrate generated or environment-specific artifacts:

- `.pio/`
- `build/`
- generated `sdkconfig.*` snapshots unless a specific setting is intentionally extracted

Instead, copy only the settings that are still needed into tracked configuration files.

## Default Decision Rule

If there is a conflict between "copying old behavior fast" and "keeping the new boundary clean",
prefer the cleaner boundary unless it blocks first boot. If first boot would be blocked, use the
smallest temporary shim possible and document it in the commit or task notes.
