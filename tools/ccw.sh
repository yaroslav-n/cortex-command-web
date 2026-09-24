#!/bin/sh
# cc.sh with a deadline. A page whose main thread is blocked outside JavaScript
# never answers DevTools, and cc.sh then waits forever; this gives up instead.
# Usage: ccw.sh <seconds> <cc.sh arguments...>
limit=$1; shift
"$(dirname "$0")/cc.sh" "$@" &
pid=$!
elapsed=0
while kill -0 "$pid" 2>/dev/null; do
  if [ "$elapsed" -ge "$limit" ]; then
    kill "$pid" 2>/dev/null
    echo "ccw: no answer after ${limit}s (page main thread blocked?)" >&2
    exit 124
  fi
  sleep 1
  elapsed=$((elapsed + 1))
done
wait "$pid"
