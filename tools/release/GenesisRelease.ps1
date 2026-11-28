# Mechanical Windows release assembly. Title payload/provenance audits run first.
. "$PSScriptRoot/RuntimeDllClosure.ps1"

function Get-GenesisRuntimeSearchDirs {
  param([string[]]$SearchDirs = @())
  $dirs = @($SearchDirs)
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
  if (Test-Path -LiteralPath $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vs) {
      $dirs += @(Get-ChildItem -Path (Join-Path $vs 'VC/Redist/MSVC/*/x64/Microsoft.VC14*.CRT') -Directory |
        Sort-Object FullName -Descending | ForEach-Object FullName)
    }
  }
  return @($dirs | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Container) } | Select-Object -Unique)
}

function Write-GenesisReleaseArchive {
  param(
    [Parameter(Mandatory = $true)][string]$StageDir,
    [Parameter(Mandatory = $true)][string]$Archive
  )
  $stage = [IO.Path]::GetFullPath($StageDir).TrimEnd('\', '/')
  $files = @(Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object FullName)
  if (-not $files.Count) { throw 'Release stage is empty' }
  foreach ($file in $files) {
    $rel = $file.FullName.Substring($stage.Length + 1).Replace('\', '/')
    if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint -or
        $rel -match '(?i)\.(bin|gen|smd|srm|ram|vram|log|map|pdb|sav|replay(?:\..*)?)$' -or
        $rel -match '(?i)(settings\.ini|rom.*\.cfg|mods\.ini|rka-video\.ini|savestate|ramdump)') {
      throw "Forbidden release file: $rel"
    }
  }
  Add-Type -AssemblyName System.IO.Compression
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $archivePath = [IO.Path]::GetFullPath($Archive)
  [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($archivePath)) | Out-Null
  $pending = "$archivePath.pending-$([Guid]::NewGuid().ToString('N'))"
  $zip = [IO.Compression.ZipFile]::Open($pending, [IO.Compression.ZipArchiveMode]::Create)
  try {
    foreach ($file in $files) {
      $entry = $file.FullName.Substring($stage.Length + 1).Replace('\', '/')
      [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
        $zip, $file.FullName, $entry, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
  } finally { $zip.Dispose() }
  $zip = [IO.Compression.ZipFile]::OpenRead($pending)
  try {
    if ($zip.Entries.Count -ne $files.Count) { throw 'ZIP entry count mismatch' }
    foreach ($entry in $zip.Entries) {
      if ($entry.FullName.Contains('\') -or $entry.FullName.StartsWith('/') -or
          $entry.FullName -match '(^|/)\.\.(/|$)') { throw "Unsafe ZIP path: $($entry.FullName)" }
      $source = Join-Path $stage $entry.FullName
      $sha = [Security.Cryptography.SHA256]::Create()
      $stream = $entry.Open()
      try { $actual = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
      finally { $stream.Dispose(); $sha.Dispose() }
      if ($actual -ne (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash) {
        throw "ZIP content mismatch: $($entry.FullName)"
      }
    }
  } finally { $zip.Dispose() }
  # Replace only after the new archive has been read and checked.
  Move-Item -LiteralPath $pending -Destination $archivePath -Force
  $hash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
  [IO.File]::WriteAllText("$archivePath.sha256", "$hash  $([IO.Path]::GetFileName($archivePath))`n", (New-Object Text.UTF8Encoding($false)))
  Write-Host "Built $archivePath ($($files.Count) files); SHA256 $hash"
}

function Complete-GenesisReleaseStage {
  param(
    [Parameter(Mandatory = $true)][string]$StageDir,
    [Parameter(Mandatory = $true)][string]$Archive,
    [string[]]$SearchDirs = @()
  )
  $dirs = @(Get-GenesisRuntimeSearchDirs -SearchDirs $SearchDirs)
  $added = @(Copy-RuntimeDllClosure -StageDir $StageDir -SearchDirs $dirs)
  Assert-RuntimeDllClosure -StageDir $StageDir | Out-Null
  Write-Host "Staged dependency closure: $($added -join ', ')"
  Write-GenesisReleaseArchive -StageDir $StageDir -Archive $Archive
}
