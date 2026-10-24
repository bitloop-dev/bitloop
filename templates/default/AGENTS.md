# {SIM_NAME} Project Instructions

## Project Boundary

This repository is the ownership boundary for `{SIM_NAME}`. Keep its simulation code, assets, CMake wiring, third-party dependencies, architecture documentation, and diagnostics inside this project.

The project must remain independently cloneable, configurable, buildable, and runnable even when a parent Bitloop project includes it with `bitloop_add_dependency(...)`.

Do not modify Bitloop framework source or a parent/sibling project unless the user explicitly requests work across that boundary. When running inside a parent workspace, continue to honor the parent's cross-repository safety and ownership instructions.

## Bitloop Guidance

Use the repository-local `bitloop-development` skill whenever implementation or validation depends on Bitloop-owned APIs, integration, tooling, or framework behaviour. The skill owns evolving generic Bitloop guidance; keep project-specific policy in this file.

Also use its simulation-health workflow when project-owned, framework-neutral work can change or assess a persistent simulation trajectory; the project still owns the physical contract and correctness policy.

Load only the references relevant to the current task. For exact or version-sensitive API details, inspect the framework headers used by the active build when they are already available. Do not configure, install, or update packages merely to answer a documentation question.

If `ARCHITECTURE.md` or a project-local documentation index exists, read only the sections relevant to the requested subsystem. Architecture documentation is reference material, not a replacement for this instruction file.

## Scope And Architecture

Implement the requested behavior one coherent step at a time. Do not anticipate future mechanics, controls, analysis layers, automation, visual polish, optimization, or telemetry.

Ask before adding a dependency or changing architecture when it broadens ownership, introduces a parallel framework-like system, or serves speculative future needs. Add third-party packages to this project's own `vcpkg.json` and wire them in this project's own `CMakeLists.txt`.

Keep simulation mutation in process/event hooks and drawing as a projection of current state. Use existing Bitloop facilities before creating project-specific substitutes.

## Build And Validation

Derive the root application target from this project's `bitloop_new_project(...)` call. Use Codex-owned local presets/build directories when building, and preserve user build state.

Choose validation that proves the requested outcome: compilation for API/wiring, semantic traces for behavior, snapshots for visual claims, and checkpoints only when restart economics justify them.

If the matching Bitloop package version is not yet published in the configured registry, do not silently update dependencies or claim standalone compatibility. Report standalone validation as deferred.

## Plans And Diagnostics

Store Codex working artifacts under this project:

```text
.codex/plans/
.codex/runs/run0000_YYYYMMDD_HHMMSS/
```

Use the next sequential run index, preserve existing runs unless cleanup is requested, and keep temporary probes behind `BITLOOP_DEV_MODE`. Reusable instructions belong under `.agents/skills`, not `.codex`.
