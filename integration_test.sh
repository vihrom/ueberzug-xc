#!/usr/bin/env bash
# Usage: integration_test.sh [valgrind|asan]
set -euo pipefail

MODE="${1:-valgrind}"
BINARY="${BINARY:-./ueberzug-xc}"
TEST_DIR="/tmp/ueberzug_test_img"
PIPE="/tmp/ueberzug_pipe_test"
LOG="/tmp/ueberzug-xc-$MODE.log"
PID=""

cleanup() {
    exec 3>&- 2>/dev/null || true
    if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
        kill -TERM "$PID" 2>/dev/null || true
        wait "$PID" 2>/dev/null || true
    fi
    rm -f "$PIPE"
}
trap cleanup EXIT

if command -v magick >/dev/null; then IM="magick"; else IM="convert"; fi
[ -x "$BINARY" ] || { echo "$BINARY not built"; exit 1; }

case "$MODE" in
    valgrind)
        command -v valgrind >/dev/null || { echo "valgrind not found"; exit 1; }
        ;;
    asan)
        # Build with: make clean && make CFLAGS="-g -O1 -fsanitize=address,undefined -fno-sanitize=object-size" LDFLAGS="-fsanitize=address,undefined"
        export ASAN_OPTIONS="detect_leaks=1"
        export UBSAN_OPTIONS="print_stacktrace=1"
        ;;
    *) echo "Unknown mode: $MODE (use valgrind or asan)"; exit 1 ;;
esac

mkdir -p "$TEST_DIR"
"$IM" -size 800x600 xc:blue "$TEST_DIR/blue.png"
"$IM" -size 1200x800 xc:red "$TEST_DIR/red.jpg"
"$IM" -size 400x400 xc:green "$TEST_DIR/green.png"

send_scenario() {
    echo "{\"action\":\"add\",\"identifier\":\"preview\",\"path\":\"$TEST_DIR/blue.png\",\"x\":2,\"y\":2,\"width\":40,\"height\":20}"
    sleep 1.5
    for i in {1..50}; do
        echo "{\"action\":\"add\",\"identifier\":\"stress_$i\",\"path\":\"$TEST_DIR/green.png\",\"x\":$i,\"y\":$i,\"width\":10,\"height\":10}"
        echo "{\"action\":\"remove\",\"identifier\":\"stress_$i\"}"
    done
    sleep 3.0
    PADDING=$(printf ' %.0s' {1..10000})
    echo "{\"action\":\"add\",${PADDING}\"identifier\":\"preview\",\"path\":\"$TEST_DIR/red.jpg\",\"x\":5,\"y\":5,\"width\":30,\"height\":15}"
    sleep 1.5
    for i in {1..20}; do
        echo "{\"action\":\"add\",\"identifier\":\"toggle\",\"path\":\"$TEST_DIR/blue.png\",\"x\":0,\"y\":0,\"width\":50,\"height\":25}"
        echo "{\"action\":\"add\",\"identifier\":\"toggle\",\"path\":\"$TEST_DIR/red.jpg\",\"x\":0,\"y\":0,\"width\":50,\"height\":25}"
    done
    sleep 3.0
    echo "{\"action\":\"remove\",\"identifier\":\"preview\"}"
    echo "{\"action\":\"remove\",\"identifier\":\"toggle\"}"
    sleep 1.5
}

rm -f "$PIPE" "$LOG"
echo "=== Running ueberzug-xc ($MODE) ==="

if [ "$MODE" = "valgrind" ]; then
    mkfifo "$PIPE"
    exec 3<> "$PIPE"
    valgrind --leak-check=full --show-leak-kinds=all --log-file="$LOG" \
             "$BINARY" <&3 >/dev/null &
    PID=$!
    sleep 2.0
    kill -0 "$PID" 2>/dev/null || { echo "ERROR: binary died at start"; exit 1; }
    send_scenario >&3
    cleanup
else
    # EOF at the end lets the binary exit normally, so LSan runs its check
    send_scenario | "$BINARY" 2>"$LOG" >/dev/null || true
fi

[ -f "$LOG" ] || { echo "FAIL: no log produced"; exit 1; }
echo "=== Report ($LOG) ==="
cat "$LOG"
echo "======================="

if grep -Eq "Invalid (read|write)|uninitialised|ERROR SUMMARY: [1-9]|(definitely|indirectly|possibly) lost: [1-9]|ERROR: AddressSanitizer|LeakSanitizer|runtime error:" "$LOG"; then
    echo "FAIL ($MODE): memory errors, leaks or sanitizer reports"
    exit 1
fi

echo "PASS ($MODE): no errors"
