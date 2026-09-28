#!/usr/bin/env python3
"""Test project-relative scripts with an offline CMake fixture, never MaxChat."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SOURCE_ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(all(shutil.which(tool) for tool in ('cmake', 'ninja', 'c++', 'flock', 'rsync')),
                     'existing build tools and rsync are required; nothing is installed')
class ProjectPathsTest(unittest.TestCase):
    def setUp(self):
        scratch = SOURCE_ROOT.parent / 'tmp/project-path-tests'
        scratch.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='paths-', dir=scratch)
        self.addCleanup(self.temporary.cleanup)
        self.scratch = Path(self.temporary.name)
        self.project = self.scratch / 'original project'
        self.repo = self.project / 'repo'
        self.caller = self.scratch / 'unrelated working directory'
        self.caller.mkdir()
        for name in ('repo/tools', 'repo/packaging', 'repo/assets/themes', 'run/launchers', '.config/maxchat'):
            (self.project / name).mkdir(parents=True, exist_ok=True)
        for name in ('build.sh', 'start.sh', 'stop.sh', 'project-env.sh'):
            shutil.copy2(SOURCE_ROOT / 'tools' / name, self.repo / 'tools' / name)
            if name != 'build.sh':
                shutil.copy2(SOURCE_ROOT / 'tools' / name, self.project / 'run/launchers' / name)
        for name, target in (('build.sh', 'repo/tools/build.sh'), ('start.sh', 'run/launchers/start.sh'),
                             ('stop.sh', 'run/launchers/stop.sh')):
            (self.project / name).symlink_to(target)
        shutil.copy2(SOURCE_ROOT / 'packaging/stage-assets.cmake', self.repo / 'packaging/stage-assets.cmake')
        shutil.copy2(SOURCE_ROOT / 'sync-to-win.sh', self.repo / 'sync-to-win.sh')
        (self.repo / 'packaging/runtime-assets.txt').write_text('assets/themes/example.json\nLICENSE\n')
        (self.repo / 'packaging/source-files.txt').write_text('CMakeLists.txt\nassets/themes/example.json\nLICENSE\n')
        (self.repo / 'assets/themes/example.json').write_text('{"fixture": true}\n')
        (self.repo / 'LICENSE').write_text('Offline test fixture\n')
        (self.repo / 'placeholder').write_text('#!/bin/sh\nexit 99\n')
        # CMake/Ninja really configure and stage files, but do not compile or
        # execute any application. The placeholder executable is never launched.
        (self.repo / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.24)
project(PathFixture LANGUAGES NONE)
add_custom_target(maxchat-c
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/placeholder" "${CMAKE_CURRENT_BINARY_DIR}/maxchat")
add_custom_target(maxchat-secrets
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/placeholder" "${CMAKE_CURRENT_BINARY_DIR}/maxchat-secrets")
add_custom_target(maxchat-script-worker
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/placeholder" "${CMAKE_CURRENT_BINARY_DIR}/maxchat-script-worker")
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/maxchat-script-worker" "${CMAKE_CURRENT_BINARY_DIR}/maxchat" "${CMAKE_CURRENT_BINARY_DIR}/maxchat-secrets"
        DESTINATION "." COMPONENT Runtime)
''')
        (self.project / '.config/maxchat/settings.json').write_text('{"preserve": true}\n')
        self.environment = dict(os.environ, NO_COLOR='1', MAXCHAT_BUILD_JOBS='1')

    def execute(self, args, check=True):
        return subprocess.run([str(arg) for arg in args], cwd=self.caller, env=self.environment,
                              capture_output=True, text=True, check=check, timeout=30)

    def test_build_survives_move_and_launchers_need_no_checkout(self):
        first = self.execute([self.project / 'build.sh'])
        self.assertIn('Source: repo/', first.stdout)
        self.assertTrue((self.project / 'run/app/maxchat').is_file())
        self.assertTrue((self.project / 'run/app/maxchat-script-worker').is_file())
        self.assertEqual((self.project / 'run/app/assets/themes/example.json').read_text(), '{"fixture": true}\n')
        self.assertFalse((self.repo / 'CMakeCache.txt').exists())
        original = str(self.project)
        renamed = self.scratch / 'renamed project with spaces'
        self.project.rename(renamed)
        self.project = renamed
        self.repo = renamed / 'repo'
        second = self.execute([renamed / 'build.sh'])
        self.assertIn('Project location changed', second.stdout)
        cache = (renamed / 'run/build/CMakeCache.txt').read_text()
        self.assertNotIn(original, cache)
        self.assertEqual((renamed / '.config/maxchat/settings.json').read_text(), '{"preserve": true}\n')
        self.assertEqual(os.readlink(renamed / '.config/maxchat/logs'), '../../logs/chat')
        for folder in ('run/app', 'run/launchers'):
            for file in (renamed / folder).rglob('*'):
                if file.is_file():
                    self.assertNotIn(original.encode(), file.read_bytes())
        self.repo.rename(self.scratch / 'source kept separately')
        status = self.execute([renamed / 'start.sh', '--status'])
        self.assertIn('Runtime available: run/app/maxchat', status.stdout)
        self.assertEqual(self.execute([renamed / 'stop.sh', '--help']).returncode, 0)

    def test_environment_paths_follow_current_project_location(self):
        script = self.project / 'repo/tools/project-env.sh'
        command = 'source "$1"; project_environment; printf "%s\\n" "$XDG_CONFIG_HOME" "$TMPDIR" "$CCACHE_DIR"'
        result = self.execute(['bash', '-c', command, 'path-test', script])
        self.assertEqual(result.stdout.splitlines(), [str(self.project / '.config'),
                          str(self.project / 'tmp'), str(self.project / '.cache/ccache')])

    def test_relative_sync_destination_uses_project_root(self):
        self.execute([self.repo / 'sync-to-win.sh', '../copy target'])
        target = self.scratch / 'copy target/repo'
        self.assertTrue((target / 'CMakeLists.txt').is_file())
        self.assertTrue((target / 'assets/themes/example.json').is_file())
        self.assertFalse((target / 'placeholder').exists())
        self.assertFalse((self.caller / 'copy target').exists())

    def test_sync_rejects_destination_inside_checkout(self):
        result = self.execute([self.repo / 'sync-to-win.sh', 'repo/nested'], check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('outside the source checkout', result.stderr)
        self.assertFalse((self.repo / 'nested').exists())

    def test_windows_script_has_no_fixed_drive_paths(self):
        script = (SOURCE_ROOT / 'build.bat').read_bytes()
        self.assertNotRegex(script.decode(), r'(?<![A-Za-z_])[A-Za-z]:[\\/]')
        self.assertIn(b'set "ROOT=repo"', script)
        self.assertIn(b'DisableDelayedExpansion', script)
        self.assertIn(b'--fresh', script)
        self.assertNotIn(b'\n', script.replace(b'\r\n', b''))


if __name__ == '__main__':
    unittest.main()
