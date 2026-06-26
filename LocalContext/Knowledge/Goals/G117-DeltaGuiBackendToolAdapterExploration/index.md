# Goal: G117 — DeltaGUI Backend Tool Adapter Exploration

**Status**: DEFERRED
**Created**: 2026-06-21
**Parent/Track**: TinyCC Native Interoperability / Downstream Applications
**Depends On**: 🔗[G116 — TCC Native Symbol Cache and C ABI Bridge](../G116-TccNativeSymbolCacheCAbiBridge/index.md)

## Objective

Explore how AgentC could leverage the `~/DeltaGUI` C++ backend as a tool surface through a controlled C ABI façade and/or TCC-generated adapters.

## Rationale

The user explicitly wants to think downstream about how AgentC applications could interact with the DeltaGUI C++ backend. TinyCC cannot compile C++, so the right interface is not "compile DeltaGUI with TCC". The right interface is: expose selected backend capabilities through a stable `extern "C"` adapter library, register those functions into AgentC/TCC through the symbol-cache bridge, and run generated adapters in isolated workers.

## Acceptance Criteria

- [ ] Inventory candidate DeltaGUI backend capabilities worth exposing to AgentC.
- [ ] Define a minimal C ABI façade over one safe/read-only backend capability.
- [ ] Build or load the façade as a separate adapter library; do not pass raw backend/provider handles into Edict or worker contexts.
- [ ] Register selected façade symbols through the G116 symbol cache.
- [ ] Demonstrate an AgentC/TCC adapter calling one façade function and returning a structured Listree/JSON result.
- [ ] Document credential/state ownership boundaries for DeltaGUI backend access.

## Progress

### 2026-06-23
- Added `edict/tests/tcc_test_adapter.cpp` as a stand-in AgentC-side `extern "C"` adapter probe so the G116 symbol-cache path can be validated without modifying `~/DeltaGUI`.
- No DeltaGUI source has been touched yet; the actual backend inventory and façade selection remain deferred until the core TCC substrate ACs close.

### 2026-06-24
- Core TinyCC substrate goals G112-G116 are now complete and verified, so this goal's technical prerequisite is satisfied.
- The goal remains deferred until the user wants the downstream DeltaGUI capability inventory / minimal `extern "C"` façade slice.

## Deferral Note

The core TinyCC substrate now exists. Keep this deferred until the user wants downstream DeltaGUI adapter exploration; this remains an application/integration goal rather than part of the core TCC runtime mechanism.

See 📄[WP — TinyCC Native Interoperability Plan](../../WorkProducts/WP_G112_TinyccNativeInteropPlan.md).
