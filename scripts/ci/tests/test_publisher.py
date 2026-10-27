"""Hermetic publisher regression tests: no real token, network or release writes."""
import io
import json
import os
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch
from urllib.parse import parse_qs, urlsplit

import test_release as fixtures

release = fixtures.release


class PublisherTests(unittest.TestCase):
    def setUp(self):
        fixtures.ReleaseTests.setUp(self)
        self.tag = fixtures.TAG
        self.failure = None
        self.existing = None
        self.created = False
        self.calls, self.downloads, self.bodies = [], [], []
        self.reads = 0
        self.state = {'id': 123, 'tag_name': self.tag, 'target_commitish': fixtures.COMMIT,
                      'draft': True, 'prerelease': True, 'assets': [],
                      'upload_url': f'https://uploads.github.com/repos/{release.REPO}/releases/123/assets{{?name,label}}'}
        self.blobs = {}

    def gh(self, *args):
        self.calls.append(args)
        self.assertEqual(args[0], 'api', 'No tag-based gh release commands are permitted')
        if '--paginate' in args:
            # Reproduce the GITHUB_TOKEN failure: drafts stay invisible even after POST.
            return json.dumps([[self.existing] if self.existing else []])
        method = args[args.index('--method') + 1] if '--method' in args else 'GET'
        endpoint = next(a for a in args if a.startswith(('repos/', 'https://uploads.')))
        if method == 'POST' and endpoint == f'repos/{release.REPO}/releases':
            if self.failure == 'create-conflict':
                raise RuntimeError('HTTP 422 already_exists')
            self.created = True
            self.assertIn('Content-Type: application/json', args)
            body = json.loads(Path(args[args.index('--input') + 1]).read_text())
            self.bodies.append(body)
            self.assertTrue(body['draft'])
            self.assertTrue(body['generate_release_notes'])
            self.assertEqual(body['tag_name'], self.tag)
            self.assertEqual(body['target_commitish'], fixtures.COMMIT)
            self.assertIn('\n\n', body['body'])
            if self.failure == 'create-response-lost':
                raise RuntimeError('Response lost after server created draft')
            changes = {
                'bad-create-id': {'id': True}, 'bad-create-tag': {'tag_name': 'v9.9.9'},
                'bad-create-target': {'target_commitish': 'main'}, 'bad-create-draft': {'draft': False},
                'bad-upload-url': {'upload_url': 'https://example.invalid/assets'},
                'wrong-upload-id': {'upload_url': f'https://uploads.github.com/repos/{release.REPO}/releases/999/assets'},
                'populated-create': {'assets': [{'name': 'existing.zip'}]},
            }
            return json.dumps({**self.state, **changes.get(self.failure, {})})
        if method == 'POST' and endpoint.startswith('https://uploads.'):
            self.assertTrue(self.created)
            self.assertIn('/releases/123/assets?', endpoint)
            self.assertIn('Content-Type: application/octet-stream', args)
            if self.failure == 'upload':
                raise RuntimeError('Upload failed')
            path = Path(args[args.index('--input') + 1])
            self.assertEqual(parse_qs(urlsplit(endpoint).query), {'name': [path.name]})
            self.assertIn(f'Content-Length: {path.stat().st_size}', args)
            asset = {'id': 1000 + len(self.state['assets']), 'name': path.name,
                     'digest': 'sha256:' + release.sha(path), 'size': path.stat().st_size, 'state': 'uploaded'}
            self.blobs[asset['id']] = path.read_bytes()
            self.state['assets'].append(asset)
            if self.failure == 'upload-id':
                return json.dumps({**asset, 'id': None})
            return json.dumps(asset)
        self.assertEqual(endpoint, f'repos/{release.REPO}/releases/123')
        if method == 'GET':
            self.reads += 1
            if self.failure == 'id-read-denied':
                raise RuntimeError('HTTP 404 for release ID')
            data = json.loads(json.dumps(self.state))
            asset = data['assets'][0]
            if self.failure == 'digest': asset['digest'] = None
            if self.failure == 'size': asset['size'] += 1
            if self.failure == 'state': asset['state'] = 'starter'
            if self.failure == 'changed-asset-id': asset['id'] += 999
            if self.failure == 'missing-asset': data['assets'].pop()
            if self.failure == 'extra-asset': data['assets'].append({**asset, 'name': 'foreign.zip'})
            if self.failure == 'duplicate-asset': data['assets'].append(asset.copy())
            if self.failure == 'changed-release-id': data['id'] = 999
            if self.failure == 'changed-tag': data['tag_name'] = 'v9.9.9'
            if self.failure == 'changed-target': data['target_commitish'] = 'c' * 40
            if self.failure == 'changed-channel': data['prerelease'] = False
            if self.failure == 'already-public': data['draft'] = False
            if self.failure == 'changed-after-download' and self.reads == 2: asset['size'] += 1
            return json.dumps(data)
        self.assertEqual(method, 'PATCH')
        self.assertEqual(len(self.downloads), 5, 'Publish must follow five verified downloads')
        self.assertEqual(self.reads, 2, 'Draft must be rechecked after downloads')
        self.assertIn('draft=false', args)
        self.state['draft'] = False
        return json.dumps(self.state)

    def download(self, args, **kwargs):
        self.assertEqual(args[:2], ['gh', 'api'])
        self.assertEqual(args[-2:], ['-H', 'Accept: application/octet-stream'])
        self.assertTrue(kwargs['check'])
        self.assertNotIn('text', kwargs)
        asset_id = int(args[2].rsplit('/', 1)[1])
        self.assertEqual(args[2], f'repos/{release.REPO}/releases/assets/{asset_id}')
        if self.failure == 'download':
            raise subprocess.CalledProcessError(1, args)
        self.downloads.append(asset_id)
        kwargs['stdout'].write(b'corrupt' if self.failure == 'corrupt' else self.blobs[asset_id])
        return subprocess.CompletedProcess(args, 0)

    def publish(self):
        with patch.dict(os.environ, self.env), patch.object(release, 'git', return_value=fixtures.COMMIT), \
                patch.object(release, 'gh', side_effect=self.gh), \
                patch.object(release.subprocess, 'run', side_effect=self.download), \
                patch.object(release, 'same_tag', side_effect=[None, ValueError('Tag moved')] if self.failure == 'moved-tag' else [None, None]), \
                patch('sys.stdout', io.StringIO()), patch('sys.stderr', io.StringIO()):
            release.publish(self.root, self.tag, fixtures.COMMIT, fixtures.TAG_OBJECT)

    def mutations(self, method):
        return [c for c in self.calls if '--method' in c and c[c.index('--method') + 1] == method]

    def test_draft_omitted_from_release_listing_still_publishes_by_id(self):
        self.publish()
        self.assertEqual(len([c for c in self.calls if '--paginate' in c]), 1)
        with patch.object(release, 'gh', side_effect=self.gh):
            self.assertIsNone(release.find_release(self.tag))  # The listing still omits this release.
        self.assertEqual(len(self.mutations('PATCH')), 1)
        patch_call = self.mutations('PATCH')[0]
        self.assertIn('prerelease=true', patch_call)
        self.assertIn('make_latest=false', patch_call)
        self.assertFalse(any('DELETE' in c or '--clobber' in c for c in self.calls))

    def test_every_transfer_or_identity_failure_prevents_publication(self):
        failures = ('create-conflict', 'create-response-lost', 'bad-create-id', 'bad-create-tag', 'bad-create-target',
                    'bad-create-draft', 'bad-upload-url', 'wrong-upload-id', 'populated-create', 'upload', 'upload-id',
                    'id-read-denied', 'download', 'corrupt', 'digest', 'size', 'state', 'changed-asset-id',
                    'missing-asset', 'extra-asset', 'duplicate-asset', 'changed-release-id', 'changed-tag',
                    'changed-target', 'changed-channel', 'already-public', 'changed-after-download', 'moved-tag')
        for failure in failures:
            with self.subTest(failure=failure):
                self.calls, self.downloads, self.bodies, self.blobs = [], [], [], {}
                self.state['assets'] = []; self.state['draft'] = True; self.reads = 0; self.created = False
                self.failure = failure
                with self.assertRaises((ValueError, RuntimeError, subprocess.CalledProcessError)):
                    self.publish()
                self.assertEqual(self.mutations('PATCH'), [])
                self.assertEqual(len([c for c in self.mutations('POST') if c[3] == f'repos/{release.REPO}/releases']), 1)
                self.assertFalse(any('DELETE' in c or '--clobber' in c for c in self.calls))

    def test_existing_draft_or_release_never_reused_or_modified(self):
        for draft in (True, False):
            self.existing = {'tag_name': self.tag, 'id': 999, 'draft': draft}
            with self.assertRaises(ValueError): self.publish()
            self.assertFalse(self.mutations('POST') or self.mutations('PATCH'))

    def test_stable_release_keeps_version_based_latest_policy(self):
        self.tag = 'v0.2.0'
        self.env.update(GITHUB_REF='refs/tags/' + self.tag, GITHUB_REF_NAME=self.tag)
        self.state.update(tag_name=self.tag, prerelease=False)
        for platform in release.PLATFORMS:
            path = self.root / f'provenance-{platform}.json'
            data = json.loads(path.read_text())
            data.update(tag=self.tag, app_version=self.tag[1:])
            files = {}
            for name, digest in data['assets'].items():
                updated = name.replace(fixtures.TAG, self.tag)
                (self.root / name).rename(self.root / updated)
                files[updated] = digest
            data['assets'] = files
            release.write_json(path, data)
        self.publish()
        self.assertIn('prerelease=false', self.mutations('PATCH')[0])
        self.assertIn('make_latest=legacy', self.mutations('PATCH')[0])

    def test_missing_platform_or_corrupt_build_never_creates_draft(self):
        for corrupt in (True, False):
            path = self.root / 'provenance-linux.json'
            if corrupt:
                data = json.loads(path.read_text())
                (self.root / next(iter(data['assets']))).write_bytes(b'corrupt')
            else:
                path.unlink()
            with self.assertRaises((ValueError, FileNotFoundError)): self.publish()
            self.assertEqual(self.calls, [])

    def test_asset_download_preserves_binary_bytes(self):
        destination = self.root / 'binary.bin'
        payload = b'\x00\xff\xfe\r\n\x1b[31m binary\x00'
        def binary(args, **kwargs):
            self.assertEqual(args[2], f'repos/{release.REPO}/releases/assets/456')
            self.assertNotIn('text', kwargs)
            kwargs['stdout'].write(payload)
        with patch.object(release.subprocess, 'run', side_effect=binary):
            release.download_release_asset(456, destination)
        self.assertEqual(destination.read_bytes(), payload)


if __name__ == '__main__':
    unittest.main()
