"""Release identity must survive a source archive without Git metadata."""
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CMAKE = sys.argv.pop(1) if len(sys.argv) > 1 else 'cmake'
sys.path.insert(0, str(ROOT / 'scripts'))
from package_identity import read_package_identity


class BuildIdentityTest(unittest.TestCase):
    def configure(self, *args):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source'
            source.mkdir()
            (source / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.16)\nproject(identity NONE)\n'
                f'include("{(ROOT / "cmake/ViewerBuildIdentity.cmake").as_posix()}")\n'
                'file(WRITE "${CMAKE_BINARY_DIR}/identity.txt" '
                '"${PRISM_VIEWER_REVISION};${PRISM_VIEWER_BUILD_EPOCH}")\n')
            env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(root))
            result = subprocess.run([CMAKE, '-S', str(source), '-B', str(root / 'build'), *args],
                                    capture_output=True, text=True, env=env)
            output = root / 'build/identity.txt'
            return result.returncode, output.read_text() if output.exists() else ''

    def test_explicit_identity_survives_no_git(self):
        self.assertEqual(self.configure('-DPRISM_REQUIRE_RELEASE_IDENTITY=ON',
                         '-DPRISM_VIEWER_REVISION=' + 'a' * 40,
                         '-DPRISM_VIEWER_BUILD_EPOCH=1794372234'),
                         (0, 'a' * 40 + ';1794372234'))

    def test_release_cannot_silently_fallback(self):
        self.assertNotEqual(self.configure('-DPRISM_REQUIRE_RELEASE_IDENTITY=ON')[0], 0)
        self.assertEqual(self.configure(), (0, 'source-build;0'))

    def test_reject_invalid_or_partial_identity(self):
        for revision, epoch in [('a'*40, '0'), ('source-build', '100'),
                                ('a'*40, ''), ('a'*40+'"', '100')]:
            with self.subTest(revision=revision, epoch=epoch):
                self.assertNotEqual(self.configure('-DPRISM_VIEWER_REVISION=' + revision,
                                    '-DPRISM_VIEWER_BUILD_EPOCH=' + epoch)[0], 0)

    def test_packaged_identity(self):
        expected = dict(version='1.3.0', revision='a'*40, build_epoch=123)
        payload = json.dumps(expected).encode()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for suffix in ('.zip', '.tar.gz'):
                path = root / ('package' + suffix)
                if suffix == '.zip':
                    with zipfile.ZipFile(path, 'w') as archive:
                        archive.writestr('Viewer/BUILD_IDENTITY.json', payload)
                else:
                    with tarfile.open(path, 'w:gz') as archive:
                        entry = tarfile.TarInfo('Viewer/BUILD_IDENTITY.json')
                        entry.size = len(payload)
                        archive.addfile(entry, io.BytesIO(payload))
                self.assertEqual(read_package_identity(path), expected)
            with zipfile.ZipFile(root / 'bad.zip', 'w') as archive:
                archive.writestr('Viewer/README.txt', 'no identity')
            with self.assertRaises(ValueError):
                read_package_identity(root / 'bad.zip')


if __name__ == '__main__':
    unittest.main()
