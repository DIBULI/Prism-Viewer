"""Exercise the shipped replacement helper only in fresh temporary packages."""
import os,shutil,subprocess,sys,tempfile,time
from pathlib import Path
resources=Path(sys.argv[1]);fixture=Path(sys.argv[2]).resolve()
def wait(path,process,seconds=12):
    until=time.monotonic()+seconds
    while not path.exists():
        assert process.poll() is None,('helper exited',process.returncode,path)
        assert time.monotonic()<until,('deadline',path)
        time.sleep(.05)
for scenario in ('success','rollback','cancel'):
    with tempfile.TemporaryDirectory(prefix='prism updater space-') as folder:
        base=Path(folder);target=base/'Viewer installed';stage=base/'new package';work=base/'.prism-viewer-update-test';backup=base/'old backup'
        for p in (target,stage,work):p.mkdir()
        relative='fixture.exe' if os.name=='nt' else 'fixture'
        for p in (target,stage):shutil.copy2(fixture,p/relative)
        (target/'user-data.txt').write_text('keep me')
        if scenario=='rollback':(stage/'fail-start').touch()
        parent=subprocess.Popen([sys.executable,'-c','import time;time.sleep(30)'])
        args=[str(parent.pid),str(target),str(stage),str(backup),relative,str(work)]
        if os.name=='nt':
            command=['powershell.exe','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',str(resources/'update-install.ps1'),*args]
        else:command=['/bin/sh',str(resources/'update-install.sh'),*args]
        helper=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            wait(work/'ready',helper)
            assert (target/'user-data.txt').read_text()=='keep me' and not backup.exists()
            if scenario=='cancel':(work/'cancelled').touch()
            parent.terminate();parent.wait(timeout=5)
            stdout,stderr=helper.communicate(timeout=15)
            if scenario=='success':
                assert helper.returncode==0,(stdout,stderr)
                assert (work/'result').read_text()=='installed'
                assert (backup/'user-data.txt').read_text()=='keep me'
                assert (target/relative).exists() and not stage.exists()
            elif scenario=='rollback':
                assert helper.returncode!=0
                assert (work/'result').read_text()=='rollback'
                assert (target/'user-data.txt').read_text()=='keep me'
                until=time.monotonic()+5
                while not(target/'started-old').exists():
                    assert time.monotonic()<until
                    time.sleep(.05)
            else:
                assert helper.returncode!=0 and not backup.exists() and stage.exists()
                assert (target/'user-data.txt').read_text()=='keep me'
        finally:
            for p in (parent,helper):
                if p.poll() is None:p.kill();p.wait()
        print('PASS',scenario)
