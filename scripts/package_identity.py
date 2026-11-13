"""Read the identity captured from the executable before publication."""
import json
import tarfile
import zipfile


def read_package_identity(path):
    if str(path).endswith('.zip'):
        with zipfile.ZipFile(path) as archive:
            entries = [e for e in archive.infolist()
                       if e.filename.endswith('/BUILD_IDENTITY.json')]
            if len(entries) != 1 or entries[0].file_size > 1024:
                raise ValueError(f'{path}: missing or invalid build identity')
            payload = archive.read(entries[0])
    else:
        with tarfile.open(path, 'r:gz') as archive:
            entries = [e for e in archive.getmembers()
                       if e.name.endswith('/BUILD_IDENTITY.json')]
            if len(entries) != 1 or not entries[0].isfile() or entries[0].size > 1024:
                raise ValueError(f'{path}: missing or invalid build identity')
            payload = archive.extractfile(entries[0]).read()
    return json.loads(payload)
