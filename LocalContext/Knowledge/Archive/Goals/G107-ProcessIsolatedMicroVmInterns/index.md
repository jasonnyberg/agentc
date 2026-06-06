# Goal: G107 — Process-Isolated Micro-VM Interns

**Status**: COMPLETE
**Created**: 2026-05-14
**Completed**: 2026-05-28

## Objective
Run intern workers as process-isolated micro-VMs that mmap the static core image read-only, own private dynamic overlays, and communicate with Root1 through async job events plus optional advertised read-only result slabs.

## Summary

G107 has been fully implemented across all launch modes:

### Launch Modes Implemented
1. **Thread worker** — shared frozen/static-immortal base thunk with read-only context
2. **Static-image/mmap-backed launch** — `PROT_READ` mmap of G103 static declaration image, handle-free `static_mounts` descriptors
3. **Forked worker** — `fork()` child inherits frozen shared base + read-only static mounts; structured result over anonymous pipe
4. **Public async process-backed worker** — `intern_start!`/`intern_sync!` over the fork backend
5. **Fork/exec worker** — independent `edict_worker_exec` process mmaps G103 declaration image independently
6. **Public async exec-backed worker** — `intern_start!`/`intern_sync!` over the exec backend via `edict-exec-async` worker label
7. **Static program-entry contract** — `program_source` resolved from independently mounted static image
8. **Static bytecode-entry contract** — `bytecode_hex` decoded from declaration; no compile step needed
9. **OS-backed borrowed bytecode-slab contract** — `bytecode_slab_path` + offset/size/hash → `mmap(PROT_READ)` → `EdictVM::executeBorrowedStaticCode()` with activation-local `code_ips_`

### Handle/Capability Policy (Documented)
The 🔗[WP — G107 Process-Isolated Launch Contract](../WorkProducts/WP_G107_ProcessIsolatedLaunchContract.md) defines the full policy:

- **Inherited (fork)**: frozen read-only Listree (`context`, `imports`), static-immortal slab slots, static declaration images, borrowed bytecode slabs, input snapshot
- **Rehydrated (exec)**: `context`/`imports`/`input` via JSON pipe; `static_mounts` via independent `mmap(PROT_READ)`; borrowed bytecode slab via independent `mmap` + hash validation
- **Blocked**: raw fds (eventfd, pidfd, epoll), provider handles, credentials, VM pointers, activation frames, `dlopen` handles, function pointers

### Acceptance Criteria (All Met)
- [x] Deterministic intern task runs in separate process, returns structured result
- [x] Worker maps shared static/core slabs read-only, uses private dynamic state
- [x] Worker publication/result transfer doesn't require mutable coordinator-owned Listree
- [x] Documentation states which handles/capabilities may be inherited, rehydrated, or blocked

### Key Files
- `edict/edict_worker_runtime.*` — worker runtime (thread, fork, exec backends)
- `edict/worker_exec_main.cpp` — `edict_worker_exec` executable entry point
- `edict/edict_worker_primitives.cpp` — `InternJobManager`, task parsing, lifecycle management
- `edict/edict_intern_service.h` — intern service C API
- `edict/static_declaration_image.*` — G103 static declaration image generation/validation/mount
- `edict/static_slot_table_image.*` — borrowed static slot-table inspector
- `edict/tests/intern_worker_test.cpp` — 9+ tests covering all launch modes and static entry types
- `edict/tests/static_declaration_image_test.cpp` — static image validation tests including native handle rejection
- `cpp-agent/edict/modules/worker.edict` — Edict worker primitive wrappers
- `cpp-agent/edict/modules/intern.edict` — public intern surface
- `cpp-agent/edict/modules/root1.edict` — Root1 primitive wrappers

### Validation
- 2026-05-27: All 17 focused G107 process/static slice tests passed
- 2026-05-28: Additional static declaration image validation tests (`ValidationRejectsNativeHandleImage`, `ValidationRejectsSymbolWithNativeHandle`) added and passed

### Related Goals
- 🔗[G091 — Intern Worker Concurrency MVP](../G091-InternWorkerConcurrencyMvp/index.md) — parent track
- 🔗[G110 — Root1 eventfd/epoll Resource Broker](../G110-EventfdEpollMicroVmIpcDesign/index.md) — async IPC substrate
- 🔗[G103 — Build-Time Static Core Declaration Image](../G103-BuildTimeStaticCoreDeclarationImageMvp/index.md) — declaration image format/mount
- 🔗[G105 — ReadOnly Static Slab Ownership Model](../G105-ReadOnlyStaticSlabOwnershipModel/index.md) — static-immortal slab ownership
- 🔗[G104 — Immutable Code Object / Activation Frame Split](../G104-ImmutableCodeObjectActivationFrameSplit/index.md) — borrowed code frames