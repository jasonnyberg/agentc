# Knowledge: J2 Reflection (C/DWARF)

**ID**: LOCAL:K013
**Category**: [J2 Archive](index.md)
**Tags**: #j2, #reflection, #dwarf, #ffi

## Overview
J2 achieves introspection by parsing its own DWARF debug information at runtime. It maps C types and functions into `Listree` structures, enabling the Edict interpreter to manipulate C variables and call C functions directly ("Zero Glue").

## Architecture

### Runtime DWARF Parsing (`reflect.c`)
- **Library**: `libdwarf`.
- **Mechanism**: Iterates over Compilation Units (CUs) and Debug Information Entries (DIEs).
- **Mapping**:
  - `DW_TAG_structure_type` -> Listree Dictionary (Scope).
  - `DW_TAG_member` -> Listree Item (Key).
  - `DW_TAG_base_type` -> Primitive type info.
- **Storage**: `TYPE_INFO_LTV` struct (extends `LTV`) stores DWARF metadata (offset, tag, size).

### Foreign Function Interface (`ffi`)
- **Library**: `libffi`.
- **CIF Preparation**: `cif_ffi_prep` converts DWARF type info into `ffi_type` and `ffi_cif` structures.
- **Invocation**: `cif_ffi_call` uses the prepared CIF to execute the function pointer.
- **Closures**: `cif_create_closure` allows Edict code (thunks) to be called as C functions.

### C Variables (`CVAR`)
- **Flags**: `LT_CVAR` indicates a value is a direct reference to C memory.
- **Coercion**: `cif_coerce_i2c` (Interpreter to C) and `cif_coerce_c2i` (C to Interpreter) handle data marshalling between `LTV`s and raw C buffers.

## Comparison to J3
- **Approach**:
  - **J2**: Runtime DWARF parsing. Requires unstripped binaries. Dynamic and flexible but complex dependency (`libdwarf`, `libelf`).
  - **J3**: Build-time analysis (`Cartographer` using `libclang`). Generates static reflection metadata (Listree graphs) embedded in the binary or loaded from a file.
- **Type Safety**: J2 relies on runtime checks against DWARF info. J3 can leverage C++ type system more (though still dynamic in the VM).
