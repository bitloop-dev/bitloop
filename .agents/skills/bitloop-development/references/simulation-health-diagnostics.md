# Simulation Health Diagnostics

## Contents

- When this workflow applies
- Diagnostic design, gate classification, horizon selection, and correctness oracles
- Baseline anomaly inventory and committed-state timing
- Per-tick monitors, observer checks, and adaptive full-state inspections
- Large-state analysis and ephemeral sessions
- Refinement, regression, and cleanup

Use this page when a simulation may be numerically or semantically wrong even though it builds and looks plausible: long-horizon drift, unexpected overlap, instability, conservation loss, topology corruption, phase-transition errors, rare events, or scale-dependent behavior. It also applies when implementation, optimisation, or restoration can change a persistent trajectory and existing tests do not already prove delayed, cumulative, recurrent, and rare behavior over a justified horizon.

Do not load this page for ordinary UI, drawing, input, lifecycle, or build edits. Read `temporary-diagnostics.md` for probe boundaries, `performance.md` when inspection cost matters, and `checkpoints-snapshots.md` only when replay economics or visual state require them.

## Core Principle

Inspect as much authoritative state as the question requires, but expose only the evidence needed to reason about it.

A complete committed-state inspection is not the same as serializing a complete snapshot. A temporary C++ probe may scan every entity in memory, reduce gigabytes of state to statistics and a few suspicious stable IDs, and write only kilobytes. Serialize raw state only when post-hoc analysis actually needs information that cannot be reduced safely in process.

Diagnostic cost is not automatically a reason to weaken an oracle. Estimate it, choose an intentional budget, and prefer correctness when the user accepts a slower investigation. Avoid waste that adds no information: repeated identical scans, quadratic pair searches when a spatial method is available, or raw output the analyzer immediately discards.

## Define Correctness Before Running

Write a short diagnostic design record before adding probes. It may remain in working notes unless its conclusions are needed as retained evidence:

1. scenario, seed, configuration, fixed `dt`, render/output-frame mapping, authoritative state, and commit boundary;
2. behavioral claim or hazard, temporal signature (immediate, transient, cumulative, recurrent, rare, stochastic, or chaotic), affected mechanisms, expected regimes, and earliest plausible delayed failure;
3. strongest available oracle, units and scale-relative tolerances, with hard failures, quality warnings, and merely surprising behavior distinguished;
4. candidate horizon in fixed ticks and simulated time, representative or adversarial variants, and explicit extension and stopping conditions;
5. baseline anomaly inventory, per-tick monitors, selected complete-state inspections, semantic or visual captures, and evidence required to call the issue reproduced, absent, fixed, or still ambiguous;
6. estimated scan, storage, and replay cost, including which evidence can be reduced in process and which—if any—requires raw serialization.

Use the strongest available oracle, in roughly this order:

- explicit user, product, or physical contract;
- hard invariants such as finite state, identity uniqueness, contract-required containment, valid topology, and legal phase/state ranges;
- conservation or bounded-residual laws with stated sources, sinks, and numerical tolerance;
- analytic or deliberately simplified reference cases;
- convergence under smaller time steps, finer resolution, or tighter solver tolerances;
- deterministic replay and state fingerprints;
- metamorphic relations such as translation, rotation, relabeling, permutation, or mirrored initial conditions;
- comparison with a trusted implementation or recorded baseline;
- visual judgment for properties that are genuinely visual.

Do not turn an intuition into a hard failure silently. If no authoritative oracle distinguishes a novel behavior from a bug, report the observation and evidence, then seek product or physical judgment before changing production behavior.

## Classify The Validation Gate

Do not let one short run silently stand for every kind of evidence:

- **Smoke** proves construction, wiring, basic advancement, and immediate sanity. It may be only a few frames or ticks and cannot accept persistent simulation behavior.
- **Behavioral validation** covers the regimes, transitions, recurrence opportunities, and cumulative error relevant to the changed dynamics. This is the normal acceptance gate for a meaningful state-evolution change unless an existing focused gate already supplies equivalent coverage.
- **Endurance validation** extends beyond the behavioral claim to sustained resource use, exceptional recurrence, or unusually long stability. Require it only when the claim or risk needs it.

Record which gate each run supplies. More frames do not automatically turn a smoke test into behavioral evidence; the run must exercise the relevant mechanisms and use an oracle capable of recognizing failure.

## Choose An Adequate Behavior Horizon

Choose the horizon from simulated mechanisms rather than a familiar render-frame count. Render frames may contain several fixed ticks, and UI pacing is not simulated time.

1. Enumerate the slowest relevant physical and numerical timescales: domain or signal crossing, advection, damping or relaxation, forcing cycles, diffusion or thermal transport, phase rates, neighbourhood turnover, contact or topology recurrence, and plausible accumulation of solver error.
2. Cover initialization, onset, every expected regime or transition, and enough post-event evolution to judge recovery or persistence. Include multiple opportunities for recurrent or conditional behavior where one occurrence would be weak evidence.
3. Prefer an analytic estimate, a measured characteristic period, or a conservative scale argument. State the estimate and its safety margin rather than presenting the final tick count as self-justifying.
4. When the slowest timescale is unknown, use progressively and materially longer deterministic windows. Compare extrema, residual envelopes, distribution quantiles, event rates, degeneracy findings, and trend slopes between windows. The growth pattern is a method, not a fixed sequence or multiplier.
5. Do not accept the first apparently healthy window as long-horizon evidence. After all expected regimes have appeared, require a materially longer confirmation window capable of revealing continued drift, recurrence, or a new regime.
6. Extend while new extrema or regimes appear, a trend remains unresolved, warning incidence changes materially, or the latest window contradicts the proposed steady or bounded behavior. Stop when expected regimes have been covered, hard monitors remain clean, quality measures are bounded or converged for the claim, and the longer confirmation adds no materially new behavior. If the diagnostic budget ends first, report uncertainty instead of a pass.

One deterministic trajectory cannot establish behavior that depends materially on a seed, configuration, resolution, or symmetry. Use representative and adversarial variants, convergence runs, or metamorphic cases as the contract requires; allocate the budget across them deliberately rather than spending it all on one arbitrarily long run.

Match the evidence to the dynamics:

- For recurrent or periodic behavior, cover enough cycles and distinct phases to evaluate recurrence, drift, and recovery.
- For stochastic behavior, budget independent seeds or event opportunities from the claim and report the remaining uncertainty; observing no failure is not proof that its probability is zero.
- For chaotic behavior, use exact replay or pointwise comparison only over a justified predictability window. Beyond it, compare hard invariants, bounded statistics, distributions, event rates, and convergence properties instead of demanding identical trajectories.

## Build A Baseline Anomaly Inventory

Do not instrument only the failure already suspected or the healthy facts named in the prompt. Behavioral acceptance of a new or changed trajectory requires a compact baseline inventory derived from the authoritative state schema and project contract; explicitly scope out irrelevant categories instead of silently omitting them. Consider:

- finite values, legal ranges, counts, stable identities, references, and topology;
- conserved or bounded totals with known sources and sinks;
- spatial bounds, center and spread, nearest-neighbour or density distributions, clustering, and their trends;
- per-identity displacement, activity, and distinct identities that become persistently indistinguishable or move in numerical lockstep across contract-relevant fields;
- event frequencies, state or material populations, correlations, and deterministic fingerprints.

Not every unusual value is a generic failure. Classify universal invariants as hard failures and semantic assumptions such as separation, dispersion, or independent motion as warnings until project physics supplies an oracle. The purpose of the inventory is to surface unanticipated behavior for review, not to smuggle one material model into every simulation.

## Observe The Right State

Locate the authoritative state owner and the commit boundary before instrumenting. Unless the hypothesis concerns an intermediate solver phase, health checks should inspect the finite committed state after structural changes and the simulation update have completed.

In the current Bitloop headless lifecycle, scene traces are collected after `processHeadlessFrame(...)`; inspect the active framework source when exact ordering matters. A project may also need a temporary call immediately after its own solver transaction commits. Never read a shadow buffer and describe it as committed state.

Define frame numbering in the diagnostic contract. Distinguish initial state, first committed tick, and post-frame trace state so an apparent one-frame disagreement is not mistaken for corruption.

Probes must be observational. They may reuse read-only production queries, spatial indexes, and deterministic scratch, but must not mutate authoritative state, change iteration order, consume random numbers, or alter scheduling. If fail-fast behavior would hide later evidence, record the first failure and continue long enough to characterize it. Stop at the transition on a reproducible rerun; otherwise preserve bounded trigger context during the original run.

## Verify The Observer

Before trusting a nontrivial temporary monitor or analyzer, exercise a synthetic positive case that must trigger and a known-good negative case that must remain clean. Add a threshold-boundary case when tolerance logic matters. If instrumentation could affect timing, task order, RNG consumption, or floating-point reductions, compare diagnostic-on and diagnostic-off runs using the strongest stable invariants or fingerprints available. A direct, self-evident finite/range assertion does not require elaborate fixtures.

## Use Three Complementary Layers

### Cheap Per-Tick Health Monitors

Run focused checks every committed tick across the complete chosen horizon when a failure can appear and disappear between sampled frames. Typical checks include:

- non-finite values and impossible enum/state values;
- entity counts, stable-identity uniqueness, or topology generation validity;
- contract-required containment violations and scale-relative extrema;
- conservation residuals or energy injection beyond known sources;
- event counts and the first frame/IDs crossing a threshold;
- a small rolling summary needed to explain the lead-up to a trigger.

“Cheap” is relative to the simulation. An existing O(N) state pass may absorb a few reductions; a new global pair comparison is not cheap. Keep reports bounded and preserve the first occurrence, worst value, total occurrence count, and a small set of stable-ID examples.

### Selected Complete Committed-State Inspections

At intentionally selected frames, inspect every relevant field of every relevant entity, not only the values already suspected. Compute summaries that can reveal unanticipated failure modes:

- counts by state/material/type and stable-ID integrity;
- finite/range summaries for positions, velocities, energies, masses, temperatures, and transported quantities;
- spatial bounds, center of mass, momentum, energy, and contract-specific residuals;
- topology degree/components, boundary ownership, and invalid references;
- correlations or joint conditions relevant to the hypothesis;
- nearest-neighbour and density distributions, per-identity motion, and scale-relative duplicate-state or lockstep-persistence findings;
- deterministic fingerprints when replay comparison is meaningful.

This is a complete inspection, not necessarily a complete copy. Scan authoritative contiguous arrays directly, merge chunk-local reducers, and emit compact frame summaries plus capped exemplars.

Compare complete inspections across time windows rather than treating each as an isolated snapshot. Track whether bounds, quantiles, residuals, clusters, event rates, and suspicious stable IDs persist, converge, drift, or enter a new regime. A coarse scan can discover a candidate; when replay is reproducible, a dense neighboring rerun establishes its transition and persistence.

### Local Semantic And Visual Captures

When a global scan finds suspicious IDs, regions, or frames, rerun deterministically and capture a small local neighborhood, event history, or `traceCanvas(...)` image. Local evidence explains a mechanism more effectively than flooding the trace with every entity.

Semantic trace cadence is independent of monitor and complete-inspection cadence. A long headless run may use `--trace-every-frames` or `--trace-at` to avoid calling and serializing `traceFrame(...)` on every output frame, but sparse semantic frames must not become the only correctness oracle: retain per-tick monitors for failures that can appear between samples and execute selected complete inspections at their planned committed-state boundaries. Bound the maximum semantic-trace gap by the shortest relevant phenomenon that no independent monitor latches. Use `--trace-every-frames 0` only when those independent layers fully cover the claim. Once a coarse run finds a suspect window, rerun it with every-frame or exact neighboring semantic traces to recover the causal sequence.

## Choose Inspection Frames Adaptively

Choose the behavior horizon first; then choose inspection frames within it. There is no universal “every 100 frames” rule. The agent that understands the hypothesis should own the initial schedule; delegating frame-number selection without that context is not inherently more intelligent. Independent review is useful only for a large or high-risk investigation where a second hypothesis set is worth the context reload.

Construct the first schedule from:

1. lifecycle landmarks: initial/first committed state, initialization settling, forced events, phase changes, and final horizon;
2. logarithmic or Fibonacci-like coverage of early and long-time behavior so both fast transients and slow drift are visible;
3. hypothesis windows around predicted onset, forcing periods, collision times, or user-observed frames;
4. neighboring frames where persistence or a one-tick transition matters;
5. an empirical scan-cost measurement and the accepted diagnostic budget.

Record why each frame group exists. Dense consecutive samples belong around transitions; sparse growth-spaced samples belong across quiet horizons. For a small 256-particle system, a broad schedule plus periodic coverage may be inexpensive. For a million entities, selected O(N) scans can still be appropriate, while their number should be justified by measured cost and information value.

When periodic or recurrent behavior is plausible, do not rely on one fixed stride that may repeatedly observe the same phase. Include event-phase landmarks or deterministic offset/non-commensurate gaps. If a relevant transient can fit entirely between observations and no per-tick monitor catches it, shorten the appropriate cadence or add a focused latch.

Before committing to a large run, measure one representative monitor-enabled interval and complete inspection, plus one semantic frame or raw snapshot only when that channel is planned. Forecast total runtime and output size, then remove redundant evidence channels rather than weakening the oracle.

Do not automatically give complete inspections, semantic traces, and visual captures the same cadence. Complete inspections answer whole-state questions, semantic traces preserve compact explanatory state, and visuals answer appearance questions. Align them at important landmarks when correlation helps; otherwise spend each budget where that evidence channel is informative.

When replay is reproducible, refine the first run as follows:

1. compare successive time windows for new regimes, extrema, distribution movement, residual slopes, event-rate changes, and persistent suspicious identities;
2. find the last known-good and first suspicious sampled states;
3. rerun with midpoint or otherwise hypothesis-driven samples to bracket the transition;
4. add consecutive frames around the bracket and event-triggered captures;
5. repeat until the causal sequence is resolved or the oracle proves the behavior acceptable.

This coarse-to-fine replay normally yields a stronger schedule than guessing one large fixed list in advance. Checkpoints can shorten reruns when their measured save/load cost beats deterministic replay and their restore fidelity is validated.

If reproducibility is not demonstrated, do not assume a second run will revisit the same transition. Keep a bounded rolling prehistory in the temporary monitor and capture the first trigger's local semantic or raw state during the original run. Use reruns to measure incidence or test hypotheses, not as the sole record of a nondeterministic event.

## Scale-Safe Whole-State Analysis

For large simulations:

- prefer an O(N) in-process scan over O(N) serialization when reductions answer the question;
- merge chunk-local statistics rather than synchronizing on every entity;
- reuse the simulation's read-only spatial plan for neighborhood questions when valid at the observation boundary;
- otherwise build a temporary spatial hash/tree for thresholded pair checks; never default to O(N²) all-pairs work;
- avoid permanent per-entity diagnostic fields and allocations in production layouts;
- cap exemplars without capping aggregate failure counts, and report how many examples were omitted;
- process raw snapshots one at a time with streaming, chunking, memory mapping, or columnar arrays when post-hoc analysis is necessary.

When independent stable identities are expected to carry independent state, include a degeneracy screen. Define tolerances from physical scale and numeric precision, locate pairs or groups whose contract-relevant state vectors become nearly indistinguishable, and test persistence, relative motion, and history across adjacent committed states. Treat this as a warning unless the project contract forbids it: momentary contact or legitimately shared state is not equivalent to identities becoming numerically locked. Use the project's neighbourhood structure or a temporary spatial index rather than an all-pairs search at scale.

The optional [`health_probe_support.hpp`](../assets/health_probe_support.hpp) asset provides explicit frame selection, mergeable numerical summaries, and bounded findings. It deliberately contains no particle schema, tolerance, or correctness policy. Copy or include it only from a temporary diagnostic adapter and remove that integration when the objective ends.

## Ephemeral Diagnostic Sessions

Retained, compact evidence still follows `tracing-diagnostics.md` and belongs under `.codex/runs`. High-volume working data, generated analyzers, and raw complete snapshots are different: keep them in a manifest-owned session under the Codex build directory, for example:

```text
<CodexBuildDir>/health/session_<timestamp>_<id>/
    monitors/
    raw/
    analysis/
    findings.json
```

Use [`health_session.py`](../scripts/health_session.py) to create, inspect, execute within, and safely clean these sessions:

```powershell
python <SkillRoot>/scripts/health_session.py create --root <CodexBuildDir>/health --label pairing
python <SkillRoot>/scripts/health_session.py exec --session <SessionPath> -- <command> <arguments>
python <SkillRoot>/scripts/health_session.py clean --session <SessionPath>
```

`exec` sets `BITLOOP_HEALTH_SESSION` and replaces an argument equal to `{session}` with the absolute session path. It invokes the command directly without a shell. The session manager only owns paths bearing its marker; it must not become a general recursive deletion tool.

Prefer one temporary monitor header/source pair for one investigation, with several related checks inside it. One file per predicate scatters integration and cleanup; one ever-growing global scratch file carries stale assumptions between jobs. If the build can compile generated source from the session safely, keep it there. Otherwise use one clearly named `BITLOOP_DEV_MODE`-guarded adapter under the project's diagnostics folder and remove it at completion. Raw output and custom analysis code still belong in the session.

## Custom Local Analyzers

When in-process reduction is insufficient, generate an analyzer for the exact temporary schema instead of forcing arbitrary simulation state through a weak universal format.

- Prefer C++ during the simulation for direct access, spatial queries, and large reductions.
- Prefer a temporary Python/NumPy or other local analyzer for flexible post-hoc correlations, histograms, clustering, or comparison across a few serialized frames.
- Give the analyzer a tiny synthetic fixture containing a known good case and the anomaly it is meant to detect.
- Stream or map large inputs and write a compact machine-readable findings file.
- Read the findings, aggregate tables, and selected suspicious records into model context—not gigabytes of raw state.
- If findings expose a new hypothesis, revise the analyzer and rerun deterministically rather than retaining every intermediate dump.

The script is temporary investigation code, not a production dependency. Expensive local computation is acceptable when it materially strengthens the conclusion and the user accepts the wait.

## Record Horizon Evidence

Retain a compact validation summary naming the fixed `dt`, total fixed ticks and simulated time, output-frame mapping, timescales and event opportunities covered, scenarios/seeds/configurations, per-tick monitors, complete-inspection schedule, extension decisions, stopping rationale, and remaining uncertainty. A bare frame count or a clean final snapshot is not sufficient evidence of behavioral adequacy.

## Turn A Finding Into A Durable Result

Once the cause is understood:

1. reproduce it with the smallest deterministic scenario that preserves the failure;
2. for diagnosis-only work, do not retain a new test or change production behavior; continue through compact evidence recording and cleanup;
3. when implementation is authorized, fix the production cause, add the smallest durable regression whose oracle is now known, and rerun the long-horizon and focused transition gates;
4. record compact evidence and relevant thresholds in the plan, task tree, authorized test, or retained run summary;
5. remove temporary monitor calls, adapters, includes, CMake entries, metrics, raw snapshots, and generated analyzers;
6. search for the investigation prefix and session environment name, compile with `BITLOOP_DEV_MODE=FALSE` when practical, then clean the marked session.

Do not keep a temporary health framework inside the simulation after the job. Durable production safety checks and regression tests remain; disposable observation machinery does not.
