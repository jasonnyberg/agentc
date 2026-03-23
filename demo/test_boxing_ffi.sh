#!/usr/bin/env bash
# test_boxing_ffi.sh — regression test for the boxing/unboxing pipeline.
#
# Tests the full round-trip from Edict through the FFI boxing functions
# (agentc_box / agentc_unbox / agentc_box_free in libcartographer.so).
#
# This script exercises:
#   - struct timespec round-trip: box, unbox, verify field values
#   - struct timeval round-trip:  different field names / typedef aliases
#   - agentc_box_free: heap allocation freed without error
#   - Zero-value fields: all fields unbox to zero when source has no matching keys
#   - Max int64 round-trip: boundary value check
#   - Varied scalar types (int8..int64, uint8..uint64, float, double)
#   - Nested struct round-trip: ComplexStruct with InnerPoint origin field
#
# NOTE: cartographer.box takes (source, typeDef) and cartographer.unbox takes
#       (boxed). The ns argument is no longer needed — type_def links are
#       resolved at parse time by bindTypes() inside Mapper::materialize().
#
# Exit code: 0 = all assertions pass, 1 = one or more failures.

set -euo pipefail

EDICT=${EDICT:-$(dirname "$0")/../build/edict/edict}
DEMO_COMPLEX_H="$(cd "$(dirname "$0")" && pwd)/demo_complex.h"
PASS=0
FAIL=0

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
assert_contains() {
    local label="$1"
    local expected="$2"
    local actual="$3"
    if echo "$actual" | grep -qF -- "$expected"; then
        echo "  PASS: $label"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $label"
        echo "        expected to find: '$expected'"
        echo "        in output:        '$actual'"
        FAIL=$((FAIL + 1))
    fi
}

assert_not_contains() {
    local label="$1"
    local unexpected="$2"
    local actual="$3"
    if ! echo "$actual" | grep -qF "$unexpected"; then
        echo "  PASS: $label"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $label (unexpected output found: '$unexpected')"
        FAIL=$((FAIL + 1))
    fi
}

# ---------------------------------------------------------------------------
echo "--- Test 1: struct timespec round-trip ---"
# ---------------------------------------------------------------------------
out=$("$EDICT" - 2>&1 <<EDICT
unsafe_extensions_allow ! pop
'/usr/include/time.h parser.__native.map ! @timedefs

{ "tv_sec": "1234567890", "tv_nsec": "500000000" } @src
src timedefs.timespec cartographer.box ! @boxed
boxed cartographer.unbox ! @unboxed

unboxed.tv_sec print
unboxed.tv_nsec print
EDICT
)

assert_contains "timespec tv_sec round-trip"  "1234567890" "$out"
assert_contains "timespec tv_nsec round-trip" "500000000"  "$out"

# ---------------------------------------------------------------------------
echo "--- Test 2: struct timeval round-trip (different typedef aliases) ---"
# ---------------------------------------------------------------------------
out=$("$EDICT" - 2>&1 <<EDICT
unsafe_extensions_allow ! pop
'/usr/include/sys/time.h parser.__native.map ! @sysTime

{ "tv_sec": "7777", "tv_usec": "333" } @src
src sysTime.timeval cartographer.box ! @boxed
boxed cartographer.unbox ! @unboxed

unboxed.tv_sec print
unboxed.tv_usec print
EDICT
)

assert_contains "timeval tv_sec round-trip"  "7777" "$out"
assert_contains "timeval tv_usec round-trip" "333"  "$out"

# ---------------------------------------------------------------------------
echo "--- Test 3: agentc_box_free does not crash ---"
# ---------------------------------------------------------------------------
out=$("$EDICT" - 2>&1 <<EDICT
unsafe_extensions_allow ! pop
'/usr/include/time.h parser.__native.map ! @timedefs

{ "tv_sec": "99", "tv_nsec": "1" } @src
src timedefs.timespec cartographer.box ! @boxed
boxed cartographer.box_free !
'box_free_ok print
EDICT
)

assert_contains "box_free completes without error" "box_free_ok" "$out"
assert_not_contains "box_free produces no error"   "error"        "$out"

# ---------------------------------------------------------------------------
echo "--- Test 4: boxed LTV contains __ptr (binary:8) field ---"
# ---------------------------------------------------------------------------
out=$("$EDICT" - 2>&1 <<EDICT
unsafe_extensions_allow ! pop
'/usr/include/time.h parser.__native.map ! @timedefs

{ "tv_sec": "42", "tv_nsec": "0" } @src
src timedefs.timespec cartographer.box ! @boxed
boxed.__ptr print
EDICT
)

assert_contains "boxed __ptr is 8-byte binary" "<binary:8>" "$out"

# ---------------------------------------------------------------------------
echo "--- Test 5: zero-fill when source field is absent ---"
# ---------------------------------------------------------------------------
out=$("$EDICT" - 2>&1 <<EDICT
unsafe_extensions_allow ! pop
'/usr/include/time.h parser.__native.map ! @timedefs

# Source has no tv_nsec — should unbox as "0"
{ "tv_sec": "5" } @src
src timedefs.timespec cartographer.box ! @boxed
boxed cartographer.unbox ! @unboxed

unboxed.tv_sec print
unboxed.tv_nsec print
EDICT
)

assert_contains "partial source: tv_sec set" "5" "$out"
assert_contains "partial source: tv_nsec is zero" "0" "$out"

# ---------------------------------------------------------------------------
echo "--- Test 6: max-value int64 round-trip ---"
# ---------------------------------------------------------------------------
out=$("$EDICT" - 2>&1 <<EDICT
unsafe_extensions_allow ! pop
'/usr/include/time.h parser.__native.map ! @timedefs

{ "tv_sec": "9223372036854775807", "tv_nsec": "999999999" } @src
src timedefs.timespec cartographer.box ! @boxed
boxed cartographer.unbox ! @unboxed

unboxed.tv_sec print
unboxed.tv_nsec print
EDICT
)

assert_contains "max int64 tv_sec round-trip"  "9223372036854775807" "$out"
assert_contains "max tv_nsec round-trip"        "999999999"          "$out"

# ---------------------------------------------------------------------------
echo "--- Test 7: varied scalar types (ComplexStruct) ---"
# ---------------------------------------------------------------------------
out=$("$EDICT" - 2>&1 <<EDICT
unsafe_extensions_allow ! pop
'${DEMO_COMPLEX_H} parser.__native.map ! @ns

{ "byte_val": "-42", "ubyte_val": "200", "short_val": "-1000", "ushort_val": "60000", "int_val": "-100000", "uint_val": "4000000000", "long_val": "-9000000000000", "ulong_val": "9000000000000", "float_val": "3.5", "double_val": "2.71828182845905" } @src

src ns.ComplexStruct cartographer.box ! @boxed
boxed cartographer.unbox ! @unboxed

unboxed.byte_val print
unboxed.ubyte_val print
unboxed.short_val print
unboxed.ushort_val print
unboxed.int_val print
unboxed.uint_val print
unboxed.long_val print
unboxed.ulong_val print
unboxed.float_val print
unboxed.double_val print
EDICT
)

assert_contains "byte_val round-trip"    "-42"              "$out"
assert_contains "ubyte_val round-trip"   "200"              "$out"
assert_contains "short_val round-trip"   "-1000"            "$out"
assert_contains "ushort_val round-trip"  "60000"            "$out"
assert_contains "int_val round-trip"     "-100000"          "$out"
assert_contains "uint_val round-trip"    "4000000000"       "$out"
assert_contains "long_val round-trip"    "-9000000000000"   "$out"
assert_contains "ulong_val round-trip"   "9000000000000"    "$out"
assert_contains "float_val round-trip"   "3.5"              "$out"
assert_contains "double_val round-trip"  "2.71828182845905" "$out"

# ---------------------------------------------------------------------------
echo "--- Test 8: nested struct round-trip (ComplexStruct.origin) ---"
# ---------------------------------------------------------------------------
out=$("$EDICT" - 2>&1 <<EDICT
unsafe_extensions_allow ! pop
'${DEMO_COMPLEX_H} parser.__native.map ! @ns

{ "int_val": "42", "origin": { "x": "100", "y": "200" } } @src

src ns.ComplexStruct cartographer.box ! @boxed
boxed cartographer.unbox ! @unboxed

unboxed.int_val print
unboxed.origin.x print
unboxed.origin.y print
EDICT
)

assert_contains "int_val alongside nested struct" "42"  "$out"
assert_contains "nested struct origin.x"          "100" "$out"
assert_contains "nested struct origin.y"          "200" "$out"

# ---------------------------------------------------------------------------
echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
[ "$FAIL" -eq 0 ]
