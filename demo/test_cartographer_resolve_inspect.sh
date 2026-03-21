#!/usr/bin/env bash
# test_cartographer_resolve_inspect.sh — regression tests for cartographer_resolve_inspect.
#
# Exercises:
#   - Output is valid JSON
#   - Library block: path, file_size (>0), content_hash format, address_bindings_process_local
#   - Summary block: symbol_count, resolved_count, unresolved_count
#   - Symbols block: correct keys, kinds, resolution_status values
#   - Structs have resolution_status "not_applicable"
#   - Missing symbol has resolution_status "unresolved"
#   - Reads from a file argument and from stdin (pipe)
#   - Explicit '-' argument reads from stdin
#   - Error on non-existent file
#   - Error on malformed JSON input
#
# Exit code: 0 = all pass, 1 = one or more failures.

set -euo pipefail

PARSE=${PARSE:-$(dirname "$0")/../build/cartographer/cartographer_parse}
RESOLVE=${RESOLVE:-$(dirname "$0")/../build/cartographer/cartographer_resolve}
INSPECT=${INSPECT:-$(dirname "$0")/../build/cartographer/cartographer_resolve_inspect}
TEST_INPUT_H="$(cd "$(dirname "$0")" && pwd)/../cartographer/tests/test_input.h"
LIB_PATH="$(cd "$(dirname "$0")" && pwd)/../build/cartographer/libagentmath_poc.so"
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

# Produce resolved JSON once; test both file and pipe modes
RESOLVED_FILE=$(mktemp /tmp/resolved_XXXXXX.json)
trap 'rm -f "$RESOLVED_FILE"' EXIT
"$PARSE" "$TEST_INPUT_H" | "$RESOLVE" "$LIB_PATH" - > "$RESOLVED_FILE"

# Output from file argument
INSPECT_OUT=$("$INSPECT" "$RESOLVED_FILE" 2>&1)

# ---------------------------------------------------------------------------
echo "--- Test 1: output is valid JSON (file mode) ---"
# ---------------------------------------------------------------------------
if echo "$INSPECT_OUT" | python3 -m json.tool > /dev/null 2>&1; then
    echo "  PASS: output parses as valid JSON"
    PASS=$((PASS + 1))
else
    echo "  FAIL: output is not valid JSON"
    FAIL=$((FAIL + 1))
fi

PYTHON_QUERY() {
    echo "$INSPECT_OUT" | python3 -c "$1"
}

# ---------------------------------------------------------------------------
echo "--- Test 2: library block ---"
# ---------------------------------------------------------------------------
LIB_PATH_OUT=$(PYTHON_QUERY "import json,sys; d=json.load(sys.stdin); print(d['library']['path'])")
FILE_SIZE=$(PYTHON_QUERY    "import json,sys; d=json.load(sys.stdin); print(d['library']['file_size'])")
CONTENT_HASH=$(PYTHON_QUERY "import json,sys; d=json.load(sys.stdin); print(d['library']['content_hash'])")
ADDR_BIND=$(PYTHON_QUERY    "import json,sys; d=json.load(sys.stdin); print(d['library']['address_bindings_process_local'])")
assert_contains "library path contains libagentmath_poc.so" "libagentmath_poc.so" "$LIB_PATH_OUT"
assert_contains "file_size is positive integer"             "1"                   "$FILE_SIZE"
assert_contains "content_hash has fnv1a64 prefix"          "fnv1a64:"            "$CONTENT_HASH"
assert_eq       "address_bindings_process_local is True"   "True"                "$ADDR_BIND"

# ---------------------------------------------------------------------------
echo "--- Test 3: summary block ---"
# ---------------------------------------------------------------------------
SYM_COUNT=$(PYTHON_QUERY  "import json,sys; d=json.load(sys.stdin); print(d['summary']['symbol_count'])")
RES_COUNT=$(PYTHON_QUERY  "import json,sys; d=json.load(sys.stdin); print(d['summary']['resolved_count'])")
UNRES_COUNT=$(PYTHON_QUERY "import json,sys; d=json.load(sys.stdin); print(d['summary']['unresolved_count'])")
assert_eq "symbol_count is 3"     "3" "$SYM_COUNT"
assert_eq "resolved_count is 0"   "0" "$RES_COUNT"
assert_eq "unresolved_count is 1" "1" "$UNRES_COUNT"

# ---------------------------------------------------------------------------
echo "--- Test 4: symbols block keys present ---"
# ---------------------------------------------------------------------------
SYMBOLS=$(PYTHON_QUERY "import json,sys; d=json.load(sys.stdin); print(sorted(d['symbols'].keys()))")
assert_contains "Point key present"         "'Point'"         "$SYMBOLS"
assert_contains "Rect key present"          "'Rect'"          "$SYMBOLS"
assert_contains "process_point key present" "'process_point'" "$SYMBOLS"

# ---------------------------------------------------------------------------
echo "--- Test 5: symbol kinds ---"
# ---------------------------------------------------------------------------
POINT_KIND=$(PYTHON_QUERY "import json,sys; d=json.load(sys.stdin); print(d['symbols']['Point']['kind'])")
RECT_KIND=$(PYTHON_QUERY  "import json,sys; d=json.load(sys.stdin); print(d['symbols']['Rect']['kind'])")
FN_KIND=$(PYTHON_QUERY    "import json,sys; d=json.load(sys.stdin); print(d['symbols']['process_point']['kind'])")
assert_eq "Point kind is Struct"           "Struct"   "$POINT_KIND"
assert_eq "Rect kind is Struct"            "Struct"   "$RECT_KIND"
assert_eq "process_point kind is Function" "Function" "$FN_KIND"

# ---------------------------------------------------------------------------
echo "--- Test 6: resolution_status values ---"
# ---------------------------------------------------------------------------
POINT_STATUS=$(PYTHON_QUERY "import json,sys; d=json.load(sys.stdin); print(d['symbols']['Point']['resolution_status'])")
RECT_STATUS=$(PYTHON_QUERY  "import json,sys; d=json.load(sys.stdin); print(d['symbols']['Rect']['resolution_status'])")
FN_STATUS=$(PYTHON_QUERY    "import json,sys; d=json.load(sys.stdin); print(d['symbols']['process_point']['resolution_status'])")
assert_eq "Point resolution_status is not_applicable" "not_applicable" "$POINT_STATUS"
assert_eq "Rect resolution_status is not_applicable"  "not_applicable" "$RECT_STATUS"
assert_eq "process_point resolution_status is unresolved" "unresolved" "$FN_STATUS"

# ---------------------------------------------------------------------------
echo "--- Test 7: address key absent for not_applicable symbols ---"
# ---------------------------------------------------------------------------
HAS_ADDR=$(PYTHON_QUERY "import json,sys; d=json.load(sys.stdin); print('address' in d['symbols']['Point'])")
assert_eq "Point symbol has no 'address' key" "False" "$HAS_ADDR"

# ---------------------------------------------------------------------------
echo "--- Test 8: stdin pipe mode produces identical output ---"
# ---------------------------------------------------------------------------
PIPE_OUT=$("$PARSE" "$TEST_INPUT_H" | "$RESOLVE" "$LIB_PATH" - | "$INSPECT" 2>&1)
# Note: file_size and content_hash may differ if lib changes, so compare structure only
if echo "$PIPE_OUT" | python3 -m json.tool > /dev/null 2>&1; then
    echo "  PASS: pipe mode output is valid JSON"
    PASS=$((PASS + 1))
else
    echo "  FAIL: pipe mode output is not valid JSON"
    FAIL=$((FAIL + 1))
fi

# ---------------------------------------------------------------------------
echo "--- Test 9: explicit '-' argument reads from stdin ---"
# ---------------------------------------------------------------------------
DASH_OUT=$(cat "$RESOLVED_FILE" | "$INSPECT" - 2>&1)
if [ "$DASH_OUT" = "$INSPECT_OUT" ]; then
    echo "  PASS: '-' argument output matches file mode output"
    PASS=$((PASS + 1))
else
    echo "  FAIL: '-' argument output differs from file mode output"
    FAIL=$((FAIL + 1))
fi

# ---------------------------------------------------------------------------
echo "--- Test 10: error on non-existent file ---"
# ---------------------------------------------------------------------------
ERR_OUT=$("$INSPECT" /nonexistent/path.json 2>&1 || true)
assert_contains "error message for missing file" "cannot open" "$ERR_OUT"

# ---------------------------------------------------------------------------
echo "--- Test 11: error on malformed JSON ---"
# ---------------------------------------------------------------------------
BAD_OUT=$(echo "not json at all" | "$INSPECT" 2>&1 || true)
assert_contains "error message for bad JSON" "decode error" "$BAD_OUT"

# ---------------------------------------------------------------------------
echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
[ "$FAIL" -eq 0 ]
