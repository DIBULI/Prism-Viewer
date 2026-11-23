"""Record distribution files so update cleanup preserves user additions."""
import argparse
import json
import os
from pathlib import Path

MANIFEST = "PRISM_PACKAGE_FILES.json"


def write_inventory(root, checksums=False, verify=False):
    root = Path(root).resolve()
    manifest = (root / "Contents/Resources" / MANIFEST
                if root.suffix == ".app" else root / MANIFEST)
    files, directories = [], []
    for parent, dirs, names in os.walk(root, followlinks=False):
        for name in dirs[:]:
            path = Path(parent) / name
            if path.is_symlink():
                files.append(path.relative_to(root).as_posix())
                dirs.remove(name)
            else:
                directories.append(path.relative_to(root).as_posix())
        files.extend((Path(parent) / name).relative_to(root).as_posix()
                     for name in names)
    files.append(manifest.relative_to(root).as_posix())
    if checksums:
        files.append("SHA256SUMS.txt")
    inventory = {"format": 1, "product": "Prism Viewer",
                 "files": sorted(set(files)), "directories": sorted(directories)}
    if verify:
        if json.loads(manifest.read_text(encoding="utf-8")) != inventory:
            raise SystemExit("Package inventory does not match distribution files")
        return
    manifest.write_text(json.dumps(inventory,
                                  ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--checksums", action="store_true")
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    write_inventory(args.root, args.checksums, args.verify)
