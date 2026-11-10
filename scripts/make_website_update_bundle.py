#!/usr/bin/env python3
"""Verify release assets and stage the official website update tree.

Usage: make_website_update_bundle.py ASSET_DIRECTORY NEW_OUTPUT_DIRECTORY
Publish the complete tree atomically, retaining previous revision directories.
"""
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path


def stage(assets, destination):
    raw = (assets / "viewer-update.json").read_bytes()
    if len(raw) > 1024 * 1024:
        raise ValueError("Oversized manifest")
    manifest = json.loads(raw)
    version, revision = manifest["version"], manifest["revision"]
    if (manifest["schema"] != 1 or
            not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version) or
            not re.fullmatch(r"[0-9a-f]{40}", revision) or
            type(manifest["build_epoch"]) is not int or manifest["build_epoch"] <= 0):
        raise ValueError("Invalid update identity")
    verified = []
    for target in ("windows-x64", "macos-arm64", "linux-x64", "linux-arm64"):
        item = manifest["platforms"][target]
        suffix = ".tar.gz" if target.startswith("linux-") else ".zip"
        name = f"Prism-Viewer-{version}-{target}{suffix}"
        if item["name"] != name:
            raise ValueError("Unexpected package name")
        path = assets / name
        digest = hashlib.sha256()
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
        if not (0 < path.stat().st_size == item["size"] <= 2 * 1024**3):
            raise ValueError(f"Package size mismatch: {name}")
        if digest.hexdigest() != item["sha256"]:
            raise ValueError(f"Package checksum mismatch: {name}")
        verified.append(path)
    destination.mkdir(parents=True, exist_ok=False)
    root = destination / "updates" / "prism-viewer"
    packages = root / revision
    packages.mkdir(parents=True)
    for path in verified:
        shutil.copyfile(path, packages / path.name)
    (root / "viewer-update.json").write_bytes(raw)


if __name__ == "__main__":
    stage(Path(sys.argv[1]), Path(sys.argv[2]))
