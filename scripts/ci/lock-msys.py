#!/usr/bin/env python3
"""Explicit maintainer operation: refresh the reviewed MSYS2 CLANG64 dependency lock."""
import io
import json
from pathlib import Path
import re
import subprocess
import tarfile
import urllib.request

path = Path(__file__).with_name('dependencies.json')
lock = json.loads(path.read_text())
data = urllib.request.urlopen(lock['msys']['base_url'] + 'clang64.db', timeout=60).read()
if data.startswith(b'\x28\xb5\x2f\xfd'):
    data = subprocess.check_output(['zstd', '-dc'], input=data)
packages = {}
with tarfile.open(fileobj=io.BytesIO(data)) as archive:
    for member in archive:
        if member.name.endswith('/desc'):
            parts = archive.extractfile(member).read().decode().strip().split('\n\n')
            fields = {part.splitlines()[0].strip('%'): part.splitlines()[1:] for part in parts}
            packages[fields['NAME'][0]] = fields
prefix = 'mingw-w64-clang-x86_64-'
roots = ('clang cmake curl fast_float fftw hidapi json-c libevent lcms2 libdovi meson miniupnpc '
         'nasm ninja openssl opus pkgconf protobuf python python-psutil python-glad python-jinja '
         'python-protobuf qt6-base qt6-declarative qt6-svg shaderc speexdsp spirv-cross vulkan-loader '
         'vulkan-headers compiler-rt').split()
providers = {re.split('[<>=]', value)[0]: name for name, package in sorted(packages.items())
             for value in package.get('PROVIDES', [])}
selected, pending = {}, [prefix + name for name in roots]
while pending:
    name = re.split('[<>=]', pending.pop())[0]
    if name in selected:
        continue
    name = name if name in packages else providers[name]
    package = packages[name]
    selected[name] = package
    pending.extend(package.get('DEPENDS', []))
lock['msys']['packages'] = {package['FILENAME'][0]: package['SHA256SUM'][0]
                            for name, package in sorted(selected.items())}
path.write_text(json.dumps(lock, indent=2) + '\n', encoding='utf-8')
print(f'Updated {len(selected)} locked packages. Review the diff and run validation before tagging.')
