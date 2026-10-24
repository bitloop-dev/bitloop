# Advanced Project Execution

## Contents

- Relationship to durable work packages
- Planning trigger and execution protocol
- Stage execution modes, context handoffs, and project-local plans
- Validation, dependencies, and completion

Use this page for broad, advanced, ambiguous, or multi-system Bitloop project work where success depends on several coordinated implementation and validation stages.

Do not use this workflow for simple local edits, bug fixes, or explicitly incremental requests. Scope discipline still applies: the plan decomposes the requested goal; it does not invent a larger product.

## Relationship To Durable Work Packages

When a project has a durable task tree or roadmap, its focused work package defines the approved scope, dependencies, and acceptance boundary. The temporary advanced plan is subordinate execution memory for that package, not a competing roadmap.

One coherent work package may span multiple bounded agent runs. If its frontier is not one semantic or validation outcome, restructure the durable plan before implementation. If it is coherent but too substantial for one clean context, retain the work package and divide its execution into validated stages. Task IDs do not automatically determine agent-run boundaries: one stage may cover several tightly coupled leaves, while one unusually substantial leaf may need several temporary stages.

## Planning Trigger

Create a staged plan before editing when the request has several of these traits:

- touches multiple systems such as simulation, input, UI, drawing, diagnostics, performance, or build wiring.
- requires technical judgment about architecture, dependencies, precision, threading, or validation strategy.
- has an outcome that cannot be trusted from compilation alone.
- is likely to need multiple build/run/diagnostic loops.
- would benefit from preserving the reasoning between Codex turns.
- is likely to leave substantial work after one natural validation boundary, especially when later work needs materially different context.

If the work is small enough to finish safely in one pass, keep the plan in the conversation instead of creating a file.

## Execution Protocol

For large work, the plan is not just a sketch. It is the execution contract.

1. Create or update the project-local plan before editing project behavior.
2. Divide the work package into execution stages based on coupling, routed context, and meaningful validation gates rather than a fixed task count or size threshold.
3. Pick the next pending stage and treat it as the current prompt.
4. Choose the stage execution mode: same-agent for moderate or tightly interleaved stages, or linear subagent/fresh-invocation isolation when a clean context is expected to improve quality.
5. Re-read only the routed docs and project files needed for that stage.
6. Implement the smallest coherent change that satisfies that stage.
7. Run the stage's validation gate.
8. Have the integrating agent review the actual changes and evidence, then update the plan before moving on.
9. Repeat until every stage and the combined integration gate pass.

Do not collapse several pending stages into one unverified edit when the plan says they need separate validation. Do not mark the overall objective complete while any required stage is pending, failed, or unvalidated.

## Stage Execution Modes

Use the lightest mode that preserves quality.

Do not use subagents merely because work can be divided or to reduce wall-clock time. Use them when the overall package is too substantial for one clean agent run, or when independent context on a large or risky package materially improves diagnostic or verification coverage.

Choose execution isolation by expected quality gain, not leaf count, estimated duration, or lines changed. Require a bounded serial subagent or fresh-invocation boundary when all three conditions hold:

1. the current stage can leave a stable on-disk result with a meaningful validation gate;
2. substantial work remains that needs materially different routed context, would follow noisy tool or diagnostic output, or needs fresh high-judgment reasoning; and
3. an explicit handoff can reconstruct the next stage more reliably than carrying the accumulated context forward.

If a proposed stage is itself too substantial for one clean run, first decompose it to the nearest coherent, testable boundary. Otherwise, if a condition is absent, continue with the same agent and reassess at the next natural gate. A large number of leaves is a reason to inspect the work shape, never a threshold by itself.

Same-agent stages are appropriate when the change is moderate and the main agent can keep enough context cleanly:

- main agent reads `AGENTS.md`, routed docs, relevant files, tool outputs, and validation evidence.
- main agent implements, validates, and updates the plan directly.
- prefer this mode when decisions are tightly interleaved across the same state or files and the handoff would cost more or lose more than a fresh context would save.

Linear subagent stages are preferred for substantial implementation or verification stages when subagents or fresh Codex invocations are available and appropriate:

- main agent keeps ownership of the user goal, plan file, acceptance checklist, and final integration.
- main agent delegates only the next pending stage to one subagent at a time.
- subagent starts from the project-local plan, reloads relevant `AGENTS.md`/routed docs/files, implements only that stage, runs the stage validation gate, and returns concise evidence.
- main agent reviews the subagent's result and the actual files or evidence, updates the plan, runs any required seam gate, then decides how to execute the next stage.

This costs more tokens because the subagent repeats context loading and tool work, then the main agent reviews the result. Use it when the extra isolation is likely to improve correctness, reduce context pollution, or make a long task more resumable. Do not create one subagent per tiny edit.

Parallel subagents are optional and should be limited to independent read-only exploration, diagnostics, visual review, or clearly disjoint write scopes. Prefer linear implementation stages whenever agents would touch shared mutable state, overlapping files, or an ordered chain of decisions. The integrating agent remains the single writer for the plan and any durable task-tree state.

### Context Handoff Contract

Before delegating a stage or resuming it in a fresh invocation, ensure its plan entry provides:

- the durable work-package and relevant task IDs, stage goal, and non-goals;
- approved decisions, constraints, completed prerequisites, and unresolved risks;
- the authoritative docs and project files to reload, plus allowed ownership and write scope;
- the validation gate and the concise changes, evidence, findings, and questions to return.

If this cannot be stated cleanly, the stage is not ready for isolation: improve its contract, restructure it, or keep the same agent. Prefer this explicit packet and authoritative on-disk state over inheriting an entire noisy conversation.

## Project-Local Plan Files

Create one top-level temporary plan for the work package. Use a linked sub-plan only when one stage remains unusually broad or risky; do not create unrelated plans per task leaf.

For substantial work, create a plan under the active project:

```text
<ProjectName>\.codex\plans\YYYYMMDD_HHMMSS_<short-goal>.md
```

Keep the file concise. Use it as working memory for the current objective, not as permanent product documentation.

Plan files should include:

- objective and non-goals.
- durable work-package/task IDs when a task tree or roadmap exists.
- constraints from the user and existing project.
- stages, each with a goal, execution mode, touched files, implementation notes, validation method, and completion status.
- evidence fields for build commands, run folders, snapshots, checkpoints, or trace reconstruction used to validate each stage.
- risks or questions that require user confirmation before broad architectural changes.
- an explicit final acceptance checklist that maps back to the user's request.

## Stage Discipline

Treat each stage as a fresh scoped prompt:

1. Re-read only the routed docs and project files needed for that stage.
2. Implement the smallest coherent change that satisfies the stage.
3. Validate using the stage's stated method.
4. Update the plan file with the result before moving on.

Stage boundaries are execution and context boundaries, not automatic durable task boundaries. Do not mark a task-tree leaf accepted merely because its temporary stage completed; follow the work package's shared integration and acceptance policy.

Stages may have sub-plans when a stage is still broad or risky. Create the sub-plan in the same `.codex/plans/` folder and link it from the parent stage. Prefer one level of nesting unless deeper recursion clearly prevents confusion. Stop decomposing once the next action is obvious.

When resuming from a plan, rely on the plan file and current project state rather than conversation memory. A fresh Codex run should be able to read the plan, find the next pending stage, execute it, validate it, and update the plan.

## Validation

Each stage should define how success will be observed. Use the cheapest evidence that actually proves the behavior:

- compilation for API and wiring correctness.
- headless traces for semantic state.
- snapshots for visual/readability claims.
- checkpoints for late-state or restart behavior.
- focused temporary diagnostics for short-lived uncertainty.
- adaptive health-monitor runs for long-horizon or whole-state behavior whose correctness cannot be established from ordinary traces.

For every stage that meaningfully changes persistent simulation-state evolution, classify the validation as smoke, behavioral, or endurance evidence. A short Scene run is smoke only. Acceptance must either name an existing focused gate that covers delayed, cumulative, recurrent, and rare behavior over a justified horizon, or include a behavior-horizon gate selected through `simulation-health-diagnostics.md`. Record the horizon rationale and stopping decision; do not substitute a conventional frame count.

Do not add permanent telemetry just because a plan exists. Follow `temporary-diagnostics.md` for focused probes and `simulation-health-diagnostics.md` for long-horizon or full-state investigations. Remove objective-only probes when they stop carrying their weight.

## Dependencies And Architecture

Planning can identify candidate dependencies or architecture changes, but do not add them automatically. First check the routed topic docs for existing Bitloop or standard C++ capabilities. Ask for confirmation when the proposed change broadens ownership, affects framework behavior, or solves speculative future needs.

## Completion

A large project objective is done when every required stage is implemented, validation evidence exists, known failures are either fixed or recorded as findings, and the plan file accurately reflects the final state.

For a large or risky combined change, consider a fresh read-only integration review after the normal gates. Use it only when independent context is likely to expose cross-stage omissions worth the additional reload and review cost.

For cold validation of this workflow, start a fresh agent invocation with a prompt that points at the plan file and asks it to execute only the next pending stage. Repeating that process should eventually complete the same project without relying on prior conversation context.
