# Category: J2 Archive (AgentC Progenitor)

**ID**: LOCAL:C_J2
**Parent Category**: [Knowledge](../index.md)
**Description**: Knowledge related to the J2 project (Edict J2), the C-based predecessor to the current J3 (AgentC) system. This category serves as a historical archive and a basis for architectural comparison.

## Sub-Categories / Items
- **[Overview](K010_J2_Overview.md)**: High-level summary of J2's architecture and goals.
- **[Listree (J2)](K011_J2_Listree.md)**: Details of the C-based Listree implementation.
- **[VM (J2)](K012_J2_VM.md)**: The stackless bytecode VM and its instruction set.
- **[Reflection (J2)](K013_J2_Reflection.md)**: DWARF-based reflection and FFI system.
- **[FFI Implementation](K014_J2_FFI_Implementation.md)**: Historical FFI behavior used as a comparison reference for J3.

## Comparison Notes
J2 laid the foundation for J3's "Slab & Listree" architecture but differs significantly in implementation:
- **Language**: J2 is C (GNU99); J3 is C++17.
- **Reflection**: J2 uses runtime DWARF parsing; J3 uses build-time Clang parsing (Cartographer).
- **Structure**: J2 Listree is a C struct with unions; J3 is a C++ class with explicit pointers.

## Navigation
- **Up**: [Knowledge Root](../index.md)
- **Lateral**: [Container Hardening (J3)](../K001_Container_CLL.md) (Compare J2 Listree vs J3 Listree)
