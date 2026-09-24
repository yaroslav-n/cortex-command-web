#!/usr/bin/env python3
"""Record all real engine assets, preserving case and paths for MEMFS."""
import hashlib
import json
import os
from pathlib import Path
root = Path(__file__).resolve().parents[1]
data = Path(os.environ.get('ORIGINAL_CODE', root.parent / 'original-code')) / 'Data'
if not data.is_dir():
    raise SystemExit(f'Missing engine assets: {data}')
entries = []
for path in sorted(data.rglob('*')):
    if path.is_file():
        entries.append({'path': '/Data/' + path.relative_to(data).as_posix(), 'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
output = root / 'build' / 'assets.json'
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps({'files': entries, 'totalBytes': sum(x['bytes'] for x in entries)}, separators=(',', ':')))
print(f'{len(entries)} assets, {sum(x["bytes"] for x in entries):,} bytes -> {output}')
