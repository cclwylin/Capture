#!/usr/bin/env bash
set -euo pipefail
cd /workspace/Capture
source ./env.sh
mkdir -p /workspace/verification-logs
if ! python3 tools/check_display.py; then
  setsid /workspace/sysroot/usr/bin/Xvfb :99 -screen 0 1280x720x24 -ac \
    </dev/null >/workspace/verification-logs/xvfb.log 2>&1 &
  for attempt in {1..40}; do
    if python3 tools/check_display.py; then break; fi
    sleep 0.1
  done
fi
python3 tools/check_display.py
/workspace/vulkan-xcb-build/bin/triangle --benchmark --benchwarmup 0 \
  --benchmarkframes 3 --width 640 --height 360
