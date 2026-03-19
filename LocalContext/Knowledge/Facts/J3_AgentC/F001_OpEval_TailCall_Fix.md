# Fact: OpEval Tail Call Infinite Loop Fix

**ID**: LOCAL:F001
**Related**: 🔗[LOCAL:G003](../../Goals/G003_Compiler_Regression_Matrix.md), 🔗[LOCAL:K022](K022_J3_VM.md)
**Date**: 2026-01-17
**Status**: Verified

## Defect
In `EdictVM`, the `op_EVAL` instruction performs a tail-call optimization by popping the current code frame (`popCodeFrame()`) before pushing the new one, *if* `op_EVAL` is the last instruction in the buffer.

However, if `op_EVAL` encounters an error (e.g., empty stack, missing argument) *before* pushing the new frame, it returns early. 
In the `execute` loop, the `skip_ip_write` flag (calculated based on it being the last instruction) prevents the Instruction Pointer (IP) from being advanced/saved.
Consequently, on the next cycle, the VM reads the *same* `op_EVAL` instruction again, leading to an infinite loop (Hang).

## Fix
Modified `EdictVM::op_EVAL` to explicitly save the current instruction pointer (`writeFrameIp`) if it aborts execution (returns early) when `tail_eval` logic was active.

```cpp
void EdictVM::op_EVAL() {
    auto v = popData(); 
    if (!v || !v->getData()) { 
        tail_eval = false;
        // FIX: Manually persist IP progress if aborting tail-call candidate
        writeFrameIp(peek(VMRES_CODE), static_cast<int>(instruction_ptr)); 
        return; 
    }
    // ...
}
```

## Verification
- **Test**: `repro_hang.cpp` (`ReproHangTest.AssignEvalLoop`).
- **Scenario**: `PUSH 1`, `PUSH "test"`, `ASSIGN` (consumes stack), `EVAL` (aborts due to empty stack).
- **Result**: Test passes (terminates), confirming the fix.
