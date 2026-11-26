"""Upgrade from an old helper, legacy manifests, deferred Windows file handles.

All data and cleanup receipts are disposable. No installed Viewer is touched.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from package_inventory import write_inventory

fixture = Path(sys.argv[1]).resolve()
scenarios = ["old-helper", "legacy-checksums", "legacy-modified", "legacy-traversal",
             "missing-inventory", "rollback", "uncommitted", "retry-after-restart"]
if os.name == "nt":
    scenarios += ["transient-file-lock", "persistent-file-lock"]
else:
    scenarios += ["stage-symlink"]

def lock_file(path):
    import ctypes
    from ctypes import wintypes
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                  wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.CreateFileW(str(path), 0x80000000, 1, None, 3, 0, None)
    assert handle != wintypes.HANDLE(-1).value, ctypes.get_last_error()
    return lambda: kernel.CloseHandle(handle)

for scenario in scenarios:
    with tempfile.TemporaryDirectory(prefix="viewer cleanup recovery-") as directory:
        base = Path(directory).resolve()
        target = base / "Viewer 中文 installed"
        backup = base / (target.name + ".backup-ABC123")
        stage = base / ".prism-viewer-stage-ABC123"
        work = base / ".prism-viewer-update-OLD123"
        data = base / "appdata"
        for path in (target, backup / "lib", stage / "extract", work, data):
            path.mkdir(parents=True)
        executable = target / fixture.name
        shutil.copy2(fixture, executable)
        (backup / "prism-viewer.exe").write_bytes(b"old executable")
        old_file = backup / "lib/old.dll"
        old_file.write_bytes(b"old library")
        if scenario.startswith("legacy-"):
            lines = [hashlib.sha256(p.read_bytes()).hexdigest() + "  " + str(p.relative_to(backup)).replace("/", "\\")
                     for p in (backup / "prism-viewer.exe", old_file)]
            with (backup / "SHA256SUMS.txt").open("w", encoding="utf-8", newline="") as output:
                output.write("\ufeff" + "\r\n".join(lines) + "\r\n")
            if scenario == "legacy-modified":
                old_file.write_bytes(b"user modified")
            elif scenario == "legacy-traversal":
                with (backup / "SHA256SUMS.txt").open("a") as output:
                    output.write("0" * 64 + "  ..\\outside.txt\n")
        elif scenario != "missing-inventory":
            write_inventory(backup)
        personal = backup / "recording-user.dat"
        if scenario == "legacy-modified":
            personal.write_text("preserve recording")
        outside = base / "outside.txt"
        outside.write_text("outside unchanged")
        unrelated = base / "Other Viewer.backup-XYZ123"
        unrelated.mkdir(); (unrelated / "data").write_text("unrelated")
        failed_stage = base / ".prism-viewer-stage-FAILED"
        failed_stage.mkdir(); (failed_stage / "failed-package").write_text("keep failure evidence")
        if scenario == "stage-symlink":
            shutil.rmtree(stage)
            stage.symlink_to(unrelated, target_is_directory=True)
        (work / "started").write_text("started")
        if scenario != "uncommitted":
            (work / "result").write_text("rollback" if scenario == "rollback" else "installed")
        env = dict(os.environ, TMPDIR=str(data), TMP=str(data), TEMP=str(data))
        command = [str(executable), "--finish-update-cleanup", str(work)]
        unlock = None
        child = None
        try:
            if scenario.endswith("file-lock"):
                unlock = lock_file(old_file)
                child = subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                if scenario == "transient-file-lock":
                    time.sleep(.8); unlock(); unlock = None
                stdout, stderr = child.communicate(timeout=15)
                code = child.returncode
            elif scenario == "old-helper":
                # Actual startup scheduler handles a helper that commits later
                # and never calls --cleanup-update-backup itself.
                (work / "result").unlink()
                child = subprocess.Popen([str(executable), "--startup-cleanup-fixture", str(work / "started")], env=env)
                time.sleep(.5)
                assert backup.exists() and stage.exists()
                (work / "result").write_text("installed")
                code = child.wait(timeout=12)
            else:
                code = subprocess.run(command, env=env, timeout=15).returncode
            if scenario in ("rollback", "uncommitted"):
                assert code == 2 and backup.exists() and stage.exists()
            elif scenario == "persistent-file-lock":
                assert code == 4 and old_file.exists()
                assert (backup / "PRISM_PACKAGE_FILES.json").exists()
                unlock(); unlock = None
                assert subprocess.run([str(executable), "--finish-update-cleanup", ""], env=env, timeout=15).returncode == 0
                assert not backup.exists()
            elif scenario in ("missing-inventory", "legacy-traversal"):
                assert code == 0 and (backup / "prism-viewer.exe").exists()
                assert not stage.exists(), "empty stage must go even if backup cannot be identified"
            elif scenario == "legacy-modified":
                assert code == 0 and old_file.read_bytes() == b"user modified"
                assert personal.read_text() == "preserve recording"
                assert not (backup / "prism-viewer.exe").exists() and not stage.exists()
            else:
                assert code == 0 and not backup.exists(), (scenario, code,
                    [str(p.relative_to(backup)) for p in backup.rglob("*")],
                    (work / "startup-cleanup.json").read_text() if (work / "startup-cleanup.json").exists() else "no cleanup report")
                if scenario != "stage-symlink":
                    assert not stage.exists()
                else:
                    assert stage.is_symlink()
            if scenario == "retry-after-restart":
                # Prove restart retry works after temp files have been removed.
                shutil.rmtree(work)
                backup.mkdir(); (backup / "old-file").write_text("old")
                write_inventory(backup)
                assert subprocess.run([str(executable), "--finish-update-cleanup", ""], env=env, timeout=15).returncode == 0
                assert not backup.exists()
            assert outside.read_text() == "outside unchanged"
            assert (unrelated / "data").read_text() == "unrelated"
            assert (failed_stage / "failed-package").read_text() == "keep failure evidence"
            assert executable.exists()
        finally:
            if unlock: unlock()
            if child and child.poll() is None: child.kill(); child.wait()
        print("PASS", scenario, flush=True)
