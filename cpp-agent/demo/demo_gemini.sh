#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT_DIR"

SOCK="/tmp/agentc.sock"
LOG="/tmp/agentc_demo.log"
PIDFILE="/tmp/agentc_demo.pid"

cleanup() {
  if [[ -f "$PIDFILE" ]]; then
    kill "$(cat "$PIDFILE")" 2>/dev/null || true
    rm -f "$PIDFILE"
  fi
  rm -f "$SOCK"
}
trap cleanup EXIT

if [[ -z "${GEMINI_API_KEY:-}" && -z "${GOOGLE_API_KEY:-}" ]]; then
  echo "Set GEMINI_API_KEY or GOOGLE_API_KEY first."
  exit 1
fi

echo "--- Building cpp-agent ---"
cmake --build build --target cpp-agent >/dev/null

echo "--- Cleaning stale processes ---"
pkill -9 -f '/cpp-agent/cpp-agent' 2>/dev/null || true
rm -f "$SOCK" "$LOG" "$PIDFILE"

echo "--- Starting AgentC daemon ---"
nohup ./build/cpp-agent/cpp-agent >"$LOG" 2>&1 & echo $! > "$PIDFILE"

for _ in $(seq 1 40); do
  [[ -S "$SOCK" ]] && break
  sleep 0.25
done

if [[ ! -S "$SOCK" ]]; then
  echo "Agent socket did not appear."
  echo "--- daemon log ---"
  cat "$LOG" || true
  exit 1
fi

echo "--- Running persistent client session ---"
python3 - <<'PY'
import socket
import time

SOCK = "/tmp/agentc.sock"
PROMPTS = [
    "Print Hello world!",
    "Now answer: what did you just print?"
]


def recv_until_prompt(sock):
    data = b""
    while b"\n> " not in data:
        chunk = sock.recv(4096)
        if not chunk:
            break
        data += chunk
    return data.decode("utf-8", errors="replace")

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(SOCK)

print("--- banner ---")
print(recv_until_prompt(sock), end="")

for prompt in PROMPTS:
    print(f"--- prompt ---\n{prompt}")
    sock.sendall((prompt + "\n").encode("utf-8"))
    time.sleep(0.2)
    print("--- reply ---")
    print(recv_until_prompt(sock), end="")

sock.sendall(b"exit\n")
sock.close()
PY

echo
echo "--- Daemon log ---"
cat "$LOG" || true

echo
echo "--- Shutting down daemon ---"
kill "$(cat "$PIDFILE")" 2>/dev/null || true
wait "$(cat "$PIDFILE")" 2>/dev/null || true
rm -f "$PIDFILE"
