"""Exercise the shipped replacement helper only in fresh temporary packages."""
import json,os,shutil,subprocess,sys,tempfile,time
from pathlib import Path
resources=Path(sys.argv[1]);fixture=Path(sys.argv[2]).resolve()
sys.path.insert(0,str(resources.parent/'scripts'))
from package_inventory import write_inventory
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

scenarios=['success','success-personal-files','missing-inventory','invalid-inventory','rollback','cancel']
if os.name=='nt':scenarios+=['transient-lock','persistent-lock']
for scenario in scenarios:
    with tempfile.TemporaryDirectory(prefix='prism updater space-') as folder:
        base=Path(folder);target=base/'Viewer installed';stage_root=base/'.prism-viewer-stage-ABC123';stage=stage_root/'extract/new package';work=base/'prism-viewer-update-test';backup=base/'Viewer installed.backup-ABC123'
        for p in (target,stage,work):p.mkdir(parents=True)
        relative='fixture.exe' if os.name=='nt' else 'fixture'
        for p in (target,stage):shutil.copy2(fixture,p/relative)
        (target/'lib').mkdir();(target/'lib/old-library').write_bytes(b'old binary')
        write_inventory(target)
        if scenario=='missing-inventory':(target/'PRISM_PACKAGE_FILES.json').unlink()
        if scenario=='invalid-inventory':
            inventory=json.loads((target/'PRISM_PACKAGE_FILES.json').read_text())
            inventory['files'].append('../outside.txt')
            (target/'PRISM_PACKAGE_FILES.json').write_text(json.dumps(inventory))
        (base/'outside.txt').write_text('outside unchanged')
        personal=scenario!='success'
        if personal:
            (target/'user-data.txt').write_text('keep me')
            (target/'lib/user-data.txt').write_text('nested user data')
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
            assert (target/relative).exists() and not backup.exists()
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
            if scenario in ('success','success-personal-files','transient-lock','missing-inventory','invalid-inventory'):
                assert helper.returncode==0,(stdout,stderr)
                assert (work/'result').read_text()=='installed'
                if personal:
                    assert (backup/'user-data.txt').read_text()=='keep me'
                    assert (backup/'lib/user-data.txt').read_text()=='nested user data'
                assert (target/relative).exists() and not stage.exists()
                if scenario in ('missing-inventory','invalid-inventory'):
                    assert (backup/relative).exists() and (work/'cleanup-warning.txt').exists()
                else:
                    assert not (backup/relative).exists() and not (backup/'lib/old-library').exists()
                    assert not stage_root.exists()
                    result=json.loads((work/'cleanup.json').read_text())
                    assert result['status']==('personal-files-preserved' if personal else 'cleaned'),result
                    assert backup.exists()==personal
            elif scenario=='rollback':
                assert helper.returncode!=0
                assert (work/'result').read_text()=='rollback'
                assert (target/'user-data.txt').read_text()=='keep me'
                assert (target/'lib/old-library').exists() and not (work/'cleanup.json').exists()
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
        assert (base/'outside.txt').read_text()=='outside unchanged'
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
