#!/usr/bin/env python3
"""Reject missing assets, foreign architecture and external development dependencies."""
from pathlib import Path
import hashlib
import json
import os
import plistlib
import re
import subprocess
import sys

app = Path(sys.argv[1]).resolve(strict=True)
arch = sys.argv[2]
repo = Path(__file__).resolve().parents[1]
project = repo.parent
resources = app / 'Contents/Resources'
for name in ['MaxChat','maxchat-secrets','maxchat-script-worker']:
    if not (app / 'Contents/MacOS' / name).is_file():
        raise SystemExit('Missing packaged executable: ' + name)
with (app / 'Contents/Info.plist').open('rb') as stream:
    info = plistlib.load(stream)
if info.get('CFBundleExecutable') != 'MaxChat' or info.get('CFBundleIdentifier') != 'org.maxchat.MaxChat':
    raise SystemExit('Missing or inconsistent app executable/identifier metadata')
manifest = [line for line in (repo / 'packaging/runtime-assets.txt').read_text().splitlines()
            if line and not line.startswith('#')]
for name in manifest:
    if (resources / name).read_bytes() != (repo / name).read_bytes():
        raise SystemExit('Missing or different runtime asset: ' + name)
requirements = set()
count = 0
for file in sorted(app.rglob('*')):
    if file.is_symlink():
        if os.path.isabs(os.readlink(file)) or not file.resolve().is_relative_to(app):
            raise SystemExit('Bundle symlink escapes: ' + str(file.relative_to(app)))
        continue
    if not file.is_file() or 'Mach-O' not in subprocess.check_output(['file', '-b', str(file)], text=True):
        continue
    count += 1
    if subprocess.check_output(['lipo', '-archs', str(file)], text=True).split() != [arch]:
        raise SystemExit('Architecture mismatch: ' + file.name)
    data = file.read_bytes()
    for prefix in [str(project).encode(), str(project).encode('utf-16-le')]:
        if prefix in data:
            raise SystemExit('Development path in binary: ' + file.name)
    for line in subprocess.check_output(['otool', '-L', str(file)], text=True).splitlines()[1:]:
        dependency = line.strip().split(' (', 1)[0]
        if not dependency.startswith(('@rpath/', '@loader_path/', '@executable_path/', '/System/Library/', '/usr/lib/')):
            raise SystemExit('External runtime dependency in ' + file.name)
    commands = subprocess.check_output(['otool', '-l', str(file)], text=True)
    for match in re.finditer(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset', commands):
        if not match.group(1).startswith(('@loader_path', '@executable_path')):
            raise SystemExit('External runtime search path in ' + file.name)
    requirements.update(re.findall(r'\bminos ([\d.]+)', commands))
result = dict(architecture=arch, mach_o_files=count, manifest_assets=len(manifest),
              minimum_macos=max(requirements, key=lambda v: tuple(map(int, v.split('.')))) if requirements else None)
print(json.dumps(result, indent=2))
