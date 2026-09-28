#!/usr/bin/env python3
"""Collect notices for the actual distro libraries bundled by linuxdeploy."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path


def collect(root):
    root = root.resolve(strict=True)
    source = Path(__file__).resolve().parents[1]
    if root == source or source in root.parents:
        raise ValueError('Runtime notices must be generated outside the source checkout')
    if not shutil.which('dpkg-query'):
        raise RuntimeError('This release packager requires dpkg package metadata for system-library notices')
    output = root / 'usr/bin/licenses/system-runtime'
    if any(parent.is_symlink() for parent in [output, *output.parents]):
        raise ValueError('Refusing symlinked notice output')
    output.mkdir(parents=True, exist_ok=True)
    packages = {}
    libraries = []
    for library in sorted((root / 'usr/lib').glob('*.so*')):
        if not library.is_file():
            continue
        if library.is_symlink() and not library.resolve().is_relative_to(root):
            raise ValueError('Runtime library escapes the package')
        # These are covered by the explicit Qt/FFmpeg/ICU notice manifest.
        if re.match(r'lib(?:Qt6|avcodec\.|avformat\.|avutil\.|swresample\.|swscale\.|icu)', library.name):
            continue
        query = subprocess.run(['dpkg-query', '-S', '*/' + library.name],
                               capture_output=True, text=True, check=False)
        owners = {line.split(': /', 1)[0] for line in query.stdout.splitlines()
                  if ': /' in line and ('/lib/' in line or '/lib64/' in line)}
        if len(owners) != 1:
            raise RuntimeError('Missing or ambiguous package owner for ' + library.name)
        owner = owners.pop()
        if owner not in packages:
            metadata = subprocess.check_output(
                ['dpkg-query', '-W', '-f=${Version}\t${source:Package}\t${source:Version}', owner],
                text=True).split('\t')
            notice = Path('/usr/share/doc') / owner.split(':', 1)[0] / 'copyright'
            if not notice.is_file():
                raise RuntimeError('Missing copyright notice for ' + owner)
            filename = owner.replace(':', '_') + '.txt'
            destination = output / filename
            if destination.is_symlink():
                raise ValueError('Refusing symlinked notice file')
            shutil.copyfile(notice, destination)
            packages[owner] = dict(version=metadata[0], source=metadata[1],
                                   source_version=metadata[2], notice=filename)
        with library.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        libraries.append(dict(file=library.relative_to(root).as_posix(),
                              package=owner, sha256=digest))
    destination = output / 'DEPENDENCIES.json'
    if destination.is_symlink():
        raise ValueError('Refusing symlinked dependency inventory')
    destination.write_text(json.dumps(dict(packages=packages, libraries=libraries), indent=2) + '\n')
    print(f'Collected {len(packages)} package notices for {len(libraries)} runtime libraries.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('appdir', type=Path)
    collect(parser.parse_args().appdir)
