# AgentC Extensions

This directory holds importable helper libraries that act as an early stdlib for AgentC.

Current modules:

- `agentc_stdlib.h` / `agentc_stdlib.cpp`
  - memory allocation and freeing
  - LTV string to owned C-string conversion
  - scalar packing into raw memory buffers
  - array/struct-style indexed field writes and reads
  - pointer slicing for subviews into existing buffers
  - typed binary packing, slicing, concatenation, and scalar views

Design rules:

- Keep helpers generic and reusable across libraries and demos.
- Prefer `ltv` arguments when Edict values need to cross into native code without first becoming raw pointers.
- Use helpers to fill practical language gaps, not to hide whole foreign APIs behind one-off wrappers.
