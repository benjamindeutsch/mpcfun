#!/usr/bin/env bash
# Launches both parties of sh2pc_demo against each other over localhost.
#
# Usage: ./run.sh <alice-dnf-file> <bob-dnf-file>
# Example: ./run.sh   -> uses the bundled alice.dnf / bob.dnf,
#                        giving total_weight=20, intervals=[0,8,12,16]

set -euo pipefail

DIR="$(dirname "$0")"
ALICE_FILE="${1:-$DIR/alice.dnf}"
BOB_FILE="${2:-$DIR/bob.dnf}"
BIN="$DIR/build/sh2pc_demo"

if [[ ! -x "$BIN" ]]; then
    echo "error: $BIN not found -- build first (see README.md)" >&2
    exit 1
fi

"$BIN" 1 "$ALICE_FILE" &
ALICE_PID=$!

# Give Alice a moment to bind/listen before Bob connects.
sleep 0.3

"$BIN" 2 "$BOB_FILE" &
BOB_PID=$!

wait "$ALICE_PID" "$BOB_PID"
