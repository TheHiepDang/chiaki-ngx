#!/usr/bin/env python3
"""Audit every packaged ELF with the host loader, before native startup.

Only run on trusted, locally built packages: ldd may invoke an ELF interpreter.
The explicit host policy follows the pinned linuxdeploy exclusions, not whatever
happens to be installed on the CI runner. No builder library paths are inherited.
"""
import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess

POLICY = Path(__file__).with_name('linux-runtime.json')


def command(args, env):
    result = subprocess.run(args, env=env, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=30, check=False)
    return result.returncode, result.stdout


def audit(root, policy, run=command):
    root = Path(root).resolve()
    hosts = {name: group['purpose'] for group in policy['host_groups'] for name in group['sonames']}
    env = {k: v for k, v in os.environ.items()
           if k not in ('LD_PRELOAD', 'LD_AUDIT', 'LD_LIBRARY_PATH') and not k.startswith(('QT_', 'QML'))}
    env.update(LC_ALL='C', LD_LIBRARY_PATH=str(root / 'usr/lib'))
    report = {'schema': 1, 'elfs': [], 'unresolved': [], 'errors': []}
    unresolved, errors, seen = set(), set(), set()
    for path in sorted(root.rglob('*')):
        if path.is_symlink():
            target = path.resolve()
            if not target.is_relative_to(root) or not target.exists():
                errors.add(f'Escaping or broken package link: {path.relative_to(root)}')
                continue
        if not path.is_file():
            continue
        real = path.resolve()
        if real in seen:
            continue
        seen.add(real)
        with path.open('rb') as stream:
            header = stream.read(64)
        if not header.startswith(b'\x7fELF'):
            continue  # Includes the shell AppRun wrapper; its delegated ELF is scanned.
        name = real.relative_to(root).as_posix()
        entry = {'path': name, 'needed': {}, 'rpaths': [], 'loader_output': ''}
        report['elfs'].append(entry)
        if len(header) < 64 or header[4:6] != b'\x02\x01' or struct.unpack_from('<H', header, 18)[0] != 62:
            errors.add(f'Not an x86_64 little-endian ELF: {name}')
            continue
        if re.search(r'(?:^|/)(?:lib(?:nvidia|cuda|GLX_nvidia|EGL_nvidia)[^/]*\.so|[^/]+_dri\.so|[^/]+_drv_video\.so)', name):
            errors.add(f'GPU vendor driver must remain on host: {name}')
        try:
            code, dynamic = run(['readelf', '--wide', '--dynamic', str(real)], env)
            if code:
                errors.add(f'readelf failed for {name}: {dynamic.strip()}')
                continue
            needed = re.findall(r'\(NEEDED\).*?\[([^\]]+)\]', dynamic)
            entry['rpaths'] = re.findall(r'\((?:RUNPATH|RPATH)\).*?\[([^\]]*)\]', dynamic)
            if not needed:
                continue  # Static launchers have no runtime library closure.
            code, output = run(['ldd', str(real)], env)
            entry['loader_output'] = output
            missing = set(re.findall(r'^\s*(\S+)\s+=>\s+not found', output, re.M))
            unresolved.update(missing)
            resolved = dict(re.findall(r'^\s*(\S+)\s+=>\s+((?:/|[A-Za-z]:[\\/])[^\s]+)', output, re.M))
            # glibc prints the ELF interpreter as a bare path rather than an arrow.
            for target in re.findall(r'^\s*(/[^\s]+)\s+\(0x[0-9a-f]+\)', output, re.M):
                resolved[Path(target).name] = target
            if code:
                errors.add(f'Host loader rejected {name} (exit {code}); see loader_output')
            # ldd checks the complete transitive closure, including host libraries.
            # Each bundled ELF is also checked directly so a host Qt/SDL installation
            # cannot conceal an omitted plugin or application dependency.
            for dependency in needed:
                target = resolved.get(dependency)
                if dependency in missing or not target:
                    unresolved.add(dependency)
                    category = 'missing host interface' if dependency in hosts else 'packaging gap'
                    errors.add(f'{category}: {name} -> {dependency}')
                elif Path(target).resolve().is_relative_to(root):
                    category = 'bundled'
                elif dependency in hosts:
                    category = 'host: ' + hosts[dependency]
                else:
                    category = 'packaging gap'
                    errors.add(f'Unbundled application dependency: {name} -> {dependency} ({target})')
                entry['needed'][dependency] = {'classification': category, 'resolved': target}
        except (OSError, subprocess.TimeoutExpired) as exc:
            errors.add(f'ELF audit failed for {name}: {exc}')
    if not report['elfs']:
        errors.add('Package contains no ELF executables or libraries')
    report['unresolved'], report['errors'] = sorted(unresolved), sorted(errors)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--report', required=True, type=Path)
    args = parser.parse_args()
    result = audit(args.root, json.loads(POLICY.read_text()))
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(f'[App][Package] Audited {len(result["elfs"])} ELF files; report: {args.report}')
    print('[App][Package] Complete unresolved set: ' + (', '.join(result['unresolved']) or '(none)'))
    for error in result['errors']:
        print('[App][Package] ' + error)
    raise SystemExit(1 if result['errors'] or result['unresolved'] else 0)


if __name__ == '__main__':
    main()
