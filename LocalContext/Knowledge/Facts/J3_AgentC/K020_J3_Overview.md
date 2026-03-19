# Knowledge: J3 (AgentC) Overview

**ID**: LOCAL:K020
**Category**: [J3 AgentC](index.md)
**Tags**: #j3, #agentc, #architecture, #overview

## Identity
- **Name**: AgentC (formerly J3).
- **Concept**: A "Cognitive Operating Environment" designed to align with the cognitive constraints of Large Language Models (LLMs).
- **Philosophy**: Replaces token-verbose text (Python/JSON) with a high-density, introspectable, and reversible structural representation.

## Architectural Pillars
1.  **Physical Substrate**: Slab-Allocated Relocatable Heap. All state resides in contiguous arenas addressed by `CPtr`.
2.  **Data Structure**: Unified `Listree` (List + Dictionary). Homogeneous representation for code, stack, and heap.
3.  **Semantic Bridge**: `Cartographer`. Uses `libclang` to map C++ structs/functions into the Concept Space.
4.  **Cognitive Logic**: `Mini-Kanren` integration for planning and search (reversible state).

## The "Stack of Lists" VM
The VM operates on five primary resource stacks (Listrees):
1.  `VMRES_DICT`: Dictionary/Scope.
2.  `VMRES_STACK`: Data Stack.
3.  `VMRES_FUNC`: Function/Call Stack.
4.  `VMRES_EXCP`: Exception Stack.
5.  `VMRES_CODE`: Instruction Pointer Stack.

## Comparison to J2
- **Language**: C++17 vs C.
- **Reflection**: Static (Clang) vs Dynamic (DWARF).
- **Memory**: Smart Pointers (`CPtr`) vs Manual Refcounting.
- **Focus**: AI/Reasoning vs General Scripting/FFI.
