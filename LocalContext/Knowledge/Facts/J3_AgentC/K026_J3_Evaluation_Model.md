# Knowledge: J3 Evaluation Model

**ID**: LOCAL:K026
**Category**: [J3 AgentC](index.md)
**Tags**: #j3, #evaluation, #design, #ffi

## explicit Evaluation Principle
A core tenet of AgentC (J3) is **Explicit Evaluation**. 

### The Rule
**"Reference vs. Evaluation is never ambiguous."**

1.  **Values are Passive**: Pushing an item to the stack (whether it be an integer, a list, a string, or a *function*) simply places that value on the stack.
2.  **No Implicit Execution**: The VM **never** automatically executes a value just because of its type.
    - An integer `42` pushed to the stack is just `42`.
    - A function `add` pushed to the stack is just the *function object* `add`.
3.  **Explicit Action**: Execution only happens when an explicit opcode (like `EVAL`, `FUN_EVAL`, or `!`) is invoked.

### Application to FFI (Foreign Functions)
This principle extends strictly to Foreign Function Interface (FFI) objects ("Thunks").

- **Creation**: When a C function (e.g., `add`) is imported or bound, it exists in the system as a **Foreign Function Value**.
- **Usage**:
    - **Naming**: It can be assigned to a variable: `"my_add" @` (where stack has the thunk).
    - **Passing**: It can be passed to other functions as an argument.
    - **Invoking**: It is only executed when `EVAL` is called on it.
    
**Example:**
```edict
"libmath.so" load-lib
"add" lookup-symbol   ( Stack: <ForeignFunc:add> )
"plus" @              ( Stack: empty. 'plus' is now bound to the thunk )

10 20 "plus" $        ( Stack: 10 20 <ForeignFunc:add> )
!                     ( Stack: 30 )
```

### Benefits
- **Zero Ambiguity**: The programmer never has to guess if a word will execute or just push itself.
- **Higher-Order Simplicity**: Functions are treated exactly like data. No special "quote" syntax is needed to pass a function as an argument.
- **VM Simplicity**: The inner loop does not need type-dispatch logic for every push operation.
