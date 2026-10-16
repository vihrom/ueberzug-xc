#!/usr/bin/env bash
set -e

BINARY="./ueberzug-xc"
TEST_DIR="/tmp/ueberzug_test_img"
LOG_FILE="/tmp/ueberzug_cache/ueberzug-xc.log"
PIPE="/tmp/ueberzug_pipe_test"

mkdir -p "$TEST_DIR"
# Clear stale logs from previous test runs
mkdir -p "$(dirname "$LOG_FILE")"
rm -f "$LOG_FILE"

# Generate test images
if command -v convert &> /dev/null; then
    convert -size 800x600 xc:blue "$TEST_DIR/blue.png" 2>/dev/null || true
    convert -size 1200x800 xc:red "$TEST_DIR/red.jpg" 2>/dev/null || true
    convert -size 400x400 xc:green "$TEST_DIR/green.png" 2>/dev/null || true
fi

echo "=== Starting Advanced Stress Test ==="

rm -f "$PIPE"
mkfifo "$PIPE"

# 1. Start binary and redirect its stdout/stderr to the log file
$BINARY < "$PIPE" > "$LOG_FILE" 2>&1 &
BINARY_PID=$!

# 2. Keep PIPE open by attaching FD 3 to prevent blocking
exec 3> "$PIPE"

# Cleanup on exit
cleanup() {
    exec 3>&- 2>/dev/null || true
    kill $BINARY_PID 2>/dev/null || true
    rm -f "$PIPE"
}
trap cleanup EXIT

sleep 0.2

# Check if the binary is still running
if ! kill -0 $BINARY_PID 2>/dev/null; then
    echo "ERROR: $BINARY died immediately after start!"
    exit 1
fi

echo "[1/7] Sending baseline ADD command..."
echo "{\"action\":\"add\",\"identifier\":\"preview\",\"path\":\"$TEST_DIR/blue.png\",\"x\":2,\"y\":2,\"width\":40,\"height\":20}" >&3
sleep 0.5

# --- STRESS TESTS START HERE ---

echo "[2/7] STRESS: Rapid-fire loop (100 sequential operations)..."
# Bombard the binary with rapid state updates to check X11 and buffer handling
for i in {1..50}; do
    echo "{\"action\":\"add\",\"identifier\":\"stress_$i\",\"path\":\"$TEST_DIR/green.png\",\"x\":$i,\"y\":$i,\"width\":10,\"height\":10}" >&3
    echo "{\"action\":\"remove\",\"identifier\":\"stress_$i\"}" >&3
done
echo "-> Done sending 100 rapid commands."
sleep 1.0

echo "[3/7] STRESS: Big payload / Buffer spam (10KB chunk)..."
# Create a massive valid JSON with huge amount of padding spaces to test input buffer size
SPAM_PADDING=$(printf ' %.0s' {1..10000})
echo "{\"action\":\"add\",${SPAM_PADDING}\"identifier\":\"preview\",\"path\":\"$TEST_DIR/red.jpg\",\"x\":5,\"y\":5,\"width\":30,\"height\":15}" >&3
sleep 0.5

echo "[4/7] STRESS: Rapid toggling of a single identifier..."
for i in {1..20}; do
    echo "{\"action\":\"add\",\"identifier\":\"toggle\",\"path\":\"$TEST_DIR/blue.png\",\"x\":0,\"y\":0,\"width\":50,\"height\":25}" >&3
    echo "{\"action\":\"add\",\"identifier\":\"toggle\",\"path\":\"$TEST_DIR/red.jpg\",\"x\":0,\"y\":0,\"width\":50,\"height\":25}" >&3
done
sleep 1.0

# Check if binary survived the stress sequence
if ! kill -0 $BINARY_PID 2>/dev/null; then
    echo "❌ ERROR: $BINARY crashed during the stress tests!"
    exit 1
fi
echo "-> Success: Binary survived all stress scenarios."

# --- STRESS TESTS END HERE ---

echo "[5/7] Cleanup: Hiding remaining elements..."
echo "{\"action\":\"remove\",\"identifier\":\"preview\"}" >&3
echo "{\"action\":\"remove\",\"identifier\":\"toggle\"}" >&3
sleep 0.5

echo "[6/7] Done!"

echo "=== Test Completed! Captured Logs: ==="
if [ -f "$LOG_FILE" ]; then
    cat "$LOG_FILE"
    echo "======================================"
    
    # Check for crashes, panics or sanitizers output
    if grep -iq "crash\|panic\|segmentation\|addresssanitizer\|leak" "$LOG_FILE"; then
        echo "CRITICAL: Found crash, panic, or memory leak entries in the log!"
        exit 1
    else
        echo "SUCCESS: No critical errors found in the log."
    fi
else
    echo "WARNING: Log file not found at $LOG_FILE"
fi
