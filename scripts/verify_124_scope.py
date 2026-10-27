"""Fail closed if the 1.2.4 maintenance release includes unrelated changes."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
base = '222cd720f37e33853fb25b1f15c7e15173ba9a8c'
sdk = 'f1e75ed4d6b7832a132ac03ad251bc2f59eb3674'
allowed = {
    'VERSION', 'CMakeLists.txt', 'MAINTENANCE-RELEASE.txt', 'README.md',
    'docs/release-notes/v1.2.4.md', '.github/workflows/pps5-platforms.yml',
    'scripts/package_pps5_linux.py', 'scripts/package_pps5_windows.ps1',
    'scripts/verify_124_scope.py', 'src/ui/gnss_visualization.cpp',
    'src/ui/trajectory_view.hpp', 'tests/gnss_visualization_test.cpp',
}


def git(*args):
    return subprocess.check_output(['git', *args], cwd=root, text=True).strip()


subprocess.run(['git', 'merge-base', '--is-ancestor', base, 'HEAD'], cwd=root, check=True)
changed = set(git('diff', '--name-only', base, 'HEAD').splitlines())
assert changed and changed <= allowed, sorted(changed - allowed)
assert git('ls-tree', 'HEAD', 'third_party/Prism-SDK').split()[2] == sdk
assert git('-C', 'third_party/Prism-SDK', 'rev-parse', 'HEAD') == sdk
assert (root / 'VERSION').read_text().strip() == '1.2.4'
assert 'project(prism_viewer VERSION 1.2.4 LANGUAGES CXX)' in (root / 'CMakeLists.txt').read_text()
assert 'set(PRISM_USB_SDK_VERSION "1.2.1")' in (root / 'CMakeLists.txt').read_text()
print('PASS: v1.2.3 baseline; only RTK display, tests, version/docs and packaging changed; SDK1.2.1 unchanged')
