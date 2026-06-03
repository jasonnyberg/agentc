# WorkProduct: Process-Isolated Micro-VM Launch Contract

## Objective
Formalize the interface and handle-security policy for the process-isolated `edict_worker_exec` runtime, established in G107.

## 1. Task Envelope Schema
The task envelope is a JSON structure consumed by `edict_worker_exec`. It acts as the "source of truth" for the worker's initial environment.

```json
{
  "worker_type": "edict-exec-async",
  "task_id": "uuid-v4",
  "input": { ... },
  "context": { ... },
  "static_mounts": {
    "base": {
      "container_path": "/path/to/g103_image",
      "root_id": "id"
    }
  },
  "static_program_mount": "base",
  "static_program_word": "entry_point",
  "bytecode_encoding": "agentc.edict.bytecode.v1.mmap",
  "bytecode_slab_path": "/path/to/slab",
  "bytecode_slab_offset": 0,
  "bytecode_slab_size": 1024
}
```

## 2. Handle & Capability Policy
To ensure isolation and security, the following policy applies to all `edict_worker_exec` instances:

### A. Inherited Handles
*   **Allowed**: Standard pipes for `stdin`, `stdout`, `stderr` (typically redirected to `/dev/null` or logging collectors). Private pipes for `outcome_pipe` (for status return) and `task_pipe` (for initial JSON-safe envelope delivery).
*   **Blocked**: All file descriptors related to the coordinator's LMDB arena, network sockets, or other active session slabs.

### B. Rehydrated Handles
*   **Static Mounts**: The worker is permitted to `mmap` specific `container_path` files found in the task envelope. These are strictly `PROT_READ` and mapped as immutable static images.
*   **Bytecode Slabs**: The worker is permitted to `mmap` the file at `bytecode_slab_path` for the purpose of pinning the `.code_frame` binary object.

### C. Blocked/Forbidden Handles
*   **VM State**: No raw pointers originating from the coordinator process are valid inside the worker. All cross-process references must be mapped through the `Root1` resource broker or the static mmap slabs.
*   **Mutable Handles**: Any FD pointing to a writable slab or internal database lock is strictly stripped during the `exec` transition.

## 3. Output Channel
*   **Format**: The worker writes a single serialized `InternWorkerOutcome` JSON envelope to the private `outcome_pipe` before terminating.
*   **Validation**: The coordinator-side supervisor performs schema validation on this envelope before publishing it to the Root1 broker.
