#!/usr/bin/env bash
# test_cartographer_schema_inspect.sh — regression tests for cartographer_schema_inspect.
#
# Exercises:
#   - Output is valid JSON
#   - Round-trip: schema_inspect output matches cartographer_dump for the same header
#   - Correct symbol kinds, sizes, field offsets, field types
#   - Nested struct field type preserved
#   - Function return_type and Parameter children
#   - Offset absent on Struct nodes (omitted when null)
#   - Reads from a file argument and from stdin (pipe)
#   - Error on non-existent file
#
# Exit code: 0 = all pass, 1 = one or more failures.

set -euo pipefail

PARSE=${PARSE:-$(dirname "$0")/../build/cartographer/cartographer_parse}
INSPECT=${INSPECT:-$(dirname "$0")/../build/cartographer/cartographer_schema_inspect}
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

# Produce schema once; test both file and pipe modes
SCHEMA_FILE=$(mktemp /tmp/schema_XXXXXX.json)
trap 'rm -f "$SCHEMA_FILE"' EXIT
"$PARSE" "$TEST_INPUT_H" > "$SCHEMA_FILE"

# Output from file argument
INSPECT_OUT=$("$INSPECT" "$SCHEMA_FILE" 2>&1)

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
assert_eq "Rect.origin offset is 0"           "0"            "$ORIGIN_OFFSET"
assert_eq "Rect.origin type is struct Point"  "struct Point" "$ORIGIN_TYPE"
assert_eq "Rect.w offset is 8"                "8"            "$W_OFFSET"
assert_eq "Rect.h offset is 12"               "12"           "$H_OFFSET"

# ---------------------------------------------------------------------------
echo "--- Test 5: function node has return_type and Parameter children ---"
# ---------------------------------------------------------------------------
FN_KIND=$(PYTHON_QUERY    "import json,sys; d=json.load(sys.stdin); print(d['process_point']['kind'])")
FN_RETTYPE=$(PYTHON_QUERY "import json,sys; d=json.load(sys.stdin); print(d['process_point']['return_type'])")
PARAM_KIND=$(PYTHON_QUERY "import json,sys; d=json.load(sys.stdin); print(d['process_point']['children']['p']['kind'])")
PARAM_TYPE=$(PYTHON_QUERY "import json,sys; d=json.load(sys.stdin); print(d['process_point']['children']['p']['type'])")
assert_eq "process_point kind is Function"    "Function"     "$FN_KIND"
assert_eq "process_point return_type is void" "void"         "$FN_RETTYPE"
assert_eq "process_point param p kind"        "Parameter"    "$PARAM_KIND"
assert_eq "process_point param p type"        "struct Point" "$PARAM_TYPE"

# ---------------------------------------------------------------------------
echo "--- Test 6: offset key absent on Struct nodes ---"
# ---------------------------------------------------------------------------
HAS_OFFSET=$(PYTHON_QUERY "import json,sys; d=json.load(sys.stdin); print('offset' in d['Point'])")
assert_eq "Struct node has no 'offset' key" "False" "$HAS_OFFSET"

# ---------------------------------------------------------------------------
echo "--- Test 7: stdin pipe mode produces identical output ---"
# ---------------------------------------------------------------------------
PIPE_OUT=$("$PARSE" "$TEST_INPUT_H" | "$INSPECT" 2>&1)
if [ "$PIPE_OUT" = "$INSPECT_OUT" ]; then
    echo "  PASS: pipe mode output matches file mode output"
    PASS=$((PASS + 1))
else
    echo "  FAIL: pipe mode output differs from file mode output"
    FAIL=$((FAIL + 1))
fi

# ---------------------------------------------------------------------------
echo "--- Test 8: explicit '-' argument reads from stdin ---"
# ---------------------------------------------------------------------------
DASH_OUT=$(cat "$SCHEMA_FILE" | "$INSPECT" - 2>&1)
if [ "$DASH_OUT" = "$INSPECT_OUT" ]; then
    echo "  PASS: '-' argument output matches file mode output"
    PASS=$((PASS + 1))
else
    echo "  FAIL: '-' argument output differs from file mode output"
    FAIL=$((FAIL + 1))
fi

# ---------------------------------------------------------------------------
echo "--- Test 9: error on non-existent file ---"
# ---------------------------------------------------------------------------
ERR_OUT=$("$INSPECT" /nonexistent/path.json 2>&1 || true)
assert_contains "error message for missing file" "cannot open" "$ERR_OUT"

# ---------------------------------------------------------------------------
echo "--- Test 10: output matches cartographer_dump for same header ---"
# ---------------------------------------------------------------------------
DUMP_OUT=$("$DUMP" "$TEST_INPUT_H" 2>&1)
if [ "$INSPECT_OUT" = "$DUMP_OUT" ]; then
    echo "  PASS: schema_inspect output matches cartographer_dump output"
    PASS=$((PASS + 1))
else
    echo "  FAIL: schema_inspect output differs from cartographer_dump output"
    echo "        dump lines: $(echo "$DUMP_OUT" | wc -l)"
    echo "        inspect lines: $(echo "$INSPECT_OUT" | wc -l)"
    # Show first differing line for diagnosis
    diff <(echo "$DUMP_OUT") <(echo "$INSPECT_OUT") | head -20 || true
    FAIL=$((FAIL + 1))
fi

# ---------------------------------------------------------------------------
echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
[ "$FAIL" -eq 0 ]
