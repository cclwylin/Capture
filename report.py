#!/usr/bin/env python3
"""Validate saved artifacts and generate an auditable per-example report."""
from collections import Counter
import csv
import json
from pathlib import Path
import shutil
from PIL import Image, ImageChops, ImageStat

ROOT = Path('/workspace')
OUT = ROOT / 'verified-batch'
results = json.loads((OUT / 'results.json').read_text())
assert len(results) == 99 and len({r['example'] for r in results}) == 99
blockers = {
    'conservativeraster': 'Native driver lacks VK_EXT_conservative_rasterization.',
    'descriptorbuffer': 'descriptorBufferCaptureReplay=false; RenderDoc filters the native extension.',
    'descriptorheap': 'Native driver lacks VK_EXT_descriptor_heap.',
    'descriptorheapuntyped': 'Native driver lacks descriptor heap and VK_KHR_shader_untyped_pointers.',
    'fragmentshaderbarycentrics': 'Native driver lacks VK_KHR_fragment_shader_barycentric.',
    'variablerateshading': 'Native driver lacks VK_KHR_fragment_shading_rate.',
}
for result in results:
    name = result['example']
    if name in blockers:
        assert result['status'] == 'CAPTURE_FAILED' and result['capture_exit'] != 0
        result['failure_reason'] = blockers[name]
        continue
    assert result['capture_exit'] == result['replay_exit'] == 0, name
    assert Path(result['capture']).is_file(), name
    if name == 'computeheadless':
        assert result['status'] == 'REPLAYED_NO_IMAGE'
        continue
    assert result['status'] == 'REPLAYED_PNG', name
    actual = Image.open(result['png']).convert('RGB')
    reference = OUT / 'reference' / (name + '.png')
    if name == 'renderheadless':
        reference = reference.with_suffix('.ppm')
        shutil.copy2(ROOT / 'vulkan/headless.ppm', reference)
    original = Image.open(reference).convert('RGB')
    assert actual.size == original.size, name
    difference = ImageChops.difference(actual, original)
    assert difference.getbbox() is None, name
    result.update(pixel_exact=True, reference=str(reference),
                  reference_kind='native_headless_ppm' if name == 'renderheadless' else 'capture_lossless_png',
                  reference_mean_absolute_error=ImageStat.Stat(difference).mean)

counts = dict(Counter(r['status'] for r in results))
assert counts == {'REPLAYED_PNG': 92, 'REPLAYED_NO_IMAGE': 1, 'CAPTURE_FAILED': 6}, counts
(OUT / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
with (OUT / 'results.tsv').open('w') as file:
    writer = csv.writer(file, delimiter='\t')
    writer.writerow(['example', 'status', 'capture_exit', 'replay_exit', 'RGB_exact', 'reason'])
    for r in results:
        writer.writerow([r['example'], r['status'], r.get('capture_exit', ''), r.get('replay_exit', ''),
                         r.get('pixel_exact', ''), r.get('failure_reason', r.get('detail', ''))])

lines = ['# Verified lavapipe replay results', '',
         '99 examples attempted; 93 captured and replayed successfully. '
         '92 PNGs match their original RGB pixels exactly; one compute-only example has no image. '
         'Six examples are blocked before capture by the extensions listed below.', '',
         'All nine `raytracing*` examples and `rayquery` passed. Capture and replay run in separate '
         'processes. Every replay explicitly uses the patched Mesa lavapipe ICD with '
         '`LVP_CAPTURE_REPLAY=1`. This is an experimental Linux 64-bit implementation.', '',
         'A PNG in the replay column is produced by executing the capture and reading the final '
         'texture with `SaveTexture`. The separate reference column is the original capture-time '
         'image. RGB pixels match exactly for every exported image.', '',
         '[Source, patches and reproduction instructions](/workspace/Capture/README.md)', '',
         '| Example | Result | Replay PNG | Original reference | Capture |',
         '| --- | --- | --- | --- | --- |']
for r in results:
    link = lambda key, label: f'[{label}]({r[key]})' if key in r else '—'
    result = 'RGB exact' if r.get('pixel_exact') else r.get('failure_reason', r['status'])
    lines.append(f"| {r['example']} | {result} | {link('png', 'PNG')} | "
                 f"{link('reference', 'Reference')} | {link('capture', 'RDC')} |")
(OUT / 'report.md').write_text('\n'.join(lines) + '\n')
for name in ('results.json', 'results.tsv', 'report.md'):
    shutil.copy2(OUT / name, ROOT / 'Capture/results' / name)
print('PASS: 92/92 replay PNGs match original RGB pixels exactly; 93 successful replays; 6 blockers.')
