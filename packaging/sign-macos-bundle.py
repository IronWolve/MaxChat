#!/usr/bin/env python3
"""Thin and ad-hoc sign only generated code inside a macOS package stage."""
from pathlib import Path
import subprocess
import sys

app = Path(sys.argv[1]).resolve(strict=True)
arch = sys.argv[2]
if arch not in {'arm64', 'x86_64'} or app.suffix != '.app':
    raise SystemExit('Invalid bundle or architecture')
for file in sorted(app.rglob('*')):
    if file.is_symlink() or not file.is_file():
        continue
    kind = subprocess.check_output(['file', '-b', str(file)], text=True)
    if 'Mach-O' not in kind:
        continue
    architectures = subprocess.check_output(['lipo', '-archs', str(file)], text=True).split()
    if arch not in architectures:
        raise SystemExit('Missing target architecture: ' + file.name)
    if len(architectures) > 1:
        temporary = file.with_name(file.name + '.thin')
        subprocess.run(['lipo', str(file), '-thin', arch, '-output', str(temporary)], check=True)
        temporary.chmod(file.stat().st_mode)
        temporary.replace(file)
    subprocess.run(['codesign', '--force', '--sign', '-', str(file)], check=True)
for framework in sorted(app.rglob('*.framework'), key=lambda p: len(p.parts), reverse=True):
    if framework.is_dir() and not framework.is_symlink():
        subprocess.run(['codesign', '--force', '--sign', '-', str(framework)], check=True)
subprocess.run(['codesign', '--force', '--sign', '-', str(app)], check=True)
