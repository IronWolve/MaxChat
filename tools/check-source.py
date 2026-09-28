#!/usr/bin/env python3
"""Validate the explicit publication manifest without printing secret values."""
import argparse
import re
from fnmatch import fnmatchcase
import subprocess
from pathlib import Path, PurePosixPath

MANIFEST = 'packaging/source-files.txt'
PATTERNS = {
    'identifying home path': re.compile(rb'/(?:home|Users)/[A-Za-z0-9_.-]+(?:/|\\)'),
    'fixed development path': re.compile(rb'(?:\b[A-Za-z]:[\\/]+(?:work|Users|Qt|src|dev|projects|Program Files)[\\/]|/mnt/[A-Za-z]/(?:work|Users|src|dev|projects)/)', re.I),
    'assistant attribution': re.compile(rb'(?:co-authored-' rb'by:[^\r\n]*(?:claude|anthropic|openai|codex|copilot|assistant)|generated (?:with|by)[^\r\n]*(?:claude|anthropic|openai|codex|chatgpt|copilot))', re.I),
    'private key': re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----'),
    'GitHub token': re.compile(rb'\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{50,})\b'),
    'AWS access key': re.compile(rb'\b(?:AKIA|ASIA)[A-Z0-9]{16}\b'),
    'OpenAI key': re.compile(rb'\bsk-(?:proj-|svcacct-)?[A-Za-z0-9_-]{40,}\b'),
    'Slack token': re.compile(rb'\bxox[baprs]-[A-Za-z0-9-]{25,}\b'),
}
# Shared by the ignore generator and the staged-file guard. Keep exact source
# paths in the manifest; these categories must never become source by accident.
PRIVATE_DIRS = {
    '.git', '.config', '.cache', '.secrets', '.venv', 'venv', 'env',
    '.aws', '.ssh', '.gnupg', '.azure', '.kube', '.terraform', '.direnv',
    '.agents', '.codex', '.claude', '.idea', '.vscode', '.vs', '.fleet', '.history',
    'node_modules', 'docs', 'logs', 'tmp', 'temp', 'run', 'dist', 'build', 'out',
    'devdocs', 'old', 'backups', 'notes', 'plans', 'coverage', 'htmlcov', 'testing', 'cmakefiles',
    '__pycache__', '.pytest_cache', '.mypy_cache', '.ruff_cache', '.tox', '.nox',
    '.ccache', '.sccache', '.gradle', '.hypothesis', 'xcuserdata', 'deriveddata',
}
PRIVATE_DIR_GLOBS = ('build-*', 'dist-*', 'cmake-build-*')
PRIVATE_NAMES = {
    'agents.md', 'agent.md', 'claude.md', 'gemini.md',
    'id_rsa', 'id_dsa', 'id_ecdsa', 'id_ed25519',
    'credentials', 'credentials.json', 'credentials.yaml', 'credentials.yml',
    'secrets.json', 'secrets.yaml', 'secrets.yml', 'auth.json', 'tokens.json',
    'settings.json', 'profiles.json', 'providers.json', 'cookies.txt',
    '.netrc', '.npmrc', '.pypirc', '.git-credentials', '.envrc', '.flaskenv', 'lifecycle.lock',
    '.ds_store', 'thumbs.db', 'desktop.ini',
    'cmakecache.txt', 'cmakeuserpresets.json', 'cmake_install.cmake',
    'compile_commands.json', 'build.ninja', '.ninja_deps', '.ninja_log',
    'ctesttestfile.cmake', 'dartconfiguration.tcl', '.coverage',
}
PRIVATE_NAME_GLOBS = ('.env', '.env.*', '*.env', '*.env.*', '.coverage.*',
                      '*.db-*', '*.sqlite-*', '*.sqlite3-*', '*.so.*',
                      '*~', '*~.*', '.#*', '#*#', '*.sw?', 'core.[0-9]*')
PRIVATE_SUFFIXES = {
    '.pem', '.key', '.p12', '.pfx', '.keystore', '.jks', '.kdbx',
    '.log', '.trace', '.pcap', '.pcapng', '.har', '.sqlite', '.sqlite3', '.db',
    '.bak', '.backup', '.orig', '.rej', '.dmp', '.pyc', '.pyo',
    '.o', '.obj', '.a', '.lib', '.so', '.dylib', '.dll', '.exe', '.pdb', '.ilk',
    '.exp', '.qm', '.gcda', '.gcno', '.profraw', '.profdata', '.class', '.jar',
    '.suo', '.user', '.sdf', '.opensdf', '.ipch', '.pch', '.idb', '.pid', '.sock',
    '.zip', '.7z', '.rar', '.tar', '.gz', '.bz2', '.xz', '.zst', '.tgz',
    '.appimage', '.msi', '.msix', '.dmg', '.deb', '.rpm', '.bundle',
}
ROOT_DOCUMENTS = ('readme.md', 'scripting.md', 'release-*.md')


def case_pattern(value):
    # Git patterns are case-sensitive on many hosts. Protect common variants
    # without relying on the host's core.ignorecase configuration.
    return ''.join(f'[{c.lower()}{c.upper()}]' if c.isalpha() else c for c in value)


def ignore_rules(approved):
    parents = {parent.as_posix() for name in approved
               for parent in PurePosixPath(name).parents if parent.as_posix() != '.'}
    rules = ['*']
    rules += ['!/' + name + '/' for name in sorted(parents, key=lambda name: (name.count('/'), name))]
    rules += ['!/' + name for name in sorted(approved)]
    rules += ['**/' + case_pattern(name) + '/' for name in sorted(PRIVATE_DIRS) if name != '.git']
    rules += ['**/' + case_pattern(pattern) + '/' for pattern in PRIVATE_DIR_GLOBS]
    rules += ['**/' + case_pattern(name) for name in sorted(PRIVATE_NAMES)]
    rules += ['**/' + case_pattern(pattern) for pattern in PRIVATE_NAME_GLOBS]
    rules += ['**/*' + case_pattern(suffix) for suffix in sorted(PRIVATE_SUFFIXES)]
    rules += ['/' + case_pattern(pattern) for pattern in ROOT_DOCUMENTS]
    rules += ['/core']  # Never ignore src/core/ as a core-dump filename.
    return rules


def render_ignore(approved):
    return ('# Deny by default: only exact approved source files may be tracked.\n'
            '# Generated from packaging/source-files.txt and tools/check-source.py.\n'
            '# After reviewing a source-manifest change: python3 tools/check-source.py --write-ignore\n'
            '# Keep private/build output outside the checkout; exclusions also apply when nested.\n'
            + '\n'.join(ignore_rules(approved)) + '\n')


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args])


def private_path(name):
    path = PurePosixPath(name)
    parts = tuple(part.lower() for part in path.parts)
    base = path.name.lower()
    return (not name or path.is_absolute() or '..' in path.parts or '\\' in name or
            any(ord(c) < 32 or ord(c) == 127 for c in name) or
            any(part in PRIVATE_DIRS or any(fnmatchcase(part, pattern) for pattern in PRIVATE_DIR_GLOBS)
                for part in parts) or
            base in PRIVATE_NAMES or path.suffix.lower() in PRIVATE_SUFFIXES or
            any(fnmatchcase(base, pattern) for pattern in PRIVATE_NAME_GLOBS) or
            (len(path.parts) == 1 and (base == 'core' or
             any(fnmatchcase(base, pattern) for pattern in ROOT_DOCUMENTS))))


def audit(root, staged=False):
    errors = []
    entries = {}
    if staged:
        for row in git(root, 'ls-files', '--stage', '-z').split(b'\0'):
            if not row:
                continue
            metadata, name = row.split(b'\t', 1)
            mode, oid, stage = metadata.decode().split()
            name = name.decode('utf-8', 'surrogateescape')
            if stage != '0':
                errors.append(f'{name}: unresolved merge')
            entries[name] = (mode, oid)

    def read(name):
        if staged:
            return git(root, 'cat-file', 'blob', entries[name][1])
        path = root / name
        if path.resolve() != path.absolute():
            raise ValueError('symlink or non-canonical source path')
        return path.read_bytes()

    try:
        approved = [line for line in read(MANIFEST).decode('utf-8').splitlines()
                    if line and not line.startswith('#')]
    except (OSError, KeyError, ValueError, subprocess.CalledProcessError) as error:
        return [f'{MANIFEST}: missing or unreadable publication manifest ({type(error).__name__})']
    if len(approved) != len(set(approved)):
        errors.append('publication manifest has duplicate entries')
    for name in approved:
        if private_path(name) or str(PurePosixPath(name)) != name:
            errors.append(f'{name}: forbidden publication path')
            continue
        try:
            if staged and entries[name][0] not in {'100644', '100755'}:
                errors.append(f'{name}: only regular source files may be published')
                continue
            data = read(name)
        except (OSError, KeyError, ValueError, subprocess.CalledProcessError) as error:
            errors.append(f'{name}: missing/unsafe approved file ({type(error).__name__})')
            continue
        for label, pattern in PATTERNS.items():
            if pattern.search(data):
                errors.append(f'{name}: possible {label}; inspect locally, do not print the value')
    if staged:
        published = set(entries)
    else:
        tracked = {name.decode('utf-8', 'surrogateescape') for name in
                   git(root, 'ls-files', '-z').split(b'\0') if name}
        untracked = {name.decode('utf-8', 'surrogateescape') for name in
                     git(root, 'ls-files', '--others', '--exclude-standard', '-z').split(b'\0') if name}
        published = {name for name in tracked | untracked if (root / name).exists() or (root / name).is_symlink()}
    for name in sorted(published - set(approved)):
        errors.append(f'{name}: outside the approved source manifest')
    # Check the same exact policy in the working tree and the index. Broad
    # negations or weakened exclusions must not be smuggled into a commit.
    try:
        ignore = [line for line in read('.gitignore').decode('utf-8').splitlines()
                  if line and not line.startswith('#')]
    except (OSError, KeyError, ValueError, subprocess.CalledProcessError):
        return errors + ['.gitignore: missing or unreadable publication policy']
    if ignore != ignore_rules(approved):
        errors.append('.gitignore: policy differs from manifest; run python3 tools/check-source.py --write-ignore')
    if not staged:
        result = subprocess.run(['git', '-C', str(root), 'check-ignore', '--no-index', '-z', '--stdin'],
                                input=('\0'.join(approved) + '\0').encode(), capture_output=True)
        if result.returncode not in (0, 1):
            errors.append('could not verify Git ignore policy')
        for name in result.stdout.decode().split('\0'):
            if name:
                errors.append(f'{name}: approved source is ignored by Git')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--staged', action='store_true', help='check exact index blobs before committing')
    mode.add_argument('--write-ignore', action='store_true', help='regenerate ignore rules from reviewed manifest')
    mode.add_argument('--commit-message', type=Path, help='check commit text without printing matched values')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    if args.commit_message is not None:
        try:
            data = args.commit_message.read_bytes()
        except OSError:
            print('Commit message is unreadable.')
            return 1
        findings = [label for label, pattern in PATTERNS.items() if pattern.search(data)]
        if findings:
            print('Commit message rejected: ' + ', '.join(findings) + '. Inspect it locally.')
            return 1
        return 0
    if args.write_ignore:
        approved = [line for line in (root / MANIFEST).read_text().splitlines()
                    if line and not line.startswith('#')]
        if len(approved) != len(set(approved)) or any(
                private_path(name) or str(PurePosixPath(name)) != name for name in approved):
            parser.error('manifest contains duplicate, private or non-canonical paths')
        if (root / '.gitignore').is_symlink():
            parser.error('refusing to overwrite a symlinked .gitignore')
        (root / '.gitignore').write_text(render_ignore(approved))
    errors = audit(root, args.staged)
    if errors:
        print('\n'.join(errors))
        return 1
    print('Publication manifest, file types, private-path rules and credential-pattern checks passed.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
