# Archive Index

Retired items. Files preserved in Archive/ subdirectories.
Last updated: 2026-06-21

## Goals
| Archived | ID | Title | Status | Summary |
|---|---|---|---|---|
| 2026-06-21 | G093 | Reference-Scoped ReadOnly Sharing | Complete | Explicit overlay dictionaries (7 VM opcodes), shadow/fallthrough lookup, coordinator isolation, ReadOnly preservation |
| 2026-06-21 | G074 | Real-time FFI Token Streaming | Complete | SSE/streaming provider worker, `agentc_call_stream!`/`agentc_stream_sync!`, live Google/Gemma smoke |
| 2026-06-21 | G078 | Edict-Resident Agent Loop Consolidation | Complete | Edict owns provider/session/tool/context; C++ retains transport/credentials/lifecycle; catalog-driven rehydration |
| 2026-06-21 | G079 | Edict Agent Loop Tool Support | Complete | `agentc_tools` with file read/write/replace/shell helpers; `llm.edict` provider tools attachment |
| 2026-06-21 | G080 | LLM REPL Context Management | Complete | Provider-owned `context_reset!`/`context_inspect!` plus REPL slash commands `/reset`, `/clear`, `/context`, `/inspect` |
| 2026-06-21 | G084 | Remove Dead Dictionary Payload | Complete | Removed unused dictionary payload slot from ListreeItem |
| 2026-06-21 | G085 | Split Edict VM Translation Unit | Complete | EdictVM split into core/ffi/bootstrap/treesitter/knowledge-graph translation units |
| 2026-06-21 | G086 | Live Google LLM Regression Coverage | Complete | Live Google/Gemma request/response regression tests |
| 2026-06-21 | G087 | Prefer Adjacent Eval Sigil | Complete | Adjacent `word!` spelling documented as canonical; `!` as separate token retained for compatibility |
| 2026-06-21 | G088 | Refresh README Current State | Complete | README updated to reflect current architecture and capabilities |
| 2026-06-21 | G089 | Remove Edict Pop Keyword | Complete | `pop` removed; bare `/` is the documented stack discard |
| 2026-06-21 | G090 | User-Oriented README | Complete | README targets potential users with AgentC's unique value, applications, and maturity expectations |
| 2026-06-21 | G091 | Intern Worker Concurrency MVP | Complete | Blocking/async intern dispatch, lifecycle/drop/abandon, cancellation checkpoints, private cleanup boundary |
| 2026-06-21 | G092 | Cartographer FFI Re-entrancy Metadata | Complete | 8 capability fields, Cartographer import classification, validation rejecting missing fields |
| 2026-06-21 | G094 | Curated Native Cognitive Libraries | Complete | Tree-sitter grammar, C++ bridge, Edict builtins, structural diff, persistent knowledge graph |
| 2026-06-21 | G095 | Edict Cognitive Skill Scaffolds | Complete | `cognitive.edict` module with investigation/code-review/refactor-plan scaffolds, JSON-serializable state |
| 2026-06-21 | G096 | Authoritative mmap Session Resume | Complete | Deterministic root/session restore, file-backed slab[0], scheduler persistence, layered static mount restore |
| 2026-06-21 | G097 | Composite Speculation + Logic + FFI Demo | Complete | `demo_composite_speculation_logic_ffi` composing Cartographer FFI + miniKanren + durable Listree state |
| 2026-06-21 | G098 | Architectural Vision Work Product | Complete | `WP-AgentCArchitecturalVisionInternConcurrency-2026-06-21.md` preserving full architectural vision and roadmap |
| 2026-06-21 | G099 | Intern Task Quality Contracts | Complete | Bounded task schemas, gather/classify/filter examples, dispatch rejection, evidence/confidence result validators |
| 2026-06-21 | G100 | Edict Isolation Contract Hardening | Complete | Isolated-call parent-binding protection, non-leaking thunk locals, explicit context-mutation test |
| 2026-06-21 | G101 | Direct Edict Tool-Emission Path | Complete | `agentc_direct_action!` with `read_file` whitelist, denied-operation envelopes, helper-equivalence tests |
| 2026-06-21 | G102 | Edict Session ID Startup Flag | Complete | `--session ID` creates/resumes root scopes under `/tmp/session/<id>/` |
| 2026-06-21 | G103 | Build-Time Static Core Declaration Image MVP | Complete | Static declaration images, slot-table section descriptors, mmap read-only mounting |
| 2026-06-21 | G104 | Immutable Code Object / Activation Frame Split | Complete | `makeCodeFrame()` immutable code objects, `code_ips_` activation-local IPs, recursive freeze on binary thunks |
| 2026-06-21 | G106 | Root1 Slab Advertisement Registry | Complete | Logical publication layer registry, manifest/hash/root validation, owner/lease/epoch lifecycle |
| 2026-06-21 | G108 | Cursor-Scoped Traversal Visit Bitmaps | Complete | Per-slab bitmaps replacing unordered_set; ~2KB/slab dense vs ~40B/node; benchmark under 10ms |
| 2026-06-21 | G110 | Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design | Complete | Eventfd/epoll broker, mailbox descriptors, resource keys/grants, await!, scheduler save/load, publication registry |
| 2026-05-28 | G105 | ReadOnly Static Slab Ownership Model | Complete | Immortal static slab model, mount lease/registry, PROT_READ mmap safety proven |
| 2026-05-28 | G107 | Process-Isolated Micro-VM Interns | Complete | Fork/exec workers, independent G103 image mounting, borrowed bytecode slab execution, documented handle/capability policy (inherited/rehydrated/blocked) |
| 2026-05-28 | G109 | Listree ReadOnly Mutation Surface Hardening | Complete | Frozen-tree removal/item-history gaps closed for public VM/Cursor paths |
| 2026-05-28 | G111 | Root1/Worker Primitive FFI and Edict Intern Surface Migration | Complete | VM opcodes → imported LTV primitives + plain Edict words; intern words as module code |
| 2026-05-11 | G068 | Client/Agent Split with Embedded Persistent Edict VM | Complete | Foundational embedded-VM/client-host architecture completed; remaining live-loop/tool/context work now tracked by G078/G079/G080 |
| 2026-05-11 | G071 | Session-Scoped Allocator Image Persistence | Complete | Mmap/file-backed allocator persistence substrate completed and fed into runtime rehydration work |
| 2026-05-11 | G072 | Direct Slab Restore Without Full Library Re-import | Complete | Warm restore path, library preloading/change detection, and cold-vs-warm parity validation completed |
| 2026-05-11 | G073 | Pure VM Lifecycle & Host Thinning | Complete | Host-thinning lifecycle slice completed |
| 2026-05-11 | G076 | Generalized Multiline Edict Accumulator | Complete | Multiline Edict accumulator completed |
| 2026-05-11 | G077 | Local LLM Demo Request Diagnostics | Complete | Local demo request/timeout/normalization diagnostics fixed and validated |
| 2026-05-11 | G081 | OpenAI Codex Subscription Provider | Complete | Subscription-backed Codex provider landed and live-validated via Edict `llm.init([openai-codex])` |
| 2026-05-11 | G082 | Edict Prefix Sigil Chain Semantics | Complete | Concatenated prefix sigils now apply left-to-right to the same identifier with regression coverage |
| 2026-05-11 | G083 | Edict Tail-Prefixed Dictionary History | Complete | `-name` tail lookup/assignment/removal semantics fixed and regression-covered |
| 2026-05-03 | G016 | G016 — LMDB Optional Compile-Time Build | Retired | Archived during cleanup |
| 2026-05-03 | G017 | G017 — Edict Stdin/File Script Mode | Retired | Archived during cleanup |
| 2026-05-03 | G018 | G018 — FFI LTV Passthrough (Hoist Boxing to Pure FFI) | Retired | Archived during cleanup |
| 2026-05-03 | G019 | G019 — SlabId LTV Type Unification | Retired | Archived during cleanup |
| 2026-05-03 | G020 | G020 — Early Type Binding (`bindTypes()`) | Retired | Archived during cleanup |
| 2026-05-03 | G021 | G021 — Remove Module Name Parameter from Resolver Import API | Retired | Archived during cleanup |
| 2026-05-03 | G040 | [G040] LMDB Persistent Arena Integration | Retired | Archived during cleanup |
| 2026-05-03 | G041 | [G041] Persistent Slab Image Persistence | Retired | Archived during cleanup |
| 2026-05-03 | G042 | [G042] Persistent VM Root State and Restore Validation | Retired | Archived during cleanup |
| 2026-05-03 | G043 | Goal G043 — LMDB Pickling Bootstrapping (CANCELLED) | Retired | Archived during cleanup |
| 2026-05-03 | G044 | [G044] JSON-based Module Import Caching | Retired | Archived during cleanup |
| 2026-05-03 | G045 | G045 - Language Enhancement Review for Human + Agent Workflows | Retired | Archived during cleanup |
| 2026-05-03 | G046 | G046 - Continuation-Based Speculation | Retired | Archived during cleanup |
| 2026-05-03 | G047 | G047 - Native Relational Syntax | Retired | Archived during cleanup |
| 2026-05-03 | G048 | G048 - Library-Backed Logic Capability | Retired | Archived during cleanup |
| 2026-05-03 | G049 | G049 - Edict VM Multithreading | Retired | Archived during cleanup |
| 2026-05-03 | G050 | G050 - Thread Spawn/Join Mixed-Run Stability | Retired | Archived during cleanup |
| 2026-05-03 | G051 | G051 - Cursor-Visited Read-Only Boundary | Retired | Archived during cleanup |
| 2026-05-03 | G052 | G052 - Runtime Boundary Hardening | Retired | Archived during cleanup |
| 2026-05-03 | G053 | G053 - Shared-Root Fine-Grained Multithreading | Retired | Archived during cleanup |
| 2026-05-03 | G054 | G054 - SDL Import Demo | Retired | Archived during cleanup |
| 2026-05-03 | G055 | G055 - Native SDL POC | Retired | Archived during cleanup |
| 2026-05-03 | G056 | G056 - Extensions Stdlib | Retired | Archived during cleanup |
| 2026-05-03 | G057 | G057 - Pi + AgentC IPC Bridge | Retired | Archived during cleanup |
| 2026-05-03 | G058 | G058 — Read-Only Listree Branches for Cross-VM Sharing | Retired | Archived during cleanup |
| 2026-05-03 | G059 | G059 — Listree-to-JSON Round-Trip Hardening | Retired | Archived during cleanup |
| 2026-05-03 | G060 | G060 - Pi Frontend Integration | Retired | Archived during cleanup |
| 2026-05-03 | G061 | G061 - AgentC Stability and Hardening | Retired | Archived during cleanup |
| 2026-05-03 | G062 | G062: Logic Engine Bootstrapping | Retired | Archived during cleanup |
| 2026-05-03 | G063 | G063: Native C++ Agent Core | Retired | Archived during cleanup |
| 2026-05-03 | G066 | HRM Plan: G066 - Socket-Enabled AgentC Agent (The "AgentC Daemon") | Retired | Archived during cleanup |
| 2026-05-03 | G067 | Goal: G067 - Atomic AgentC Agent Transaction (Request-Response Lifecycle) | Retired | Archived during cleanup |
| 2026-05-03 | G069 | Goal: G069 - Remove LMDB Dependencies | Retired | Archived during cleanup |
| 2026-05-03 | G070 | Goal: G070 - Inverted Loop Surgical Cleanup | Retired | Archived during cleanup |

## WorkProducts
| Archived | ID | Title | Summary |
|---|---|---|---|
| 2026-05-03 | WP | AgentCLanguageEnhancements-2026-03-22 | Superseded/Stale (>30d) |
| 2026-05-03 | WP | ContinuationBasedSpeculationPlan-2026-03-22 | Superseded/Stale (>30d) |
| 2026-05-03 | WP | LogicCapabilityMigrationPlan-2026-03-23 | Superseded/Stale (>30d) |
| 2026-05-03 | WP | NativeRelationalSyntaxPlan-2026-03-22 | Superseded/Stale (>30d) |

## Knowledge Items
| Archived | Type | Name | Reason |
|---|---|---|---|
| 2026-05-03 | WP | EdictVMMultithreadingPlan-2026-03-23 | Superseded/Stale (>30d) |
