# Archive Index

Retired items. Files preserved in Archive/ subdirectories.
Last updated: 2026-05-28

## Goals
| Archived | ID | Title | Status | Summary |
|---|---|---|---|---|
| 2026-05-28 | G105 | ReadOnly Static Slab Ownership Model | Complete | Immortal static slab model, mount lease/registry, PROT_READ mmap safety proven |
| 2026-05-28 | G107 | Process-Isolated Micro-VM Interns | Complete | Fork/exec workers, independent G103 image mounting, borrowed bytecode slab execution |
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
