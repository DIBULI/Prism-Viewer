"""Verify the running artifact, optionally save its identity inside the package."""
import argparse
import json
import re
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('binary')
parser.add_argument('revision')
parser.add_argument('epoch', type=int)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
assert re.fullmatch('[0-9a-f]{40}', args.revision) and args.epoch > 0
expected = dict(version='1.3.0', revision=args.revision, build_epoch=args.epoch)
result = subprocess.run([str(Path(args.binary).resolve()), '--build-info'],
                        text=True, capture_output=True, check=True, timeout=20)
actual = json.loads(result.stdout)
assert actual == expected, (actual, expected)
if args.output:
    args.output.write_text(json.dumps(actual, indent=2) + '\n')
print('Verified artifact identity:', json.dumps(actual))
