#!/usr/bin/env python3
"""Release identity, locked downloads, package checks and draft-first publishing."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
REPO = 'TheHiepDang/chiaki-ngx'
LOCK = Path(__file__).with_name('dependencies.json')
PLATFORMS = {
    'windows': ('chiaki-ngx-windows-x64-{tag}.zip', 'chiaki-ngx-windows-x64-{tag}-installer.exe'),
    'linux': ('chiaki-ngx-linux-x86_64-{tag}.AppImage',),
}


def version(tag):
    match = re.fullmatch(r'v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?', tag)
    if not match or len(tag) > 128 or any(int(match[n]) > 65535 for n in (1, 2, 3)):
        raise ValueError('Use vMAJOR.MINOR.PATCH[-rc.N], with numeric components <= 65535')
    pre = match[4] or ''
    if any(p.isdigit() and len(p) > 1 and p.startswith('0') for p in pre.split('.')):
        raise ValueError('Prerelease numeric identifiers cannot have leading zeroes')
    return bool(pre)


def command(*args):
    return subprocess.check_output(list(args), text=True, encoding='utf-8').strip()


def git(*args):
    return command('git', '-C', str(ROOT), *args)


def sha(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def write_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')


def outputs(data):
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as stream:
            for key, value in data.items():
                stream.write(f'{key}={value}\n')
    print(json.dumps(data))


def prepare(tag, target):
    if target not in ('all', 'windows', 'linux'):
        raise ValueError('Unknown validation platform')
    commit = git('rev-parse', 'HEAD')
    if commit != os.environ.get('GITHUB_SHA'):
        raise ValueError('Checkout does not match the triggering commit')
    event = os.environ.get('GITHUB_EVENT_NAME')
    tag_object = ''
    if event == 'push':
        tag = os.environ.get('GITHUB_REF_NAME', '')
        version(tag)
        if os.environ.get('GITHUB_REF') != 'refs/tags/' + tag:
            raise ValueError('Publication requires a version tag push')
        if git('rev-parse', f'refs/tags/{tag}^{{commit}}') != commit:
            raise ValueError('Tag/checkout mismatch')
        command('git', '-C', str(ROOT), 'merge-base', '--is-ancestor', commit, 'origin/main')
        tag_object = git('rev-parse', 'refs/tags/' + tag)
        target = 'all'
    elif event in ('workflow_dispatch', 'schedule', 'workflow_call', 'pull_request'):
        if not tag:
            baseline = (ROOT / 'cmake/Version.txt').read_text().strip().split('-')[0]
            if baseline.count('.') == 1:
                baseline += '.0'
            tag = f'v{baseline}-ci.{os.environ.get("GITHUB_RUN_NUMBER", "0")}'
        version(tag)
    else:
        raise ValueError('Unsupported release event')
    outputs({'tag': tag, 'commit': commit, 'tag_object': tag_object,
             'windows': str(target in ('all', 'windows')).lower(),
             'linux': str(target in ('all', 'linux')).lower()})


def stamp(tag, commit):
    version(tag)
    if git('rev-parse', 'HEAD') != commit:
        raise ValueError('Build is not using the selected commit')
    source = {'repository': REPO, 'tag': tag, 'app_version': tag[1:], 'source_commit': commit,
              'source_tree': git('rev-parse', 'HEAD^{tree}'),
              'submodules': git('submodule', 'status', '--recursive').splitlines(),
              'original_version': (ROOT / 'cmake/Version.txt').read_text().strip(),
              'version_overlay': tag[1:], 'dependencies_lock_sha256': sha(LOCK)}
    # The tag controls compiled version/resources; the commit and original value stay recorded.
    (ROOT / 'cmake/Version.txt').write_text(tag[1:] + '\n', encoding='ascii')
    write_json(ROOT / 'build/ci/source.json', source)


def download_file(url, expected, destination):
    if not url.startswith('https://') or not re.fullmatch('[0-9a-f]{64}', expected):
        raise ValueError('A locked HTTPS URL and SHA-256 are required')
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file() and sha(destination) == expected:
        return
    temporary = destination.with_name(destination.name + '.part')
    try:
        with urllib.request.urlopen(url, timeout=120) as response, temporary.open('wb') as stream:
            shutil.copyfileobj(response, stream)
        if sha(temporary) != expected:
            raise ValueError(f'Dependency checksum mismatch: {destination.name}')
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)


def downloads(group, folder):
    lock = json.loads(LOCK.read_text())
    if group == 'msys':
        package = lock['msys']
        jobs = [(package['base_url'] + name, digest, Path(folder) / name)
                for name, digest in package['packages'].items()]
        with ThreadPoolExecutor(max_workers=6) as pool:
            list(pool.map(lambda args: download_file(*args), jobs))
    else:
        item = lock['downloads'][group]
        download_file(item['url'], item['sha256'], folder)


def pe_imports(path, machine=0x8664):
    """Read normal and delay-load PE imports without loading untrusted DLLs."""
    data = Path(path).read_bytes()
    if data[:2] != b'MZ':
        raise ValueError(f'Not a Windows executable: {path}')
    pe = struct.unpack_from('<I', data, 60)[0]
    if data[pe:pe + 4] != b'PE\0\0' or struct.unpack_from('<H', data, pe + 4)[0] != machine:
        raise ValueError(f'Unexpected Windows PE architecture (expected {machine:#x}): {path}')
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional_size = struct.unpack_from('<H', data, pe + 20)[0]
    optional = pe + 24
    is64 = machine == 0x8664
    if struct.unpack_from('<H', data, optional)[0] != (0x20b if is64 else 0x10b):
        raise ValueError('PE optional header does not match the expected architecture')
    sections = optional + optional_size
    image_base = struct.unpack_from('<Q' if is64 else '<I', data, optional + (24 if is64 else 28))[0]
    def offset(rva):
        for i in range(count):
            section = sections + 40 * i
            virtual_size, address, size, start = struct.unpack_from('<IIII', data, section + 8)
            if address <= rva < address + max(size, virtual_size):
                result = start + rva - address
                if result >= len(data):
                    break
                return result
        raise ValueError(f'Invalid PE RVA in {path}')
    names = set()
    for index, entry_size, name_field in ((1, 20, 12), (13, 32, 4)):
        rva, length = struct.unpack_from('<II', data, optional + (112 if is64 else 96) + index * 8)
        if not rva:
            continue
        start = offset(rva)
        for item in range(start, min(start + length, len(data)), entry_size):
            if not any(data[item:item + entry_size]):
                break
            name = struct.unpack_from('<I', data, item + name_field)[0]
            if index == 13 and not struct.unpack_from('<I', data, item)[0] & 1:
                name -= image_base
            begin = offset(name)
            end = data.index(b'\0', begin)
            names.add(data[begin:end].decode('ascii').lower())
    return names


def windows_deps(folder, allow_inno_uninstaller=False):
    folder = Path(folder)
    files = [p for p in folder.rglob('*') if p.is_file() and p.suffix.lower() in ('.dll', '.exe')]
    if not files:
        raise ValueError('No packaged Windows executables')
    system = Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32'
    root_dlls = {p.name.lower() for p in folder.iterdir() if p.is_file()}
    for path in files:
        adjacent = {p.name.lower() for p in path.parent.iterdir() if p.is_file()}
        # Only Inno's generated root uninstaller may be PE32, and only after installation.
        inno = allow_inno_uninstaller and path.parent == folder and path.name.lower() == 'unins000.exe'
        import_system = system.parent / 'SysWOW64' if inno else system
        for name in pe_imports(path, 0x14c if inno else 0x8664):
            if name.startswith(('api-ms-win-', 'ext-ms-')) or (not inno and name in adjacent | root_dlls):
                continue
            if not (import_system / name).is_file():
                raise ValueError(f'Missing packaged dependency: {path.relative_to(folder)} -> {name}')
    print(f'[App][Package] Resolved imports for {len(files)} application executables/plugins and permitted installer bootstrap files')


def assets(platform, folder):
    folder = Path(folder)
    source = json.loads((ROOT / 'build/ci/source.json').read_text())
    if git('rev-parse', 'HEAD') != source['source_commit']:
        raise ValueError('Source commit changed during build')
    files = {}
    for pattern in PLATFORMS[platform]:
        name = pattern.format(tag=source['tag'])
        path = folder / name
        if not path.is_file() or path.stat().st_size <= 0:
            raise ValueError(f'Missing release asset: {name}')
        if platform == 'windows' and name.endswith('.zip') and path.stat().st_size > 1024 ** 3:
            raise ValueError('Windows ZIP exceeds the automatic updater\'s 1 GiB limit')
        files[name] = sha(path)
    source.update(platform=platform, assets=files, tests='passed', gpu_hardware_tested=False)
    write_json(folder / f'provenance-{platform}.json', source)
    (folder / f'SHA256SUMS-{platform}').write_text(
        ''.join(f'{digest}  {name}\n' for name, digest in sorted(files.items())), encoding='ascii')


def verified_assets(folder, tag, commit):
    version(tag)
    if not re.fullmatch('[0-9a-f]{40}', commit):
        raise ValueError('Invalid source commit')
    folder = Path(folder)
    files, provenance = {}, []
    for platform, patterns in PLATFORMS.items():
        manifest = json.loads((folder / f'provenance-{platform}.json').read_text())
        expected = {pattern.format(tag=tag) for pattern in patterns}
        if (manifest['tag'] != tag or manifest['source_commit'] != commit
                or manifest['app_version'] != tag[1:] or manifest['tests'] != 'passed'
                or set(manifest['assets']) != expected):
            raise ValueError(f'{platform} build provenance mismatch')
        for name, digest in manifest['assets'].items():
            if platform == 'windows' and name.endswith('.zip') and (folder / name).stat().st_size > 1024 ** 3:
                raise ValueError('Windows ZIP exceeds the automatic updater\'s 1 GiB limit')
            if sha(folder / name) != digest:
                raise ValueError(f'Artifact checksum mismatch: {name}')
            files[name] = digest
        provenance.append(manifest)
    return files, provenance


def gh(*args):
    return command('gh', *args)


def same_tag(tag, expected):
    actual = json.loads(gh('api', f'repos/{REPO}/git/ref/tags/{tag}'))
    if actual['object']['sha'] != expected:
        raise ValueError('Remote tag changed; publication refused')


def find_release(tag):
    pages = json.loads(gh('api', '--paginate', '--slurp', f'repos/{REPO}/releases?per_page=100'))
    return next((r for page in pages for r in page if r['tag_name'] == tag), None)


def release_identity(data, release_id, tag, commit, pre, draft=True):
    if (type(release_id) is not int or release_id <= 0 or data.get('id') != release_id
            or data.get('tag_name') != tag or data.get('target_commitish') != commit
            or data.get('draft') is not draft or data.get('prerelease') is not pre):
        raise ValueError('Release ID, tag, target, draft or channel identity mismatch')


def uploaded_assets(data, expected, folder, uploaded_ids):
    assets = data['assets']
    api_assets = {a['name']: a for a in assets}
    if len(assets) != len(api_assets) or set(api_assets) != set(expected):
        raise ValueError('Unexpected or duplicate draft asset list')
    for name, digest in expected.items():
        asset = api_assets[name]
        if (asset.get('id') != uploaded_ids[name] or asset.get('state') != 'uploaded'
                or asset.get('digest') != 'sha256:' + digest
                or asset.get('size') != (folder / name).stat().st_size):
            raise ValueError(f'Incomplete or changed GitHub asset metadata: {name}')
    return api_assets


def download_release_asset(asset_id, destination):
    """Stream authenticated bytes by asset ID; never discover a draft by tag.

    gh api handles the documented direct-200 / redirect-302 download responses.
    stdout is a binary file, so assets are never decoded as text or held in RAM.
    """
    with Path(destination).open('wb') as stream:
        subprocess.run(['gh', 'api', f'repos/{REPO}/releases/assets/{asset_id}',
                        '-H', 'Accept: application/octet-stream'], stdout=stream, check=True)


def publish(folder, tag, commit, tag_object):
    pre = version(tag)
    if (os.environ.get('GITHUB_ACTIONS') != 'true' or os.environ.get('GITHUB_EVENT_NAME') != 'push'
            or os.environ.get('GITHUB_REF') != 'refs/tags/' + tag or os.environ.get('GITHUB_REPOSITORY') != REPO):
        raise ValueError('Only this repository\'s version tag pushes may publish')
    if git('rev-parse', 'HEAD') != commit or not re.fullmatch('[0-9a-f]{40}', tag_object):
        raise ValueError('Publisher source identity mismatch')
    folder = Path(folder)
    files, provenance = verified_assets(folder, tag, commit)
    write_json(folder / 'BUILD-PROVENANCE.json', provenance)
    checksums = ''.join(f'{digest}  {name}\n' for name, digest in sorted(files.items()))
    (folder / 'SHA256SUMS').write_text(checksums, encoding='ascii')
    paths = [folder / name for name in files] + [folder / 'SHA256SUMS', folder / 'BUILD-PROVENANCE.json']
    expected = {p.name: sha(p) for p in paths}
    same_tag(tag, tag_object)
    if find_release(tag):
        raise ValueError('A release or draft already exists; never overwrite it')
    with tempfile.TemporaryDirectory(prefix='chiaki-release-') as temporary:
        request = Path(temporary) / 'create.json'
        write_json(request, {
            'tag_name': tag, 'target_commitish': commit, 'name': f'Chiaki-ngx {tag}',
            'draft': True, 'prerelease': pre, 'generate_release_notes': True,
            'body': f'Source: https://github.com/{REPO}/commit/{commit}\n\n'
                    f'Build: https://github.com/{REPO}/actions/runs/{os.environ.get("GITHUB_RUN_ID", "")}\n\n'
                    'Windows x64: portable ZIP (automatic updates) or installer. Linux x86_64: AppImage. '
                    'NR DLL is user supplied; Windows FG/SM86 runtimes are bundled. '
                    'See SHA256SUMS and BUILD-PROVENANCE.json for verification.\n\n',
        })
        release_id = None
        try:
            # POST creates only; never adopt an existing draft or retry a mutation.
            # Its response is authoritative: GITHUB_TOKEN release listings may omit drafts.
            created = json.loads(gh('api', '--method', 'POST', f'repos/{REPO}/releases',
                                    '-H', 'Content-Type: application/json', '--input', str(request)))
            release_id = created.get('id')
            release_identity(created, release_id, tag, commit, pre)
            if created.get('assets') != []:
                raise ValueError('New draft unexpectedly contains assets')
            endpoint = f'repos/{REPO}/releases/{release_id}'
            upload = f'https://uploads.github.com/{endpoint}/assets'
            if created.get('upload_url') not in (upload, upload + '{?name,label}'):
                raise ValueError('Unexpected release upload URL')
            upload = created['upload_url'].removesuffix('{?name,label}')
            print(f'[Release] Created draft ID {release_id} for {tag}; subsequent operations use this ID.')
            if os.environ.get('GITHUB_STEP_SUMMARY'):
                with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf-8') as stream:
                    stream.write(f'Created draft release ID **{release_id}** for `{tag}`. '
                                 f'Inspect with `gh api {endpoint}` if publication fails.\n')
            uploaded_ids = {}
            for path in paths:
                asset = json.loads(gh('api', '--method', 'POST', upload + '?' + urllib.parse.urlencode({'name': path.name}),
                                      '-H', 'Content-Type: application/octet-stream',
                                      '-H', f'Content-Length: {path.stat().st_size}', '--input', str(path)))
                asset_id = asset.get('id')
                if (type(asset_id) is not int or asset_id <= 0 or asset_id in uploaded_ids.values()
                        or asset.get('name') != path.name):
                    raise ValueError('Upload returned an invalid asset identity')
                uploaded_ids[path.name] = asset_id
            uploaded = json.loads(gh('api', endpoint))
            release_identity(uploaded, release_id, tag, commit, pre)
            api_assets = uploaded_assets(uploaded, expected, folder, uploaded_ids)
            check = Path(temporary) / 'downloaded'
            check.mkdir()
            for name, asset in api_assets.items():
                download_release_asset(asset['id'], check / name)
            if {p.name: sha(p) for p in check.iterdir() if p.is_file()} != expected:
                raise ValueError('Downloaded release assets differ from the verified build')
            # Recheck both draft metadata and the immutable tag identity after transfers.
            uploaded = json.loads(gh('api', endpoint))
            release_identity(uploaded, release_id, tag, commit, pre)
            uploaded_assets(uploaded, expected, folder, uploaded_ids)
            same_tag(tag, tag_object)
            published = json.loads(gh('api', '--method', 'PATCH', endpoint,
                                     '-F', 'draft=false', '-F', 'prerelease=' + str(pre).lower(),
                                     '-f', 'make_latest=' + ('false' if pre else 'legacy')))
            release_identity(published, release_id, tag, commit, pre, draft=False)
        except Exception:
            if type(release_id) is int and release_id > 0:
                print(f'::error::[Release] Publication interrupted for release ID {release_id}. '
                      f'Inspect with gh api repos/{REPO}/releases/{release_id}; it may be draft or published. '
                      'No automatic cleanup or retry was attempted.', file=sys.stderr)
            else:
                print('::error::[Release] Creation may have failed or its response was lost. '
                      'Inspect releases as a maintainer before retrying; no automatic cleanup was attempted.', file=sys.stderr)
            raise
    print(f'Published https://github.com/{REPO}/releases/tag/{tag}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=('prepare', 'stamp', 'download', 'windows-deps', 'assets', 'publish'))
    parser.add_argument('--tag', default='')
    parser.add_argument('--commit')
    parser.add_argument('--tag-object')
    parser.add_argument('--target', default='all')
    parser.add_argument('--group')
    parser.add_argument('--platform', choices=PLATFORMS)
    parser.add_argument('--folder', default='dist')
    parser.add_argument('--allow-inno-uninstaller', action='store_true')
    args = parser.parse_args()
    try:
        if args.operation == 'prepare': prepare(args.tag, args.target)
        elif args.operation == 'stamp': stamp(args.tag, args.commit)
        elif args.operation == 'download': downloads(args.group, args.folder)
        elif args.operation == 'windows-deps': windows_deps(args.folder, args.allow_inno_uninstaller)
        elif args.operation == 'assets': assets(args.platform, args.folder)
        else: publish(args.folder, args.tag, args.commit, args.tag_object)
        return 0
    except Exception as error:
        print(f'::error::[Release] {args.operation} failed: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
