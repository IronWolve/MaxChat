#!/usr/bin/env python3
"""Offline publication-policy regressions using disposable Git repositories."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
SOURCE_ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('source_guard', SOURCE_ROOT / 'tools/check-source.py')
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)

PRIVATE_PATHS = (
    'AGENTS.md', 'nested/AgEnTs.Md', 'CLAUDE.md', '.codex/state.json',
    '.env', '.ENV.local', 'server.env', 'nested/private.env.backup', '.envrc',
    '.config/settings.json', 'nested/.config/private.json', '.ssh/id_ed25519',
    '.aws/credentials', '.kube/config', '.git-credentials', '.netrc',
    'credentials.json', 'nested/credentials.YAML', 'secrets.yml', 'auth.json',
    'profiles.json', 'providers.json', 'cookies.txt', 'keys/private.PEM',
    'private.key', 'identity.p12', 'identity.pfx', 'passwords.kdbx',
    'logs/session.log', 'nested/debug.LOG', 'network.pcapng', 'trace.har',
    'profile.sqlite3', 'profile.sqlite3-wal', 'state.db-shm',
    'run/maxchat', 'nested/build-debug/app', 'cmake-build-release/app',
    'out/app', 'dist/pkg', 'CMakeCache.txt', 'CMakeUserPresets.json',
    'compile_commands.json', 'CMakeFiles/compiler.bin', 'build.ninja',
    '.ninja_log', 'Testing/Temporary/LastTest.log', 'program.o', 'program.OBJ',
    'library.a', 'library.dll', 'library.so.6', 'app.exe', 'app.pdb',
    'maxchat_fr.qm', 'release.zip', 'release.tar.gz', 'release.AppImage',
    'rollback.bundle', '.venv/bin/python', '.cache/items', '__pycache__/x.pyc',
    '.pytest_cache/state', '.coverage', '.coverage.host', 'coverage/index.html',
    'node_modules/dependency.js', '.idea/workspace.xml', '.vscode/settings.json',
    'user.vcxproj.user', 'app.suo', '.DS_Store', 'Thumbs.db',
    'notes/private.md', 'docs/audit.md', 'DEVDOCS/HANDOFF.md',
    'backups/source.cpp', 'backup.cpp.bak', 'source.cpp~', '.source.cpp.swp',
    'core', 'core.123', 'maxchat.pid', 'maxchat.sock', 'readme.md', 'RELEASE-1.0.1.md',
)


class PublicationPolicyTest(unittest.TestCase):
    def setUp(self):
        scratch = SOURCE_ROOT.parent / 'tmp/check-source-tests'
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='policy-', dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.env = dict(os.environ, GIT_CONFIG_NOSYSTEM='1', GIT_CONFIG_GLOBAL=os.devnull)
        self.run_git('init', '-q')
        self.approved = ['.gitignore', guard.MANIFEST, 'tools/example.py',
                         'src/core/example.cpp', 'assets/themes/example.json',
                         'THIRD_PARTY_NOTICES.md']
        for name in self.approved:
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text('approved fixture\n')
        self.write_policy()
        self.run_git('add', '--', *self.approved)

    def run_git(self, *args, **kwargs):
        return subprocess.run(['git', '-C', str(self.root), *args],
                              env=self.env, check=True, capture_output=True, **kwargs)

    def write_policy(self):
        (self.root / guard.MANIFEST).write_text('\n'.join(self.approved) + '\n')
        (self.root / '.gitignore').write_text(guard.render_ignore(self.approved))

    def test_approved_source_and_required_assets_remain_trackable(self):
        self.assertEqual(guard.audit(self.root), [])
        self.assertEqual(guard.audit(self.root, staged=True), [])

    def test_private_categories_are_ignored_and_guarded(self):
        for name in PRIVATE_PATHS:
            with self.subTest(path=name):
                self.assertTrue(guard.private_path(name))
        result = self.run_git('check-ignore', '--no-index', '-z', '--stdin',
                              input=('\0'.join(PRIVATE_PATHS) + '\0').encode())
        self.assertEqual(set(result.stdout.decode().rstrip('\0').split('\0')), set(PRIVATE_PATHS))

    def test_unapproved_source_files_and_directories_are_ignored(self):
        for name in ['src/core/unapproved.cpp', 'tools/stray.py', 'unapproved/project/file.sh']:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('unapproved\n')
        self.assertEqual(self.run_git('ls-files', '--others', '--exclude-standard').stdout, b'')

    def test_public_readme_requires_explicit_approval_and_content_checks(self):
        name = 'README.md'
        path = self.root / name
        path.write_text('Public project overview\n')
        self.run_git('check-ignore', '--no-index', name)
        self.run_git('add', '-f', name)
        self.assertTrue(any(name + ': outside' in e for e in guard.audit(self.root, staged=True)))
        self.approved.append(name)
        self.write_policy()
        self.run_git('add', '.gitignore', guard.MANIFEST, name)
        self.assertEqual(guard.audit(self.root), [])
        self.assertEqual(guard.audit(self.root, staged=True), [])
        path.write_text('ghp_' + 'A' * 40 + '\n')
        self.run_git('add', name)
        self.assertTrue(any(name + ': possible GitHub token' in e
                            for e in guard.audit(self.root, staged=True)))

    def test_force_added_private_file_is_rejected(self):
        (self.root / '.env').write_text('fixture=yes\n')
        self.run_git('add', '-f', '.env')
        self.assertTrue(any('.env: outside' in e for e in guard.audit(self.root, staged=True)))

    def test_manifest_cannot_authorize_private_file(self):
        name = 'nested/credentials.JSON'
        (self.root / name).parent.mkdir()
        (self.root / name).write_text('{}\n')
        self.approved.append(name)
        self.write_policy()
        self.run_git('add', '.gitignore', guard.MANIFEST)
        self.run_git('add', '-f', name)
        self.assertTrue(any(name + ': forbidden' in e for e in guard.audit(self.root, staged=True)))

    def test_unstaged_content_does_not_change_index_scan(self):
        path = self.root / 'tools/example.py'
        path.write_text('ghp_' + 'A' * 40 + '\n')
        self.assertEqual(guard.audit(self.root, staged=True), [])
        self.assertTrue(any('GitHub token' in e for e in guard.audit(self.root)))
        self.run_git('add', 'tools/example.py')
        self.assertTrue(any('GitHub token' in e for e in guard.audit(self.root, staged=True)))

    def test_staged_source_symlink_is_rejected(self):
        path = self.root / 'tools/example.py'
        path.unlink()
        path.symlink_to('../THIRD_PARTY_NOTICES.md')
        self.run_git('add', 'tools/example.py')
        self.assertTrue(any('only regular source files' in e for e in guard.audit(self.root, staged=True)))

    def test_staged_broad_allow_rule_is_rejected_even_if_worktree_is_fixed(self):
        policy = self.root / '.gitignore'
        policy.write_text(policy.read_text() + '!*.py\n')
        self.run_git('add', '.gitignore')
        self.write_policy()
        self.assertEqual(guard.audit(self.root), [])
        self.assertTrue(any('policy differs' in e for e in guard.audit(self.root, staged=True)))

    def test_removing_private_exclusions_is_rejected(self):
        policy = self.root / '.gitignore'
        protected = '**/' + guard.case_pattern('credentials.json')
        policy.write_text(policy.read_text().replace(protected + '\n', ''))
        self.run_git('add', '.gitignore')
        self.assertTrue(any('policy differs' in e for e in guard.audit(self.root, staged=True)))

    def test_identifying_paths_are_rejected_without_printing_them(self):
        location = '/home/' + 'private-user/project/source.cpp'
        (self.root / 'tools/example.py').write_text(location + '\n')
        errors = guard.audit(self.root)
        self.assertTrue(any('identifying home path' in error for error in errors))
        self.assertTrue(all(location not in error for error in errors))

    def test_fixed_development_paths_are_rejected(self):
        locations = ['C:' + chr(92) + 'work' + chr(92) + 'project',
                     'C:/' + 'projects/private', '/mnt/' + 'c/work/private']
        for location in locations:
            (self.root / 'tools/example.py').write_text(location + '\n')
            self.assertTrue(any('fixed development path' in error for error in guard.audit(self.root)))

    def test_commit_message_guard_accepts_normal_text(self):
        path = self.root / 'commit-message'
        path.write_text('Keep build outputs relative to the project root\n')
        result = subprocess.run([sys.executable, str(SOURCE_ROOT / 'tools/check-source.py'),
                                 '--commit-message', str(path)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_commit_message_guard_rejects_private_paths_and_attribution(self):
        path = self.root / 'commit-message'
        cases = ['/home/' + 'private-user/project',
                 'Co-authored-' + 'by: Assistant <automation@openai.com>']
        for text in cases:
            with self.subTest(category=text.split()[0]):
                path.write_text(text + '\n')
                result = subprocess.run([sys.executable, str(SOURCE_ROOT / 'tools/check-source.py'),
                                         '--commit-message', str(path)], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn(text, result.stdout)

    def test_missing_staged_manifest_fails_closed(self):
        self.run_git('rm', '--cached', guard.MANIFEST)
        self.assertTrue(any('missing or unreadable publication manifest' in e
                            for e in guard.audit(self.root, staged=True)))


if __name__ == '__main__':
    unittest.main()
