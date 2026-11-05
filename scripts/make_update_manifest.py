"""Publish metadata for the four packages built from this exact Git revision."""
import hashlib,json,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
assets=Path(sys.argv[1])
version=(root/'VERSION').read_text().strip()
revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
epoch=int(subprocess.check_output(['git','show','-s','--format=%ct','HEAD'],cwd=root,text=True))
platforms={}
for target in ('windows-x64','macos-arm64','linux-x64','linux-arm64'):
    suffix='.tar.gz' if target.startswith('linux-') else '.zip'
    name=f'Prism-Viewer-{version}-{target}{suffix}'
    path=assets/name
    digest=hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):digest.update(chunk)
    assert 0<path.stat().st_size<=2*1024**3
    platforms[target]=dict(name=name,size=path.stat().st_size,sha256=digest.hexdigest())
notes=(root/'docs/release-notes'/f'v{version}.md').read_text()[:8000]
(assets/'viewer-update.json').write_text(json.dumps(dict(schema=1,version=version,revision=revision,build_epoch=epoch,notes=notes,platforms=platforms),indent=2)+'\n')
