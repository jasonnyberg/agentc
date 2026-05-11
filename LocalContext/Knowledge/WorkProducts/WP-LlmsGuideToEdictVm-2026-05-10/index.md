# LLM's Guide to Edict and the VM

**Purpose**: give a coding agent the minimum accurate mental model needed to read, write, and debug Edict without re-deriving VM behavior from scratch.

## 1. Core Model
Edict is a tiny concatenative language.

- Source is a flat sequence of terms.
- Terms compile into bytecode in `edict/edict_compiler.cpp`.
- Bytecode executes in `edict/edict_vm.cpp`.
- Values live in Listree nodes, so strings, dicts, lists, bytecode payloads, and bindings all share one substrate.

The most important habit is: think in terms of stack effects, not expression trees.

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
  - Example: `[x]@a [y]/@a` means assign `x` to `a`, then for `a` pop the current binding head and assign `y`.
  - Multi-sigil chains are generalized: `[x]@a [y]@a [z]@a //a` pops `z`, then `y`, leaving `a == x`.
  - Intermediate `/` in a chain uses non-cleaning `REMOVE_HEAD` so the live dictionary entry survives until the chain finishes; a terminal `/name` keeps legacy cleanup semantics.
- Bare `/`
  - Compiles to `POP`.
- `!`
  - Compiles to `EVAL`.
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
The actual truthiness rules are in `edict/edict_vm.cpp::isTrue(...)`.

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
`<` and `>` push/pop dynamic scope.

- `ctx < ... >` makes `ctx` the innermost dictionary scope.
- Identifier lookup inside the block can resolve against that pushed object.

Important current VM behavior from `op_CTX_POP()`:

- The scope's data-frame results are merged back to the parent stack.
- The pushed context object is also returned to the parent stack.

Practical consequence:

- Many useful patterns need a trailing `pop` to discard the returned context object.

Example dynamic lookup pattern:

```edict
catalog < preset_name ! > pop
```

This is the current clean way to resolve a named preset object from a dict catalog when direct string equality is inconvenient or unavailable.

## 10. Mutating Object Methods: The Current Reliable Pattern
If you need to mutate an object in place, do not rely on isolated-call syntax alone.

Reliable pattern:

```edict
provider < [hello] request ! > pop /
```

Why it works:

- `provider < ... >` makes `provider` the current scope.
- `[hello]` pushes the prompt onto the data stack.
- `request !` evaluates the request thunk in the provider's context, not a fresh method-local context.
- `pop` discards the context object returned by `CTX_POP`.
- `/` discards the explicit success sentinel if you do not need it.

Inside a mutating request thunk, avoid temporary named locals on the provider unless you actually want persistent fields.

When a mutating thunk needs helper words that themselves bind locals (for example request-building or runtime-call wrappers), prefer isolated helper invocation such as `helper(arg1 arg2)` inside the mutating thunk. Raw `helper !` inside owner context can leak helper-local assignments onto the owner object and corrupt stable fields.

Prefer stack-passing style:

```edict
[dup @last pop [ok]]
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
provider < [What is the capital of France?] request ! > pop /
provider.assistant_text print
```

### Pattern: Provider-driven chat loop

```edict
llm.init([local-qwen]) @provider
provider < repl ! > pop /
```

Notes:

- `provider.repl` is currently intended for launcher-backed no-arg `./edict.sh` sessions where the underlying process is a real line-oriented `EdictREPL`.
- The loop reads stdin through `agentc_read_line_status !`, skips blank lines, and exits cleanly on EOF.

### Pattern: Provider-scoped tools

G079 adds a first narrow tool surface through `agentc_tools` and `provider.tools`:

```edict
llm.init([local-qwen]) @provider
provider < [/tmp/note.txt] [hello] tools.write_file ! @last_tool > pop /
provider < [/tmp/note.txt] tools.read_file ! @last_tool > pop /
provider.last_tool.content print
```

Available wrappers:

- `agentc_file_read !` / `tools.read_file` — `( path -- result )`
- `agentc_file_write !` / `tools.write_file` — `( path content -- result )`
- `agentc_file_replace !` / `tools.replace_file` — `( path old_text new_text -- result )`, exact replacement must match once
- `agentc_shell !` / `tools.shell` — `( command -- result )`

Each result uses an explicit `ok` list sentinel (`["ok"]` or `[]`), so branch on `result.ok`, not the whole result object.

### Pattern: Success/failure control flow

```edict
provider < [prompt text] request ! > pop &
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
llm.catalog ! @catalog
[local-qwen] @preset_name
catalog < preset_name ! > pop
```

### Pattern: Head-binding replacement when needed

```edict
new_value /@provider
```

This is a concatenated prefix-sigil chain. It pops the current head value of `provider` without cleaning up the live dictionary entry, then assigns `new_value` to `provider`.

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
- `CTX_POP` returns the context object back onto the stack; callers often need `pop`.
- Isolated calls are great for local computation, but they are the wrong default tool for mutating owner objects.
- Rebinding with `@name` adds a new history head; it does not erase prior values.

## 15. Working Heuristic
When designing Edict code, ask three questions:

1. What is on the data stack right now?
2. What is the current innermost dictionary scope?
3. Am I creating a fresh frame (`(...)`) or mutating the current context (`< ... >` + `!`)?

If those three answers are clear, most Edict behavior becomes predictable.
