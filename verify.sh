#!/usr/bin/env bash
set -euo pipefail
cd /workspace/Capture
source ./env.sh
mkdir -p /workspace/verification-logs
./start.sh
address_file=/workspace/verification-logs/verified_addresses.txt
/workspace/capture_replay_probe capture "$address_file" /workspace/noop.rgen.spv
/workspace/capture_replay_probe replay "$address_file" /workspace/noop.rgen.spv

# The feature must remain unavailable by default; this is an experimental path.
if env -u LVP_CAPTURE_REPLAY /workspace/capture_replay_probe capture \
  /workspace/verification-logs/disabled_addresses.txt /workspace/noop.rgen.spv \
  >/workspace/verification-logs/disabled_probe.log 2>&1; then
  echo 'ERROR: capture replay unexpectedly enabled without opt-in' >&2
  exit 1
fi
rg -q 'capture replay features unavailable' /workspace/verification-logs/disabled_probe.log

# Reject the earlier initialization-only captures. A loaded file is insufficient.
if /workspace/renderdoc-build/bin/renderdoccmd replay --verify \
  /workspace/captures/bloom_capture.rdc >/workspace/verification-logs/initialization_rejection.log 2>&1; then
  echo 'ERROR: initialization-only capture incorrectly accepted' >&2
  exit 1
fi
rg -q 'contains no draw' /workspace/verification-logs/initialization_rejection.log

for name in rayquery raytracingbasic raytracingcallable raytracinggltf \
  raytracingintersection raytracingpositionfetch raytracingreflections \
  raytracingsbtdata raytracingshadows raytracingtextures; do
  /workspace/renderdoc-build/bin/renderdoccmd replay --export-png \
    "/workspace/verified-batch/png/$name.png" \
    "/workspace/verified-batch/captures/${name}_frame1.rdc"
done
python3 - <<'PY'
from PIL import Image, ImageChops
from pathlib import Path
root = Path('/workspace/verified-batch')
names = ['rayquery'] + sorted(p.stem for p in (root / 'png').glob('raytracing*.png'))
assert len(names) == 10
for name in names:
    actual = Image.open(root / 'png' / (name + '.png')).convert('RGB')
    reference = Image.open(root / 'reference' / (name + '.png')).convert('RGB')
    assert actual.size == reference.size == (640, 360), name
    assert ImageChops.difference(actual, reference).getbbox() is None, name
    print('PASS: pixel-exact replay:', name)
PY
