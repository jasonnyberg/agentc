# Goal: G075 — Speculative Edict Native Architectures

**Status**: DEFERRED  
**Created**: 2026-05-06  
**Reassessed**: 2026-05-11  

## Objective
Leverage the fast, copy-on-write (CoW) memory-mapped Listree capabilities natively within Edict scripts to implement advanced cognitive architectures like Tree of Thoughts (ToT), Monte Carlo Tree Search (MCTS), or recursive ReAct speculation.

## Rationale
The core framework is now highly optimized for snapshot/rollback memory operations natively (via mmap slabs). We can now write complex multi-branch agent logic directly in Edict that forks the VM state, explores different LLM interaction paths, and rolls back failed branches efficiently without deep-copying memory.

## Acceptance Criteria
- [ ] Implement a speculative rollback operator/primitive natively accessible in Edict.
- [ ] Create an `agentc_tot.edict` script that demonstrates a multi-branch, rollback-supported reasoning loop.
- [ ] Prove performance parity/efficiency of branching vs linear interaction.

## Reassessment — 2026-05-11
This remains a valuable research direction but is not on the near-term execution path. The project first needs a stable Edict-resident agent control plane, tool-use surface, and explicit context management. Resume after G079/G080 or when a concrete Tree-of-Thoughts/MCTS demonstration is needed to validate Listree snapshot economics.
