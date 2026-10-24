# Visual QA

Use this page only when visual judgment is part of the task. This includes visual polish, style changes, readability work, screenshot review, and visual encodings such as trails, glow, overlays, labels, color maps, glyphs, particles-as-markers, or UI annotations.

Do not load this page for pure simulation behavior, math, physics, data model, threading, checkpointing, trace-only, or scale changes unless the user also asks for visual output changes or the rendered output is the evidence being used to judge success.

## Core Principle

Visuals must preserve semantic clarity. Effects and decoration should support the underlying state, behavior, and relationships rather than obscure or misrepresent them.

This applies to games, simulations, tools, scientific views, generative experiments, fields, particles, graphs, and unusual visual systems.

## Review Questions

After rendering a screenshot or snapshot, inspect it at normal viewing scale and ask:

- What are the primary objects, quantities, relationships, or states the user needs to see?
- Are they immediately distinguishable without squinting or prior knowledge?
- Do effects, trails, glow, blur, overlays, labels, or UI elements hide the thing they represent?
- Do visual effects misrepresent position, size, density, direction, speed, collision, or importance?
- Does the image still communicate the simulation or experiment before it communicates the style?
- Would a smaller, subtler, or less persistent effect communicate the same idea more clearly?

If an effect competes with the object or state it represents, reduce or remove the effect before calling the work done.

## Visual Encodings

Treat visual encodings as data displays, not only decoration. A trail should reveal motion without swallowing the object. A glow should emphasize an object without becoming the object. A label should identify state without covering the state. A color map should make differences legible without exaggerating meaning.

Prefer restrained defaults:

- Keep primary object cores visible above their effects.
- Make halos and trails lower contrast than the object they represent.
- Limit trail length and radius unless persistence itself is the subject.
- Use opacity and size falloff so older samples recede.
- Check that small objects remain visible on top of their glow/trail.
- Verify overlays do not hide collisions, boundaries, axes, or dense regions.

## Snapshot Workflow

When visual QA is activated and the app can render headlessly:

1. Choose the cheapest behavior-representative timeline. A short pass is sufficient only for static or immediate claims; evolving visuals need initial, transition, and late-horizon evidence, with multiple phases when periodic behavior matters.
2. Build and run the corresponding snapshot or screenshot pass.
3. Inspect the selected normal-scale images yourself.
4. Compare the rendered sequence against the task's visual purpose and semantic state.
5. Adjust visual parameters if the primary semantics are unclear.
6. Report what was inspected and any remaining temporal or visual risk.

Do not treat "nonblank and visually impressive" as sufficient. The visual result should be readable, truthful to state, and useful for the user's purpose.

