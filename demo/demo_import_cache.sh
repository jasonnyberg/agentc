#!/bin/bash
set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$DIR")"

echo "Running Edict script to test import JSON cache..."
echo "------------------------------------------------"
time "$PROJECT_ROOT/build/edict/edict" "$DIR/test_import_cache.ed"
echo "------------------------------------------------"
echo "Testing cache validation (touching header)..."
echo "------------------------------------------------"
touch "$PROJECT_ROOT/cartographer/tests/libagentmath_poc.h"
time "$PROJECT_ROOT/build/edict/edict" "$DIR/test_import_cache.ed"
echo "------------------------------------------------"
echo "Running a second time to ensure cache hit is fast..."
echo "------------------------------------------------"
time "$PROJECT_ROOT/build/edict/edict" "$DIR/test_import_cache.ed"
echo "Done."