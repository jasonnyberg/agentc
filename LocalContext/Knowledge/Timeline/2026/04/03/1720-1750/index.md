# Session 1720-1750

## Summary

Wrapped up a remaining G049 follow-through item by auditing the current helper surface, confirming the protected shared-value boundary, and removing the auxiliary status-returning thread helper API.

## Work Completed

- Reviewed `cartographer/tests/libagentthreads_poc.h` / `.cpp` and `edict/tests/callback_test.cpp` against the documented first-slice threading model.
- Confirmed that ordinary thread spawn/join snapshots `ltv` values at the boundary and does not expose arbitrary live mutable graph sharing.
- Confirmed that the only supported mutable cross-thread coordination API in the helper remains `agentc_shared_create_ltv`, `agentc_shared_read_ltv`, and `agentc_shared_write_ltv`.
- Removed `agentc_thread_spawn_status(...)` and `agentc_thread_join_status(...)` from the helper because they were only supporting one shared-cell-update test and no longer provided unique value beyond the direct `ltv` callback path.
- Rewrote the shared-cell update callback test to use the ordinary `ltv` worker path with captured root state instead of the auxiliary status-returning entrypoint.
- Rebuilt and re-verified: focused thread-runtime callback coverage passed, `./build/edict/edict_tests` passed `86/86`, and `ctest --test-dir build --output-on-failure` passed `7/7`.

## Outcome

The first-slice G049 surface is now tighter and more coherent: one thread-entry model (`ltv` callback in a fresh VM) plus one explicit mutable shared-state model (protected shared-value cells). This reduces helper complexity while keeping validation green.
