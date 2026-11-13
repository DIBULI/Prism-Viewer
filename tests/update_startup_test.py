"""Real Viewer window/event-loop acknowledgement, including old-helper bootstrap."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

viewer = Path(sys.argv[1]).resolve()
for prefix, filename, valid in [('prism-viewer-update-', 'started', True),
                                ('.prism-viewer-update-', 'started', True),
                                ('unrelated-', 'started', False),
                                ('prism-viewer-update-', 'not-started', False)]:
    with tempfile.TemporaryDirectory(prefix=prefix) as directory:
        work = Path(directory)
        ack = work / filename
        env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_OPENGL='software',
                   XDG_CONFIG_HOME=str(work / 'config'))
        with (work / 'viewer.log').open('wb') as log:
            # The existing offline-self-test startup gate suppresses USB scan
            # and automatic device opening. Still runs the real window/loop.
            child = subprocess.Popen([str(viewer), '--update-startup-self-test',
                                      '--viewer-update-ack', str(ack)],
                                     env=env, cwd=work, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + (8 if valid else 3.5)
                while time.monotonic() < deadline and not ack.exists():
                    assert child.poll() is None, ('Viewer exited', (work/'viewer.log').read_text(errors='replace'))
                    time.sleep(.05)
                assert ack.exists() == valid, ('ack mismatch', prefix, filename, (work/'viewer.log').read_text(errors='replace'))
                if valid:
                    assert ack.read_text() == 'started'
                    assert child.poll() is None, 'Viewer must remain alive after acknowledgement'
            finally:
                if child.poll() is None:
                    child.terminate()
                    try: child.wait(timeout=5)
                    except subprocess.TimeoutExpired: child.kill(); child.wait(timeout=5)
        print('PASS', prefix, filename, 'accepted' if valid else 'rejected')
