#!/bin/bash

# Find paths
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
BASE_DIR="$(dirname "$SCRIPT_DIR")"
EDICT_BIN="$BASE_DIR/build/edict/edict"
LIB_PATH="$BASE_DIR/build/cartographer/libagentmath_poc.so"
HEADER_PATH="$BASE_DIR/cartographer/tests/libagentmath_poc.h"

# Ensure binary exists
if [ ! -f "$EDICT_BIN" ]; then
    echo "Error: edict binary not found. Please run 'make' in build directory."
    exit 1
fi

echo "--- AgentC FFI Demonstration ---"
echo "Library: $LIB_PATH"
echo "Header:  $HEADER_PATH"
echo ""

# Edict command:
# 1. Load the shared library
# 2. Map the header into 'defs'
# 3. FFI calculation: [123] [456] defs.add eval
# 4. Native calculation: 123 456 + (not yet implemented in VM, use literal comparison)
# Actually, since we removed native math, we just compare against the expected value 579.

EDICT_CODE="[$LIB_PATH] load [$HEADER_PATH] map @defs [123] [456] defs.add! print"

echo "Executing Edict code:"
echo "$EDICT_CODE"
echo ""

"$EDICT_BIN" -e "$EDICT_CODE"

echo ""
echo "Expected Result: 579"
