#!/usr/bin/env zsh
set -e
setopt null_glob

DIR="${0:A:h}"
PROJECT_ROOT="$(cd "$DIR/../.." && pwd)"
HOST_BIN="${HOST_BIN:-$PROJECT_ROOT/build/cpp-agent/cpp-agent}"
CONFIG="${CONFIG:-$PROJECT_ROOT/cpp-agent/config/runtime.default.json}"
SOCKET="${SOCKET:-/tmp/agentc_persist_demo.sock}"
STATE_BASE="${STATE_BASE:-/tmp/agentc_persist_demo_state}"
LOG1="${LOG1:-/tmp/agentc_persist_demo_1.log}"
LOG2="${LOG2:-/tmp/agentc_persist_demo_2.log}"
PID1=/tmp/agentc_persist_demo_1.pid
PID2=/tmp/agentc_persist_demo_2.pid

rm -f "$SOCKET" "$STATE_BASE"* "$LOG1" "$LOG2" "$PID1" "$PID2"

echo "=== Demo: runtime-backed host persistence across restart ==="
echo "Host:       $HOST_BIN"
echo "Config:     $CONFIG"
echo "Socket:     $SOCKET"
echo "State base: $STATE_BASE"
echo

"$HOST_BIN" --config "$CONFIG" --socket "$SOCKET" --state-base "$STATE_BASE" >"$LOG1" 2>&1 & echo $! > "$PID1"
sleep 1
python3 - <<PY
import socket, time
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect('$SOCKET')
print(sock.recv(4096).decode(), end='')
sock.sendall(b'Print Hello world!\n')
time.sleep(4)
print(sock.recv(65536).decode(), end='')
sock.sendall(b'shutdown-agent\n')
time.sleep(0.2)
print(sock.recv(65536).decode(), end='')
sock.close()
PY
sleep 1

"$HOST_BIN" --config "$CONFIG" --socket "$SOCKET" --state-base "$STATE_BASE" >"$LOG2" 2>&1 & echo $! > "$PID2"
sleep 1
python3 - <<PY
import socket, time
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect('$SOCKET')
print(sock.recv(4096).decode(), end='')
sock.sendall(b'What did you just print?\n')
time.sleep(4)
print(sock.recv(65536).decode(), end='')
sock.sendall(b'shutdown-agent\n')
time.sleep(0.2)
print(sock.recv(65536).decode(), end='')
sock.close()
PY

echo
echo "--- first host log ---"
cat "$LOG1"
echo
echo "--- second host log ---"
cat "$LOG2"
