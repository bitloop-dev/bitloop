# Bitloop Framework Instructions

## Instruction Routing

Use the repository-local `bitloop-development` skill for work involving Bitloop projects or framework APIs. Load only the references routed by its `SKILL.md`; inspect authoritative headers or implementations when exact signatures or version-sensitive behavior matter.

When this repository is embedded in a gallery or another superproject, continue to honor the parent repository's cross-repository ownership and safety rules. This file governs framework-owned paths only.

## Framework Ownership

This repository owns the Bitloop runtime, public headers, CMake integration, CLI, default project templates, examples, tests, and canonical Codex framework skill.

Project-specific simulation behavior, assets, dependencies, diagnostics, and architecture belong to the project using Bitloop. Do not move project policy into the framework merely because the project is currently built through a parent gallery.

## API Evolution

Prefer the cleanest long-term API over compatibility aliases, duplicate spellings, forwarding wrappers, or transitional overloads. When an intentional breaking change is the better design, update all in-repository callers, examples, tests, templates, and relevant skill references together. Add compatibility behavior only when the user explicitly requests a compatibility strategy.

Before adding a framework abstraction, confirm that it represents reusable framework behavior rather than one project's needs. Ask for confirmation when a proposal broadens ownership, couples unrelated systems, or anticipates speculative future requirements.

## Project And Package Boundaries

- Generated projects must remain independently cloneable, configurable, buildable, and runnable.
- A child project's `vcpkg.json` remains the source of truth for packages it uses even when a parent build merges manifests.
- Do not silently update dependency baselines, framework tags, or registries as part of unrelated work.
- Treat installed headers or the active framework checkout as authoritative over prose when versions differ.

## Implementation Discipline

Implement the requested behavior without adding future mechanics, controls, diagnostics, polish, or dependencies. Use Bitloop's existing lifecycle, drawing, buffered-state, input, threading, tracing, snapshot, and checkpoint surfaces before inventing parallel systems.

For high-performance work, use the threading and performance references from the skill. Expensive independent phases should normally use `Thread::forEachBatch` or `Thread::pool()` unless correctness requires ordered or synchronized execution.

Do not mutate simulation state in draw hooks. Keep temporary probes behind `BITLOOP_DEV_MODE` and remove objective-only diagnostics when they are no longer useful.

## Agent-Pack Ownership

The canonical reusable guidance lives under `.agents/skills/bitloop-development`. Generated projects receive a managed snapshot when AI-assisted scaffolding is selected.

- Edit the canonical pack here, not a generated project copy.
- Keep detailed knowledge in `references/` and keep `SKILL.md` as a concise router.
- Regenerate and validate `agent-pack.json` whenever managed pack files change.
- `bitloop agents init` may install the managed skill only into one explicitly selected project. Recursive sync may update only projects that already contain the managed pack.
- Agent-pack commands must never overwrite a project's own `AGENTS.md`, architecture, source, `.codex` artifacts, CMake, or manifests.

## Validation

Run the smallest relevant framework tests. For agent-pack work, run the Python skill validator and isolated agent-pack/template tests without configuring a generated project unless package-backed validation is explicitly authorized.

Do not claim standalone compatibility against an unpublished package version. Record that gate as deferred until the matching framework tag and registry baseline exist.
