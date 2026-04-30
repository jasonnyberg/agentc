# Category: J3 AgentC (Current Project)

**ID**: LOCAL:C_J3
**Parent Category**: [Knowledge](../index.md)
**Description**: Knowledge related to the AgentC (J3) project, a C++ implementation of the "Slab & Listree" architecture for autonomous agent reasoning.

## Core Data Structures (Containers)
- **[CLL (Circular Linked List)](K001_Container_CLL.md)**: The fundamental sequence structure.
- **[AATree (Dictionary)](K002_Container_AATree.md)**: The fundamental associative structure.
- **[Listree (Unified Node)](K003_Container_Listree.md)**: The unified node type (List + Tree).
- **[Cursor (Navigation)](K004_Container_Cursor.md)**: Path-based navigation and resolution.

## Architecture & Implementation
- **[Overview](K020_J3_Overview.md)**: High-level goals, "AgentC" identity, and comparison to J2.
- **[Slab Allocator](K024_J3_SlabAllocator.md)**: Relocatable memory model and `CPtr`.
- **[VM (AgentC)](K022_J3_VM.md)**: The stack-based, logic-oriented Virtual Machine.
- **[Cartographer (Reflection)](K023_J3_Cartographer.md)**: Clang-based static reflection.
- **[Logic / Mini-Kanren](K025_J3_Logic.md)**: Relational programming and unification engine.
- **[Edict Mini-Kanren Surface](K028_Edict_MiniKanren_Surface.md)**: Working native Edict logic forms, examples, and current limits.
- **[Evaluation Model](K026_J3_Evaluation_Model.md)**: Explicit evaluation rules (no auto-execution).
- **[Term Rewrite Runtime](K027_J3_Term_Rewrite_Runtime.md)**: Active VM rewrite semantics, wildcard substitution, and safety limits.
- **[Edict Rewrite Surface](K029_Edict_Rewrite_Surface.md)**: Working source-defined rewrite rules, examples, and current limits.
- **[Arena Persistence Boundary](K030_J3_Arena_Persistence.md)**: Slab-oriented persistence boundary, current file/memory-backed restore path, and legacy LMDB context.

## Notable Fixes
- **[`op_EVAL` Tail-Call Fix](F001_OpEval_TailCall_Fix.md)**: Regression note for the historical infinite-loop/hang and its validation path.

## Comparison Notes
J3 represents a shift from C (J2) to C++ (J3) to leverage stronger typing, better tooling (Clang), and a more robust object model while maintaining the "Zero-Copy / Zero-Glue" philosophy.

## Navigation
- **Up**: [Knowledge Root](../index.md)
- **Lateral**: [J2 Archive](../J2_Archive/index.md)
