# LLM's Guide to Edict and the VM

**Purpose**: give a coding agent the minimum accurate mental model needed to read, write, and debug Edict without re-deriving VM behavior from scratch.

## 1. Core Model
Edict is a tiny concatenative language.

- Source is a flat sequence of terms.
- Terms compile into bytecode in `edict/edict_compiler.cpp`.
- Bytecode executes in `edict/edict_vm_core.cpp` (dispatch loop and op handlers) and `edict/edict_vm_bootstrap.cpp` (builtin registration).
- Values live in Listree nodes, so strings, dicts, lists, bytecode payloads, and bindings all share one substrate.

The most important habit is: think in terms of stack effects, not expression trees.

Style note: write eval adjacent to the word being evaluated (`word!`, `module.word!`, `thunk!!`). The VM still accepts `!` as a standalone token for compatibility, but adjacent spelling is the preferred form for new examples and scripts.

## 2. What Terms Compile To
The compiler is very direct.

- Literal block `[hello world]`
  - Compiles to `PUSHEXT` of the literal string `hello world`.
  - It does not execute until `!`.
- Quote literal `'word`
  - Compiles to `PUSHEXT` of `word`.
- Identifier `name`
  - Compiles to `PUSHEXT("name")` then `REF`.
  - That means plain identifiers always attempt dictionary lookup.
- No-space assignment `@name`
  - Compiler emits `PUSHEXT("name")` then `ASSIGN`.
  - This consumes a value from the stack and assigns it to `name`.
- Bare assignment `@`
  - Compiler emits `SWAP` then `ASSIGN`.
  - Stack order for bare `@` is `key value @` in source, but the compiler swaps to match `ASSIGN`.
- No-space remove `/name`
  - Compiles as push `name` then `REMOVE`.
- Concatenated no-space prefix sigils such as `/@name`
  - Compile as a single prefix chain over the same identifier, with sigils applied left-to-right.
  - Example: `[x]@a [y]/@a` means assign `x` to `a`, then for `a` remove the current binding head and assign `y`.
  - Multi-sigil chains are generalized: `[x]@a [y]@a [z]@a //a` pops `z`, then `y`, leaving `a == x`.
  - Intermediate `/` in a chain uses non-cleaning `REMOVE_HEAD` so the live dictionary entry survives until the chain finishes; a terminal `/name` keeps legacy cleanup semantics.
- Bare `/` (a / with a space before the following token)
  - Compiles to `POP`.
- `yield!`
  - Compiles to `VMOP_YIELD`.
  - Sets `VM_YIELD` and pauses execution. Does not modify the stack.
  - The VM resumes at the next opcode after `vm.resume()` is called.
  - Typical usage: `yield! @destination destination` — the `@destination destination` part executes after resume, storing any data pushed by the resume caller.
- `!`
  - Compiles to `EVAL`.
- `await!`
  - Compiles to `VMOP_AWAIT`.
  - Pops a waitable ID string from the data stack (e.g. `"5"` or `job.waitable` field from `intern_start!`).
  - If the stack is empty, defaults to the coordinator participant set by `vm.setAwaitScheduler()`.
  - Calls `scheduler.parkVm(participant, vm)`, which stores a resume callback that pushes mailbox events onto the data stack and calls `vm.resume()`.
  - Sets `VM_YIELD` so execution returns to the caller (REPL, script runner, etc.).
  - On the next scheduler pump, if a descriptor has arrived for that participant, the scheduler calls the callback: events are pushed to the stack and the VM resumes at the next opcode.
  - Usage with an explicit waitable:
    ```edict
    {"program": "...", "task_id": "t1"} intern_start! @job
    job.waitable await! @events events
    ```
  - Usage with the coordinator default:
    ```
    await! @events events
    ```
- `&`
  - Compiles to `THROW`.
- `|`
  - Compiles to `CATCH`.
- `<` and `>`
  - Compile to `CTX_PUSH` and `CTX_POP`.
- `(` and `)` or isolated call syntax `f([x])`
  - Compile to `FUN_PUSH` / `FUN_EVAL` / `FUN_POP` machinery.

## 3. VM Resource Stacks
The VM has multiple internal stacks. The important ones are:

- Data stack: normal operands/results.
- Dictionary stack: scope chain for lookup and assignment.
- Function stack: pending isolated-call thunks.
- State stack: failure state used by `test`, `fail`, `&`, `|`.
- Code stack: bytecode frames / call stack.

When debugging, most surprises come from forgetting whether a form creates a new data frame, a new dict frame, or both.

## 4. Lookup and Assignment Semantics
Lookup mode is now configurable.

- `name` tries `REF` through the dictionary stack.
- In default `lax` mode, unresolved lookup pushes the original string `name`.
- `strict!` and `strict_null!` switch unresolved lookup to push `null` instead.
- `strict_fail!` switches unresolved lookup to push `null` and also enter failure state with an explicit message like `unresolved symbol: name`.
- `lax!` restores the original soft-fallback behavior.

Practical guidance:

- Use `lax!` when you intentionally want symbolic fallback for metaprogramming or catalog-driven evaluation.
- Use `strict!` / `strict_null!` when debugging or when missing data should stay data-shaped.
- Use `strict_fail!` when you want unresolved dereference to integrate directly with `& |` control flow.

Assignment is history-preserving, not destructive.

- `Cursor::assign(...)` adds the new value at the head of the item's value history.
- A later lookup sees the newest head value.
- Older values still exist in history until explicitly removed.

Practical implications:

- `x` usually means "the newest/head value bound to `x`".
- `-x` means "the oldest/tail value bound to `x`".
- `f() @x` does not erase older `x`; it adds a new head binding.
- `f() @-x` appends at the tail of `x`'s binding history.
- `/-x` removes the tail binding; `/x` removes the head binding.
- If you want true head-binding replacement, use a concatenated remove-then-assign prefix chain such as `f() /@x`. The `/` and `@` both apply to `x` in order.
- If you want tail-binding replacement, use `f() /@-x`.

Examples:

```edict
[x]@a [y]@a [z]@a -a print   -- prints x
[x]@a [y]@a [z]@a /-a -a     -- tail remove leaves y at the tail
[x]@a [y]@a [z]@a [t]/@-a -a -- tail replace leaves t at the tail
```

## 5. Truthiness: Use the VM, Not the Old Prose
The actual truthiness rules are in `edict/edict_vm_core.cpp::isTrue(...)`.

Reliable truths:

- `null` is false.
- Empty list is false.
- Non-empty list is true.
- Non-empty strings are true.
- The string `false` is still true because it is non-empty.

Important current VM behavior:

- Tree/dict objects are currently falsy under `test` unless they carry direct binary/string payload data.
- This differs from the older human-oriented reference, which described objects as truthy.

Agent rule:

- Do not branch on raw objects.
- Do not branch on string booleans like `"true"` / `"false"`.
- Prefer explicit scalar sentinels, or use `fail` to enter failure state deliberately.

## 6. `test`, `fail`, `&`, `|`
This is the control-flow backbone.

- `test`
  - Pops a value.
  - If it is falsy, pushes that value into VM state.
- `fail`
  - Pops a value and unconditionally pushes it into VM state.
- `&`
  - If state is present, enters scan mode and skips the then-branch until `|`.
- `|`
  - Skips the else-branch when execution reached it normally.

Mental model:

```edict
maybe_value test &
  [then branch]
|
  [else branch]
```

Design guidance:

- Use `fail` for semantic failure.
- Return a simple success sentinel like `[ok]` on success.
- Then write `request() & success-path | failure-path`.
- `strict_fail!` is now a good fit when a missing lookup should behave like a control-flow failure instead of symbolic fallback.

## 7. `!` Evaluation Dispatch Order
`EVAL` does not just execute source strings. It dispatches by runtime type.

`op_EVAL()` checks, in order:

1. iterator dispatch
2. thunk dispatch
3. FFI dispatch
4. source-string compilation and execution

This matters because some values that look like "data" may actually be executable thunks or FFI closures.

## 8. Isolated Calls
Isolated call syntax like `f([hello])` creates a fresh function/data/dict frame.

Properties:

- Arguments are evaluated into the isolated frame.
- Local assignments stay local.
- Results merge back to the parent data stack at `FUN_POP`.
- The isolated frame does **not** directly mutate sibling fields on an owner object through plain `@field` assignment.

This is the source of a major design trap:

- `provider.request([prompt])` looks method-like.
- But the request body runs inside a fresh function frame.
- So plain `@last_response` or `@assistant_text` will hit the isolated frame, not necessarily the owner object.

## 9. Context Scopes
`<` and `>` enter and leave dynamic scope.

- `ctx < ... >` makes `ctx` the innermost dictionary scope.
- Identifier lookup inside the block can resolve against that pushed object.

Important current VM behavior from `op_CTX_POP()`:

- The scope's data-frame results are merged back to the parent stack.
- The pushed context object is also returned to the parent stack.

Practical consequence:

- Many useful patterns need a trailing bare `/` to discard the returned context object.

Example dynamic lookup pattern:

```edict
catalog < preset_name! > /
```

This is the current clean way to resolve a named preset object from a dict catalog when direct string equality is inconvenient or unavailable.

## 10. Mutating Object Methods: The Current Reliable Pattern
If you need to mutate an object in place, do not rely on isolated-call syntax alone.

Reliable pattern:

```edict
provider < [hello] request! > / /
```

Why it works:

- `provider < ... >` makes `provider` the current scope.
- `[hello]` pushes the prompt onto the data stack.
- `request!` evaluates the request thunk in the provider's context, not a fresh method-local context.
- The first `/` discards the context object returned by `CTX_POP`.
- The second `/` discards the explicit success sentinel if you do not need it.

Inside a mutating request thunk, avoid temporary named locals on the provider unless you actually want persistent fields.

When a mutating thunk needs helper words that themselves bind locals (for example request-building or runtime-call wrappers), prefer isolated helper invocation such as `helper(arg1 arg2)` inside the mutating thunk. Raw `helper!` inside owner context can leak helper-local assignments onto the owner object and corrupt stable fields.

Prefer stack-passing style:

```edict
[dup @last / [ok]]
```

That duplicates the prompt, assigns one copy to `last`, drops the extra prompt, and returns `[ok]`.

## 11. JSON / Dict Literal Construction
JSON object literals compile through `compileJSONObject()`.

- Compiler emits a new dict value.
- Opens a context with `CTX_PUSH`.
- Compiles each JSON value.
- Emits key then `ASSIGN` for each property.
- Closes with `CTX_POP`.

This means JSON objects are not magical syntax sugar at runtime; they are ordinary context-building and assignment bytecode.

## 12. Speculation
`speculate [code]` is the safe probe tool.

- Runs in an isolated checkpoint/rollback execution.
- Side effects do not commit to the caller.
- Returns top result on success, null on failure.

Use it for:

- probing whether a path exists
- trying risky lookup or transformation logic
- validating a form before mutating durable state

## 13. Quick-Start Patterns for Agents
### Pattern: Bind once, mutate in place

```edict
llm.init([local-qwen]) @provider
provider < [What is the capital of France?] request! > / /
provider.assistant_text print
```

### Pattern: Provider-driven chat loop

```edict
llm.init([local-qwen]) @provider
provider < repl! > / /
```

Notes:

- `provider.repl` is currently intended for launcher-backed no-arg `./edict.sh` sessions where the underlying process is a real line-oriented `EdictREPL`.
- The loop reads stdin through `agentc_read_line_status!`, skips blank lines, and exits cleanly on EOF.
- Context-management slash commands are Edict-owned: `/reset` and `/clear` call `provider.context_reset!`; `/context` and `/inspect` call `provider.context_inspect!` and print a JSON summary.

### Pattern: Provider context management

```edict
llm.init([local-qwen]) @provider
provider < [first prompt] request! > / /
provider < context_inspect! > / @summary
summary.messages to_json! print
provider < context_reset! > / /
```

`context_reset!` mutates the stable provider object in place, clears conversation messages, resets the assistant text, and preserves the system prompt. The REPL command path uses raw string equality through a generic stdlib helper; it does not eval arbitrary user chat text as Edict.

### Pattern: Deterministic intern worker task (synchronous)

G091 adds the first substrate-level intern-worker primitive, `intern_run!`. The coordinator passes a bounded task envelope; Edict freezes `context` and `imports`, snapshots `input`, runs the `program` in a fresh worker VM with private `workspace`, joins the worker, and returns a structured result on the coordinator stack.

```edict
{"program": "1 2 + @result", "task_id": "t1"} intern_run! @worker_result
worker_result.result to_json! print
```

Worker programs should assign a JSON-serializable `result` object. Keep intern tasks bounded, explicit, and checkable; do not pass stateful provider handles through worker context/imports in the MVP.

### Pattern: Yield and resume

`yield!` is the low-level pause instruction. It sets `VM_YIELD` and returns control to the caller (REPL, script runner). The VM can later be resumed via `vm.resume()`.

Unlike `await!`, `yield!` does **not** register with the Root1 await scheduler. It is a purely mechanical pause — the caller must arrange for the VM to be resumed externally. Use `yield!` when you need fine-grained control over when and how the VM continues.

```edict
'before yield! 'after @events events
```

Execution: `'before` pushes the string `"before"` onto the stack, then `yield!` pauses. After `vm.resume()` is called, execution continues: `'after` pushes `"after"`, `@events` stores it in `events`, `events` pushes it back onto the stack.

### Pattern: Async intern worker with await!

`intern_start!` launches a worker asynchronously and returns an envelope with fields including `waitable`, an opaque participant ID string. Pass `job.waitable` to `await!` to park the VM on that specific worker's mailbox:

```edict
{"program": "1 2 + @result", "task_id": "my_task"} intern_start! @job
job.waitable await! @events events
```

`await!` pops the waitable string from the stack (here coming from `job.waitable`), parks the VM on that participant, and yields. On the next REPL turn the scheduler pump delivers the worker's completion descriptor, pushes the mailbox events list onto the stack, and resumes the VM at `@events`. The `events` binding now holds the delivered mailbox events; field access follows standard Edict dot-path resolution through the cursor.

To await the coordinator's default mailbox instead of a specific worker's:

```edict
await! @events events
```

### Pattern: Concurrent workers with independent awaits

Each `intern_start!` creates a separate participant mailbox. By passing each job's `waitable` to its own `await!`, the waits are independent and do not wake each other:

```edict
{"program": "...", "task_id": "ta"} intern_start! @job_a
{"program": "...", "task_id": "tb"} intern_start! @job_b

job_a.waitable await! @events_a events_a

job_b.waitable await! @events_b events_b
```

### Pattern: Provider streaming

G074 adds a first decoupled ghost-queue stream surface:

```edict
llm.init([gemma-4-31b-it]) @provider
provider < [Reply exactly ok] stream_start! > / /
# later: after yielding or doing other work:
provider < stream_sync! > / /
provider.stream_last.tokens print
```

Important details:

- `stream_start` creates `provider.stream_runtime` and `provider.stream_id`, then returns immediately after launching a detached native provider worker.
- The background worker only touches C++ queue state, never Listree/VM memory.
- `stream_sync` drains pending queue data on the VM/main thread into `provider.stream_last`, `provider.assistant_text`, and `provider.stream_complete`.
- The low-cost Google preset aliases are `gemma-4-31b-it` and `google-gemma-4-31b-it`.

### Pattern: Provider-scoped tools

G079 adds a first narrow tool surface through `agentc_tools` and `provider.tools`:

```edict
llm.init([local-qwen]) @provider
provider < [/tmp/note.txt] [hello] tools.write_file! @last_tool > / /
provider < [/tmp/note.txt] tools.read_file! @last_tool > / /
provider.last_tool.content print
```

Available wrappers:

- `agentc_file_read!` / `tools.read_file` — `( path -- result )`
- `agentc_file_write!` / `tools.write_file` — `( path content -- result )`
- `agentc_file_replace!` / `tools.replace_file` — `( path old_text new_text -- result )`, exact replacement must match once
- `agentc_shell!` / `tools.shell` — `( command -- result )`

Each result uses an explicit `ok` list sentinel (`["ok"]` or `[]`), so branch on `result.ok`, not the whole result object.

### Pattern: Success/failure control flow

```edict
provider < [prompt text] request! > / &
  provider.assistant_text print
|
  provider.last_error.message print
```

### Pattern: Strict lookup while debugging

```edict
strict!
maybe_missing_path print
lax!
```

### Pattern: Missing lookup as branch failure

```edict
strict_fail!
provider.last_response.error.message &
  [have error text] print
|
  [no error text] print
lax!
```

### Pattern: Dynamic preset lookup from a catalog

```edict
llm.catalog! @catalog
[local-qwen] @preset_name
catalog < preset_name! > /
```

### Pattern: Head-binding replacement when needed

```edict
new_value /@provider
```

This is a concatenated prefix-sigil chain. It removes the current head value of `provider` without cleaning up the live dictionary entry, then assigns `new_value` to `provider`.

### Pattern: Avoid object-truthiness bugs

Bad:

```edict
provider test & [ok] | [fail]
```

Good:

```edict
[ok] test & [ok path] | [fail path]
```

or:

```edict
[reason] fail & [unreachable] | [error path]
```

## 14. Known Sharp Edges
- Missing symbol lookup returns the symbol string.
- String booleans are truthy if non-empty.
- Dict/object truthiness is currently not reliable for branching.
- `CTX_POP` returns the context object back onto the stack; callers often need bare `/` to discard it.
- Isolated calls are great for local computation, but they are the wrong default tool for mutating owner objects.
- Rebinding with `@name` adds a new history head; it does not erase prior values.

## 15. Working Heuristic
When designing Edict code, ask three questions:

1. What is on the data stack right now?
2. What is the current innermost dictionary scope?
3. Am I creating a fresh frame (`(...)`) or mutating the current context (`< ... >` + `!`)?

If those three answers are clear, most Edict behavior becomes predictable.
