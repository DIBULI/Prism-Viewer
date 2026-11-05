param([string]$Archive,[string]$Destination,[string]$Root)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::OpenRead($Archive)
try {
  [long]$total=0
  foreach($entry in $zip.Entries){
    $name=$entry.FullName.Replace('\','/')
    if(!$name.StartsWith($Root+'/') -or $name.Split('/') -contains '..' -or $name.Contains(':') -or $name.StartsWith('/')){throw 'Invalid archive path'}
    if((($entry.ExternalAttributes -shr 16) -band 61440) -eq 40960){throw 'Archive symlink rejected'}
    $total+=$entry.Length
    if($total -gt 8GB){throw 'Archive exceeds extraction limit'}
  }
  foreach($entry in $zip.Entries){
    $path=Join-Path $Destination $entry.FullName
    if($entry.FullName.EndsWith('/')){[IO.Directory]::CreateDirectory($path)|Out-Null}
    else {[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))|Out-Null;[IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$path,$false)}
  }
}finally{$zip.Dispose()}
