#!/bin/bash
# Test script for event_server command injection

cd /home/phdyex/my-repos/cygwin/bash/examples/loadables

# Clean up any existing socket
rm -f /tmp/bash_test.sock /tmp/bash_test.sock.cmd

echo "=== Loading event_server ==="
enable -f ./event_server event_server

echo "=== Configuring ==="
event_server socket /tmp/bash_test.sock
event_server auth secret123
event_server allow '*'
event_server start
event_server inject enable

echo "=== Running test loop (5 ticks) ==="
echo "=== Waiting for command injection via socket ==="

# Send a test command in the background after 2 seconds
(
  sleep 2
  echo '{"type": "execute", "id": "test1", "auth": "secret123", "command": "echo INJECTION_WORKED"}' | \
    socat - UNIX-SENDTO:/tmp/bash_test.sock.cmd
) &

# Run a simple loop
for i in 1 2 3 4 5; do
  echo "tick $i"
  sleep 1
done

echo "=== Test complete ==="
event_server status
