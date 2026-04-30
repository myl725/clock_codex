# clock_codex Versioning Rules

## Purpose

This document defines how `clock_codex` should evolve over time using version control.
The goal is to make hardware bring-up, driver migration, UI changes, and speech experiments
traceable, reversible, and easy to continue on another machine.

## Default Tool

Use `git` as the default versioning system for this project.

Treat `clock_codex` as the main repository for all future work.
Treat `weather_clock_esp_idf` as a reference project, not the active target of iteration.

## Repository Scope

- Create and maintain a dedicated git repository in `clock_codex`.
- Do not mix the new repository history with the old reference project unless there is a deliberate archival reason.
- Keep project documentation, skills, and tracked configuration files inside the repository so another machine can resume work.

## What Must Be Tracked

Track these kinds of files in git:

- source code in `src/` and `components/`
- project documentation in `docs/`
- local project skills in `.codex/skills/`
- tracked build configuration such as:
  - `platformio.ini`
  - `sdkconfig.defaults`
  - `partitions.csv`
  - `idf_component.yml`
  - helper scripts under `tools/`

## What Must Not Be Tracked

Do not commit generated or machine-local artifacts such as:

- `.pio/`
- `build/`
- generated `sdkconfig.*` snapshots unless intentionally preserved as a reference
- serial monitor logs
- temporary test recordings or local dump files unless they are explicitly curated fixtures

If a generated file must be preserved for diagnosis, place it in a clearly named temporary folder
and remove it once the issue is resolved.

## Commit Granularity

Make one commit per verifiable milestone.

A good commit usually means one of these:

- one module was migrated and verified
- one driver bug was fixed and tested
- one project rule or document set was added
- one transport or service feature was integrated to a usable checkpoint

Do not combine unrelated work in one commit if it would make rollback or blame harder.

## Commit Timing

Create a commit after a change reaches a stable checkpoint.

Examples of stable checkpoints:

- `LVGL` compiles and boots
- display shows a valid frame
- touch reaches LVGL
- speech model packaging works
- WebSocket transport connects and returns text

Avoid committing half-migrated code that breaks boot unless the commit message clearly marks it as an experimental branch checkpoint.

## Commit Message Style

Use short, capability-oriented commit messages.

Recommended prefixes:

- `feat:` new feature or module milestone
- `fix:` bug fix or regression fix
- `refactor:` structural cleanup without intended behavior change
- `docs:` documentation update
- `build:` toolchain, partition, or build-system change
- `test:` test or verification helper

Examples:

- `feat: integrate lvgl runtime scaffold`
- `feat: migrate ili9341 display driver`
- `feat: add xpt2046 lvgl input driver`
- `feat: add inmp441 speech demo skeleton`
- `fix: clamp touch coordinates before lvgl dispatch`
- `build: add custom upload flow based on flash_args`
- `docs: record current speech integration status`

## Branching Rules

Keep the branch model simple.

- `main`
  - always represents the latest reasonably stable line
  - should compile, and ideally should boot
- `feature/<name>`
  - use for larger experiments or milestones that may destabilize the project

Recommended feature branch names:

- `feature/inmp441-input`
- `feature/speech-websocket`
- `feature/xiaozhi-style-transport`
- `feature/app-ui-migration`

If a change is very small and low risk, it may go directly into `main`.
If a change touches multiple subsystems or has uncertain behavior, prefer a feature branch.

## Tagging Rules

Tag important stable milestones so they are easy to recover later.

Use lightweight milestone tags such as:

- `v0.1-lvgl-base`
- `v0.2-display-touch`
- `v0.3-speech-skeleton`
- `v0.4-websocket-loopback`

Create a tag only when the corresponding state is known to be reproducible.

## Hardware Bring-up Rule

For hardware-related changes, prefer this sequence:

1. make the code change
2. compile successfully
3. upload successfully
4. verify the physical behavior
5. then commit

If step 4 cannot be completed immediately because hardware is unavailable, the commit message or task notes should say so.

## Cross-Machine Continuity

To continue on another machine reliably:

- commit the latest stable state
- push to a remote repository when possible
- keep `docs/current-status.md` updated for non-obvious blockers

Do not rely on chat history as the only place where current project context exists.

## Document Update Rule

Update documentation in the same milestone when:

- architecture decisions change
- hardware pin assumptions change
- build or upload workarounds change
- a new major blocker is discovered
- a new integration path becomes the preferred direction

At minimum, keep these files aligned with reality:

- `docs/current-status.md`
- `docs/architecture.md`
- `docs/versioning.md`

## Recovery Rule

If an experiment goes wrong:

- use git history to return to the last stable milestone
- do not manually reconstruct a known-good state from memory

If a feature branch becomes too messy:

- create a fresh branch from the last stable commit
- reapply only the still-valid pieces

## Minimum Workflow

The default workflow for this project should be:

1. change code in `clock_codex`
2. compile
3. upload and verify when hardware is involved
4. update docs if project state changed
5. commit with a focused message
6. tag if the checkpoint is a milestone
7. push if cross-machine continuity matters
