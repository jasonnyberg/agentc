#!/usr/bin/env bash
# test_cartographer_dump.sh — regression tests for the cartographer_dump utility.
#
# Exercises:
#   - Output is valid JSON (parsed by python3)
#   - All top-level symbol kinds present (Struct, Function)
#   - Correct struct sizes and field offsets
#   - Correct field types (including nested struct type strings)
#   - Function nodes carry return_type and Parameter children
#   - Optional fields omitted when not applicable (offset absent on Struct nodes)
#   - Real-world header (time.h): timespec struct round-trip
#
# Exit code: 0 = all assertions pass, 1 = one or more failures.

set -euo pipefail

DUMP=${DUMP:-$(dirname "$0")/../build/cartographer/cartographer_dump}
TEST_INPUT_H="$(cd "$(dirname "$0")" && pwd)/../cartographer/tests/test_input.h"
PASS=0
FAIL=0

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
assert_eq() {
    local label="$1" expected="$2" actual="$3"
    if [ "$actual" = "$expected" ]; then
        echo "  PASS: $label"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $label"
        echo "        expected: '$expected'"
        echo "        actual:   '$actual'"
        FAIL=$((FAIL + 1))
    fi
}

assert_contains() {
    local label="$1" expected="$2" actual="$3"
    if echo "$actual" | grep -qF -- "$expected"; then
        echo "  PASS: $label"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $label"
        echo "        expected to find: '$expected'"
        echo "        in output: '$actual'"
        FAIL=$((FAIL + 1))
    fi
}

# Run the dump once and cache results
DUMP_OUT=$("$DUMP" "$TEST_INPUT_H" 2>&1)

# ---------------------------------------------------------------------------
echo "--- Test 1: output is valid JSON ---"
# ---------------------------------------------------------------------------
if echo "$DUMP_OUT" | python3 -m json.tool > /dev/null 2>&1; then
    echo "  PASS: output parses as valid JSON"
    PASS=$((PASS + 1))
else
    echo "  FAIL: output is not valid JSON"
    FAIL=$((FAIL + 1))
fi

# Parse into python for field-level assertions
PYTHON_QUERY() {
    echo "$DUMP_OUT" | python3 -c "$1"
}

# ---------------------------------------------------------------------------
echo "--- Test 2: expected top-level symbols present ---"
# ---------------------------------------------------------------------------
SYMBOLS=$(PYTHON_QUERY "import json,sys; d=json.load(sys.stdin); print(sorted(d.keys()))")
assert_contains "Point symbol present"         "'Point'"         "$SYMBOLS"
assert_contains "Rect symbol present"          "'Rect'"          "$SYMBOLS"
assert_contains "process_point symbol present" "'process_point'" "$SYMBOLS"

# ---------------------------------------------------------------------------
echo "--- Test 3: struct kinds and sizes ---"
# ---------------------------------------------------------------------------
POINT_KIND=$(PYTHON_QUERY "import json,sys; d=json.load(sys.stdin); print(d['Point']['kind'])")
POINT_SIZE=$(PYTHON_QUERY "import json,sys; d=json.load(sys.stdin); print(d['Point']['size'])")
RECT_KIND=$(PYTHON_QUERY  "import json,sys; d=json.load(sys.stdin); print(d['Rect']['kind'])")
RECT_SIZE=$(PYTHON_QUERY  "import json,sys; d=json.load(sys.stdin); print(d['Rect']['size'])")
assert_eq "Point kind is Struct"  "Struct" "$POINT_KIND"
assert_eq "Point size is 8"       "8"      "$POINT_SIZE"
assert_eq "Rect kind is Struct"   "Struct" "$RECT_KIND"
assert_eq "Rect size is 16"       "16"     "$RECT_SIZE"

# ---------------------------------------------------------------------------
echo "--- Test 4: struct field offsets and types ---"
# ---------------------------------------------------------------------------
X_OFFSET=$(PYTHON_QUERY "import json,sys; d=json.load(sys.stdin); print(d['Point']['children']['x']['offset'])")
Y_OFFSET=$(PYTHON_QUERY "import json,sys; d=json.load(sys.stdin); print(d['Point']['children']['y']['offset'])")
X_TYPE=$(PYTHON_QUERY   "import json,sys; d=json.load(sys.stdin); print(d['Point']['children']['x']['type'])")
assert_eq "Point.x offset is 0" "0"   "$X_OFFSET"
assert_eq "Point.y offset is 4" "4"   "$Y_OFFSET"
assert_eq "Point.x type is int" "int" "$X_TYPE"

ORIGIN_OFFSET=$(PYTHON_QUERY "import json,sys; d=json.load(sys.stdin); print(d['Rect']['children']['origin']['offset'])")
ORIGIN_TYPE=$(PYTHON_QUERY   "import json,sys; d=json.load(sys.stdin); print(d['Rect']['children']['origin']['type'])")
W_OFFSET=$(PYTHON_QUERY      "import json,sys; d=json.load(sys.stdin); print(d['Rect']['children']['w']['offset'])")
H_OFFSET=$(PYTHON_QUERY      "import json,sys; d=json.load(sys.stdin); print(d['Rect']['children']['h']['offset'])")
assert_eq "Rect.origin offset is 0"            "0"            "$ORIGIN_OFFSET"
assert_eq "Rect.origin type is struct Point"   "struct Point" "$ORIGIN_TYPE"
assert_eq "Rect.w offset is 8"                 "8"            "$W_OFFSET"
assert_eq "Rect.h offset is 12"                "12"           "$H_OFFSET"

# ---------------------------------------------------------------------------
echo "--- Test 5: function node has return_type and Parameter children ---"
# ---------------------------------------------------------------------------
FN_KIND=$(PYTHON_QUERY        "import json,sys; d=json.load(sys.stdin); print(d['process_point']['kind'])")
FN_RETTYPE=$(PYTHON_QUERY     "import json,sys; d=json.load(sys.stdin); print(d['process_point']['return_type'])")
PARAM_KIND=$(PYTHON_QUERY     "import json,sys; d=json.load(sys.stdin); print(d['process_point']['children']['p']['kind'])")
PARAM_TYPE=$(PYTHON_QUERY     "import json,sys; d=json.load(sys.stdin); print(d['process_point']['children']['p']['type'])")
assert_eq "process_point kind is Function"    "Function"     "$FN_KIND"
assert_eq "process_point return_type is void" "void"         "$FN_RETTYPE"
assert_eq "process_point param p kind"        "Parameter"    "$PARAM_KIND"
assert_eq "process_point param p type"        "struct Point" "$PARAM_TYPE"

# ---------------------------------------------------------------------------
echo "--- Test 6: offset key absent on Struct nodes (not a field) ---"
# ---------------------------------------------------------------------------
HAS_OFFSET=$(PYTHON_QUERY "import json,sys; d=json.load(sys.stdin); print('offset' in d['Point'])")
assert_eq "Struct node has no 'offset' key" "False" "$HAS_OFFSET"

# ---------------------------------------------------------------------------
echo "--- Test 7: real-world header (time.h) — timespec struct ---"
# ---------------------------------------------------------------------------
TIMESPEC_OUT=$("$DUMP" /usr/include/time.h 2>/dev/null)
if echo "$TIMESPEC_OUT" | python3 -m json.tool > /dev/null 2>&1; then
    echo "  PASS: time.h output is valid JSON"
    PASS=$((PASS + 1))
else
    echo "  FAIL: time.h output is not valid JSON"
    FAIL=$((FAIL + 1))
fi

TS_SIZE=$(echo "$TIMESPEC_OUT" | python3 -c "import json,sys; d=json.load(sys.stdin); print(d['timespec']['size'])")
TS_TV_SEC_OFFSET=$(echo "$TIMESPEC_OUT" | python3 -c "import json,sys; d=json.load(sys.stdin); print(d['timespec']['children']['tv_sec']['offset'])")
TS_TV_NSEC_OFFSET=$(echo "$TIMESPEC_OUT" | python3 -c "import json,sys; d=json.load(sys.stdin); print(d['timespec']['children']['tv_nsec']['offset'])")
assert_eq "timespec size is 16"      "16" "$TS_SIZE"
assert_eq "timespec.tv_sec offset 0" "0"  "$TS_TV_SEC_OFFSET"
assert_eq "timespec.tv_nsec offset 8" "8" "$TS_TV_NSEC_OFFSET"

# ---------------------------------------------------------------------------
echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
[ "$FAIL" -eq 0 ]
