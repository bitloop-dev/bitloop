# Bitloop Diagnostic Decision Cases

Use these cases to forward-test `bitloop-development` diagnostic decisions after a meaningful routing or simulation-health change. This file is repository test material and is not distributed in the managed skill pack.

Give a fresh evaluator only the case prompt, the available skill, and the minimum repository artifacts the prompt requires. For routing checks, do not name the skill; for reasoning checks, explicitly select it so routing and policy failures remain distinguishable. Score the result afterward against the decision properties; do not require matching prose. Run the affected cases for a narrow change and the full set for a broad diagnostic rewrite.

Across applicable cases, assess whether the agent selects an oracle, behavior horizon and stop/extension rule, observation layers and cadences, relevant variants, scale-safe analysis, evidence status, and cleanup. Penalize invented physical contracts, fixed universal frame counts, pixel-only correctness claims, unnecessary raw serialization, and permanent objective-only probes.

## D01 — Render-Only Negative Control

Prompt: "Change a particle overlay's label color without changing simulation timing, state, or acceptance behavior. Validate the edit."

Expected decisions: use focused build and visual QA as needed; do not activate simulation-health diagnostics, invent a long run, or add telemetry.

## D02 — Framework-Neutral Trajectory Change

Prompt: "Optimise the project-owned particle update loop by changing its iteration and parallel reduction strategy. The public Bitloop API is unchanged. Validate the result."

Expected decisions: activate the behavioral gate despite framework-neutral ownership; identify ordering and floating-point risks; retain hard invariants, compare behavior over a justified horizon, and include deterministic or tolerance-based equivalence plus diagnostic perturbation checks when relevant.

## D03 — Blind Long-Horizon Particle Audit

Prompt: "A deterministic 256-particle continuum scene keeps a constant particle count and looks plausible in its startup smoke. Decide whether its persistent behavior is ready for acceptance."

Expected decisions: obtain the project physical/product contract; derive a horizon beyond startup from relevant mechanisms; inventory finite/range/conservation, spatial spread and clustering, nearest-neighbour behavior, and persistent identity degeneracy or lockstep as warnings unless forbidden; combine per-tick latches with selected whole-state inspections; extend unresolved trends rather than assuming a familiar frame count.

## D04 — Periodic Oscillator

Prompt: "Validate a driven oscillator whose suspected energy error may occur briefly at a particular forcing phase. Existing traces sample every 100 frames."

Expected decisions: cover multiple cycles and phases; reject one fixed cadence as sufficient when it can alias the forcing period; use a per-tick residual latch or observations dense enough for the transient, with offset/non-commensurate or event-phase samples for explanation.

## D05 — Rare Stochastic Transition

Prompt: "Validate that a seeded stochastic material transition remains acceptably rare over long runs. No failure appeared in one seed."

Expected decisions: define the acceptable rate or confidence claim; allocate work across independent seeds or event opportunities; keep hard monitors per run; report uncertainty rather than treating one clean trajectory as proof of impossibility.

## D06 — Chaotic Flow

Prompt: "A fluid scene matches a reference exactly for a short time, then trajectories diverge chaotically while aggregate behavior appears similar. Validate a solver refactor."

Expected decisions: use exact replay only inside a justified predictability window; compare invariants, distributions, spectra or event rates later; include convergence or metamorphic evidence where appropriate instead of demanding indefinite pointwise equality.

## D07 — Million-Entity Inspection

Prompt: "Investigate a possible local collapse in a one-million-particle simulation. Complete snapshots may be several gigabytes, and correctness matters more than a few extra minutes."

Expected decisions: preserve a strong oracle; preflight representative scan and output costs; prefer O(N) in-process reductions, chunk-local merges, spatial indexing, bounded exemplars, and trigger-local raw capture; do not default to O(N^2) pairs or serialize every frame.

## D08 — Nondeterministic Parallel Failure

Prompt: "A topology violation appears roughly once in several parallel runs but does not replay at the same frame. Diagnose it without changing production behavior."

Expected decisions: do not assume deterministic coarse-to-fine replay; latch the first trigger during the original run with bounded prehistory and local state; measure incidence across reruns; verify the monitor with positive/negative controls and check whether instrumentation perturbs ordering or timing; remove all temporary integration afterward.

## D09 — Checkpoint And Late Visual Evidence

Prompt: "Use a checkpoint to avoid an expensive warmup while validating a visual phase transition that occurs late in the simulation."

Expected decisions: justify checkpoint economics; prove uninterrupted-versus-restored continuation equivalence before relying on restored runs; select initial, transition-neighbor, and late visual frames; pair visual evidence with a semantic oracle and avoid treating a successful load or attractive screenshot as behavioral proof.
