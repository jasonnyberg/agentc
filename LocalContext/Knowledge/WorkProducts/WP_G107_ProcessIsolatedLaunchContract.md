# WorkProduct — G107 Process-Isolated Micro-VM Interns: Static Launch Contract & Handle/Capability Policy

**Status**: Complete  
**Date**: 2026-05-28  
**Goal**: 🔗[G107 — Process-Isolated Micro-VM Interns](../Goals/G107-ProcessIsolatedMicroVmInterns/index.md)  
**Parent**: 🔗[G091 — Intern Worker Concurrency MVP](../Goals/G091-InternWorkerConcurrencyMvp/index.md)  
**Related**: 🔗[G110 — Root1 eventfd/epoll Resource Broker](../Goals/G110-EventfdEpollMicroVmIpcDesign/index.md), 🔗[G103 — Build-Time Static Core Declaration Image](../Goals/G103-BuildTimeStaticCoreDeclarationImageMvp/index.md), 🔗[G105 — ReadOnly Static Slab Ownership](../Goals/G105-ReadOnlyStaticSlabOwnershipModel/index.md)

---

## 1. Overview

This document defines the **static launch contract** for process-isolated micro-VM interns (G107) and the **handle/capability policy** governing what state may cross the coordinator→worker boundary.

The launch contract is the agreement between:
- **Coordinator** — the Edict VM that dispatches `intern_start!` / `intern_run!`
- **Worker** — the process-isolated Edict VM (`edict_worker_exec` or forked child)
- **Root1 Broker** — the `eventfd`/`epoll` resource broker (G110) providing async IPC

The handle/capability policy ensures:
1. **Safety** — no mutable coordinator-owned state leaks into workers
2. **Isolation** — workers cannot access raw kernel handles (eventfd, pidfd, epoll, credentials)
3. **Determinism** — worker execution depends only on declared inputs, not ambient process state
4. **Resumability** — all durable state is in Listree/slab form; process-local handles are reconstructed on resume

---

## 2. Static Launch Contract

### 2.1 Task Envelope (Coordinator → Worker)

The coordinator constructs a task envelope containing only JSON-serializable or read-only-slab-referencable data:

```json
{
  "task_id": "string",
  "program": "string (Edict source) | omitted for static entry",
  "input": { ... },                    // JSON-serializable snapshot
  "context": { ... },                  // frozen read-only Listree (shared)
  "imports": { ... },                  // frozen read-only Listree (shared)
  "static_mounts": {                   // read-only static declaration image refs
    "base": {
      "container_path": "/path/to/image.acsdi",
      "source": "g103-exec-mounted-mmap-container",
      "image_id": "worker.edict:<payload_hash>",
      "manifest_hash": "<fnv1a64>",
      "root_descriptor": "worker.edict/declarations",
      "contains_native_handles": "false"
    }
  },
  "static_program_mount": "base",      // which static_mounts entry
  "static_program_word": "worker.edict_prepare_task",
  "expect": {
    "success_field": "ok",
    "evidence_field": "evidence",
    "min_evidence_count": 1,
    "min_confidence": "medium"
  },
  "limits": {
    "max_result_bytes": "65536",
    "timeout_ms": "10000",
    "max_events": "100"
  },
  "worker": "edict-thread-async | edict-fork-async | edict-exec-async",
  "worker_exec_path": "/path/to/edict_worker_exec",  // required for exec
  "max_active_jobs": 64                // optional backpressure
}
```

### 2.2 Static Program Entry Resolution

When `program` is omitted, the worker resolves a **static program entry** from the independently mounted declaration image:

| Declaration Field | Purpose | Execution Path |
|-------------------|---------|----------------|
| `program_source` | Edict source text | Compile → execute |
| `bytecode_hex` | Hex-encoded bytecode | Decode → `BytecodeBuffer` → execute |
| `bytecode_slab_path` + `bytecode_slab_offset` + `bytecode_slab_size` + `bytecode_slab_hash` | OS-backed read-only mmap slab | `mmap(PROT_READ)` → `EdictVM::executeBorrowedStaticCode()` |

**Encoding identifiers:**
- `agentc.edict.bytecode.v1.hex` — hex-encoded bytecode in declaration
- `agentc.edict.bytecode.v1.mmap` — OS-backed borrowed bytecode slab

### 2.3 Worker Execution Modes

| Mode | `worker` value | Isolation | Code Source | Static Mounts |
|------|----------------|-----------|-------------|---------------|
| Thread | `edict-thread-async` | Same process, fresh VM | Inline or static entry | Coordinator's read-only refs |
| Fork | `edict-fork-async` | `fork()` child, fresh VM | Inline or static entry | Inherited read-only refs (fork) |
| Exec | `edict-exec-async` | `fork()`+`exec()` fresh process | Static entry only | Independent mmap from `container_path` |

---

## 3. Handle/Capability Policy

### 3.1 Classification

| Category | Description | Examples |
|----------|-------------|----------|
| **Inherited (Fork)** | Process state duplicated by `fork()`; read-only by construction | Frozen `context`, `imports`, `static_mounts` Listree roots; static-immortal slab slots |
| **Rehydrated (Exec)** | Explicitly reconstructed in child from durable representation | `input`/`context`/`imports` via JSON pipe; `static_mounts` via independent `mmap(PROT_READ)` of `container_path` |
| **Blocked** | Never crosses coordinator→worker boundary; process-local only | Raw fds (eventfd, pidfd, epoll), provider handles, credentials, VM pointers, activation frames, `dlopen` handles, function pointers |

### 3.2 Detailed Policy Table

| Handle / Capability | Fork Worker | Exec Worker | Rationale |
|---------------------|-------------|-------------|-----------|
| **Frozen read-only Listree (`context`, `imports`)** | ✅ Inherited (fork copies address space; pages are COW, read-only enforced by Listree flags) | ✅ Rehydrated (JSON pipe → `fromJson` → `setReadOnly(true)`) | Immutable shared substrate; safe for concurrent reads |
| **Static-immortal slab slots** | ✅ Inherited (slab pages are COW; `markSlotStaticImmortal` marks allocator metadata) | ✅ Rehydrated (independent mmap → `mountDeclarationImageReadOnly` → `markSlotStaticImmortal`) | OS read-only mmap + allocator static marks = true immutability |
| **Static declaration image (`static_mounts`)** | ✅ Inherited (fork) | ✅ Rehydrated (child mmaps `container_path` independently) | G103 container is self-contained; no coordinator coupling |
| **Borrowed bytecode slab** | ✅ Inherited (fork) | ✅ Rehydrated (child mmaps `bytecode_slab_path` independently, validates hash) | OS read-only mmap; `executeBorrowedStaticCode` uses activation-local `code_ips_` |
| **Input snapshot (`input`)** | ✅ Inherited (fork) | ✅ Rehydrated (JSON pipe) | Pure data; no identity |
| **Worker executable path (`worker_exec_path`)** | ❌ N/A | ✅ Rehydrated (passed in task envelope) | Coordinator knows path; child receives via JSON |
| **Root1 broker participant ID** | ❌ Blocked | ❌ Blocked | Process-local epoll registration; reconstructed on resume via G110 |
| **Root1 eventfd / mailbox fd** | ❌ Blocked | ❌ Blocked | Kernel fd; not durable; G110 reconstructs from participant metadata |
| **pidfd (owner-death monitoring)** | ❌ Blocked | ❌ Blocked | Kernel fd; process-specific; G110 re-registers on resume |
| **Provider handles (LLM runtime, credentials)** | ❌ Blocked | ❌ Blocked | Stateful, credential-bearing; coordinator-owned only |
| **Cartographer `dlopen` handles / FFI function pointers** | ❌ Blocked | ❌ Blocked | Process-local address space; `native_binding_policy = "lazy_process_local_sidecar"` |
| **Edict VM pointers / activation frames** | ❌ Blocked | ❌ Blocked | Process-local; `code_ips_` is per-VM activation state |
| **Credentials / secrets** | ❌ Blocked | ❌ Blocked | Never in Listree; coordinator-only via native C++ |

### 3.3 Enforcement Mechanisms

| Mechanism | What It Enforces |
|-----------|------------------|
| **JSON-only pipe (exec)** | Only serializable data crosses; no pointers/fds |
| **`setReadOnly(true)` on all shared Listree** | Mutations refused at Listree level (G109) |
| **`markSlotStaticImmortal` + `PROT_READ` mmap** | Allocator skips `inUse` writes; OS enforces read-only |
| **Static declaration image validation** | `contains_native_handles = "false"`; `forbidden_payloads` list in manifest |
| **`allowUnsafeFfiCalls` flag** | Explicit opt-in; default `false`; worker cannot enable |
| **No fd passing in `writeWorkerInput`/`readWorkerInput`** | Only strings, JSON, and one `allowUnsafe` byte |

---

## 4. Static Declaration Image Manifest Extensions

The G103 manifest (in `static_declaration_image.cpp::buildWorkerPrimitiveDeclarationImage`) already declares:

```json
{
  "format": "agentc.static_declaration_image",
  "format_version": "1",
  "image_kind": "declarative_import_module",
  "module": "worker.edict",
  "root_id": "worker.edict/declarations",
  "hash_algorithm": "fnv1a64",
  "payload_hash": "<fnv1a64 of declarations>",
  "contains_native_handles": "false",
  "native_binding_policy": "lazy_process_local_sidecar",
  "forbidden_payloads": [
    "dlopen_handle", "dlsym_pointer", "function_pointer",
    "eventfd", "epoll_fd", "pidfd",
    "edict_vm_pointer", "activation_frame",
    "credential", "provider_handle"
  ]
}
```

### 4.1 Per-Symbol Capability Metadata (G092 Future Work)

Each symbol declaration in the image carries capability metadata for G092 (Cartographer FFI Re-entrancy Metadata):

```json
{
  "word": "worker.edict_prepare_task",
  "native_symbol": "agentc_worker_edict_prepare_task_ltv",
  "stack_signature": "(ltv task) -> ltv",
  "category": "task",
  "binding": "lazy_process_local",
  "stores_native_handle": "false",
  "worker_allowed": "true",
  "notes": "Declarative metadata only; actual Cartographer/FFI binding is process-local."
}
```

**Fields relevant to handle policy:**
- `stores_native_handle`: `"false"` — symbol does not capture raw pointers/fds
- `worker_allowed`: `"true"` — symbol is safe for worker execution
- `binding`: `"lazy_process_local"` — binding is re-established per-process

---

## 5. Persistence & Resume Boundary

### 5.1 Durable State (Persisted in Listree/Slabs)

| State | Location | Resume Behavior |
|-------|----------|-----------------|
| Task envelopes (JSON) | Coordinator Listree | Replayed on coordinator restart |
| Static declaration images | File-backed containers (`.acsdi`) | Independent mmap on worker launch |
| Borrowed bytecode slabs | File-backed slabs (`.bin`) | Independent mmap + hash validation |
| Root1 broker metadata | Coordinator Listree (participant ids, resource keys, lease epochs) | G110 reconstructs fds/epoll from metadata |
| Worker results | Coordinator Listree (via `intern_sync!`) | Already in durable form |

### 5.2 Non-Durable State (Reconstructed on Resume)

| State | Reconstructed By |
|-------|------------------|
| Root1 eventfds / epoll registrations | `Root1ResourceBroker::reconstructParticipantsFromSlab()` |
| pidfd registrations | `attachParticipantPid()` on worker re-launch |
| Cartographer `dlopen` handles | `EdictVM::preload_imported_libraries()` (G072) |
| Worker process PIDs | New processes launched; old PIDs not reused |

---

## 6. Acceptance Criteria Verification

| Criterion | Status | Evidence |
|-----------|--------|----------|
| Deterministic intern task runs in separate process, returns structured result | ✅ | `InternWorkerTest.InternStartCanDispatchExecedBorrowedBytecodeSlabFromMountedImage` |
| Worker maps shared static/core slabs read-only, uses private dynamic state | ✅ | `static_mounts_read_only: "true"` in safety envelope; `workspace` is private |
| Worker publication/result transfer doesn't require mutable coordinator Listree | ✅ | Results copied via JSON on coordinator thread; `result_merge_thread: "coordinator"` |
| **Documentation states which handles/capabilities may be inherited, rehydrated, or blocked** | ✅ | **This document** |

---

## 7. Test Coverage

| Test | Launch Mode | Static Entry Type | Verifies |
|------|-------------|-------------------|----------|
| `WorkerExecutesSharedStaticBaseCodeThunk` | Thread | Inline `program` | Frozen read-only context + static-immortal base thunk |
| `WorkerExecutesSharedBaseWithMmapStaticMountContract` | Thread | Inline `program` + `static_mounts` | Read-only mmap static mount descriptor |
| `ForkedWorkerExecutesInheritedSharedBaseAndStaticMounts` | Fork | Inline `program` + `static_mounts` | Fork inheritance of read-only + static-immortal |
| `InternStartCanDispatchForkedWorkerProcess` | Fork (async) | Inline `program` + `static_mounts` | Public `intern_start!`/`intern_sync!` over fork |
| `ExecedWorkerIndependentlyMountsStaticDeclarationImage` | Exec (sync) | `static_program_mount` + `static_program_word` (`program_source`) | Independent mmap + static source entry |
| `InternStartCanDispatchExecedStaticImageWorker` | Exec (async) | `static_program_mount` + `static_program_word` | Public async over exec + independent mount |
| `InternStartCanDispatchExecedStaticProgramFromMountedImage` | Exec (async) | `program_source` in declaration | Static program-entry contract |
| `InternStartCanDispatchExecedStaticBytecodeFromMountedImage` | Exec (async) | `bytecode_hex` in declaration | Static bytecode-entry contract (hex) |
| `InternStartCanDispatchExecedBorrowedBytecodeSlabFromMountedImage` | Exec (async) | `bytecode_slab_path` + offset/size/hash | **OS-backed borrowed bytecode-slab contract (mmap)** |

---

## 8. Future Work (Deferred to Later Goals)

| Work | Goal | Dependency |
|------|------|------------|
| Root1 forkserver mode (if handle inheritance risks controlled) | G107 stretch | G110 broker stability |
| Durable/session-safe continuation reconstruction for `await!` | G096, G104 | G104 code/activation split |
| Per-symbol re-entrancy annotations (read-only, thread-safe, process-isolated) | G092 | G110 capability hooks |
| Automatic coordinator merge policy for trusted worker results | G099 stretch | G099 result contracts |
| Lease reconstruction after remap/resume for static mounts | G106, G096 | G106 slab advertisement registry |

---

## 9. Related Documents

- 🔗[WP — Static Slot Table Image Boundary](../../WorkProducts/WP-StaticSlotTableImageBoundary-2026-05-19/index.md)
- 🔗[WP — Static Declaration Image Mount Plan](../../WorkProducts/WP-StaticDeclarationImageMountPlan-2026-05-19/index.md)
- 🔗[WP — Static Slab Ownership Audit](../../WorkProducts/WP-StaticSlabOwnershipAudit-2026-05-19/index.md)
- 🔗[WP — Code Object / Activation State Boundary](../../WorkProducts/WP-CodeObjectActivationBoundary-2026-05-25/index.md)
- 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md)
- 🔗[G110 — Root1 eventfd/epoll Resource Broker](../Goals/G110-EventfdEpollMicroVmIpcDesign/index.md)
- 🔗[G103 — Build-Time Static Core Declaration Image](../Goals/G103-BuildTimeStaticCoreDeclarationImageMvp/index.md)
- 🔗[G105 — ReadOnly Static Slab Ownership Model](../Goals/G105-ReadOnlyStaticSlabOwnershipModel/index.md)