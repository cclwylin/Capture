#!/usr/bin/env python3
"""Capture whole presented frames, then replay in an independent lavapipe process."""
import concurrent.futures
import json
import os
from pathlib import Path
import re
import subprocess
import shutil
import time

ROOT = Path('/workspace')
OUT = ROOT / 'verified-batch'
for folder in ('captures', 'png', 'reference', 'logs'):
    (OUT / folder).mkdir(parents=True, exist_ok=True)
BASE = dict(os.environ)
for key in ('LD_PRELOAD', 'VK_INSTANCE_LAYERS', 'VK_LAYER_PATH', 'VK_ICD_FILENAMES',
            'VK_LOADER_LAYERS_DISABLE', 'RDOC_LVP_DESCRIPTOR_REPLAY'):
    BASE.pop(key, None)
BASE.update(DISPLAY=':99', VK_DRIVER_FILES=str(ROOT / 'lavapipe-capture-replay.json'),
            LVP_CAPTURE_REPLAY='1', LP_NUM_THREADS='2',
            LD_LIBRARY_PATH=f'{ROOT}/renderdoc-build/lib:{ROOT}/sysroot/usr/lib/x86_64-linux-gnu')
CMD = ROOT / 'renderdoc-build/bin/renderdoccmd'
DESCRIPTOR_EXAMPLES = {'descriptorbuffer', 'descriptorheap', 'descriptorheapuntyped'}

def replay_environment(name):
    env = dict(BASE)
    if name in DESCRIPTOR_EXAMPLES:
        env.update(VK_DRIVER_FILES=str(ROOT / 'Capture/lavapipe-descriptor-replay.json'),
                   RDOC_LVP_DESCRIPTOR_REPLAY='1')
    return env

def run(command, env, log):
    started = time.monotonic()
    with log.open('w') as stream:
        try:
            p = subprocess.run(command, env=env, cwd=ROOT / 'vulkan', stdout=stream,
                               stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=120)
            return p.returncode, round(time.monotonic() - started, 3)
        except subprocess.TimeoutExpired:
            stream.write('\nTIMED OUT after 120 seconds\n')
            return 124, round(time.monotonic() - started, 3)

def process(name):
    result = dict(example=name)
    base = replay_environment(name)
    result['driver_profile'] = ('Mesa 26.2.5 descriptor regeneration' if name in DESCRIPTOR_EXAMPLES
                                else 'Mesa 25.0.7 capture/replay')
    headless = name in ('computeheadless', 'renderheadless')
    env = dict(base, VK_LAYER_PATH=str(ROOT), VK_INSTANCE_LAYERS='VK_LAYER_RENDERDOC_Capture',
               RDOC_LOSSLESS_REFERENCE='1',
               RDOC_CAPTURE_PATH=str(OUT / 'captures' / name))
    if not headless:
        env['LD_PRELOAD'] = str(ROOT / 'libframe_capture_hook.so')
    if name == 'computecullandlod':
        # Its asynchronous compute uses a per-frame UBO before the graphics
        # path updates it. Wait until every buffered UBO has been initialized.
        env['RDOC_CAPTURE_AFTER'] = '4'
    log = OUT / 'logs' / f'{name}.capture.log'
    files = sorted((OUT / 'captures').glob(name + '_frame*.rdc'))
    old_capture = OUT / 'captures' / (name + '_capture.rdc')
    before = {p: (p.stat().st_mtime_ns, p.stat().st_ctime_ns)
              for p in files + ([old_capture] if old_capture.exists() else [])}
    executed_capture = False
    if not REFRESH and len(files) == 1 and results.get(name, {}).get('capture_exit') == 0:
        rc, duration = 0, results[name]['capture_seconds']
    else:
        executed_capture = True
        command = [str(ROOT / 'headless-capture-build' / name)] if headless else [
            str(ROOT / 'vulkan-xcb-build/bin' / name), '--benchmark',
            '--benchwarmup', '0', '--benchmarkframes',
            '6' if name == 'computecullandlod' else '3', '--width', '640', '--height', '360']
        rc, duration = run(command, env, log)
    result.update(capture_exit=rc, capture_seconds=duration)
    files = sorted((OUT / 'captures').glob(name + '_frame*.rdc'))
    capture = OUT / 'captures' / (name + '_capture.rdc')
    if headless and capture.exists():
        files.append(capture)
    fresh = len(files) == 1 and (not executed_capture or
            before.get(files[0]) != (files[0].stat().st_mtime_ns, files[0].stat().st_ctime_ns))
    if rc != 0 or len(files) != 1 or not fresh:
        result['status'] = 'CAPTURE_FAILED'
        result['detail'] = log.read_text(errors='replace').strip()[:1600]
        if rc == 0 and not fresh:
            result['detail'] += '\nNo fresh capture file was produced by this run.'
        return result
    result['capture'] = str(files[0])
    png = OUT / 'png' / f'{name}.png'
    log = OUT / 'logs' / f'{name}.replay.log'
    command = [str(CMD), 'replay', '--verify', str(files[0])] if name == 'computeheadless' else [
        str(CMD), 'replay', '--export-png', str(png), str(files[0])]
    rc, duration = run(command, base, log)
    result.update(replay_exit=rc, replay_seconds=duration)
    text = log.read_text(errors='replace')
    if name == 'computeheadless' and rc == 0:
        result['status'] = 'REPLAYED_NO_IMAGE'
        result['detail'] = 'Compute-only example writes a Fibonacci buffer; it has no colour image.'
        return result
    if rc != 0 or not png.exists():
        result['status'] = 'REPLAY_FAILED'
        result['detail'] = text.strip()[:1600]
        return result
    from PIL import Image, ImageChops, ImageStat
    image = Image.open(png)
    image.verify()
    image = Image.open(png).convert('RGB')
    result.update(status='REPLAYED_PNG', png=str(png), dimensions=list(image.size),
                  channel_ranges=image.getextrema())
    matches = re.search(r'\((\d+) draw/dispatch actions, (\d+) ray dispatches\)', text)
    if matches:
        result.update(actions=int(matches[1]), ray_dispatches=int(matches[2]))
    reference = OUT / 'reference' / f'{name}.png'
    if name == 'renderheadless':
        reference = reference.with_suffix('.ppm')
        shutil.copy2(ROOT / 'vulkan/headless.ppm', reference)
        result['reference_kind'] = 'native_headless_ppm'
        rc = 0
    else:
        rc, _ = run([str(CMD), 'thumb', '--out=' + str(reference), str(files[0])], base,
                    OUT / 'logs' / f'{name}.reference.log')
        result['reference_kind'] = 'capture_lossless_png'
    if rc == 0 and reference.exists():
        ref = Image.open(reference).convert('RGB')
        if ref.size == image.size:
            result['reference_mean_absolute_error'] = ImageStat.Stat(ImageChops.difference(image, ref)).mean
            result['pixel_exact'] = ImageChops.difference(image, ref).getbbox() is None
            if not result['pixel_exact']:
                result['status'] = 'PIXEL_MISMATCH'
                result['detail'] = 'Replay differs from the lossless capture-time reference.'
        else:
            result['status'] = 'REFERENCE_FAILED'
            result['detail'] = 'Reference and replay dimensions differ.'
    else:
        result['status'] = 'REFERENCE_FAILED'
        result['detail'] = 'Capture-time reference unavailable.'
    return result

if __name__ == '__main__':
    import sys
    REFRESH = '--refresh' in sys.argv
    names = [arg for arg in sys.argv[1:] if arg != '--refresh'] or sorted(
        p.name for p in (ROOT / 'vulkan/examples').iterdir() if p.is_dir())
    manifest = OUT / 'results.json'
    results = {r['example']: r for r in json.loads(manifest.read_text())} if manifest.exists() else {}
    names = [name for name in names if REFRESH or results.get(name, {}).get('status') not in
             ('REPLAYED_PNG', 'REPLAYED_NO_IMAGE')]
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        futures = [pool.submit(process, name) for name in names]
        for future in concurrent.futures.as_completed(futures):
            result = future.result()
            results[result['example']] = result
            print(result['example'], result['status'], flush=True)
            manifest.write_text(json.dumps([results[name] for name in sorted(results)], indent=2) + '\n')
    from collections import Counter
    print(dict(Counter(r['status'] for r in results.values())), flush=True)
