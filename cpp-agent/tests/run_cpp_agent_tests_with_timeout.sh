#!/usr/bin/env bash
set -euo pipefail

BINARY="${1:-./build/cpp-agent/cpp_agent_tests}"
PER_TEST_TIMEOUT="${CPP_AGENT_TEST_TIMEOUT:-20}"
STOP_ON_FAILURE="${STOP_ON_FAILURE:-1}"

if [[ ! -x "$BINARY" ]]; then
  echo "error: test binary not found or not executable: $BINARY" >&2
  exit 2
fi

mapfile -t TESTS < <(
  python - "$BINARY" <<'PY'
import subprocess
import sys
binary = sys.argv[1]
out = subprocess.check_output([binary, '--gtest_list_tests'], text=True)
current = ''
for line in out.splitlines():
    if line.endswith('.'):
        current = line.strip()
    elif line.startswith('  '):
        name = line.strip().split()[0]
        print(current + name)
PY
)

if [[ ${#TESTS[@]} -eq 0 ]]; then
  echo "error: no tests discovered from $BINARY" >&2
  exit 3
fi

failures=0
for test_name in "${TESTS[@]}"; do
  echo "=== RUN $test_name ==="
  set +e
  timeout "$PER_TEST_TIMEOUT" "$BINARY" --gtest_filter="$test_name" --gtest_color=no
  rc=$?
  set -e

  if [[ $rc -eq 0 ]]; then
    echo "=== PASS $test_name ==="
    continue
  fi

  failures=$((failures + 1))
  if [[ $rc -eq 124 ]]; then
    echo "=== TIMEOUT $test_name (${PER_TEST_TIMEOUT}s) ===" >&2
  else
    echo "=== FAIL $test_name (rc=$rc) ===" >&2
  fi

  if [[ "$STOP_ON_FAILURE" != "0" ]]; then
    exit $rc
  fi
done

if [[ $failures -ne 0 ]]; then
  exit 1
fi

echo "All tests passed under per-test timeout validation."
