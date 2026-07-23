#!/usr/bin/env bash
# Launches both parties of sh2pc_demo against each other over localhost.
#
# Usage: ./run.sh <alice-value> <bob-value>
# Example: ./run.sh 7 5   ->  sum=12, max=7

set -euo pipefail

ALICE_VAL="${1:-7}"
BOB_VAL="${2:-5}"
BIN="$(dirname "$0")/build/sh2pc_demo"

if [[ ! -x "$BIN" ]]; then
    echo "error: $BIN not found -- build first (see README.md)" >&2
    exit 1
fi

"$BIN" 1 "$ALICE_VAL" &
ALICE_PID=$!

# Give Alice a moment to bind/listen before Bob connects.
sleep 0.3

"$BIN" 2 "$BOB_VAL" &
BOB_PID=$!

wait "$ALICE_PID" "$BOB_PID"
