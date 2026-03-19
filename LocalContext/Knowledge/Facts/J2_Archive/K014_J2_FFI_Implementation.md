# Knowledge: J2 FFI Implementation Reference

**ID**: LOCAL:K014
**Scope**: CORE (Archive) / LOCAL (Reference)
**Context**: Deep dive analysis of `j2/reflect.c` for J3 re-implementation.

## Overview
J2's FFI (Foreign Function Interface) is built on `libffi` and uses DWARF-based reflection to automate C function calls without manual wrappers ("Zero Glue").

## Key Components

### 1. Argument Marshaling (`cif_ffi_call`)
- **Mechanism**: Iterates over a list of "coerced" interpreter values and places their data pointers into a `void**` array for `libffi`.
- **Handling Arrays**: If the `LT_ARR` flag is set, it passes the address of the data (`&ltv->data`); otherwise, it passes the data pointer directly (`ltv->data`).
- **Signature**: `int cif_ffi_call(LTV *cif, void *loc, LTV *rval, CLL *coerced_args)`

### 2. Type Coercion (`cif_coerce_i2c`)
- **Interpreter -> C**:
    - **LTV Pointer**: If the destination type is `(LTV)*`, it creates a C-compatible wrapper for the interpreter object.
    - **Strings**: Maps LTV data to `char*` or `unsigned char*`.
    - **Base Types**: Uses `Type_pullUVAL` / `Type_pushUVAL` to copy raw bytes for ints/floats.
    - **Protection**: Attaches the original LTV to the coerced LTV via `TYPE_CAST` attribute to prevent garbage collection during the call.
- **C -> Interpreter (`cif_coerce_c2i`)**:
    - Mostly unwraps `(LTV)*` pointers back into native interpreter references.

### 3. Closures and Callbacks (`cif_create_closure`)
- **Allocation**: Uses `ffi_closure_alloc` to get a pair of pointers: one for the executable function pointer (passed to C) and one for the writeable structure (managed by J2).
- **Initialization**: `ffi_prep_closure_loc` links the closure to a "thunk" (a C function in J2) and passes the specific interpreter closure object as `user_data`.
- **Storage**: The closure object stores its "writeable" address in an attribute for cleanup.

### 4. Call Interface (CIF) Preparation
- **Discovery**: Uses `cif_find_function` to get the signature from metadata.
- **Preparation**: `ffi_prep_cif` is called with the return type and argument types derived from the discovered metadata.

## Lessons for J3
1.  **Marshaling**: Maintain the `void**` array strategy.
2.  **Types**: Replace DWARF discovery with `clang` AST traversal (Cartographer).
3.  **Encapsulation**: The `(LTV)*` pattern is critical for allowing C code to hold references to AgentC objects safely.
4.  **Math**: As per the new design, basic math ops will be implemented by calling a C library using this FFI mechanism.
