---
name: bitloop-development
description: Develop, extend, diagnose, build, or review Bitloop C++ framework and simulation projects. Use for Bitloop project hierarchy and CMake macros, Project/Scene/Viewport lifecycle, drawing, ImGui buffered state, camera/input, math and precision, threading/performance, assets, headless traces, snapshots, checkpoints, temporary diagnostics, visual QA, or generated-project workflows. Do not use for unrelated generic C++ work that does not involve Bitloop.
---

# Bitloop Development

Use the smallest relevant reference set. Do not load every reference preemptively.

Name C++ members without prefixes or suffixes such as `m_` or a trailing `_`; resolve ambiguity with `this->` or a clearer parameter/local name, and do not rename unrelated existing code solely for style.

Omit comments for self-evident C++; add one brief intent comment before theory-dependent mathematical or scientific code.

Prefer compact formatting and tolerate lines up to ~180 characters; wrap only when it clarifies structure, keeping related expressions together and aligning continuations when useful.

## Establish ownership

1. Identify the active Bitloop project from explicit project or target paths; otherwise use the nearest `.bitloop-workspace` when present.
2. Apply the nearest `AGENTS.md` governing that target. Read it only if it has not already been supplied or read for this job. If a parent instruction file routed you there manually, continue to honor the parent's cross-repository safety and ownership rules.
3. Treat the active project as the ownership boundary for its simulation code, data, CMake, dependencies, diagnostics, and architecture documents.
4. Do not change the framework or a sibling project unless the request explicitly spans that boundary.

For a substantial, broad, ambiguous, multi-system, or multi-stage work package, read [advanced-project-workflow.md](references/advanced-project-workflow.md). For ordinary incremental work, implement only the requested behavior.

## Resolve framework truth

Use these references for concepts and established conventions. When an exact signature, overload, compile definition, or version-sensitive behavior matters, inspect the authoritative headers or implementation used by the active build:

1. In a framework checkout, paths in the references are relative to the Bitloop source root, such as `include/bitloop` and `src`.
2. In a gallery checkout, the source is normally under its `bitloop` child repository.
3. In a package-backed standalone project, use already-installed headers under the current build's vcpkg installation when available. Do not configure, install, or update packages merely to answer a documentation question.
4. If authoritative source is unavailable, state the uncertainty instead of inventing an API.

Start with [framework-map.md](references/framework-map.md) only when orientation is needed.

## Route by task

- Project creation, generated shape, project ownership, and parent composition: [project-workflow.md](references/project-workflow.md).
- CMake, targets, dependencies, package ownership, and project wiring: [build-cmake.md](references/build-cmake.md).
- Establishing or repairing a Codex-owned configure/build/run context: [configure-build-run.md](references/configure-build-run.md).
- Project, scene, viewport, update, draw, event, and cleanup hooks: [scene-lifecycle.md](references/scene-lifecycle.md).
- ImGui controls and double-buffered live state: [imgui-ui.md](references/imgui-ui.md).
- Scalar/vector/geometry types and precision selection: [types.md](references/types.md).
- Camera setup, transforms, navigation, and viewport sizing: [camera-viewport.md](references/camera-viewport.md).
- `Viewport*` drawing APIs and coordinate modes: [drawing.md](references/drawing.md).
- Mouse, touch, keyboard, gesture, and event ownership: [input-events.md](references/input-events.md).
- Shared thread pool and batched work: [threading.md](references/threading.md).
- Scale limits, algorithm choice, batching, and performance-sensitive design: [performance.md](references/performance.md).
- Fonts, images, shader surfaces, paths, and project data: [assets-data.md](references/assets-data.md).
- Stable semantic traces and indexed run folders: [tracing-diagnostics.md](references/tracing-diagnostics.md).
- Visual snapshots, native-resolution/custom pixel routes, and restartable checkpoints: [checkpoints-snapshots.md](references/checkpoints-snapshots.md).
- Short-lived probes that must not become permanent architecture: [temporary-diagnostics.md](references/temporary-diagnostics.md).
- Simulation correctness, long-horizon anomalies, adaptive committed-state inspection, and temporary health monitors: [simulation-health-diagnostics.md](references/simulation-health-diagnostics.md).
- Visual polish, readability, screenshots, trails, glow, overlays, labels, or color encodings: [visual-qa.md](references/visual-qa.md).

Load visual QA only when visual judgment is part of acceptance. Load threading and performance when scale or throughput is explicitly important. Load temporary diagnostics only when a focused probe is necessary.

Any implementation, optimisation, or diagnosis capable of changing or assessing a persistent simulation trajectory or its restoration—including initialization/configuration, time stepping, solver or interaction laws, topology, RNG or precision, execution ordering or parallelism, performance refactors, checkpoint loading, and cumulative or stochastic dynamics—requires an explicit behavioral-horizon decision before acceptance. A short run proves startup and integration only. If existing focused tests do not already cover delayed, cumulative, recurrent, and rare behavior over a justified horizon, load [simulation-health-diagnostics.md](references/simulation-health-diagnostics.md) and run that gate. Ordinary UI, drawing, input, lifecycle, and build edits do not need it unless they affect simulation timing, ordering, or state.

## Preserve diagnostic boundaries

- Keep Codex-generated plans and retained run evidence under the active project `.codex` directory. Keep high-volume, immediately disposable health data in a manifest-owned session under the Codex build directory and remove it after reducing it to useful findings.
- Use sequential `runNNNN_YYYYMMDD_HHMMSS` folders and preserve earlier runs unless cleanup is requested.
- Keep stable telemetry minimal. Put disposable probes behind `BITLOOP_DEV_MODE`, use a diagnostics namespace, and remove them when the objective is complete.
- Never infer semantic correctness solely from pixels when a trace or focused metric is required.

## Validate proportionally

- Build the smallest target that proves wiring or API correctness.
- Use semantic traces for behavior, snapshots for visual claims, and checkpoints only when restart economics justify them.
- Preserve standalone project buildability even when working through a parent gallery.
- Report any validation intentionally deferred because the matching package, toolchain, or registry release is unavailable.
