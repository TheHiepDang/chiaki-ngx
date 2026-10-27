"""Cross-platform ELF policy fixtures; the real loader is exercised in Linux CI."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('audit_appimage', Path(__file__).resolve().parents[1] / 'audit-appimage.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class AppImageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.policy = json.loads(audit.POLICY.read_text())
        self.responses = {}

    def elf(self, name, needed=(), machine=62):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        header = bytearray(64); header[:6] = b'\x7fELF\x02\x01'
        struct.pack_into('<H', header, 18, machine)
        path.write_bytes(header)
        self.responses[str(path.resolve())] = (needed, {})
        return path

    def run_tool(self, args, env):
        needed, resolved = self.responses[args[-1]]
        self.assertEqual(env['LD_LIBRARY_PATH'], str(self.root / 'usr/lib'))
        if args[0] == 'readelf':
            return 0, '\n'.join(f' (NEEDED) Shared library: [{name}]' for name in needed)
        return 0, '\n'.join(f'{name} => {resolved.get(name, "not found")}' for name in needed)

    def check(self):
        return audit.audit(self.root, self.policy, self.run_tool)

    def test_complete_missing_set_includes_executable_library_and_qt_plugin(self):
        self.elf('AppRun', ['libOpenGL.so.0'])
        self.elf('usr/bin/chiaki', ['libva-drm.so.2'])
        self.elf('usr/lib/libQt6Gui.so.6', ['libmissing.so.1'])
        self.elf('usr/plugins/platforms/libqxcb.so', ['libQt6XcbQpa.so.6'])
        report = self.check()
        self.assertEqual(len(report['elfs']), 4)
        self.assertEqual(report['unresolved'], ['libOpenGL.so.0', 'libQt6XcbQpa.so.6', 'libmissing.so.1', 'libva-drm.so.2'])
        self.assertTrue(any('missing host interface' in e for e in report['errors']))
        self.assertTrue(any('packaging gap' in e for e in report['errors']))

    def test_host_qt_cannot_hide_incomplete_package(self):
        path = self.elf('usr/bin/chiaki', ['libQt6Core.so.6'])
        self.responses[str(path)] = (['libQt6Core.so.6'], {'libQt6Core.so.6': '/usr/lib/libQt6Core.so.6'})
        self.assertIn('Unbundled application dependency', self.check()['errors'][0])

    def test_bundled_application_and_explicit_host_graphics_resolve(self):
        library = self.elf('usr/lib/libQt6Core.so.6')
        path = self.elf('usr/bin/chiaki', ['libQt6Core.so.6', 'libOpenGL.so.0'])
        self.responses[str(path)] = (['libQt6Core.so.6', 'libOpenGL.so.0'],
                                    {'libQt6Core.so.6': str(library), 'libOpenGL.so.0': '/usr/lib/libOpenGL.so.0'})
        self.assertEqual(self.check()['errors'], [])

    def test_gpu_vendor_drivers_must_not_be_bundled(self):
        for name in ('libnvidia-glcore.so.1', 'libGLX_nvidia.so.0', 'iris_dri.so', 'iHD_drv_video.so'):
            self.elf('usr/lib/' + name)
        self.assertEqual(len(self.check()['errors']), 4)

    def test_wrong_architecture_is_rejected(self):
        self.elf('usr/lib/plugin.so', machine=183)
        self.assertIn('Not an x86_64', self.check()['errors'][0])

    def test_loader_failure_is_not_silently_ignored(self):
        self.elf('usr/bin/chiaki', ['libc.so.6'])
        def failed(args, env):
            return self.run_tool(args, env) if args[0] == 'readelf' else (1, 'version GLIBC_X not found')
        self.assertTrue(audit.audit(self.root, self.policy, failed)['errors'])

    def test_empty_package_fails(self):
        self.assertTrue(self.check()['errors'])

    def test_direct_interpreter_requirement_accepts_ldd_bare_path_format(self):
        self.elf('usr/lib/libQt6Core.so.6', ['ld-linux-x86-64.so.2'])
        def loader(args, env):
            return self.run_tool(args, env) if args[0] == 'readelf' else (0, '/lib64/ld-linux-x86-64.so.2 (0x1234)')
        self.assertEqual(audit.audit(self.root, self.policy, loader)['errors'], [])

    def test_unresolved_transitive_dependencies_are_also_collected(self):
        self.elf('usr/bin/chiaki', ['libGL.so.1'])
        def transitive(args, env):
            return self.run_tool(args, env) if args[0] == 'readelf' else (0, 'libGL.so.1 => /lib/libGL.so.1\nlibGLX.so.0 => not found')
        self.assertEqual(audit.audit(self.root, self.policy, transitive)['unresolved'], ['libGLX.so.0'])


if __name__ == '__main__':
    unittest.main()
