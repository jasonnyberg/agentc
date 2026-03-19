# Knowledge: J2 Project Overview

**ID**: LOCAL:K010
**Category**: [J2 Archive](index.md)
**Tags**: #j2, #architecture, #history, #c

## Project Identity
- **Name**: J2 (Edict)
- **Concept**: "Executable Dictionary" combined with C Reflection/FFI.
- **Goal**: Provide a minimalist, introspection-heavy environment that can dynamically bind to C libraries without glue code.

## Core Pillars
1.  **Listree**: A unified, recursive data structure (List + Tree) that holds *all* system state (stack, dictionary, code).
2.  **Reflection**: Imports C types/functions via DWARF debug info.
3.  **VM**: A stackless, minimalist bytecode VM.

## Key Features
- **Language**: Edict (Stack-based, concatenative, point-free).
- **Evaluation**: Explicit evaluation operator (`!`) treats data as code.
- **Homoiconicity**: "Code is data", but distinct from Lisp's s-expressions; code is stored as literal sequences.
- **Zero Glue**: Can load a `.so` and call functions/access structs immediately.

## Historical Context
- **Predecessor**: J1 (presumably).
- **Successor**: J3 (AgentC).
- **Motivation for J3**: Move to C++ for better runtime reflection (Reflect C++ objects), remove GCC dependencies (nested functions), and support KLEE/Clang.

## Architecture Diagram (Conceptual)
```
[ C Libraries ] <--(DWARF/FFI)-- [ Reflection Engine ]
                                        |
                                        v
                                   [ Listree ] <==> [ VM ]
```
