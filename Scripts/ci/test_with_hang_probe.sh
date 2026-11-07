#!/usr/bin/env bash
# Runs a command; if it is still running after $1 seconds, prints stack
# samples of every test process (macOS `sample`, Linux `gdb` if present) and
# kills it. Used in CI to diagnose hangs instead of waiting for the job
# timeout.
set -u
LIMIT="$1"; shift
"$@" &
PID=$!
for ((i = 0; i < LIMIT; i++)); do
  if ! kill -0 "$PID" 2>/dev/null; then
    wait "$PID"
    exit $?
  fi
  sleep 1
done
echo "::error::command still running after ${LIMIT}s, sampling stacks"
for p in $(pgrep -f 'target/release/deps/' || true); do
  echo "===== $(ps -o command= -p "$p" | cut -c1-160) (pid $p)"
  if command -v sample > /dev/null; then
    sample "$p" 3 -mayDie 2>&1 | head -400
  elif command -v gdb > /dev/null; then
    gdb -p "$p" -batch -ex "thread apply all bt" 2>&1 | head -400
  fi
done
pkill -P "$PID" 2>/dev/null
kill "$PID" 2>/dev/null
exit 124
