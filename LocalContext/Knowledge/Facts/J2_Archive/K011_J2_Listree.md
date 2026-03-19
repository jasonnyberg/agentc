# Knowledge: J2 Listree (C Implementation)

**ID**: LOCAL:K011
**Category**: [J2 Archive](index.md)
**Tags**: #j2, #c, #listree, #data-structure

## Overview
The J2 Listree is the C-based precursor to J3's unified data structure. It uses a `struct` with a `union` to hold either a list (`CLL`) or a tree (`LTI*`).

## Core Structures

### `LTV` (Listree Value)
- **Purpose**: The fundamental node.
- **Structure**:
  ```c
  typedef struct {
      union {
          CLL ltvs;   // List of children (LTVR)
          LTI *ltis;  // Tree root (Dictionary)
      } sub;
      LTV_FLAGS flags;
      void *data;
      int len;
      int refs;       // Reference count
  } LTV;
  ```
- **Union**: `sub` determines the container mode.
  - `LT_LIST` flag unset: `sub.ltis` is valid (Tree Mode).
  - `LT_LIST` flag set: `sub.ltvs` is valid (List Mode).

### `LTI` (Listree Item)
- **Purpose**: A named bucket in the AA-Tree dictionary.
- **Structure**:
  ```c
  struct LTI {
      LTI *lnk[2];  // Left/Right children
      char *name;   // Key
      CLL ltvs;     // Stack of values (LTVR)
      // ...
  };
  ```
- **Difference from J3**: J3 wraps `AATree` in `ListreeValue`, whereas J2's `LTV` holds a pointer to `LTI` root directly.

### `LTVR` (Listree Value Reference)
- **Purpose**: A node in the `CLL` that points to an `LTV`.
- **Structure**:
  ```c
  typedef struct {
      CLL lnk;
      LTV *ltv;
  } LTVR;
  ```

## Flags (`LTV_FLAGS`)
- `LT_LIST`: Determines the union mode.
- `LT_DUP`/`LT_OWN`: Memory management for `data`.
- `LT_CVAR`: Indicates data is a C variable (for reflection).
- `LT_NULL`: Empty value.

## Comparison to J3
- **Union vs Explicit**: J2 uses a union (`sub`), risking type confusion if flags are wrong. J3 uses explicit `list` and `tree` pointers.
- **Language**: J2 is C, manual refcounting (`refs` member). J3 is C++, smart pointers (`CPtr`).
- **Reflection**: J2 has `LT_CVAR` flags for embedding C vars. J3 uses a separate "Cartographer" mapping approach.
