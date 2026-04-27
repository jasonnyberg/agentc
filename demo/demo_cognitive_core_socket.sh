#!/usr/bin/env zsh
# demo_cognitive_core_socket.sh
# Demonstrates Mini-Kanren logic over the Edict-VM Socket IO interface.
# This proves the Pi frontend integration pipeline works.

set -e

# Define paths relative to this script
EDICT=${EDICT:-$(dirname "$0")/../build/edict/edict}
SOCKET_PATH="/tmp/kanren_demo_socket.sock"

# Use absolute paths for the FFI resolver to avoid working-directory issues
# inside the backgrounded VM.
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJECT_ROOT=$(dirname "$SCRIPT_DIR")
KANREN_LIB="${PROJECT_ROOT}/build/kanren/libkanren.so"
KANREN_HDR="${PROJECT_ROOT}/cartographer/tests/kanren_runtime_ffi_poc.h"

echo "=== Cognitive Core Socket Integration Demo ==="
echo "Using interpreter: $EDICT"
echo "Using kanren library: $KANREN_LIB"
echo "Using kanren header: $KANREN_HDR"
echo

echo "--- Starting Edict VM in Socket Mode ---"
"$EDICT" --socket "$SOCKET_PATH" > /dev/null 2>&1 &
VM_PID=$!
sleep 1

echo "--- Sending Logic Setup and Query via Socket ---"
# We use socat to connect to the Unix Domain Socket
cat <<EOF | socat - UNIX-CONNECT:$SOCKET_PATH > kanren_demo_socket.log 2>&1
# 1. Allow FFI Extensions
unsafe_extensions_allow ! pop

# 2. Import the Logic Engine FFI
[$KANREN_LIB] [$KANREN_HDR] resolver.import ! @logicffi
logicffi.agentc_logic_eval_ltv @logic

# 3. Execute a Mini-Kanren Query (finds 'tea', 'cake', 'jam')
{ "fresh": ["q"], "conde": [[["==", "q", "tea"]], [["membero", "q", ["cake", "jam"]]]], "results": ["q"] } logic !
print

# 4. Graceful Shutdown
exit
EOF

echo "--- Response Received ---"
cat kanren_demo_socket.log

echo
echo "--- Cleaning Up ---"
wait $VM_PID 2>/dev/null
rm -f "$SOCKET_PATH" kanren_demo_socket.log
echo "=== Demo complete ==="
