#!/usr/bin/env python3
"""Collect the already-built Linux Viewer runtime inside its Ubuntu 20.04 container."""
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile

source = Path(__file__).resolve().parents[1]
build = source / 'package'
viewer_version = '1.2.2'
arch = os.environ['PRISM_PACKAGE_ARCH']
assert arch in ('x64', 'arm64')
package = build / f'Prism-Viewer-{viewer_version}-linux-{arch}'
assert (package/'bin/prism-viewer').is_file()
plugins = Path(subprocess.check_output(['qmake', '-query', 'QT_INSTALL_PLUGINS'], text=True).strip())
for rel in ('platforms/libqxcb.so', 'platforms/libqoffscreen.so', 'imageformats/libqjpeg.so', 'sqldrivers/libqsqlite.so'):
    target = package/'plugins'/rel
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(plugins/rel, target)
shutil.copy2(source/'packaging/qt.conf', package/'bin/qt.conf')

def elf_files():
    found = []
    for p in package.rglob('*'):
        if p.is_file():
            with p.open('rb') as f:
                if f.read(4) == b'\x7fELF':
                    found.append(p)
    return sorted(found)

system = re.compile(r'^(ld-linux|lib(c|dl|m|pthread|resolv|rt|util)\.so\.)')
env = dict(os.environ, LD_LIBRARY_PATH=str(package/'lib'))
for attempt in range(10):
    added = 0
    for binary in elf_files():
        output = subprocess.check_output(['ldd', str(binary)], text=True, env=env)
        assert 'not found' not in output, output
        for line in output.splitlines():
            match = re.search(r'(?:=>\s+)?(/\S+)', line)
            if not match:
                continue
            dependency = Path(match.group(1))
            if str(dependency).startswith(str(package)) or system.match(dependency.name):
                continue
            target = package/'lib'/dependency.name
            if not target.exists():
                shutil.copy2(dependency, target)
                added += 1
    if not added:
        break
else:
    raise RuntimeError('dependency collection did not converge')

reports = []
for binary in elf_files():
    relative = os.path.relpath(package/'lib', binary.parent)
    subprocess.run(['patchelf', '--set-rpath', '$ORIGIN' + (('/'+relative) if relative != '.' else ''), str(binary)], check=True)
    output = subprocess.check_output(['ldd', str(binary)], text=True)
    assert 'not found' not in output, output
    reports.append(f'[{binary.relative_to(package)}]\n' + output.replace(str(package), '<package>'))
(package/'RUNTIME_DEPENDENCIES.txt').write_text('\n'.join(reports))
subprocess.run(['bash', str(source/'scripts/verify_linux_glibc.sh'), str(package), '2.31'], check=True)
output = subprocess.check_output([str(package/'bin/prism-viewer'), '--version'], text=True,
                                 env=dict(os.environ, QT_QPA_PLATFORM='offscreen'))
assert f'Prism Viewer {viewer_version}' in output and 'Prism SDK 1.2.1' in output, output
print(output)
smoke = subprocess.run(['timeout', '8s', str(package/'bin/prism-viewer')],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                       env=dict(os.environ, QT_QPA_PLATFORM='offscreen', XDG_CONFIG_HOME='/tmp/prism-pps5-viewer-config'))
assert smoke.returncode == 124, (smoke.returncode, smoke.stdout)
(build/'viewer-smoke.log').write_text(smoke.stdout)
rows = [hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(package))
        for p in sorted(package.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS.txt']
(package/'SHA256SUMS.txt').write_text('\n'.join(rows)+'\n')
subprocess.run([str(package/'bin/prism-viewer'), '--startup-self-test'], check=True,
               env=dict(os.environ, QT_QPA_PLATFORM='offscreen'))
subprocess.run([str(package/'bin/prism-viewer'), '--imu-only-recorder-self-test', str(build/'recording-smoke')], check=True,
               env=dict(os.environ, QT_QPA_PLATFORM='offscreen'))
target = build/f'Prism-Viewer-{viewer_version}-linux-{arch}.tar.gz'
assert not target.exists()
with tarfile.open(target, 'w:gz') as archive:
    archive.add(package, arcname=package.name)
print(f'PASS: Viewer {viewer_version} / SDK 1.2.1 runtime dependencies, GLIBC<=2.31, version and offscreen startup')
