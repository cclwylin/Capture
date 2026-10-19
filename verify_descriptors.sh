#!/usr/bin/env bash
set -euo pipefail
cd /workspace/Capture
source ./descriptor-env.sh
mkdir -p /workspace/descriptor-replay/logs
addresses=/workspace/descriptor-replay/verified_addresses.txt
/workspace/bda_address_probe capture "$addresses"
/workspace/bda_address_probe replay "$addresses"
if env -u LVP_CAPTURE_REPLAY /workspace/bda_address_probe capture \
    /workspace/descriptor-replay/disabled_addresses.txt \
    >/workspace/descriptor-replay/logs/bda-disabled.log 2>&1; then
  echo 'ERROR: capture/replay enabled without driver opt-in' >&2
  exit 1
fi
rg -q 'unavailable without opt-in' /workspace/descriptor-replay/logs/bda-disabled.log
for name in descriptorbuffer descriptorheap descriptorheapuntyped; do
  renderdoccmd replay --export-png "/workspace/verified-batch/png/$name.png" \
    "/workspace/verified-batch/captures/${name}_frame1.rdc"
done
if env -u RDOC_LVP_DESCRIPTOR_REPLAY renderdoccmd replay --verify \
    /workspace/verified-batch/captures/descriptorheap_frame1.rdc \
    >/workspace/descriptor-replay/logs/descriptor-mode-disabled.log 2>&1; then
  echo 'ERROR: heap replay accepted without descriptor regeneration' >&2
  exit 1
fi
rg -q 'requires the experimental lavapipe' /workspace/descriptor-replay/logs/descriptor-mode-disabled.log
python3 - <<'PY'
from pathlib import Path
from PIL import Image, ImageChops
root = Path('/workspace/verified-batch')
for name in ('descriptorbuffer', 'descriptorheap', 'descriptorheapuntyped'):
    actual = Image.open(root / 'png' / (name + '.png')).convert('RGB')
    reference = Image.open(root / 'reference' / (name + '.png')).convert('RGB')
    assert actual.size == reference.size == (640, 360), name
    assert ImageChops.difference(actual, reference).getbbox() is None, name
    print('PASS: independent replay, pixel-exact PNG:', name)
PY
