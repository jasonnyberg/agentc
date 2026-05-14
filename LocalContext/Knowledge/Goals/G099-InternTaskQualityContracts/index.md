# Goal: G099 — Intern Task Quality Contracts

**Status**: PLANNED  
**Created**: 2026-05-14  
**Parent**: 🔗[G091 — Intern Worker Concurrency MVP](../G091-InternWorkerConcurrencyMvp/index.md)

## Objective
Define the task-contract layer for local intern agents: which tasks are safe to delegate, how success criteria are expressed, and how outputs are validated before the coordinator trusts them.

## Source Extracted From
Conversation summary section **“Quality constraint”** and the intern delegation taxonomy.

## Rationale
The intern architecture only works if delegated tasks are bounded and checkable. Local models can grep, summarize, extract, and classify; they should not be trusted as autonomous architects over deep multi-file reasoning. The system needs explicit contracts that encode this distinction.

## Candidate Contract Fields
- Task kind: gather, classify, filter, compress, context-proxy.
- Inputs: files, commands, search terms, context node refs, limits.
- Expected output schema.
- Success criteria and validation checks.
- Token/time/resource budget.
- Confidence/evidence fields for returned findings.

## Implementation Plan
- [ ] Define a minimal Listree/JSON task contract schema.
- [ ] Add Edict helpers for constructing and validating intern task contracts.
- [ ] Provide examples for each safe task class.
- [ ] Integrate the contract with the worker dispatch path from 🔗[G091](../G091-InternWorkerConcurrencyMvp/index.md).
- [ ] Add tests that reject underspecified or over-broad intern tasks.

## Acceptance Criteria
- [ ] Intern tasks have explicit, machine-checkable success criteria.
- [ ] At least gather/classify/filter task contracts are represented.
- [ ] Coordinator validation can reject malformed or low-confidence results.
- [ ] Documentation states the “intern, not architect” constraint clearly.
