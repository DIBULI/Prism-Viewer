"""Check destructive cleanup boundaries using disposable package trees only."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from package_inventory import write_inventory

fixture = Path(sys.argv[1]).resolve()
scenarios = ["not-committed", "wrong-backup", "mac-bundle", "file-became-directory"]
if os.name != "nt":
    scenarios += ["parent-symlink", "file-symlink"]
for scenario in scenarios:
    with tempfile.TemporaryDirectory(prefix="viewer cleanup-") as temp:
        base = Path(temp)
        target = base / ("Viewer.app" if scenario == "mac-bundle" else "Viewer")
        backup = base / (target.name + ".backup-ABC123")
        work = base / "prism-viewer-update-test"
        for path in (target, work):
            path.mkdir()
        (target / "lib").mkdir()
        (target / "lib/runtime").write_text("old runtime")
        if scenario == "mac-bundle":
            (target / "Contents/Resources").mkdir(parents=True)
        write_inventory(target)
        write_inventory(target, verify=True)
        target.rename(backup)
        target.mkdir()
        executable = target / fixture.name
        shutil.copy2(fixture, executable)
        outside = base / "personal"
        outside.mkdir()
        (outside / "runtime").write_text("personal content")
        (work / "started").write_text("started")
        if scenario != "not-committed":
            (work / "result").write_text("installed")
        if scenario == "wrong-backup":
            other = base / "Unrelated.backup-ABC123"
            backup.rename(other)
            backup = other
        elif scenario == "parent-symlink":
            shutil.rmtree(backup / "lib")
            (backup / "lib").symlink_to(outside, target_is_directory=True)
        elif scenario == "file-symlink":
            (backup / "lib/runtime").unlink()
            (backup / "lib/runtime").symlink_to(outside / "runtime")
        elif scenario == "file-became-directory":
            (backup / "lib/runtime").unlink()
            (backup / "lib/runtime").mkdir()
            (backup / "lib/runtime/user-recording").write_text("preserve")
        result = subprocess.run([str(executable), "--cleanup-update-backup", str(backup), str(work)],
                                timeout=10, capture_output=True)
        assert (outside / "runtime").read_text() == "personal content"
        assert executable.exists()
        if scenario in ("mac-bundle", "file-symlink"):
            assert result.returncode == 0, (scenario, result.returncode, result.stderr)
            assert not backup.exists()
        else:
            assert result.returncode != 0
            assert backup.exists()
            if scenario == "file-became-directory":
                assert (backup / "lib/runtime/user-recording").read_text() == "preserve"
        print("PASS", scenario)
