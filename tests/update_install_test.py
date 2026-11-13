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
def lock_directory(path):
    import ctypes
    from ctypes import wintypes
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,wintypes.LPVOID,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
    kernel.CreateFileW.restype=wintypes.HANDLE
    kernel.CloseHandle.argtypes=[wintypes.HANDLE];kernel.CloseHandle.restype=wintypes.BOOL
    # A zero-access metadata handle can bypass normal sharing checks. Request
    # real read access so this models an application holding the directory.
    handle=kernel.CreateFileW(str(path),0x80000000,3,None,3,0x02000000,None)
    assert handle!=wintypes.HANDLE(-1).value,ctypes.get_last_error()
    return lambda: kernel.CloseHandle(handle)

scenarios=['success','rollback','cancel']
if os.name=='nt':scenarios+=['transient-lock','persistent-lock']
for scenario in scenarios:
    with tempfile.TemporaryDirectory(prefix='prism updater space-') as folder:
        base=Path(folder);target=base/'Viewer installed';stage=base/'new package';work=base/'prism-viewer-update-test';backup=base/'old backup'
        for p in (target,stage,work):p.mkdir()
        relative='fixture.exe' if os.name=='nt' else 'fixture'
        for p in (target,stage):shutil.copy2(fixture,p/relative)
        (target/'user-data.txt').write_text('keep me')
        if scenario=='rollback':(stage/'fail-start').touch()
        parent=subprocess.Popen([sys.executable,'-c','import time;time.sleep(30)'],cwd=target)
        args=[str(parent.pid),str(target),str(stage),str(backup),relative,str(work)]
        if os.name=='nt':
            command=['powershell.exe','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',str(resources/'update-install.ps1'),*args]
        else:command=['/bin/sh',str(resources/'update-install.sh'),*args]
        # The helper must release its inherited installation working directory.
        helper=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,cwd=target)
        unlock=None
        try:
            wait(work/'ready',helper)
            assert (target/'user-data.txt').read_text()=='keep me' and not backup.exists()
            if scenario.endswith('-lock'):unlock=lock_directory(target)
            if scenario=='cancel':(work/'cancelled').touch()
            parent.terminate();parent.wait(timeout=5)
            if scenario=='transient-lock':
                time.sleep(1.5)
                diagnostic={p.name:p.read_text(errors='replace') for p in work.iterdir() if p.is_file()}
                assert helper.poll() is None and not backup.exists(),(helper.poll(),backup.exists(),diagnostic)
                assert (target/'user-data.txt').read_text()=='keep me'
                unlock();unlock=None
            stdout,stderr=helper.communicate(timeout=25)
            if scenario in ('success','transient-lock'):
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
                if scenario=='persistent-lock':
                    assert (work/'install-error.txt').exists() and not (work/'result').exists()
        finally:
            if unlock:unlock()
            for p in (parent,helper):
                if p.poll() is None:p.kill();p.wait()
        if scenario=='persistent-lock':
            until=time.monotonic()+5
            while not(target/'started-old').exists():
                assert time.monotonic()<until,'preserved Viewer was not restarted'
                time.sleep(.05)
        # The restarted fixture acknowledges just before exiting. Windows can
        # still hold its executable mapping after the helper has returned.
        # Wait only on deletion of our disposable test tree; never kill a PID.
        until=time.monotonic()+5
        while True:
            try:shutil.rmtree(base);break
            except PermissionError:
                if time.monotonic()>=until:raise
                time.sleep(.05)
        print('PASS',scenario)
