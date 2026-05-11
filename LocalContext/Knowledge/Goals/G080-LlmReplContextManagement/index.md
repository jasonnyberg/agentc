# Goal: G080 — LLM REPL Context Management

**Status**: NEXT  
**Created**: 2026-05-10  
**Reassessed**: 2026-05-11

## Objective
Add explicit context-management behavior to the launcher-backed `llm` provider REPL so long-running chat sessions can manage conversation state intentionally instead of only accumulating raw history.

## Rationale
`provider.repl()` is now live and usable, but it currently behaves like a thin multi-turn chat loop over monotonically growing conversation state. The next UX step is explicit context management inside that loop: trimming, summarizing, checkpointing, resetting, or otherwise controlling what the provider keeps and sends.

## Acceptance Criteria
- [ ] Define a first explicit context-management contract for provider-owned conversation state.
- [ ] Add at least one practical context-management operation to the REPL loop.
- [ ] Ensure the context-management behavior is driven from Edict/provider state rather than host-owned policy.
- [ ] Demonstrate the behavior through launcher-backed REPL validation.

## Candidate Directions
- Reset or clear conversation state
- Summarize older turns into compact retained state
- Limit retained history by policy
- Expose explicit provider/context inspection helpers

## Constraints
- Respect the current Edict truthiness/control-flow sharp edges documented in the VM guide.
- Keep provider objects stable and mutating in place.
- Build on the current launcher-backed `provider.repl()` seam instead of introducing a parallel host loop.

## Reassessment — 2026-05-11
This is now the immediate next slice after G079 landed the first tool-use surface. Context management becomes more important once the loop can run longer engineering sessions. The first useful slice should likely be simple and inspectable: reset/clear history, summarize older turns, or enforce a retained-history limit through provider-owned state.

## Relationship To G078
G078 establishes the Edict-owned loop. G080 makes that loop sustainable for longer-running interactions by adding first-class context management.
