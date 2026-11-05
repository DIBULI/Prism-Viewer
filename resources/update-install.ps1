param([int]$ParentId,[string]$Target,[string]$Staged,[string]$Backup,[string]$Relative,[string]$Work)
$ErrorActionPreference='Stop'
$parentExited=$false
$restored=$false
try {
  if(!(Test-Path -LiteralPath $Target -PathType Container) -or (Test-Path -LiteralPath $Backup)) {throw 'Invalid update paths'}
  $parentProcess=Get-Process -Id $ParentId -ErrorAction Stop
  [IO.File]::WriteAllText((Join-Path $Work 'ready'),'ready')
  $waitUntil=[DateTime]::UtcNow.AddSeconds(60)
  while(!$parentProcess.WaitForExit(200)){
    if((Test-Path -LiteralPath (Join-Path $Work 'cancelled')) -or [DateTime]::UtcNow -ge $waitUntil){throw 'Update cancelled or Viewer did not exit; unchanged'}
  }
  if(Test-Path -LiteralPath (Join-Path $Work 'cancelled')){throw 'Update cancelled; unchanged'}
  $parentExited=$true
  Move-Item -LiteralPath $Target -Destination $Backup
  try {Move-Item -LiteralPath $Staged -Destination $Target}
  catch {Move-Item -LiteralPath $Backup -Destination $Target;throw}
  $child=$null
  try {
    $ack=Join-Path $Work 'started'
    $child=Start-Process -FilePath (Join-Path $Target $Relative) -ArgumentList @('--viewer-update-ack',('"'+$ack+'"')) -PassThru
    $until=[DateTime]::UtcNow.AddSeconds(30)
    while(!(Test-Path -LiteralPath $ack)){
      if($child.HasExited -or [DateTime]::UtcNow -ge $until){throw 'Updated Viewer did not start'}
      Start-Sleep -Milliseconds 200
    }
    [IO.File]::WriteAllText((Join-Path $Work 'result'),'installed')
  } catch {
    if($child -and !$child.HasExited){$child.Kill();$child.WaitForExit()}
    Move-Item -LiteralPath $Target -Destination (Join-Path $Work 'failed-package')
    Move-Item -LiteralPath $Backup -Destination $Target
    Start-Process -FilePath (Join-Path $Target $Relative)
    $restored=$true
    [IO.File]::WriteAllText((Join-Path $Work 'result'),'rollback')
    throw
  }
} catch {
  $_ | Out-String | Set-Content -LiteralPath (Join-Path $Work 'install-error.txt')
  if($parentExited -and !$restored -and !(Test-Path -LiteralPath $Backup) -and (Test-Path -LiteralPath (Join-Path $Target $Relative))){Start-Process -FilePath (Join-Path $Target $Relative)}
  exit 1
}
