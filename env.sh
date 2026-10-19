#!/usr/bin/env bash
# Source in a shell dedicated to this capture/replay workflow.
export PATH="/workspace/mesa-python/bin:/workspace/sysroot/usr/bin:$PATH"
export PYTHONPATH="/workspace/mesa-python${PYTHONPATH:+:$PYTHONPATH}"
export LD_LIBRARY_PATH="/workspace/renderdoc-build/lib:/workspace/sysroot/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export VK_DRIVER_FILES=/workspace/lavapipe-capture-replay.json
export LVP_CAPTURE_REPLAY=1
export LP_NUM_THREADS=2
export DISPLAY=:99
export BISON_PKGDATADIR=/workspace/sysroot/usr/share/bison
unset LD_PRELOAD VK_INSTANCE_LAYERS VK_LAYER_PATH
