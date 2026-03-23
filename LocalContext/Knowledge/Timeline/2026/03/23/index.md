# Timeline: 2026-03-23

## Session 0005-0015

🔗[Session Notes](./0005-0015/index.md) — Refined G047 toward `logic(...)` with equivalent `[...] logic!` semantics.

## Session 0035-0045

🔗[Session Notes](./0035-0045/index.md) — Refined G047 toward call-form logic, future capability migration, and rewrite-hosted DSLs.

---

## G047 — Native Relational Syntax — Ongoing Planning Refinement

Refined the G047 planning direction so the preferred native logic surface now leans toward ordinary call forms such as `fresh(q)`, `membero(q [tea cake jam])`, `conde(==(q 'tea) ==(q 'coffee))`, and `results(q)` inside `logic(...)`.

### Current design direction

- `logic(...)` is the preferred human-facing syntax.
- `[...] logic!` remains the equivalent literal/evaluator model underneath.
- The MVP remains a compiler-lowering project to the current object IR and existing `VMOP_LOGIC_RUN` backend.
- Longer term, the syntax may support migration of miniKanren behavior out of VM-owned primitives and into library or FFI-backed capability layers.
- Rewrite rules are now considered a plausible future mechanism for tiny domain-specific sugar layers that normalize into the native call-form substrate.
