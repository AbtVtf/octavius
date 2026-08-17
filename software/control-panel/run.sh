#!/bin/bash
# Octavius Robot - Unified Launcher
# Starts Paperclip + Vision server + Assistant in one terminal

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

# Activate venv
source .venv/bin/activate

# Use PyTorch's bundled CUDA libs (not system CUDA 12.0 which conflicts)
NVIDIA_LIBS="$(python3 -c 'import nvidia.cuda_runtime, os; print(os.path.dirname(nvidia.cuda_runtime.__file__) + "/lib")' 2>/dev/null)"
if [ -n "$NVIDIA_LIBS" ]; then
    export LD_LIBRARY_PATH="$NVIDIA_LIBS:${LD_LIBRARY_PATH:-}"
fi

# Cleanup on exit
cleanup() {
    echo ""
    echo "Shutting down Octavius..."
    kill $VISION_PID 2>/dev/null
    wait $VISION_PID 2>/dev/null
    echo "Bye!"
    exit 0
}
trap cleanup INT TERM

# Ensure Paperclip is running (local, no Docker)
echo "=== Checking Paperclip ==="
if curl -s http://127.0.0.1:3100/api/health 2>/dev/null | grep -q '"ok"'; then
    echo "Paperclip already running."
else
    echo "Starting Paperclip..."
    nohup paperclipai run > /tmp/paperclip.log 2>&1 &
    PAPERCLIP_PID=$!
    echo "Waiting for Paperclip API..."
    for i in $(seq 1 30); do
        if curl -s http://127.0.0.1:3100/api/health 2>/dev/null | grep -q '"ok"'; then
            echo "Paperclip ready!"
            break
        fi
        sleep 1
    done
fi

# Start vision server in background
echo ""
echo "=== Starting Vision Server ==="
python vision_server.py &
VISION_PID=$!

# Wait for vision API to be ready
echo "Waiting for vision server..."
for i in $(seq 1 45); do
    if curl -s http://localhost:8090/status > /dev/null 2>&1; then
        echo "Vision server ready!"
        break
    fi
    sleep 1
done

echo ""
echo "=== Starting Assistant ==="
python assistant.py

# If assistant exits, clean up vision server
cleanup
