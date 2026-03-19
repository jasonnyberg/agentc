# Procedure: VM Infinite Loop Debugging

**ID**: LOCAL:P001
**Category**: Debugging
**Context**: VM execution hangs/timeouts in test suites.

## Context
When the VM enters an infinite loop during a test suite (e.g., `RegressionMatrixTest`), standard test output is often insufficient because the process is killed by timeout, losing buffered logs.

## Technique: Iterative Isolation & Instruction Tracing

### 1. Symptom Identification
- **Observation**: `ctest` fails with Timeout (Exit Code 124).
- **Initial Log**: Run with `-V` to see the last successful test.
  ```bash
  ctest -R <TestSuite> -V
  ```

### 2. Test Isolation
- **Goal**: Reduce the failing scope to a single opcode or pair.
- **Action**: Modify the test source (e.g., `regression_matrix.cpp`) to run *only* the suspected index or range.
  ```cpp
  // Force specific pair causing the hang
  int i = 7; // VMOP_ASSIGN
  int j = 9; // VMOP_EVAL
  ```
- **Benefit**: dramatically speeds up reproduction (seconds vs minutes).

### 3. In-VM Tracing
- **Action**: Add high-verbosity logging directly to the VM's inner loop (`execute`) and suspected opcodes.
  ```cpp
  // edict_vm.cpp : execute()
  std::cerr << "IP: " << instruction_ptr << " OP: " << (int)op << std::endl;
  ```
- **Critical Detail**: Use `std::cerr` (unbuffered) instead of `std::cout` to ensure output appears before the process is killed.

### 4. Root Cause Analysis
- **Pattern**: Analyze the trace.
  - **Stationary IP**: If `IP` prints the same value repeatedly for the same `OP`, the instruction pointer is not advancing.
  - **State Loop**: If `IP` cycles (0 -> 1 -> 0), logic flow is resetting.
- **Example**: `op_EVAL` returning early without advancing IP because it was treated as a tail-call (popped frame) but didn't actually push a new frame.

### 5. Verification
- **Action**: Create a targeted regression test file (e.g., `repro_hang.cpp`) containing **only** the minimal reproduction case.
- **Commit**: Add this test to the suite permanently to prevent regression.
