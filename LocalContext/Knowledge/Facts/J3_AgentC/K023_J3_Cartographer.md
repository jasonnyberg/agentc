# Knowledge: J3 Cartographer (Reflection)

**ID**: LOCAL:K023
**Category**: [J3 AgentC](index.md)
**Tags**: #j3, #reflection, #clang, #ffi

## Overview
Cartographer is the reflection and FFI subsystem of AgentC. It maps C/C++ headers into the Concept Space (`Listree`) and enables dynamic invocation of native functions.

## Components

### `CartographerService`
- **Purpose**: Service-layer import orchestration.
- **Function**: Centralizes the current synchronous, resolved-artifact, and deferred Cartographer import pipeline by validating inputs, loading a target shared library, mapping or decoding the requested API description, annotating imported definitions with coarse metadata, and returning or staging self-contained import objects for Edict consumption.
- **Execution Contract**:
  - request/response types now carry `executionMode` (`Sync` or `Deferred`) and optional `requestId`
  - `Sync` is fully implemented today, while `Deferred` now uses a real subprocess-backed + later collection contract rather than a hard error
  - current deferred progress crosses a forked subprocess boundary over framed pipes; it is not yet a shared-arena ownership boundary
- **Current Metadata**:
  - top-level `__cartographer` provenance with `library`, `header`, `scope`, `symbol_count`, `execution_mode`, `status`, optional `request_id`, and programmer-managed binding metadata (`binding_mode`, `requested_name`)
  - the current worker-boundary transport contract is declared as `protocol = "protocol_v1"`
  - imported-definition payload format is declared as `api_schema_format = "parser_json_v1"`
  - resolved-artifact imports also expose `resolved_schema_format = "resolver_json_v1"` plus resolved-schema path and fingerprint metadata
  - deferred handles and resolved scope metadata now also record `service_boundary = "subprocess_pipe"` and `response_owner = "vm_collect"`
  - per-function `symbol`, `safety`, `imported_via = "cartographer_service"`, `policy = "blocked_by_default"` for unsafe imports, and resolver annotations such as `resolution_status` / `resolved_symbol` for resolved-artifact imports
- **Current Limit**: This is now a forked subprocess + pipe slice. Shared-arena ownership transfer, richer lifecycle management, and stronger service isolation remain future `LOCAL:G030` work.
- **Current Planning State**: Baseline capability plus the programmer-managed import-object surface are now landed and validated; the current follow-on plan is builtin bootstrap architecture inside `agentc`, with cache-preference policy treated as a later question inside that model rather than the immediate next decision. The contained stdlib-container cleanup stream is complete for its current boundary, and any deeper conversion should be treated as redesign-scoped follow-on work instead of ambient cleanup.
- **Current Protocol Shape**:
  - `cartographer/protocol.cpp` defines a constrained Edict-shaped `protocol_v1` grammar for `import_request` and `import_status` messages.
  - The service treats Edict-like protocol text as the seam between caller-owned state and subprocess-owned execution, but does not execute arbitrary Edict programs.
  - `import_status` messages now carry JSON API-schema payloads (`parser_json_v1`) so mapped definitions cross the process boundary as plain protocol data.
  - This keeps the protocol human-readable and future-IPC-friendly without turning the service boundary into remote code execution.
- **Standalone Utility Split**:
  - `cartographer/parser.cpp` exposes reusable header-to-description and header-to-JSON helpers.
  - `cartographer/resolver.cpp` consumes parsed API schema plus a shared library, computes a library fingerprint (`content_hash`, `file_size`, `modified_time_ns`), and emits annotated resolved-schema JSON (`resolver_json_v1`) with per-symbol resolution status and optional process-local addresses.
  - `cartographer/tool_cli.cpp` exposes reusable command-runner logic for parser/resolver tool entrypoints, including stdin-fed schema resolution with `-`.
  - `cartographer_parse` and `cartographer_resolve` wrap those helpers as independent CLI tools so parsing and resolution can be validated or cached outside the main AgentC process.
  - `cartographer/tests/tool_tests.cpp` now validates both the library-backed command runners and spawned standalone processes, including a direct `cartographer_parse | cartographer_resolve` pipeline over stdio, an explicit separate-process invocation flow where parser output is saved and later consumed by the resolver, and exact CLI usage/output-shape assertions for both standalone tools.
- **Bootstrap Direction**:
  - The intended long-term model is now `j2`-style self-import bootstrap: `agentc` should link the import-support libraries directly and expose native import capability at startup rather than depending on external bootstrap executables.
  - Parser and resolver should remain separately linkable/runnable components and may still be wrapped as standalone tools, but those wrappers are tooling/test surfaces rather than prerequisites for interpreter bootstrap.
  - Current code already partially satisfies the linkage side of that design: `libedict` links `cartographer`, and `EdictVM::EdictVM(...)` currently constructs `Mapper`, `FFI`, and `CartographerService` eagerly.
  - The first narrowing step is now landed: `EdictVM` installs a reserved `__bootstrap_import` capsule and no longer exposes the import-family helpers as ordinary always-on builtins.
  - `edict/edict_compiler.cpp` now also treats `map`, `load`, `import`, `import_resolved`, `import_deferred`, `import_collect`, and `import_status` as ordinary identifiers rather than hard-coded keywords, so the bootstrap seam is now explicit at the source level.
  - The next narrowing step is now also landed: `EdictVM` executes a startup Edict prelude that curates ordinary root-level `parser` and `resolver` objects from `__bootstrap_import.curate_parser` and `__bootstrap_import.curate_resolver` before user code runs.
  - Those curated objects currently carry `__cartographer` metadata with `binding_mode = "startup_curated"` and `imported_via = "bootstrap_capsule"`, making the handoff explicit without requiring source programs to call the capsule directly for normal parser/resolver use.
  - The next narrowing step is now also landed: the curated objects' normal public methods are compiled Edict wrappers, while the transitional native helper thunks now live under nested `__native` helper objects on `parser` and `resolver`.
  - The next narrowing step is now also landed: the public synchronous `resolver.import` path no longer delegates directly to a one-hop native helper and instead composes parse (`parser_json_v1`) -> resolve (`resolver_json_v1`) -> in-memory resolved import materialization in interpreted code.
  - The next narrowing step is now also landed: public `resolver.import_resolved` no longer delegates directly to a one-hop native helper and instead reads artifact text through hidden `resolver.__native.read_text` before reusing the interpreted `resolver.import_resolved_json` stage.
  - The follow-up naming cleanup is also landed: the C++ helper layer now uses `parser::parseHeaderToParserJson(...)`, `protocol::parserSchemaFormatName()`, `resolver::resolverSchemaFormatName()`, `CartographerService::importResolverJson(...)`, and resolver-side `parserSchemaFormat` / `parserSchemaJson` fields so implementation terminology matches the wire-format names.
  - The unresolved work is now extending that handoff to the remaining public surfaces and later removing no-longer-fundamental helper paths.
- **Newest Slab Cleanup Slice**:
  - `cartographer/service.cpp` now stores deferred request state in slab-backed `AATree<DeferredState>` entries instead of `std::unordered_map`, and replaces tiny unsafe-name hash sets with direct helper checks.
  - `cartographer/protocol.cpp` now decodes internal `protocol_v1` messages sequentially instead of building `std::vector` / `std::unordered_map` token-field intermediates.
  - `listree/listree.cpp` now uses slab-backed `TraversalContext` instead of `std::set<SlabId>` for `ListreeValue::toDot(...)` visited tracking.
- `listree/listree.cpp` also now uses a slab-backed `CLL<ListreeValue>` queue instead of `std::deque<CPtr<ListreeValue>>` for breadth-first `ListreeValue::traverse(...)` execution.
- `cartographer/mapper.cpp` now uses a fixed small argument array instead of a transient `std::vector<const char*>` when selecting libclang parse arguments in `Mapper::parseDescription(...)`.
- `cartographer/resolver.h` / `cartographer/resolver.cpp` now use slab-backed `CLL<ResolvedSymbol>` storage inside `ResolvedApi` instead of `std::vector<ResolvedSymbol>`, with helper methods preserving encode/decode iteration order.
- `cartographer/ffi.h` / `cartographer/ffi.cpp` now use slab-backed `CLL<ClosureInfoNode>` storage for closure tracking instead of `std::vector<ClosureInfo*>`, while still keeping stable `ClosureInfo*` allocations alive for libffi callback state.
- `cartographer/mapper.h` / `cartographer/mapper.cpp` now also use slab-backed `CLL<NodeDescription>` storage for `NodeDescription::children` and `ParseDescription::symbols`, with helper APIs preserving parse/materialize/schema traversal order.
- `cartographer/ffi.h` / `cartographer/ffi.cpp` now also consume slab-backed/Listree argument lists for `invoke(...)` and fill libffi parameter arrays directly, removing the remaining runtime vector argument path from Cartographer FFI invocation.
- `cartographer/tool_cli.h` / `cartographer/tool_cli.cpp` plus `cartographer_parse` / `cartographer_resolve` now also consume slab-backed/Listree argument lists instead of `std::vector<std::string>` command argument wrappers.
- `cartographer/tests/tool_tests.cpp` and `edict/tests/callback_test.cpp` now also use slab-backed/Listree argument lists for spawned-process helper invocation instead of vector wrappers.
- `edict/edict_vm.cpp` has also started the selected Stage 4 cleanup by replacing the temporary rewrite-stack inspection vector in `applyRewriteOnce(...)` with slab-backed/Listree list handling.
- `edict/edict_vm.cpp` now also replaces the temporary splice-item collection vector in `op_SPLICE()` with slab-backed/Listree list handling, preserving existing list order by replaying the copied list in reverse iteration order when writing into the destination list.
- `edict/edict_vm.cpp` now also replaces the temporary iterator-function collection vector in `op_EVAL()` with slab-backed/Listree list handling, preserving iterator-evaluation order by replaying the collected function list in reverse index order.
- `edict/edict_vm.cpp` now also replaces the temporary iterator-value collection vector in `executeIterativeFFI()` with slab-backed/Listree list handling, preserving iterative FFI expansion order through indexed replay over a collected Listree snapshot.
- `edict/edict_vm.cpp` now also replaces the temporary parameter-node collection vector in `op_EVAL()` with slab-backed/Listree list handling, preserving FFI parameter discovery order through indexed access over a collected Listree snapshot.
- `edict/edict_vm.cpp` now also replaces `SpeculativeSnapshot::bytes` with non-container string-backed binary snapshot storage, removing another local vector from the VM helper path without widening subsystem boundaries.
- `edict/edict_vm.cpp` now also replaces the remaining temporary FFI argument vectors in `op_EVAL()` / `executeIterativeFFI(...)` with slab-backed/Listree argument lists while preserving callback auto-closure behavior and iterative expansion order.
- `EdictVM::dumpStack()` now returns a slab-backed/Listree stack snapshot instead of `std::vector<std::string>`, with CLI/REPL/tests/demos decoding that snapshot only at their boundaries.
- The contained post-scope cleanup stream is complete for its current boundary; any deeper remaining conversion work should be treated as redesign-scoped follow-on work rather than part of the finished `LOCAL:G032` stream.

## Current Policy Boundary
- The Edict VM now enforces Cartographer-imported safety metadata at call time.
- Functions imported through `cartographer_service` with `safety = "unsafe"` are rejected by default with a VM error unless unsafe FFI calls are explicitly enabled on the VM.
- Edict also exposes source-level extension-policy words: `unsafe_extensions_allow`, `unsafe_extensions_block`, and `unsafe_extensions_status`.
- This makes current Cartographer safety tags operational policy rather than passive annotation.

### `Mapper`
- **Purpose**: Static Reflection.
- **Engine**: `libclang` (C Interface).
- **Function**: `parse(filename)` reads a C/C++ source/header file and traverses the AST (Abstract Syntax Tree) to generate a `Listree` representation of types, structs, and functions.
- **Current Split**: `parseDescription(filename, out)` now produces a plain STL-owned description tree, and `materialize(description)` converts that description into `ListreeValue` objects later on the VM thread.
- **Output**: A "Map" (Dictionary) of definitions that the VM can use to understand memory layouts and function signatures.

### `FFI`
- **Purpose**: Dynamic Invocation.
- **Engine**: `libffi` + `libdl`.
- **Function**: `invoke(funcName, definition, args)`
  - Loads shared libraries (`dlopen`).
  - Prepares call frames (`ffi_prep_cif`).
  - Marshals AgentC values (`ListreeValue`) to C types.
  - Executes the function.
  - Unmarshals the return value back to `ListreeValue`.

## Comparison to J2
- **Source vs Binary**: J2 reflected on *binaries* using DWARF. J3 reflects on *source/headers* using Clang. This makes J3 less dependent on compiler debug flags but requires header files.
- **Structure**: J2 mixed reflection and FFI in `reflect.c`. J3 separates them into `Mapper` (Static) and `FFI` (Dynamic).

## Current Edict Surface
- Edict now exposes the transitional native import family through the reserved `__bootstrap_import` object rather than the ordinary root built-in namespace.
- Startup now curates ordinary root-level `parser` and `resolver` objects by executing `__bootstrap_import.curate_parser ! @parser __bootstrap_import.curate_resolver ! @resolver` inside `EdictVM` initialization.
- `parser.materialize_json !` now exposes the `parser_json_v1`-to-definitions stage as an interpreted wrapper over `parser.__native.materialize_json !`.
- `parser.map !` now composes multiple interpreted stages: it parses a header to `parser_json_v1` via `parser.parse_json !` and then materializes that schema to definitions via `parser.materialize_json !`.
- `parser.parse_json !` now exposes the header-to-`parser_json_v1` stage as an interpreted wrapper over `parser.__native.parse_json !`.
- `resolver.resolve_json !` now exposes the library-plus-`parser_json_v1`-to-`resolver_json_v1` stage as an interpreted wrapper over `resolver.__native.resolve_json !`.
- `resolver.import_resolved_json !` now exposes in-memory `resolver_json_v1` materialization as an interpreted wrapper over `resolver.__native.import_resolved_json !`.
- `resolver.import !` is now the first normal resolver/import-facing source surface that composes multiple interpreted stages: it parses headers to `parser_json_v1`, resolves that schema against the target library to `resolver_json_v1`, and then materializes the returned import object from in-memory resolved JSON.
- `resolver.import_resolved !` now also composes multiple stages: it reads a resolved-artifact file through `resolver.__native.read_text !` and then materializes the returned import object through `resolver.import_resolved_json !`.
- `resolver.import_status !` now composes multiple stages: it first normalizes either a deferred handle or raw request-id string through `resolver.__native.request_id !` and then queries the narrower native status helper.
- `resolver.import_collect !` now follows the same interpreted request-id normalization path before delegating to the narrower native collection helper.
- `resolver.import_deferred !` now also composes multiple stages: it queues through `resolver.__native.import_deferred !`, normalizes the returned request identity through `resolver.__native.request_id !`, performs an immediate status poll through `resolver.__native.import_status !`, discards that first status snapshot, and returns the deferred handle object.
- `resolver.load !` now also routes through interpreted wrapper shape (`@library ... resolver.__native.load !`) rather than direct one-hop delegation, aligning it with the same interpreted-boundary contract as the other public parser/resolver surfaces.
- `__bootstrap_import` is now narrowed to curation entrypoints only (`curate_parser`, `curate_resolver`); raw helper thunks are no longer published there.
- Hidden helper scaffolding was narrowed as well: `parser.__native.map`, `resolver.__native.import`, and `resolver.__native.import_resolved` were removed because those public surfaces now stage through interpreted choreography built from lower-level helpers.
- Deferred handle metadata now also exposes `protocol = "protocol_v1"` and `api_schema_format = "parser_json_v1"` so source-visible results reflect the current service-contract format.
- `resolver.import_resolved !` consumes a resolved artifact file path plus scope name, reloads the target library in-process, decodes the embedded `parser_json_v1` schema, and returns the resulting self-contained import object for explicit source-level binding.
- `edict/tests/callback_test.cpp` now also proves the built `edict` CLI can run that `resolver.import_resolved !` demo end to end as a spawned process and print the expected stack result.
