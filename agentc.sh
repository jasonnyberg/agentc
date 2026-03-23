#!/usr/bin/env zsh

BUILD=${BUILD:-$(dirname "${(%):-%x}")/build}
PATH=$PATH:$BUILD/edict
PATH=$PATH:$BUILD/cartographer

# Individual utilities

edict() { # [script]
    $BUILD/edict/edict "$@";
}

cartographer_dump() { # <header>
    $BUILD/cartographer/cartographer_dump "$1";
}

cartographer_parse() { # <header>
    $BUILD/cartographer/cartographer_parse "$1";
}

cartographer_schema_inspect() { # (reads stdin: parser_json_v1)
    $BUILD/cartographer/cartographer_schema_inspect;
}

cartographer_resolve() { # <library> <schema_json|->
    $BUILD/cartographer/cartographer_resolve "$1" "$2";
}

cartographer_resolve_inspect() { # (reads stdin: resolver_json_v1)
    $BUILD/cartographer/cartographer_resolve_inspect;
}

agentc_visualize() { # <header>
    $BUILD/cartographer/agentc_visualize "$1";
}

agentc_visualize_dot() { # <header>
    $BUILD/cartographer/agentc_visualize "$1" | xdot -;
}

# Pipeline compositions

agentc_schema() { # <header>
    cartographer_parse "$1" | cartographer_schema_inspect;
}

agentc_resolve() { # <header> <library>
    cartographer_parse "$1" | cartographer_resolve "$2" - | cartographer_resolve_inspect;
}

# Regression test — invokes every wrapper function with a working example.
# Exit code: 0 = all pass, non-zero = number of failures.

agentc_test() {
    local _SCRIPT_DIR="$(cd "$(dirname "${(%):-%x}")" && pwd)"
    local _TEST_H="$_SCRIPT_DIR/cartographer/tests/test_input.h"
    local _TEST_LIB="$BUILD/cartographer/libagentmath_poc.so"
    local _PASS=0 _FAIL=0

    _check() {
        local label="$1"; shift
        local out
        out=$("$@" 2>&1 || true)
        if [ -n "$out" ]; then
            echo "  PASS: $label"
            _PASS=$((_PASS + 1))
        else
            echo "  FAIL: $label (empty output)"
            _FAIL=$((_FAIL + 1))
        fi
    }

    echo "--- agentc_test ---"

    # edict: run a trivial inline script
    local _edict_out
    _edict_out=$(edict - 2>&1 <<'EDICT' || true
'hello print
EDICT
)
    if [ -n "$_edict_out" ]; then
        echo "  PASS: edict"
        _PASS=$((_PASS + 1))
    else
        echo "  FAIL: edict (empty output)"
        _FAIL=$((_FAIL + 1))
    fi

    # Individual cartographer utilities
    _check "cartographer_dump"             cartographer_dump "$_TEST_H"
    _check "cartographer_parse"            cartographer_parse "$_TEST_H"

    # cartographer_schema_inspect reads stdin
    local _schema_out
    _schema_out=$(cartographer_parse "$_TEST_H" | cartographer_schema_inspect 2>&1 || true)
    if [ -n "$_schema_out" ]; then
        echo "  PASS: cartographer_schema_inspect"
        _PASS=$((_PASS + 1))
    else
        echo "  FAIL: cartographer_schema_inspect (empty output)"
        _FAIL=$((_FAIL + 1))
    fi

    # cartographer_resolve reads stdin
    local _resolve_out
    _resolve_out=$(cartographer_parse "$_TEST_H" | cartographer_resolve "$_TEST_LIB" - 2>&1 || true)
    if [ -n "$_resolve_out" ]; then
        echo "  PASS: cartographer_resolve"
        _PASS=$((_PASS + 1))
    else
        echo "  FAIL: cartographer_resolve (empty output)"
        _FAIL=$((_FAIL + 1))
    fi

    # cartographer_resolve_inspect reads stdin
    local _inspect_out
    _inspect_out=$(cartographer_parse "$_TEST_H" | cartographer_resolve "$_TEST_LIB" - | cartographer_resolve_inspect 2>&1 || true)
    if [ -n "$_inspect_out" ]; then
        echo "  PASS: cartographer_resolve_inspect"
        _PASS=$((_PASS + 1))
    else
        echo "  FAIL: cartographer_resolve_inspect (empty output)"
        _FAIL=$((_FAIL + 1))
    fi

    _check "agentc_visualize"              agentc_visualize "$_TEST_H"

    # Pipeline compositions
    local _agentc_schema_out
    _agentc_schema_out=$(agentc_schema "$_TEST_H" 2>&1 || true)
    if [ -n "$_agentc_schema_out" ]; then
        echo "  PASS: agentc_schema"
        _PASS=$((_PASS + 1))
    else
        echo "  FAIL: agentc_schema (empty output)"
        _FAIL=$((_FAIL + 1))
    fi

    local _agentc_resolve_out
    _agentc_resolve_out=$(agentc_resolve "$_TEST_H" "$_TEST_LIB" 2>&1 || true)
    if [ -n "$_agentc_resolve_out" ]; then
        echo "  PASS: agentc_resolve"
        _PASS=$((_PASS + 1))
    else
        echo "  FAIL: agentc_resolve (empty output)"
        _FAIL=$((_FAIL + 1))
    fi

    echo ""
    echo "Results: $_PASS passed, $_FAIL failed"
    unfunction _check 2>/dev/null
    return $_FAIL
}

# Demo — calls every wrapper function and prints its output, no pass/fail testing.
# Useful for inspecting what each function actually emits.

agentc_demo() {
    local _SCRIPT_DIR="$(cd "$(dirname "${(%):-%x}")" && pwd)"
    local _TEST_H="$_SCRIPT_DIR/cartographer/tests/test_input.h"
    local _TEST_LIB="$BUILD/cartographer/libagentmath_poc.so"

    # Print label to stderr so it reaches the terminal even when stdout is piped.
    _section() { echo "" >&2; echo "=== $1 ===" >&2; }

    # Print command to stderr, then run it. Works for single commands only;
    # for pipelines, print the label with _section then run the pipeline directly.
    _run() { print -u2 "  \$ $*"; "$@" || true; }

    _section "edict"
    _run edict - <<'EDICT'
'hello print
EDICT

    _section "cartographer_dump"
    _run cartographer_dump "$_TEST_H"

    _section "cartographer_parse"
    _run cartographer_parse "$_TEST_H"

    _section 'cartographer_parse | cartographer_schema_inspect'
    cartographer_parse "$_TEST_H" | cartographer_schema_inspect || true

    _section 'cartographer_parse | cartographer_resolve'
    cartographer_parse "$_TEST_H" | cartographer_resolve "$_TEST_LIB" - || true

    _section 'cartographer_parse | cartographer_resolve | cartographer_resolve_inspect'
    cartographer_parse "$_TEST_H" | cartographer_resolve "$_TEST_LIB" - | cartographer_resolve_inspect || true

    _section "agentc_visualize"
    _run agentc_visualize "$_TEST_H"

    _section "agentc_schema"
    _run agentc_schema "$_TEST_H"

    _section "agentc_resolve"
    _run agentc_resolve "$_TEST_H" "$_TEST_LIB"

    unfunction _section _run 2>/dev/null
}
