#!/bin/bash
# demo_gemini.sh

# Start the daemon
./build/cpp-agent/cpp-agent &
DAEMON_PID=$!
sleep 2

# Interact with the agent
echo "How do I sort an array in C++?" | socat - UNIX-CONNECT:/tmp/agentc.sock > response.log

# Output result
echo "--- Agent Response ---"
cat response.log

# Cleanup
kill $DAEMON_PID
rm /tmp/agentc.sock response.log
