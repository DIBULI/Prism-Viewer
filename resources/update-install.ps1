param([int]$ParentId,[string]$Target,[string]$Staged,[string]$Backup,[string]$Relative,[string]$Work)
$ErrorActionPreference='Stop'
$parentExited=$false
$restored=$false
# Defender/indexers may briefly retain handles after Viewer has exited. Retry
# only sharing/access errors, never terminate another process to unlock a path.
function Move-UpdateDirectory([string]$Source,[string]$Destination) {
  $deadline=[DateTime]::UtcNow.AddSeconds(15)
  while($true) {
    try { [IO.Directory]::Move($Source,$Destination); return }
    catch {
      $e=$_.Exception; $retry=$false
      while($e) {
        if(($e.HResult -band 65535) -in @(5,32,33)) { $retry=$true }
        $e=$e.InnerException
      }
      if(!$retry -or [DateTime]::UtcNow -ge $deadline) { throw }
      Start-Sleep -Milliseconds 250
    }
  }
}
try {
  if(!(Test-Path -LiteralPath $Target -PathType Container) -or (Test-Path -LiteralPath $Backup)) {throw 'Invalid update paths'}
  $workPath=[IO.Path]::GetFullPath($Work).TrimEnd('\','/')
  $targetPath=[IO.Path]::GetFullPath($Target).TrimEnd('\','/')
  if($workPath -eq $targetPath -or $workPath.StartsWith($targetPath+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Update helper must be outside the installation'}
  # PowerShell's provider location and the Win32 process working directory are
  # independent. Release both before trying to rename the installation.
  Set-Location -LiteralPath $workPath
  [Environment]::CurrentDirectory=$workPath
  $launchDirectory=[IO.Path]::GetDirectoryName($targetPath)
  $failedPackage=Join-Path ([IO.Path]::GetDirectoryName($Staged)) 'failed-package'
  $parentProcess=Get-Process -Id $ParentId -ErrorAction Stop
  [IO.File]::WriteAllText((Join-Path $Work 'ready'),'ready')
  $waitUntil=[DateTime]::UtcNow.AddSeconds(60)
  while(!$parentProcess.WaitForExit(200)){
    if((Test-Path -LiteralPath (Join-Path $Work 'cancelled')) -or [DateTime]::UtcNow -ge $waitUntil){throw 'Update cancelled or Viewer did not exit; unchanged'}
  }
  if(Test-Path -LiteralPath (Join-Path $Work 'cancelled')){throw 'Update cancelled; unchanged'}
  $parentProcess.Dispose()
  $parentExited=$true
  Move-UpdateDirectory $Target $Backup
  try {Move-UpdateDirectory $Staged $Target}
  catch {Move-UpdateDirectory $Backup $Target;throw}
  $child=$null
  try {
    $ack=Join-Path $Work 'started'
    $child=Start-Process -FilePath (Join-Path $Target $Relative) -WorkingDirectory $launchDirectory -ArgumentList @('--viewer-update-ack',('"'+$ack+'"')) -PassThru
    $until=[DateTime]::UtcNow.AddSeconds(30)
    while(!(Test-Path -LiteralPath $ack)){
      if($child.HasExited -or [DateTime]::UtcNow -ge $until){throw 'Updated Viewer did not start'}
      Start-Sleep -Milliseconds 200
    }
    [IO.File]::WriteAllText((Join-Path $Work 'result'),'installed')
  } catch {
    if($child -and !$child.HasExited){$child.Kill();$child.WaitForExit()}
    Move-UpdateDirectory $Target $failedPackage
    Move-UpdateDirectory $Backup $Target
    Start-Process -FilePath (Join-Path $Target $Relative) -WorkingDirectory $launchDirectory
    $restored=$true
    [IO.File]::WriteAllText((Join-Path $Work 'result'),'rollback')
    throw
  }
} catch {
  $_ | Out-String | Set-Content -LiteralPath (Join-Path $Work 'install-error.txt')
  if($parentExited -and !$restored -and !(Test-Path -LiteralPath $Backup) -and (Test-Path -LiteralPath (Join-Path $Target $Relative))){Start-Process -FilePath (Join-Path $Target $Relative) -WorkingDirectory $launchDirectory}
  exit 1
}
