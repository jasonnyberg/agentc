# Edict Language Reference & Examples

> A ground-up walkthrough of every feature in the edict stack-based language,
> with runnable examples drawn from the regression test suite and codebase.

---

## Table of Contents

1. [What Is Edict?](#1-what-is-edict)
2. [Running Edict](#2-running-edict)
3. [The Stack — Core Mental Model](#3-the-stack--core-mental-model)
4. [Data Types & Literals](#4-data-types--literals)
   - 4.1 Strings (numbers are strings too)
   - 4.2 Booleans
   - 4.3 Quoted Code Blocks
   - 4.4 JSON Object Literals
   - 4.5 JSON Array Literals
5. [Stack Manipulation](#5-stack-manipulation)
6. [Variables — Assignment and Lookup](#6-variables--assignment-and-lookup)
7. [Evaluation — The `!` Operator](#7-evaluation--the--operator)
8. [Quoted Code Blocks](#8-quoted-code-blocks)
9. [Conditional Control Flow](#9-conditional-control-flow)
10. [Isolated Function Calls](#10-isolated-function-calls)
11. [Context Scopes](#11-context-scopes)
12. [The Splice Operator `^`](#12-the-splice-operator-)
13. [Dictionary / JSON Object Literals](#13-dictionary--json-object-literals)
14. [Speculative Execution](#14-speculative-execution)
15. [Rewrite Rules](#15-rewrite-rules)
16. [Logic Programming (miniKanren)](#16-logic-programming-minikanren)
17. [Transactions](#17-transactions)
18. [Cursor Navigation](#18-cursor-navigation)
19. [FFI — Native Library Integration](#19-ffi--native-library-integration)
20. [FFI Closures — Passing Edict as a C Callback](#20-ffi-closures--passing-edict-as-a-c-callback)
21. [Unsafe Extensions Policy](#21-unsafe-extensions-policy)
22. [Output](#22-output)
23. [VM Resource Model (Advanced)](#23-vm-resource-model-advanced)
24. [Opcode Quick Reference](#24-opcode-quick-reference)

---

## 1. What Is Edict?

Edict is a **stack-based, concatenative language** — in the same family as Forth and Factor. Programs are sequences of tokens; each token either pushes a value onto the data stack or performs an operation that consumes and produces stack values. There are no expressions in the traditional sense: you build up values on the stack and then operate on them.

The VM is built on top of **Listree**, a persistent arena-allocated tree/list graph. All runtime values — strings, booleans, lists, dictionaries, compiled code — are Listree nodes. This gives the VM structural sharing, O(1) checkpoint/rollback, and a unified data model for all types.

The full pipeline is:

```
Source text
  → Tokenizer
  → EdictCompiler (produces BytecodeBuffer)
  → EdictVM.execute()
```

---

## 2. Running Edict

### REPL

```sh
./build/edict/edict
```

Special REPL commands:

| Command | Effect |
|---------|--------|
| `exit` / `quit` | Exit the REPL |
| `help` | Print syntax reference |
| `clear` | Reset the VM |
| `stack` | Dump all stack items (top first) |

### One-liner with `-e`

```sh
./build/edict/edict -e '"hello" "world" print'
```

Output format:

```
stack size: 2 (top first)
0: world
1: hello
```

---

## 3. The Stack — Core Mental Model

Edict has **six internal resource stacks** but most programs only interact with the **data stack** (index 1, `VMRES_STACK`). Think of it as a vertical list; new items go on top, operations consume from the top.

```
Push 1:     [ 1 ]
Push 2:     [ 2, 1 ]
Push 3:     [ 3, 2, 1 ]
```

The stack grows upward; "top" is the most recently pushed item.

### The six resource stacks

| Index | Name | Role |
|-------|------|------|
| 0 | Dictionary (`VMRES_DICT`) | Scoped variable lookup chain |
| 1 | Data (`VMRES_STACK`) | Operand / result stack (what you interact with) |
| 2 | Function (`VMRES_FUNC`) | Pending function thunks for isolated calls |
| 3 | Exception (`VMRES_EXCP`) | Error/condition values for `&`/`\|` flow |
| 4 | Code (`VMRES_CODE`) | Bytecode frame stack (call stack) |
| 5 | State (`VMRES_STATE`) | Condition flags for test/fail/throw/catch |

---

## 4. Data Types & Literals

### 4.1 Strings

```edict
"hello"           -- quoted string literal, pushed onto the stack
"hello \"world\"" -- escape sequences supported
```

Numbers in edict source are **strings**. There are no native integer or double literal types in the VM. Numeric-looking tokens like `42` or `3.14` are pushed as strings:

```edict
42        -- pushes the string "42"
-7        -- pushes the string "-7"
3.14      -- pushes the string "3.14"
```

Arithmetic and numeric computation are performed by **native (FFI) functions**, not by edict opcodes. The VM passes string arguments to native functions; the FFI layer converts them to C integers or doubles at the call boundary.

### 4.2 Booleans

The language represents booleans as truthy/falsy values; there is no separate bool literal type. Truthiness rules:

| Value | Truthiness |
|-------|-----------|
| Null | false |
| Empty list `[]` | false |
| Empty string `""` | false |
| Any non-empty string | true |
| Any list with items | true |
| Any dict/object | true |

Note: the string `"0"` is truthy (it is non-empty). Only the null value, empty list, and empty string are falsy.

### 4.3 Quoted Code Blocks (Thunks)

Square brackets push their content as a **literal string**, not as code. The content is evaluated later with `!`.

```edict
[hello world]       -- pushes the string "hello world" (not executed)
[1 2 +]             -- pushes the string "1 2 +" (not executed yet)
```

### 4.4 JSON Object Literals (Dictionaries)

```edict
{ "name": "alice", "age": "30" }   -- pushes a dictionary value
{ "x": "1", "y": "2" }             -- multi-key dict
```

### 4.5 JSON Array Literals (Lists)

Arrays are only valid inside JSON object literals:

```edict
{ "items": [1, 2, 3] }
{ "tags": ["a", "b", "c"] }
```

---

## 5. Stack Manipulation

### `dup` — Duplicate the top

```edict
"hello" dup
-- stack: [ "hello", "hello" ]
```

### `swap` — Swap top two items

```edict
"a" "b" swap
-- stack: [ "a", "b" ]   (was [ "b", "a" ])
```

### `pop` — Discard the top

```edict
"a" "b" pop
-- stack: [ "a" ]
```

### `/` (bare) — Also discards the top

```edict
"a" "b" /
-- stack: [ "a" ]
```

---

## 6. Variables — Assignment and Lookup

### Assignment: `@name`

```edict
"alice" @name
-- assigns string "alice" to variable "name"
-- stack is now empty (value was consumed)
```

### Lookup: plain identifier

```edict
name
-- looks up "name" in the dictionary chain
-- pushes its value: "alice"
```

**Soft failure:** If a symbol is not found, it pushes its own name as a string — there is no error:

```edict
not_defined
-- stack: [ "not_defined" ]
```

### Removing a variable: `/name`

```edict
"hello" @x
/x
x       -- falls back to pushing "x" (the string)
-- stack: [ "x" ]
```

### Dotted paths

```edict
{ "a": { "b": "42" } } @obj
obj
-- "obj.a.b" resolves via cursor's dotted-path navigation
```

### Assigning with the stack form

```edict
"value"  "key"  @
-- equivalent to: "value" @key
```

---

## 7. Evaluation — The `!` Operator

`!` is the core execution primitive. It pops the top of the data stack and executes it.

**Four dispatch phases (in order):**

1. **Iterator fan-out**: if the value is a cursor (iterator), dispatch once per position
2. **Binary thunk**: if the value is a compiled code frame (built-in or lambda), execute it
3. **FFI function**: if the value is an imported native function dict, call it via FFI
4. **Source string**: if the value is a string (including `[bracketed]` thunks), compile and execute it

```edict
["hello" "world"] !    -- compile and run "hello world"; stack: [ "world", "hello" ]
dup !                  -- dup is a built-in thunk; stack: [ <top>, <top> ]
```

### Tail-call optimization

If `!` is the very last instruction in the current code frame, the current frame is popped before pushing the new one — equivalent to tail-call elimination.

---

## 8. Quoted Code Blocks

Brackets `[...]` push their content as a string without executing it. This is how you pass code as a value (a "thunk").

```edict
[print] @printer
"hello" printer !
-- prints "hello"
```

```edict
[dup] @twice
"foo" twice !
-- stack: [ "foo", "foo" ]
```

### Nested evaluation

```edict
[[hello] !] !
-- outer ! executes "[hello] !", which runs "hello" (a lookup)
-- stack: [ "hello" ]
```

### Storing and calling later

```edict
["do some work"] @task
-- ... later ...
task !
```

---

## 9. Conditional Control Flow

Edict uses a **state-stack pattern** for conditionals, not bytecode jumps. The idiom is:

```
<condition> test  &  <then-branch>  |  <else-branch>
```

### `test` — consume top, mark as failure if falsy

```edict
"1" test         -- truthy: state stack unchanged (continue normally)
[]  test         -- falsy:  pushes [] to STATE stack (marks failure)
```

### `&` — branch on state

If the state stack has a failure, `&` enters **scan mode** and skips all instructions until it finds the matching `|`. If state is clear, `&` is a no-op (continue into the then-branch).

### `|` — else separator / skip else-branch

When reached normally (no failure), `|` enters scan mode to skip the else-branch. When reached via scan mode (there was a failure), `|` cancels scan mode and resumes execution of the else-branch.

### `fail` — explicit failure

```edict
[oops] fail & [then] @res | [else] @res
-- fail pushes [oops] to STATE; & skips to |; else runs
-- res == "else"
```

### Full examples

```edict
-- if/then/else:
"1" test & ["yes"] @result | ["no"] @result
-- result == "yes"

-- false branch:
[] test & ["yes"] @result | ["no"] @result
-- result == "no"

-- if only (no else):
"1" test & ["truthy"] @result
-- result == "truthy"

-- if only with false condition: does nothing
[] test & ["truthy"] @result
-- result unset (resolves to "result" if accessed)

-- explicit fail:
"bad" fail & ["recovered"] @result | ["failed"] @result
-- result == "failed"
```

### Nested conditionals

```edict
"1" test & "1" test & ["inner"] @res | ["skip"] @res | ["skip"] @res
-- both pass; res == "inner"
```

Scan mode tracks nesting depth, so nested `&`/`|` pairs are handled correctly.

---

## 10. Isolated Function Calls

The syntax `f([args...])` runs `f` in an **isolated stack frame** — arguments are provided, results are merged back, but the function cannot touch the parent's stack or variables.

### Empty call

```edict
[pop] @f
"parent_item"
f()
-- f runs "pop" in an isolated frame (which starts empty)
-- pop on an empty isolated frame is a no-op inside the frame
-- parent stack still has "parent_item"
```

### Call with arguments

```edict
[!] @eval
eval(["hello"])
-- isolated frame has "hello", then ! evaluates it
-- result "hello" is merged back to parent stack
```

### Local variables stay local

```edict
[] @f
f(["hello"] @x)
-- x is defined inside f's isolated frame
x
-- parent scope doesn't have x; resolves to "x" (the string)
```

### Results merge back

```edict
[dup] @f
f(["hello"])
-- isolated frame: "hello" dup → "hello" "hello"
-- both merge back to parent
-- stack: [ "hello", "hello" ]
```

### Nested calls

```edict
[dup] @f
[f !] @g
g(["nested"])
-- g's isolated frame: "nested", then f! which dups it
-- result: "nested" "nested" merged to parent
```

---

## 11. Context Scopes

`<` / `>` (or `{` / `}` in non-JSON position) push and pop a **scope frame**. Items produced inside the scope are collected and returned as the scope's result.

```edict
<
  "local" @x
  x
>
-- scope produces "local" on the parent stack
-- x is not visible outside
```

This is the basis for creating temporary namespaces and collecting results.

---

## 12. The Splice Operator `^`

`^` (caret) **splices** the current stack frame's contents into a named list node, then pushes that node.

```edict
[] @collector
[^collector^] @capture

capture(1 2 3)
-- inside capture's isolated frame: items are 1, 2, 3
-- ^collector^ splices them into the list "collector"
-- collector.isListMode() == true and contains [1, 2, 3]
collector
```

The pattern `^name^` is: push `"name"`, then `SPLICE`. The splice op takes all items from the current data frame and appends them into the named node.

### Bare `^`

```edict
^ -- splices current frame into the top-of-stack node (no name lookup)
```

---

## 13. Dictionary / JSON Object Literals

JSON syntax creates dictionary (tree-mode) Listree nodes:

```edict
{ "key": "value" }
-- stack: [ <dict: key="value"> ]

{ "x": "1", "y": "2" }
-- stack: [ <dict: x="1", y="2"> ]
```

You can assign them to variables and look up keys:

```edict
{ "name": "bob", "score": "99" } @player
-- player is now a dictionary node
```

### Nested objects

```edict
{ "outer": { "inner": "42" } }
```

### Arrays inside objects

```edict
{ "tags": ["alpha", "beta", "gamma"] }
```

### Dictionary as a dynamic scope (CTX_PUSH / CTX_POP)

When you use `<dict>`, that dict becomes the current innermost scope:

```edict
{ "greeting": "hello" } <
  greeting   -- resolves to "hello" from the pushed dict scope
>
```

---

## 14. Speculative Execution

`speculate [code]` runs code in a **completely isolated snapshot** of the VM. Side effects never reach the host VM. The result (top of the speculative stack) is returned to the host on success; a Null value is returned on failure.

```edict
"baseline"
speculate ["trial"]
-- stack: [ "trial", "baseline" ]
-- "baseline" is unchanged; "trial" came from the speculation
```

```edict
"baseline"
speculate [swap]   -- swap fails on an empty speculative stack
-- stack: [ <null>, "baseline" ]
-- null indicates failure; baseline is preserved
```

### Use cases

- Try an operation that might fail; fall back if it does
- Probe a computation's result without committing
- Safe exploration of state changes

```edict
"safe"
speculate [risky_operation !]
test & [use_result !] | [use_fallback !]
```

---

## 15. Rewrite Rules

Rewrite rules define **term-rewriting transformations** over the data stack. After every instruction (in Auto mode), the VM runs up to 16 rewrite steps as a normalization pass — similar to macro expansion or algebraic simplification.

### Defining a rule

```edict
{ "pattern": ["x"], "replacement": ["w"] } rewrite_define !
-- registers: whenever "x" is on the stack, replace with "w"
```

The rule object is returned on the stack after registration (pop with `/` if not needed).

### Patterns

| Pattern token | Meaning |
|---|---|
| `"literal"` | Match that exact string |
| `"$0"`, `"$1"`, ... | Wildcard: capture the nth matched item |
| `"#atom"` | Match any non-list, non-dict value |
| `"#list"` | Match any list-mode value |

**Longest match wins.** On a tie (equal length patterns), the earlier-registered rule wins.

### Multi-token patterns

```edict
{ "pattern": ["a", "b"], "replacement": ["b", "a"] } rewrite_define !
-- "a" "b" on stack becomes "b" "a"
```

### Wildcard capture and substitution

```edict
{ "pattern": ["$1", "x"], "replacement": ["x", "$1"] } rewrite_define !
-- "tea" "x" → "x" "tea"   ($1 captures "tea")
```

### Type-aware patterns

```edict
{ "pattern": ["#atom", "x"], "replacement": ["atom-hit"] } rewrite_define !
-- any atom value followed by "x" collapses to "atom-hit"
```

### Rewrite mode

```edict
"auto"    rewrite_mode !  -- default: auto-trigger after every instruction (up to 16 steps)
"manual"  rewrite_mode !  -- only trigger when rewrite_apply ! is called
"off"     rewrite_mode !  -- never trigger
```

### Manual apply

```edict
"manual" rewrite_mode ! /
{ "pattern": ["x"], "replacement": ["hit"] } rewrite_define ! /
"x"
rewrite_apply !
-- stack: [ <trace object> ]
-- trace has: status="matched", index="0", mode="manual"
```

### Listing rules

```edict
rewrite_list !
-- returns a list of rule objects, each with: index, pattern, replacement
```

### Removing a rule

```edict
"0" rewrite_remove !
-- removes rule at index 0; returns the removed rule object
```

```edict
"9" rewrite_remove !
-- out-of-range index → VM_ERROR: "rewrite rule index out of range"
```

### Trace inspection

```edict
rewrite_trace !
-- returns the last rewrite trace object
-- has: status, reason, index, mode
```

### Preventing infinite loops

A self-referential rule (`"x" → "x"`) will not loop forever — the VM has a per-step budget (16 steps max per instruction epilogue).

### Example: algebraic normalization

```edict
{ "pattern": ["dup", "dot", "sqrt"], "replacement": ["magnitude"] } rewrite_define !
"dup" "dot" "sqrt"
-- stack: [ "magnitude" ]   (rewritten automatically)
```

---

## 16. Logic Programming (miniKanren)

Edict has an embedded miniKanren logic engine. Logic queries are expressed as JSON objects and run with `logic_run !` or the `logic { ... }` block syntax.

### Basic unification

```edict
{ "fresh": ["q"], "where": [["==", "q", "tea"]], "results": ["q"] }
logic_run !
-- stack: [ <list: ["tea"]> ]
```

### Disjunction with `conde`

```edict
{
  "fresh": ["q"],
  "conde": [
    [["==", "q", "tea"]],
    [["==", "q", "coffee"]]
  ],
  "results": ["q"]
}
logic_run !
-- stack: [ <list: ["tea", "coffee"]> ]
```

### Membership relation

```edict
{
  "fresh": ["q"],
  "where": [["membero", "q", ["tea", "cake", "jam"]]],
  "results": ["q"]
}
logic_run !
-- stack: [ <list: ["tea", "cake", "jam"]> ]
```

### Contradictory constraints → empty results

```edict
{
  "fresh": ["q"],
  "where": [
    ["==", "q", "tea"],
    ["==", "q", "coffee"]
  ],
  "results": ["q"]
}
logic_run !
-- stack: [ <empty list> ]
```

### Multi-variable results

```edict
logic {
  "fresh": ["head", "tail"],
  "where": [["conso", "head", "tail", ["tea", "cake"]]],
  "results": ["head", "tail"]
}
-- stack: [ <list: [["tea", ["cake"]]]> ]
```

### Limiting results

```edict
logic {
  "fresh": ["q"],
  "where": [["membero", "q", ["a", "b", "c", "d", "e"]]],
  "results": ["q"],
  "limit": "2"
}
-- stack: [ <list: ["a", "b"]> ]
```

### Supported relations

| Relation | Meaning |
|----------|---------|
| `["==", x, y]` | Unification |
| `["membero", x, list]` | x is a member of list |
| `["appendo", l1, l2, l3]` | l3 is l1 appended with l2 |
| `["conso", head, tail, lst]` | lst = cons(head, tail) |
| `["heado", lst, h]` | h is the head of lst |
| `["tailo", lst, t]` | t is the tail of lst |
| `["pairo", lst]` | lst is a non-empty list (a pair) |

### Logic block syntax (native keyword)

```edict
logic { "fresh": ["q"], "where": [...], "results": ["q"] }
-- equivalent to: { ... } logic_run !
-- result can be assigned:
@answers
answers    -- push the results list
```

---

## 17. Transactions

Transactions allow atomic rollback of all VM state changes: stack contents, dictionary bindings, rewrite rules, and all allocator state.

The transaction API is exposed at the C++ host level. From edict source, use `speculate` for the same effect in a safe single-value probe pattern.

### C++ API

```cpp
auto checkpoint = vm.beginTransaction();

// ... run some edict code ...
vm.execute(code);

// Roll back everything:
vm.rollbackTransaction(checkpoint);

// --- OR --- commit (no undo possible):
vm.commitTransaction(checkpoint);
```

### What is checkpointed

- All six resource stack roots (deep copies of CPtr)
- All registered rewrite rules
- VM state flags, instruction pointer, code pointer, scan state
- Slab allocator watermarks (ListreeValue, ListreeItem, ListreeValueRef, CLL, AATree, BlobAllocator)

### Nesting rules

- Transactions may be nested
- You **must** commit or rollback the innermost transaction before operating on outer ones
- `commitTransaction(outer)` while `inner` is still open returns false (error)

### Rewrite rules are also transactional

```cpp
// Register a base rule
vm.addRewriteRule({"base"}, {"stable"});

auto checkpoint = vm.beginTransaction();
vm.addRewriteRule({"temp"}, {"transient"});  // inner rule

vm.rollbackTransaction(checkpoint);
// Rule count returns to 1; "temp" rule is gone
```

### Speculative API (C++)

```cpp
Value result;
std::string error;

bool ok = vm.speculateValue([](EdictVM& snap) {
    // push something onto snap's stack
    return /* the value you want returned */;
}, &result, &error);
```

The lambda receives a **copy** of the VM; mutations never affect the original.

---

## 18. Cursor Navigation

The `cursor` capsule provides navigation over the Listree tree. Each operation pushes a boolean result (`"true"` or `"false"` as strings).

```edict
cursor.down !   -- move to first child; push "true"/"false"
cursor.up !     -- move to parent; push "true"/"false"
cursor.next !   -- move to next sibling; push "true"/"false"
cursor.prev !   -- move to previous sibling; push "true"/"false"
cursor.get !    -- push current node's value onto the data stack
cursor.set !    -- pop data stack top, assign to current node position
```

### Example: navigate and read

```edict
cursor.down !
test & [
  cursor.get !
  print
] | []
```

### Example: set a value

```edict
"new value"
cursor.set !
```

---

## 19. FFI — Native Library Integration

Edict can import C/C++ shared libraries at runtime using the **Cartographer** parser and the built-in resolver.

### Boot strap capsules

The VM automatically initializes two capsules at startup:

```edict
-- These are created automatically by the bootstrap prelude:
-- __bootstrap_import.curate_parser ! @parser
-- __bootstrap_import.curate_resolver ! @resolver
```

After boot, `parser` and `resolver` are available in the global scope.

### Load a shared library

```edict
"./libmylib.so" resolver.load !
-- stack: [ <library handle or status> ]
```

### Parse a header

```edict
"./mylib.h" parser.map ! @defs
-- defs now contains function definitions parsed from the header
```

### Call an imported function

```edict
"10" "32" defs.add !
-- calls the native `add(int, int)` function with args "10" and "32"
-- the FFI layer converts the string arguments to C ints at the call boundary
-- stack: [ "42" ]
```

### Full import (parse + resolve in one step)

```edict
"./libmylib.so" "./mylib.h" "mylib" resolver.import ! @mylib
"10" "32" mylib.add !
-- stack: [ "42" ]
```

After import, `mylib.__cartographer.status` reflects the import outcome.

### JSON round-trip pipeline

For full control over the parse/resolve pipeline:

```edict
"./mylib.h"     parser.parse_json !    @schema_json
"./libmylib.so" schema_json resolver.resolve_json !  @resolved_json
resolved_json   "mylib" resolver.import_resolved_json ! @mylib
```

### Materialize pipeline (alternative)

```edict
"./mylib.h"   parser.parse_json !
              parser.materialize_json ! @defs
"10" "32" defs.add !
```

### Deferred (async) import

For large libraries, import can run asynchronously:

```edict
"./mylib.h" "./libmylib.so" "mylib" resolver.import_deferred ! @req

-- Poll status:
req resolver.import_status ! @status
-- status has: request_id, service_boundary, protocol, api_schema_format, status

-- Collect result when ready:
req resolver.import_collect ! @mylib
"10" "32" mylib.add !
```

The `status` field cycles through `"queued"` → `"running"` → `"ready"`.

### Pre-resolved import

```edict
resolved_json "./libmylib.so" "mylib" resolver.import_resolved ! @mylib
```

### Reading a text file

```edict
"./myfile.txt" resolver.__native.read_text !
-- stack: [ <file contents as string> ]
```

### Safety levels

Imported functions carry a `safety` metadata field:

| Safety | Meaning |
|--------|---------|
| `"safe"` | Can be called without restriction |
| `"unsafe"` | Blocked by default; requires `unsafe_extensions_allow` |

---

## 20. FFI Closures — Passing Edict as a C Callback

`ffi_closure` creates a native function pointer from an edict thunk, suitable for passing to C APIs that expect a callback.

```edict
-- Build a closure: (signature, edict-function) ffi_closure !
my_signature_node my_edict_fn ffi_closure !
-- stack: [ <raw native function pointer> ]
```

Inside the edict function invoked by the native caller:
- Arguments are bound as `ARG0`, `ARG1`, ... in the root scope
- Return value is bound as `RETURN`
- A fresh `EdictVM` is spawned per call

### Example: wrap a computation

```edict
-- A thunk that adds its two args and returns the result
-- (arithmetic is performed by a native function imported via FFI)
[ARG0 ARG1 native_add ! @RETURN] @my_adder

-- Build a C-callable closure:
my_signature my_adder ffi_closure !
-- returns a raw pointer that a C function can call
```

The signature node is a Listree dictionary that encodes the parameter and return types for libffi.

---

## 21. Unsafe Extensions Policy

By default, unsafe imported functions (those with `safety: "unsafe"` in their metadata) are blocked.

```edict
unsafe_extensions_status !   -- push current status: "allow" or "block"
unsafe_extensions_allow !    -- permit unsafe calls
unsafe_extensions_block !    -- re-block unsafe calls
```

```edict
unsafe_extensions_allow !
-- now unsafe functions can be called
unsafe_extensions_block !
-- re-blocked
```

Attempting to call an unsafe function while blocked produces `VM_ERROR: "Cartographer blocked unsafe import: <category>"`.

---

## 22. Output

```edict
"hello, world" print
-- prints "hello, world" to stdout
-- pops the value from the stack

"42" print
-- prints "42"

[] print
-- prints "" (empty list formats as empty)
```

`print` consumes the top of the stack and writes `formatValueForDisplay()` to stdout.

---

## 23. VM Resource Model (Advanced)

### Cross-resource transfer operations

In advanced usage (typically from C++ or built-in thunks), values can be moved between resource stacks:

| Opcode | Effect |
|--------|--------|
| `VMOP_S2S` | Data stack → Data stack (peek/copy top) |
| `VMOP_D2S` | Dict stack → Data stack |
| `VMOP_E2S` | Exception stack → Data stack |
| `VMOP_F2S` | Function stack → Data stack |
| `VMOP_S2D` | Data stack → Dict stack |
| `VMOP_S2E` | Data stack → Exception stack |
| `VMOP_S2F` | Data stack → Function stack |

### VM state flags

| Flag | Meaning |
|------|---------|
| `VM_NORMAL` | Running normally |
| `VM_YIELD` | Paused (cooperative yield) |
| `VM_ERROR` | Fatal error; execution loop exits |
| `VM_COMPLETE` | All code frames exhausted cleanly |
| `VM_SCANNING` | Skipping instructions (conditional branch arm skip) |

### `yield` and `reset`

```edict
yield   -- set VM_YIELD flag; pause the execution loop (host resumes it)
reset   -- clear VM_ERROR flag and error message; resume from error state
```

### Frame operations (internal)

| Opcode | Effect |
|--------|--------|
| `VMOP_FRAME_PUSH` | Push a new empty data frame |
| `VMOP_FRAME_MERGE` | Merge top data frame into parent via listcat |

---

## 24. Opcode Quick Reference

| Opcode | Edict Syntax | Stack Effect / Behavior |
|--------|-------------|------------------------|
| `VMOP_PUSHEXT` | literal / `"string"` / `{ }` / `[ ]` | Push a value |
| `VMOP_DUP` | `dup` | `(a -- a a)` |
| `VMOP_SWAP` | `swap` | `(a b -- b a)` |
| `VMOP_POP` | `pop` or bare `/` | `(a --)` |
| `VMOP_REF` | `identifier` (implicit) | `(key -- value)` — dict lookup |
| `VMOP_ASSIGN` | `@name` or `value key @` | `(value key -- )` — dict store |
| `VMOP_REMOVE` | `/name` | Remove key from dict |
| `VMOP_EVAL` | `!` | `(fn -- ...)` — execute top |
| `VMOP_PRINT` | `print` | `(val --)` — stdout |
| `VMOP_TEST` | `test` | `(val --)` — push to STATE if falsy |
| `VMOP_FAIL` | `fail` | `(val --)` — push to STATE (always) |
| `VMOP_THROW` | `&` | Branch on STATE (skip then if failure) |
| `VMOP_CATCH` | `\|` | Else separator (skip else if no failure) |
| `VMOP_CTX_PUSH` | `<` or `{` (non-JSON) | Push dict + data frame |
| `VMOP_CTX_POP` | `>` or `}` (non-JSON) | Pop frames, push collected results |
| `VMOP_FUN_PUSH` | `(` | Pop thunk → VMRES_FUNC, fresh frames |
| `VMOP_FUN_EVAL` | `)` | Evaluate function on VMRES_FUNC |
| `VMOP_SPLICE` | `^` or `^name` | Splice current frame into named node |
| `VMOP_SPECULATE` | `speculate [code]` | Run code in isolated snapshot |
| `VMOP_LOGIC_RUN` | `logic_run !` / `logic { }` | Run miniKanren query |
| `VMOP_REWRITE_DEFINE` | `rewrite_define !` | Register a rewrite rule |
| `VMOP_REWRITE_LIST` | `rewrite_list !` | List all rules |
| `VMOP_REWRITE_REMOVE` | `rewrite_remove !` | Remove rule by index |
| `VMOP_REWRITE_APPLY` | `rewrite_apply !` | Manually trigger one rewrite step |
| `VMOP_REWRITE_MODE` | `rewrite_mode !` | Set mode: `"auto"` / `"manual"` / `"off"` |
| `VMOP_REWRITE_TRACE` | `rewrite_trace !` | Push last rewrite trace object |
| `VMOP_CURSOR_DOWN` | `cursor.down !` | Move cursor to first child |
| `VMOP_CURSOR_UP` | `cursor.up !` | Move cursor to parent |
| `VMOP_CURSOR_NEXT` | `cursor.next !` | Move cursor to next sibling |
| `VMOP_CURSOR_PREV` | `cursor.prev !` | Move cursor to previous sibling |
| `VMOP_CURSOR_GET` | `cursor.get !` | Push current cursor node value |
| `VMOP_CURSOR_SET` | `cursor.set !` | Assign top-of-stack to cursor position |
| `VMOP_LOAD` | `resolver.load !` | Load a shared library |
| `VMOP_MAP` | `parser.map !` | Parse a C header via Cartographer |
| `VMOP_IMPORT` | `resolver.import !` | Full import: parse + resolve + inject |
| `VMOP_IMPORT_RESOLVED` | `resolver.import_resolved !` | Import from pre-resolved JSON |
| `VMOP_IMPORT_DEFERRED` | `resolver.import_deferred !` | Async import; returns request handle |
| `VMOP_IMPORT_COLLECT` | `resolver.import_collect !` | Collect deferred import result |
| `VMOP_IMPORT_STATUS` | `resolver.import_status !` | Query status of deferred import |
| `VMOP_PARSE_JSON` | `parser.parse_json !` | Parse header → schema JSON string |
| `VMOP_MATERIALIZE_JSON` | `parser.materialize_json !` | Decode schema JSON → Listree |
| `VMOP_RESOLVE_JSON` | `resolver.resolve_json !` | Resolve schema JSON against library |
| `VMOP_CLOSURE` | `ffi_closure !` | Build native closure from edict thunk |
| `VMOP_UNSAFE_EXTENSIONS_ALLOW` | `unsafe_extensions_allow` | Permit unsafe FFI calls |
| `VMOP_UNSAFE_EXTENSIONS_BLOCK` | `unsafe_extensions_block` | Block unsafe FFI calls |
| `VMOP_UNSAFE_EXTENSIONS_STATUS` | `unsafe_extensions_status` | Push current unsafe policy |
| `VMOP_RESET` | `reset` | Clear VM_ERROR flag |
| `VMOP_YIELD` | `yield` | Pause VM (cooperative yield) |

---

## Appendix A: Complete Example Programs

### A.1 Fibonacci (illustrative — arithmetic via FFI)

```edict
-- Compute fib(10) using iteration
-- Note: arithmetic operations like +, -, and numeric comparisons are
-- performed by native (FFI) functions, not native edict opcodes.
-- The example below is pseudocode illustrating the control-flow idiom.

"0" @a
"1" @b
"10" @n

[
  n test & [
    a b math.add ! @next    -- native add: a + b
    b @a
    next @b
    n "1" math.sub ! @n     -- native sub: n - 1
    fib !                   -- tail-recursive call
  ] | []
] @fib

fib !
-- result: a contains fib(10) as a string
```

### A.2 Stack-based string processing

```edict
"hello" @greeting
"world" @subject

-- Build a greeting string via rewrite rule
{ "pattern": ["$1", "$2", "greet"], "replacement": ["$1 $2"] }
rewrite_define ! /

greeting subject "greet"
-- rewrite fires automatically: stack now has "hello world"
```

### A.3 Logic-driven data selection

```edict
logic {
  "fresh": ["item"],
  "where": [["membero", "item", ["apple", "banana", "cherry"]]],
  "results": ["item"],
  "limit": "2"
} @fruits
-- fruits is a list: ["apple", "banana"]
```

### A.4 Speculative probe with fallback

```edict
"default"

-- Try to run a computation that might fail:
speculate [
  "primary_result"
  -- ... complex work that might error ...
]

-- Use result if non-null, else keep default:
dup test
& [ swap / ]   -- result was good: pop default, use speculative result
| [ / ]        -- result was null (failed): pop null, keep default
```

### A.5 Capturing stack contents into a list

```edict
[] @results
[^results^] @capture

capture(
  "first"
  "second"
  "third"
)
-- results.isListMode() == true; contains ["first", "second", "third"]
results
```

### A.6 Rewrite-based string normalization

```edict
-- Define normalization rules (strings only — no native arithmetic)
{ "pattern": ["\"\"", "concat"], "replacement": [] } rewrite_define ! /
{ "pattern": ["concat", "\"\""], "replacement": [] } rewrite_define ! /

-- In auto mode, these fire after each instruction:
"" "hello" concat
-- After "": stack = [""]
-- After "hello": stack = ["hello", ""]
-- After "concat": stack = [<result>]
-- Rewrite fires: "" is identity; result simplified to "hello"
```

---

## Appendix B: String Storage Architecture

Edict's string storage uses three tiers (implemented in Listree):

| Size | Storage | Flag | Notes |
|------|---------|------|-------|
| ≤ 15 bytes | Inline (SSO) | `LtvFlags::Immediate` | No allocation; data lives in the struct |
| > 15 bytes | Slab (BlobAllocator) | `LtvFlags::SlabBlob` | Arena-allocated; no individual free |
| Static/mapped | Zero-copy view | `LtvFlags::StaticView` | Data pointer points into external buffer |

This gives edict's string handling excellent cache locality for short strings (the common case) and zero-copy semantics for strings in mapped files or static data.

---

*Document generated 2026-03-16. Based on full review of edict VM, compiler, REPL, and all regression tests.*
