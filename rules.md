# esp32-lvgl-project-rules.md

---
name: esp32-lvgl-project-rules
description: Use when working on this ESP32-S3 embedded project built with PlatformIO, ESP-IDF, and LVGL. Enforce long-term project rules for architecture boundaries, UI integration, SquareLine workflow, coding style, module ownership, state management, concurrency, configuration handling, and code review expectations. Apply whenever creating, editing, reviewing, or restructuring code in this project.
---

# ESP32 LVGL Project Rules

## Core Architecture Rules

- Treat the project as a layered embedded system with `drivers`, `services`, `app`, and `ui`.
- Keep hardware access in `drivers`.
- Keep business logic, network logic, periodic work, and state mutation in `services`.
- Keep boot flow, shared state, and orchestration in `app`.
- Keep page creation, rendering, and event forwarding in `ui`.
- Do not collapse these boundaries for convenience.

## UI and SquareLine Rules

- Treat SquareLine output as generated UI code only.
- Keep generated files isolated under a dedicated generated UI area.
- Do not place business logic, HTTP requests, sensor reads, Wi‑Fi callbacks, or hardware control into generated UI files.
- Use presenters or controllers to bind application state to UI objects.
- Let UI events forward commands to services instead of directly touching drivers.
- Prefer state-driven rendering over imperative widget mutation scattered across modules.

## LVGL Threading Rules

- Access LVGL only from the designated UI execution context.
- Never call LVGL APIs directly from Wi‑Fi callbacks, HTTP callbacks, ISR paths, or service worker tasks.
- Move cross-thread updates through state changes, queues, or explicit UI-side refresh mechanisms.
- Avoid blocking operations inside LVGL timers or UI event handlers.

## State Management Rules

- Keep shared runtime state in a central `app_state`.
- Do not introduce scattered writable global variables across modules.
- Make each service the owner of its domain state.
- Expose snapshots or getters instead of raw mutable globals where possible.
- Distinguish clearly between:
  - board constants
  - runtime state
  - persisted user configuration

## Configuration and Persistence Rules

- Do not hardcode user Wi‑Fi credentials, API keys, cities, or user preferences in source files.
- Store persistent user-facing settings in NVS-backed configuration services.
- Keep board-level constants in a dedicated board config header.
- Keep default values explicit and centralized.

## Coding Style Rules

- Use lowercase snake_case for file names and function names.
- Use `_t` suffix for typedef types.
- Use ALL_CAPS for macros and compile-time constants.
- Use one declaration per line.
- Minimize variable scope.
- Mark file-local symbols `static` by default.
- Keep public headers small and module-focused.
- Split `init`, `start`, `stop`, and `get_snapshot` responsibilities cleanly.
- Remove dead commented-out code instead of preserving it inline.
- Prefer concise comments that explain why, not what.
- Keep files UTF-8 encoded.
- Do not leave large blocks of disabled legacy code in active source files.

## Module Ownership Rules

- Each module must have clear ownership of its state, resources, and lifecycle.
- Do not initialize the same resource from multiple unrelated modules.
- Do not let page initialization functions own hardware or service initialization.
- Keep `init` order explicit and centralized in the boot path.

## Error Handling and Logging Rules

- Use a consistent `TAG` name per module.
- Use `ESP_LOGE` for hard failures, `ESP_LOGW` for recoverable issues, and `ESP_LOGI` for important state transitions.
- Do not silently ignore meaningful return values.
- Treat network, weather, and sensor failures as recoverable unless they break a mandatory boot requirement.
- Degrade gracefully instead of blocking the UI.

## Embedded Concurrency Rules

- Keep blocking work in services, not UI.
- Use queues, event groups, or controlled state transitions for task communication.
- Do not use arbitrary shared globals for cross-task signaling.
- Guard shared mutable state explicitly when concurrent access exists.
- Keep ISR code minimal and do not let ISR paths own business logic.

## Review Rules

When reviewing code in this project, prioritize findings about:

- architecture boundary violations
- UI-thread violations
- hidden coupling between UI and hardware
- leaked writable globals
- duplicated initialization ownership
- blocking work in UI paths
- business logic placed in SquareLine-generated files
- hardcoded credentials or runtime config
- unclear module ownership
- missing graceful degradation paths

## Preferred Project Direction

- Favor maintainability over short-term convenience.
- Favor clear module boundaries over quick direct calls.
- Favor explicit state flow over hidden side effects.
- Favor generated-layout plus hand-written presenter logic over mixing everything into generated files.
