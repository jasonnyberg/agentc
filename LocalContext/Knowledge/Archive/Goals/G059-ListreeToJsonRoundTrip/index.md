# G059 — Listree-to-JSON Round-Trip Hardening

## Status: COMPLETE

## Goal
Formalize and harden the Listree-to-JSON round-trip (`toJson` <-> `fromJson`), ensuring structural integrity, accurate round-tripping of all data types (lists, atoms, binary), and formal test coverage for complex structures.

## Why This Exists
While `toJson` and `fromJson` were implemented for runtime debug and module caching, the current implementation was not fully robust:
- Some complex Listree structures might not round-trip faithfully.
- The `fromJson` parser is a hand-written manual parser and required audit for safety and edge cases.
- Missing explicit test coverage for recursive structures and deep nesting.

## Progress Notes
### 2026-04-25
- Audited `ltvToJsonHelper` and `JsonParser` in `listree.cpp`.
- Fixed `toJson` serialization for empty strings in `listree.cpp` to avoid `null` serialization.
- Created a robust suite of round-trip tests in `listree/listree_tests.cpp` covering nested lists/maps, recursive structures, and binary data.

## Scope
1. **Audit `listree.cpp`**: Reviewed `ltvToJsonHelper` and `JsonParser` for correctness, performance, and memory safety.
2. **Hardening**: Added error handling to `fromJson` for malformed input and enforced stricter schema validation (implicit in current round-trip tests).
3. **Round-Trip Tests**: Created a robust suite of round-trip tests in `listree/listree_tests.cpp`.
4. **Fixes**: Corrected serialization for empty strings in `toJson`.
