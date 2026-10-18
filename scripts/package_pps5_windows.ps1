$ErrorActionPreference = 'Stop'
$package = Join-Path $env:RUNNER_TEMP 'Prism-Viewer-1.2.2-windows-x64'
cmake --install build --config Release --prefix "$package/stage"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Move-Item "$package/stage/bin/*" $package
Remove-Item "$package/stage" -Recurse
& "$env:QT_PREFIX/bin/windeployqt.exe" --release --no-translations --no-compiler-runtime --dir $package "$package/prism-viewer.exe"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Redist.14.Latest -property installationPath).Trim()
$redist = Get-ChildItem "$vsInstall/VC/Redist/MSVC" -Directory | Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'x64/Microsoft.VC143.CRT' } | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $redist) { throw 'MSVC x64 runtime not found' }
Copy-Item "$redist/*.dll" $package
foreach ($required in @('prism-viewer.exe','prism_usb_sdk.dll','Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6Charts.dll','Qt6Sql.dll','Qt6Network.dll','platforms/qwindows.dll','imageformats/qjpeg.dll','sqldrivers/qsqlite.dll','msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')) {
  if (-not (Test-Path "$package/$required")) { throw "Missing package dependency: $required" }
}
$env:PATH = "$env:SystemRoot/system32;$env:SystemRoot"
$version = & "$package/prism-viewer.exe" --version | Out-String
if ($LASTEXITCODE -ne 0 -or $version -notmatch 'Prism Viewer 1.2.2 \| Prism SDK 1.2.1') { throw "Version smoke failed: $version" }
& "$package/prism-viewer.exe" --startup-self-test
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& "$package/prism-viewer.exe" --imu-only-recorder-self-test "$env:RUNNER_TEMP/package-recording-smoke"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$rows = Get-ChildItem $package -Recurse -File | Sort-Object FullName | ForEach-Object {
  $rel = [IO.Path]::GetRelativePath($package, $_.FullName)
  $hash = (Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLowerInvariant()
  "$hash  $rel"
}
$rows | Set-Content "$package/SHA256SUMS.txt" -Encoding utf8
Compress-Archive -Path $package -DestinationPath 'Prism-Viewer-1.2.2-windows-x64.zip'
