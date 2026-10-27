"""No network writes, real app installation, GPU or release publication in these fixtures."""
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('release_ci', Path(__file__).resolve().parents[1] / 'release.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)
COMMIT, TAG_OBJECT, TAG = 'a' * 40, 'b' * 40, 'v0.2.0-rc.1'


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.env = {'GITHUB_ACTIONS': 'true', 'GITHUB_EVENT_NAME': 'push', 'GITHUB_REPOSITORY': release.REPO,
                    'GITHUB_REF': 'refs/tags/' + TAG, 'GITHUB_REF_NAME': TAG, 'GITHUB_SHA': COMMIT,
                    'GITHUB_OUTPUT': '', 'GITHUB_STEP_SUMMARY': ''}
        for platform, patterns in release.PLATFORMS.items():
            files = {}
            for pattern in patterns:
                path = self.root / pattern.format(tag=TAG)
                path.write_bytes(b'tested artifact ' + platform.encode())
                files[path.name] = release.sha(path)
            release.write_json(self.root / f'provenance-{platform}.json', {
                'tag': TAG, 'source_commit': COMMIT, 'app_version': TAG[1:], 'tests': 'passed', 'assets': files})

    def test_versions_match_updater_and_windows_bounds(self):
        self.assertTrue(release.version(TAG))
        self.assertFalse(release.version('v0.2.0'))
        for value in ('0.2.0', 'v0.2', 'v00.2.0', 'v0.2.0-rc.01', 'v65536.0.0', 'v0.2.0+meta', 'v0.2.0\n', 'v0.2.0/evil'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                release.version(value)

    def test_manual_dispatch_uses_validation_label_and_cannot_publish(self):
        (self.root / 'cmake').mkdir()
        (self.root / 'cmake/Version.txt').write_text('0.1\n')
        with patch.dict(os.environ, {**self.env, 'GITHUB_EVENT_NAME': 'workflow_dispatch', 'GITHUB_RUN_NUMBER': '7'}), \
                patch.object(release, 'ROOT', self.root), patch.object(release, 'git', return_value=COMMIT), \
                patch.object(release, 'outputs') as output, patch.object(release, 'gh') as gh:
            release.prepare('', 'windows')
            self.assertEqual(output.call_args.args[0]['tag'], 'v0.1.0-ci.7')
            self.assertEqual(output.call_args.args[0]['linux'], 'false')
            with self.assertRaises(ValueError):
                release.publish(self.root, TAG, COMMIT, TAG_OBJECT)
            gh.assert_not_called()

    def test_unmerged_pr_validation_cannot_publish_or_require_main_ancestry(self):
        (self.root / 'cmake').mkdir()
        (self.root / 'cmake/Version.txt').write_text('0.1\n')
        with patch.dict(os.environ, {**self.env, 'GITHUB_EVENT_NAME': 'pull_request', 'GITHUB_REF': 'refs/pull/3/merge'}), \
                patch.object(release, 'ROOT', self.root), patch.object(release, 'git', return_value=COMMIT) as git, \
                patch.object(release, 'outputs') as output, patch.object(release, 'gh') as gh:
            release.prepare('', 'all')
            self.assertEqual(git.call_args_list[0].args, ('rev-parse', 'HEAD'))
            self.assertEqual(git.call_count, 1)
            self.assertEqual(output.call_args.args[0]['tag_object'], '')
            self.assertEqual(output.call_args.args[0]['windows'], 'true')
            self.assertEqual(output.call_args.args[0]['linux'], 'true')
            with self.assertRaises(ValueError):
                release.publish(self.root, TAG, COMMIT, TAG_OBJECT)
            gh.assert_not_called()

    def test_branch_push_and_pull_request_target_cannot_enter_release_flow(self):
        for event in ('push', 'pull_request_target'):
            with patch.dict(os.environ, {**self.env, 'GITHUB_EVENT_NAME': event, 'GITHUB_REF': 'refs/heads/fix-ci', 'GITHUB_REF_NAME': 'fix-ci'}), \
                    patch.object(release, 'git', return_value=COMMIT), patch.object(release, 'gh') as gh:
                with self.assertRaises(ValueError): release.prepare('', 'all')
                with self.assertRaises(ValueError): release.publish(self.root, TAG, COMMIT, TAG_OBJECT)
                gh.assert_not_called()

    def test_tag_identity_and_main_ancestry_with_real_git(self):
        repository = self.root / 'git-fixture'
        repository.mkdir()
        def git(*args):
            return subprocess.check_output(['git', '-C', str(repository), '-c', 'user.name=Release Test',
                                            '-c', 'user.email=release-test@example.invalid', *args],
                                           text=True, stderr=subprocess.DEVNULL).strip()
        git('init', '-b', 'main')
        git('commit', '--allow-empty', '-m', 'local fixture')
        git('update-ref', 'refs/remotes/origin/main', 'HEAD')
        git('tag', '-a', TAG, '-m', 'local fixture')
        env = {**self.env, 'GITHUB_SHA': git('rev-parse', 'HEAD')}
        with patch.dict(os.environ, env), patch.object(release, 'ROOT', repository), patch.object(release, 'outputs') as output:
            release.prepare(TAG, 'linux')
            self.assertEqual(output.call_args.args[0]['windows'], 'true')
            self.assertEqual(output.call_args.args[0]['tag_object'], git('rev-parse', TAG))
            with patch.dict(os.environ, {'GITHUB_SHA': COMMIT}), self.assertRaises(ValueError):
                release.prepare(TAG, 'all')
        git('checkout', '--orphan', 'unmerged-experiment')
        git('commit', '--allow-empty', '-m', 'unmerged fixture')
        other = 'v0.0.0-ci.1'
        git('tag', other)
        with patch.dict(os.environ, {**self.env, 'GITHUB_SHA': git('rev-parse', 'HEAD'), 'GITHUB_REF_NAME': other, 'GITHUB_REF': 'refs/tags/' + other}), \
                patch.object(release, 'ROOT', repository), self.assertRaises(subprocess.CalledProcessError):
            release.prepare(other, 'all')

    def test_stamp_retains_original_version_and_exact_source(self):
        (self.root / 'cmake').mkdir()
        (self.root / 'cmake/Version.txt').write_text('0.1\n')
        def git(*args):
            return COMMIT if args == ('rev-parse', 'HEAD') else ''
        with patch.object(release, 'ROOT', self.root), patch.object(release, 'git', side_effect=git):
            release.stamp(TAG, COMMIT)
        self.assertEqual((self.root / 'cmake/Version.txt').read_text().strip(), '0.2.0-rc.1')
        metadata = json.loads((self.root / 'build/ci/source.json').read_text())
        self.assertEqual(metadata['original_version'], '0.1')
        self.assertEqual(metadata['source_commit'], COMMIT)

    def test_locked_download_rejects_changed_remote_bytes(self):
        path = self.root / 'dependency'
        with patch.object(release.urllib.request, 'urlopen', return_value=io.BytesIO(b'corrupt')), self.assertRaises(ValueError):
            release.download_file('https://example.invalid/pinned', 'a' * 64, path)
        self.assertFalse(path.exists())
        self.assertFalse(path.with_name('dependency.part').exists())

    def test_partial_build_and_wrong_source_are_rejected(self):
        files, manifests = release.verified_assets(self.root, TAG, COMMIT)
        self.assertEqual(len(files), 3)
        self.assertEqual(len(manifests), 2)
        with self.assertRaises(ValueError):
            release.verified_assets(self.root, TAG, 'c' * 40)
        (self.root / next(iter(files))).write_bytes(b'corrupt')
        with self.assertRaises(ValueError):
            release.verified_assets(self.root, TAG, COMMIT)

    def test_missing_platform_cannot_publish(self):
        (self.root / 'provenance-linux.json').unlink()
        with self.assertRaises(FileNotFoundError):
            release.verified_assets(self.root, TAG, COMMIT)

    def test_existing_release_or_draft_never_overwritten(self):
        for draft in (True, False):
            with patch.dict(os.environ, self.env), patch.object(release, 'git', return_value=COMMIT), \
                    patch.object(release, 'same_tag'), patch.object(release, 'find_release', return_value={'draft': draft}), \
                    patch.object(release, 'gh') as gh, self.assertRaises(ValueError):
                release.publish(self.root, TAG, COMMIT, TAG_OBJECT)
            gh.assert_not_called()

    def test_api_failure_and_remote_tag_mutation_fail_closed(self):
        with patch.object(release, 'gh', return_value=json.dumps({'object': {'sha': 'c' * 40}})), self.assertRaises(ValueError):
            release.same_tag(TAG, TAG_OBJECT)
        with patch.dict(os.environ, self.env), patch.object(release, 'git', return_value=COMMIT), \
                patch.object(release, 'same_tag'), patch.object(release, 'find_release', side_effect=RuntimeError('API unavailable')), \
                patch.object(release, 'gh') as gh, self.assertRaises(RuntimeError):
            release.publish(self.root, TAG, COMMIT, TAG_OBJECT)
        gh.assert_not_called()

    def test_publication_waits_for_bytes_digest_metadata_and_unchanged_tag(self):
        for failure in (None, 'upload', 'corrupt', 'digest', 'moved-tag'):
            calls, uploaded = [], []
            def gh(*args):
                calls.append(args)
                if args[:2] == ('release', 'upload'):
                    if failure == 'upload': raise RuntimeError('upload failed')
                    for name in args[3:args.index('--repo')]:
                        path = Path(name)
                        uploaded.append({'name': path.name, 'digest': 'sha256:' + release.sha(path),
                                         'size': path.stat().st_size, 'state': 'uploaded'})
                if args[:2] == ('release', 'download'):
                    destination = Path(args[args.index('--dir') + 1])
                    destination.mkdir()
                    for asset in uploaded: shutil.copyfile(self.root / asset['name'], destination / asset['name'])
                    if failure == 'corrupt': (destination / uploaded[0]['name']).write_bytes(b'corrupt')
                    if failure == 'digest': uploaded[0]['digest'] = None
                return '{}'
            with patch.dict(os.environ, self.env), patch.object(release, 'git', return_value=COMMIT), \
                    patch.object(release, 'same_tag', side_effect=[None, ValueError('moved')] if failure == 'moved-tag' else [None, None]), \
                    patch.object(release, 'find_release', side_effect=[None, {'draft': True, 'id': 123}, {'assets': uploaded}]), \
                    patch.object(release, 'gh', side_effect=gh), patch('sys.stderr', io.StringIO()), patch('sys.stdout', io.StringIO()):
                if failure:
                    with self.assertRaises((ValueError, RuntimeError)):
                        release.publish(self.root, TAG, COMMIT, TAG_OBJECT)
                else:
                    release.publish(self.root, TAG, COMMIT, TAG_OBJECT)
            published = [args for args in calls if 'PATCH' in args]
            self.assertEqual(len(published), 0 if failure else 1)
            if published:
                self.assertIn('prerelease=true', published[0])
                self.assertIn('make_latest=false', published[0])
            self.assertFalse(any('--clobber' in args for args in calls))

    def test_only_installed_inno_bootstrap_can_be_32_bit(self):
        folder = self.root / 'payload';folder.mkdir()
        data = bytearray(512);data[:2] = b'MZ';struct.pack_into('<I',data,60,64);data[64:68] = b'PE\0\0'
        struct.pack_into('<HH',data,68,0x14c,0);struct.pack_into('<H',data,84,224);struct.pack_into('<H',data,88,0x10b)
        uninstaller = folder / 'unins000.exe';uninstaller.write_bytes(data)
        with self.assertRaises(ValueError): release.windows_deps(folder)
        with patch('sys.stdout',io.StringIO()): release.windows_deps(folder,allow_inno_uninstaller=True)
        plugin = folder / 'plugin.dll';uninstaller.rename(plugin)
        with self.assertRaises(ValueError): release.windows_deps(folder,allow_inno_uninstaller=True)
        plugin.unlink();nested = folder / 'plugins';nested.mkdir();(nested / 'unins000.exe').write_bytes(data)
        with self.assertRaises(ValueError): release.windows_deps(folder,allow_inno_uninstaller=True)

    def test_inno_imports_use_32_bit_system_libraries(self):
        folder = self.root / 'installed'; folder.mkdir()
        (folder / 'unins000.exe').write_bytes(b'fixture')
        system = self.root / 'Windows'
        (system / 'System32').mkdir(parents=True)
        (system / 'SysWOW64').mkdir()
        (system / 'System32/kernel32.dll').write_bytes(b'64-bit system fixture')
        with patch.dict(os.environ, {'SystemRoot': str(system)}), \
                patch.object(release, 'pe_imports', return_value={'kernel32.dll'}) as parse:
            with self.assertRaises(ValueError): release.windows_deps(folder, allow_inno_uninstaller=True)
            parse.assert_called_with(folder / 'unins000.exe', 0x14c)
            (system / 'SysWOW64/kernel32.dll').write_bytes(b'32-bit system fixture')
            with patch('sys.stdout', io.StringIO()): release.windows_deps(folder, allow_inno_uninstaller=True)

    def test_pe_parser_checks_normal_and_delay_imports_and_architecture(self):
        for machine, magic, optional_size, directory_offset in ((0x8664, 0x20b, 240, 112), (0x14c, 0x10b, 224, 96)):
            with self.subTest(machine=machine):
                data = bytearray(1024)
                data[:2] = b'MZ'
                struct.pack_into('<I', data, 60, 64)
                data[64:68] = b'PE\0\0'
                struct.pack_into('<HH', data, 68, machine, 1)
                struct.pack_into('<H', data, 84, optional_size)
                struct.pack_into('<H', data, 88, magic)
                struct.pack_into('<IIII', data, 88 + optional_size + 8, 512, 0x1000, 512, 512)
                struct.pack_into('<II', data, 88 + directory_offset + 8, 0x1000, 40)
                struct.pack_into('<I', data, 524, 0x1080)
                data[640:653] = b'Qt6Core.dll\0\0'
                struct.pack_into('<II', data, 88 + directory_offset + 13 * 8, 0x1040, 64)
                struct.pack_into('<II', data, 576, 1, 0x10a0)
                data[672:685] = b'vulkan-1.dll\0'
                path = self.root / 'sample.exe'
                path.write_bytes(data)
                self.assertEqual(release.pe_imports(path, machine), {'qt6core.dll', 'vulkan-1.dll'})
                if machine == 0x14c:
                    with self.assertRaises(ValueError): release.pe_imports(path)


if __name__ == '__main__':
    unittest.main()
