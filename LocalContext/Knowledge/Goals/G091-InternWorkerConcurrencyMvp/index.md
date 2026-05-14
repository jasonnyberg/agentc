# Goal: G091 — Intern Worker Concurrency MVP

**Status**: PLANNED  
**Created**: 2026-05-14  
**Parent**: 🔗[G078 — Edict-Resident Agent Loop Consolidation](../G078-EdictResidentAgentLoopConsolidation/index.md)

## Objective
Implement the first practical local-intern worker architecture: a coordinator EdictVM dispatches a bounded task to a worker with a fresh private slab/VM, read-only access to shared context/imports, and a structured result copied back into coordinator-owned state.

## Source Extracted From
Conversation summary section **“The Intern Architecture”** and **“Suggested first implementation step”**.

## Rationale
The intern model is the clearest application-level payoff for AgentC's substrate: small/local models can gather, classify, filter, compress, or proxy context while the primary agent spends tokens deciding. The architecture should preserve Listree safety by avoiding cross-thread mutation of coordinator-owned state.

## Candidate Task Classes
- Information gathering: read files and extract bounded facts.
- Output classification: run tests and classify failures.
- Search + filter: find candidates and discard irrelevant matches.
- Compression: summarize diffs/logs into semantic findings.
- Context proxy: hold a large context and answer factual questions.

## Implementation Plan
- [ ] Define a worker task envelope and result schema suitable for Edict and C++ tests.
- [ ] Let the coordinator mark FFI imports and task context as recursive `ReadOnly` before dispatch.
- [ ] Spawn a worker with a fresh slab, fresh EdictVM, read-only references or safe snapshots of shared context/imports, and private mutable workspace.
- [ ] Execute a deterministic Edict worker task first; defer live local-model execution until the substrate is stable.
- [ ] Return structured results through a thread-safe queue or join result compatible with the existing StreamManager pattern.
- [ ] Copy/merge the result Listree into the coordinator slab on the coordinator/main thread.
- [ ] Free the worker slab after result collection.

## Acceptance Criteria
- [ ] A checked-in test demonstrates coordinator dispatch to at least one worker VM and structured result collection.
- [ ] Worker mutation of shared read-only context is refused or isolated.
- [ ] Coordinator-owned Listree state is mutated only on the coordinator/main thread.
- [ ] Worker output is copied into the main slab with deterministic validation.
- [ ] Documentation captures the safe intern-task rule: bounded task, explicit inputs, clear success criteria.

## Constraints
- Whole-subtree `ReadOnly` is sufficient for the first implementation.
- Do not share stateful provider handles across workers in the MVP.
- Keep local models in the “intern, not architect” role: extraction/summarization/classification before deep reasoning.
- Preserve current Edict style: adjacent eval (`word!`) and discard as `/` or `/ /`.
