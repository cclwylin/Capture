#!/usr/bin/env python3
"""Validate existing captures and publish text reports with Release artifact links.

This command does not capture, replay, upload assets, or push Git. Run report.py
first after a new batch. RDCs, PNGs, and reference images stay outside Git.
"""
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil

from PIL import Image, ImageChops

REPO = Path(__file__).resolve().parents[1]
WORKSPACE = REPO.parent
OUT = WORKSPACE / 'verified-batch'
DEST = REPO / 'results'
DESCRIPTORS = {'descriptorbuffer', 'descriptorheap', 'descriptorheapuntyped'}


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def publish(source, relative):
    target = DEST / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    return {'workspace_path': str(source), 'repository_path': str(target.relative_to(REPO)),
            'bytes': target.stat().st_size, 'sha256': sha256(target)}


def main():
    rows = json.loads((OUT / 'results.json').read_text())
    assert len(rows) == len({r['example'] for r in rows}) == 99
    counts = Counter(r['status'] for r in rows)
    assert counts == {'REPLAYED_PNG': 95, 'REPLAYED_NO_IMAGE': 1, 'CAPTURE_FAILED': 3}, counts
    records = []
    release = json.loads((DEST / 'release.json').read_text())
    base = release.get('download_base') if release.get('status') == 'published' else None
    table = ['# Capture / replay 驗證結果', '',
             '99 個範例：96 份有效 RDC、95 張 replay PNG、1 個純 compute；3 個缺少原生 extension。', '',
             'PNG / RDC / 參考影像放在 GitHub Release；文字 log 留在 repository。'
             '上傳狀態見 [release.json](release.json)，檔名、大小及 SHA-256 見 '
             '[artifacts.json](artifacts.json)。', '',
             'Profile A = 修補版 Mesa 25.0.7；Profile B = Mesa 26.2.5 BDA 修補 + '
             'RenderDoc descriptor regeneration。比較範圍為 RGB，未聲稱 alpha 一致。', '',
             '| Example | 結果 | Profile | Replay PNG | 原始參考 | 執行紀錄 | RDC 檔名 |',
             '| --- | --- | --- | --- | --- | --- | --- |']
    gallery = ['# 95 張 replay PNG', '',
               '影像由獨立程序執行 RenderDoc replay 後以 SaveTexture 匯出，全部與原始參考 RGB 逐像素一致。', '',
               '94 張視窗影像為 640×360；renderheadless 為 1024×1024。'
               'computeheadless 沒有影像，因此不在本頁。', '',
               'Release 上傳狀態見 [release.json](release.json)。', '']
    if base:
        for name in ('raytracingreflections', 'descriptorheapuntyped'):
            gallery.extend([f'![{name}]({base}/{name}.png)', ''])
    gallery.extend(['| Example | 尺寸 | PNG |', '| --- | --- | --- |'])
    for row in sorted(rows, key=lambda r: r['example']):
        name = row['example']
        profile = 'B' if name in DESCRIPTORS else 'A'
        record = {'example': name, 'status': row['status'], 'profile': profile}
        links = {}
        for kind in ('capture', 'replay', 'reference'):
            path = OUT / 'logs' / f'{name}.{kind}.log'
            if path.is_file():
                publish(path, Path('logs') / path.name)
                links[kind] = f'[{kind}](logs/{path.name})'
        assert 'capture' in links, name
        if row['status'] == 'CAPTURE_FAILED':
            status = row['failure_reason']
            png_link = ref_link = capture_name = '—'
        else:
            assert row['capture_exit'] == row['replay_exit'] == 0, name
            assert 'replay' in links, name
            capture = Path(row['capture'])
            assert capture.is_file(), name
            record['capture'] = {'workspace_path': str(capture), 'bytes': capture.stat().st_size,
                                 'sha256': sha256(capture), 'published_to_git': False,
                                 'release_archive': 'vulkan_verified_captures.tar.gz',
                                 'archive_member': 'captures/' + capture.name}
            capture_name = f'`{capture.name}`'
            if row['status'] == 'REPLAYED_NO_IMAGE':
                assert name == 'computeheadless'
                status, png_link, ref_link = 'Compute replay 通過，無影像', '—', '—'
            else:
                assert row['status'] == 'REPLAYED_PNG' and row['pixel_exact'], name
                png = Path(row['png'])
                reference = Path(row['reference'])
                actual = Image.open(png).convert('RGB')
                original = Image.open(reference).convert('RGB')
                assert actual.size == original.size, name
                assert ImageChops.difference(actual, original).getbbox() is None, name
                record['png'] = {'workspace_path': str(png), 'bytes': png.stat().st_size,
                                 'sha256': sha256(png), 'release_asset_name': png.name,
                                 'published_to_git': False}
                record['reference'] = {'workspace_path': str(reference),
                                       'bytes': reference.stat().st_size,
                                       'sha256': sha256(reference),
                                       'release_archive': 'vulkan_verified_references.zip',
                                       'archive_member': 'reference/' + reference.name,
                                       'published_to_git': False}
                record['dimensions'] = list(actual.size)
                status = 'RGB 逐像素一致'
                png_link = f'[PNG]({base}/{png.name})' if base else f'`{png.name}`（Release 待上傳）'
                ref_link = (f'[參考 ZIP]({base}/vulkan_verified_references.zip)' if base
                            else f'`reference/{reference.name}`（Release ZIP）')
                gallery.append(f'| {name} | {actual.width}×{actual.height} | {png_link} |')
        record['log_paths'] = {kind: f'results/logs/{name}.{kind}.log' for kind in links}
        records.append(record)
        table.append(f'| {name} | {status} | {profile} | {png_link} | {ref_link} | '
                     f'{" / ".join(links.values())} | {capture_name} |')
    manifest = {'generated_utc': datetime.now(timezone.utc).isoformat(),
                'counts': dict(counts), 'comparison': 'RGB pixel-exact; alpha excluded',
                'files': records}
    (DEST / 'artifacts.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    (DEST / 'report.md').write_text('\n'.join(table) + '\n')
    (DEST / 'gallery.md').write_text('\n'.join(gallery) + '\n')
    print('PASS: 95 exact PNGs, 95 references, per-example logs, and 96 RDC hashes; binaries stay outside Git.')


if __name__ == '__main__':
    main()
