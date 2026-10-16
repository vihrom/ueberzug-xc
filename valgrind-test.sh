#!/usr/bin/env bash
set -e

BINARY="./ueberzug-xc"
TEST_DIR="/tmp/ueberzug_test_img"
PIPE="/tmp/ueberzug_pipe_test"

mkdir -p "$TEST_DIR"

if command -v convert &> /dev/null; then
    convert -size 800x600 xc:blue "$TEST_DIR/blue.png" 2>/dev/null || true
    convert -size 1200x800 xc:red "$TEST_DIR/red.jpg" 2>/dev/null || true
    convert -size 400x400 xc:green "$TEST_DIR/green.png" 2>/dev/null || true
fi

echo "=== Starting Advanced Stress Test UNDER VALGRIND ==="

rm -f "$PIPE"
mkfifo "$PIPE"

exec 3<> "$PIPE"

valgrind --leak-check=full \
         --show-leak-kinds=all \
         $BINARY <&3 > /dev/null &
BINARY_PID=$!

cleanup() {
    exec 3>&- 2>/dev/null || true
    kill -15 $BINARY_PID 2>/dev/null || true
    rm -f "$PIPE"
}
trap cleanup EXIT

sleep 2.0

echo "[1/7] Sending baseline ADD command..."
echo "{\"action\":\"add\",\"identifier\":\"preview\",\"path\":\"$TEST_DIR/blue.png\",\"x\":2,\"y\":2,\"width\":40,\"height\":20}" >&3
sleep 1.5

echo "[2/7] STRESS: Rapid-fire loop (100 sequential operations)..."
for i in {1..50}; do
    echo "{\"action\":\"add\",\"identifier\":\"stress_$i\",\"path\":\"$TEST_DIR/green.png\",\"x\":$i,\"y\":$i,\"width\":10,\"height\":10}" >&3
    echo "{\"action\":\"remove\",\"identifier\":\"stress_$i\"}" >&3
done
echo "-> Done sending 100 rapid commands."
sleep 3.0

echo "[3/7] STRESS: Big payload / Buffer spam (10KB chunk)..."
SPAM_PADDING=$(printf ' %.0s' {1..10000})
echo "{\"action\":\"add\",${SPAM_PADDING}\"identifier\":\"preview\",\"path\":\"$TEST_DIR/red.jpg\",\"x\":5,\"y\":5,\"width\":30,\"height\":15}" >&3
sleep 1.5

echo "[4/7] STRESS: Rapid toggling of a single identifier..."
for i in {1..20}; do
    echo "{\"action\":\"add\",\"identifier\":\"toggle\",\"path\":\"$TEST_DIR/blue.png\",\"x\":0,\"y\":0,\"width\":50,\"height\":25}" >&3
    echo "{\"action\":\"add\",\"identifier\":\"toggle\",\"path\":\"$TEST_DIR/red.jpg\",\"x\":0,\"y\":0,\"width\":50,\"height\":25}" >&3
done
sleep 3.0

echo "[5/7] Cleanup: Hiding remaining elements..."
echo "{\"action\":\"remove\",\"identifier\":\"preview\"}" >&3
echo "{\"action\":\"remove\",\"identifier\":\"toggle\"}" >&3
sleep 1.5

echo "[6/7] Done! Closing pipe and letting Valgrind flush logs directly to terminal..."
cleanup

sleep 3.0
echo "=== Test Finished! ==="
