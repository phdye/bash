#!/bin/bash
# run_integration.sh -- Start bash-server and run integration tests
#
# Usage: ./run_integration.sh [bash-server-path]
#
# Starts a bash-server instance, runs test_session against it,
# and cleans up on exit.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SERVER="${1:-$SCRIPT_DIR/../bash-server.exe}"
TEST_BIN="$SCRIPT_DIR/test_session.exe"

# Validate binaries exist
if [ ! -x "$SERVER" ]; then
    echo "ERROR: bash-server not found at: $SERVER" >&2
    echo "Build it first: cd bash-server && make" >&2
    exit 2
fi

if [ ! -x "$TEST_BIN" ]; then
    echo "ERROR: test_session not found at: $TEST_BIN" >&2
    echo "Build it first: cd bash-server/tests && make test_session.exe" >&2
    exit 2
fi

# Create temp directory for socket
TMPDIR=$(mktemp -d /tmp/bash-server-test-XXXXXX)
SOCK="$TMPDIR/sock"
TOKEN_FILE="$SOCK.token"
PID_FILE="$TMPDIR/server.pid"

# Cleanup on exit
cleanup() {
    if [ -f "$PID_FILE" ]; then
        local pid=$(cat "$PID_FILE" 2>/dev/null)
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
            # Wait up to 5 seconds for graceful shutdown
            for i in $(seq 1 50); do
                kill -0 "$pid" 2>/dev/null || break
                sleep 0.1
            done
            # Force kill if still alive
            kill -0 "$pid" 2>/dev/null && kill -9 "$pid" 2>/dev/null || true
        fi
    fi
    rm -rf "$TMPDIR"
}
trap cleanup EXIT

# Start bash-server
echo "Starting bash-server..."
"$SERVER" --socket "$SOCK" --pidfile "$PID_FILE" --verbose &
SERVER_PID=$!

# Wait for socket to appear (up to 10 seconds)
for i in $(seq 1 100); do
    if [ -S "$SOCK" ] && [ -f "$TOKEN_FILE" ]; then
        break
    fi
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo "ERROR: bash-server exited prematurely" >&2
        exit 1
    fi
    sleep 0.1
done

if [ ! -S "$SOCK" ]; then
    echo "ERROR: socket not created after 10s" >&2
    exit 1
fi

# Read auth token
TOKEN=$(cat "$TOKEN_FILE" | tr -d '\n\r')
echo "Server ready: socket=$SOCK token=${TOKEN:0:8}..."

# Run integration tests
echo ""
"$TEST_BIN" "$SOCK" "$TOKEN"
rc=$?

echo ""
if [ $rc -eq 0 ]; then
    echo "Integration tests PASSED"
else
    echo "Integration tests FAILED (exit code $rc)"
fi

exit $rc
