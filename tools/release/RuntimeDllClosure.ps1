<#
Runtime DLL closure staging + verification for Windows release packages.

Why this exists
---------------
Every game's make_release.ps1 used to stage a HARDCODED list of MinGW runtime
DLLs (libgcc_s_seh-1, libstdc++-6, libwinpthread-1) next to the exe and SDL
backend. That list is a snapshot of what the import graph happened to look like
on the day it was written. When the build machine resolves a *different* SDL
build - e.g. SDL3 3.4.14, which links libiconv-2.dll, where 3.4.12 did not -
the new transitive dependency is silently omitted from the zip.

The shipped exe then loads fine on the developer's machine (MSYS2 / Git for
Windows put libiconv-2.dll on PATH) and fails on a player's machine:

  * dependency absent entirely  -> 0xC0000135 STATUS_DLL_NOT_FOUND
  * a 32-bit copy found on PATH -> 0xC000007B STATUS_INVALID_IMAGE_FORMAT
    ("The application was unable to start correctly")

The second case is the nastier one: the player has *some* unrelated 32-bit
libiconv-2.dll on PATH (32-bit Git for Windows, GTK, PHP, GIMP...), the loader
happily finds it, and the bitness mismatch aborts the process before main().
Nothing in the package is wrong-looking; the package is simply incomplete.

The fix is to stop enumerating dependencies by hand. This module walks the
actual PE import graph of the staged binaries, resolves every non-OS import
from the toolchain's bin directories, copies the closure into the stage, and
then re-verifies the stage is self-contained and uniformly 64-bit.

Usage from a game's make_release.ps1, replacing the hardcoded copy block:

  . (Join-Path $root 'snesrecomp\tools\release\RuntimeDllClosure.ps1')

  Copy-RuntimeDllClosure -StageDir $stage -SearchDirs @($build, $RuntimeBinDir)
  Assert-RuntimeDllClosure -StageDir $stage

Copy-RuntimeDllClosure seeds from every .exe/.dll already in the stage, so the
game-specific SDL copy (which picks build dir over RuntimeBinDir) still happens
in the caller; everything reachable from it is then pulled in automatically.
#>

# Deliberately NOT setting StrictMode here: this file is dot-sourced into each
# game's make_release.ps1, and a strict-mode change would leak into the caller.

# ---------------------------------------------------------------------------
# Minimal PE reader. Pure .NET so this works on any release host without
# needing objdump/dumpbin on PATH.
# ---------------------------------------------------------------------------

function Get-PeInfo {
  # Malformed metadata must never look like a valid image with no imports.
  param([Parameter(Mandatory = $true)][string]$Path)
  $bytes = [IO.File]::ReadAllBytes($Path)
  if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) { return $null }
  $peOff = [long][BitConverter]::ToInt32($bytes, 0x3C)
  if ($peOff -lt 0x40 -or $peOff + 24 -gt $bytes.Length) { return $null }
  if ($bytes[$peOff] -ne 0x50 -or $bytes[$peOff+1] -ne 0x45 -or
      $bytes[$peOff+2] -ne 0 -or $bytes[$peOff+3] -ne 0) { return $null }
  $machineId = [BitConverter]::ToUInt16($bytes, $peOff+4)
  $machine = switch ($machineId) {
    0x014C { 'x86' }; 0x8664 { 'x64' }; 0xAA64 { 'arm64' }
    default { '0x{0:X4}' -f $machineId }
  }
  $numSections = [BitConverter]::ToUInt16($bytes, $peOff+6)
  $optSize = [BitConverter]::ToUInt16($bytes, $peOff+20)
  $optOff = $peOff+24
  if ($numSections -eq 0 -or $optSize -lt 2 -or $optOff+$optSize -gt $bytes.Length) { return $null }
  $magic = [BitConverter]::ToUInt16($bytes, $optOff)
  if ($magic -ne 0x10B -and $magic -ne 0x20B) { return $null }
  if (($machineId -eq 0x014C -and $magic -ne 0x10B) -or
      ($machineId -in @(0x8664,0xAA64) -and $magic -ne 0x20B)) { return $null }
  $fixedSize = if ($magic -eq 0x20B) { 112 } else { 96 }
  if ($optSize -lt $fixedSize) { return $null }
  $directoryCount = [BitConverter]::ToUInt32($bytes, $optOff+$fixedSize-4)
  if ([long]$directoryCount*8 -gt $optSize-$fixedSize) { return $null }
  $imageSize = [BitConverter]::ToUInt32($bytes, $optOff+56)
  $headerSize = [BitConverter]::ToUInt32($bytes, $optOff+60)
  $secOff = $optOff+$optSize
  if ($imageSize -eq 0 -or $secOff+[long]$numSections*40 -gt $bytes.Length -or
      $headerSize -lt $secOff+[long]$numSections*40 -or $headerSize -gt $bytes.Length) { return $null }
  $sections = @()
  for ($i=0; $i -lt $numSections; $i++) {
    $off = $secOff+40*$i
    $section = @{
      VirtualSize=[BitConverter]::ToUInt32($bytes,$off+8)
      VA=[BitConverter]::ToUInt32($bytes,$off+12)
      RawSize=[BitConverter]::ToUInt32($bytes,$off+16)
      Raw=[BitConverter]::ToUInt32($bytes,$off+20)
    }
    if ([long]$section.Raw+$section.RawSize -gt $bytes.Length -or
        [long]$section.VA+[Math]::Max($section.VirtualSize,$section.RawSize) -gt $imageSize) { return $null }
    $sections += $section
  }
  function Get-RvaLocation([uint32]$Rva) {
    if ($Rva -lt $headerSize) { return @{Offset=[int]$Rva;Available=[long]$headerSize-$Rva} }
    foreach ($section in $sections) {
      # Import metadata needs bytes present in the file, not virtual/BSS padding.
      $delta = [long]$Rva-$section.VA
      if ($delta -ge 0 -and $delta -lt $section.RawSize) {
        return @{Offset=[int]($section.Raw+$delta);Available=[long]$section.RawSize-$delta}
      }
    }
    return $null
  }
  $info = @{Machine=$machine;Imports=@()}
  if ($directoryCount -lt 2) { return $info }
  $dirOff = $optOff+$fixedSize+8
  $importRva = [BitConverter]::ToUInt32($bytes,$dirOff)
  $importSize = [BitConverter]::ToUInt32($bytes,$dirOff+4)
  if ($importRva -eq 0 -and $importSize -eq 0) { return $info }
  if ($importRva -eq 0 -or $importSize -lt 20 -or [long]$importRva+$importSize -gt $imageSize) { return $null }
  $names = New-Object Collections.Generic.List[string]
  $limit = [Math]::Min(4096,[Math]::Floor($importSize/20))
  for ($i=0; $i -lt $limit; $i++) {
    $location = Get-RvaLocation ([uint32]($importRva+20*$i))
    if (-not $location -or $location.Available -lt 20) { return $null }
    $off = $location.Offset
    $fields = @(for ($j=0; $j -lt 5; $j++) { [BitConverter]::ToUInt32($bytes,$off+4*$j) })
    if (($fields[0] -bor $fields[1] -bor $fields[2] -bor $fields[3] -bor $fields[4]) -eq 0) {
      $info.Imports=$names.ToArray();return $info
    }
    if ($fields[3] -eq 0) { return $null }
    $nameLocation = Get-RvaLocation $fields[3]
    if (-not $nameLocation) { return $null }
    $length=0; $available=[Math]::Min(4096,$nameLocation.Available)
    while ($length -lt $available -and $bytes[$nameLocation.Offset+$length] -ne 0) {
      $value=$bytes[$nameLocation.Offset+$length]
      if ($value -lt 0x20 -or $value -gt 0x7E) { return $null }
      $length++
    }
    if ($length -eq 0 -or $length -eq $available) { return $null }
    $name=[Text.Encoding]::ASCII.GetString($bytes,$nameLocation.Offset,$length)
    if ([string]::IsNullOrWhiteSpace($name) -or $name.IndexOfAny([char[]]'\/:') -ge 0) { return $null }
    $names.Add($name)
  }
  return $null  # An import array requires its complete zero terminator.
}

# ---------------------------------------------------------------------------
# OS-provided classification
# ---------------------------------------------------------------------------

function Test-SystemDll {
  <#
    .SYNOPSIS
    True when an import is provided by Windows itself and must NOT be shipped.

    An import counts as OS-provided when it resolves under %SystemRoot%\System32
    (or is an api-ms-win-* / ext-ms-* API set). Everything else - libiconv-2.dll,
    libgcc_s_seh-1.dll, SDL3.dll, zlib1.dll - is ours to ship.
  #>
  param([Parameter(Mandatory = $true)][string]$Name)

  $lower = $Name.ToLowerInvariant()
  # VC redistributables: VC redistributables can exist in the developer's System32
  # without being part of a clean Windows installation. Always stage them.
  if ($lower -match '^(vcruntime|msvcp|concrt|mfc|vcomp|vccorlib)[0-9]') { return $false }
  if ($lower.StartsWith('api-ms-win-') -or $lower.StartsWith('ext-ms-')) { return $true }

  $sys32 = Join-Path $env:SystemRoot 'System32'
  return (Test-Path -LiteralPath (Join-Path $sys32 $Name) -PathType Leaf)
}

# ---------------------------------------------------------------------------
# Stage enumeration
# ---------------------------------------------------------------------------

function Get-StagedPeFile {
  <#
    .SYNOPSIS
    Every .exe/.dll under the stage, recursively.

    .DESCRIPTION
    Extension filtering is done in the pipeline rather than with
    Get-ChildItem -Include: under PowerShell 5.1, -Include is silently ignored
    when the directory is supplied via -LiteralPath, which would make both the
    closure walk and the verification pass sweep non-PE payload (assets, .lua,
    .toml) and report it as corrupt.
  #>
  param([Parameter(Mandatory = $true)][string]$StageDir)

  Get-ChildItem -LiteralPath $StageDir -File -Recurse |
    Where-Object { $_.Extension -eq '.exe' -or $_.Extension -eq '.dll' }
}

# ---------------------------------------------------------------------------
# Closure staging
# ---------------------------------------------------------------------------

function Copy-RuntimeDllClosure {
  <#
    .SYNOPSIS
    Walk the import graph of every PE already in $StageDir and copy the full
    transitive set of non-OS dependencies in from $SearchDirs.

    .DESCRIPTION
    Throws when a dependency cannot be found in any search directory - an
    incomplete package must fail the build, not ship. Returns the list of DLL
    file names that were added, so the caller can log/diff them.
  #>
  param(
    [Parameter(Mandatory = $true)][string]$StageDir,
    [Parameter(Mandatory = $true)][string[]]$SearchDirs
  )

  $dirs = @($SearchDirs | Where-Object { $_ -and (Test-Path -LiteralPath $_) })
  if ($dirs.Count -eq 0) {
    throw "Copy-RuntimeDllClosure: no usable search directory in: $($SearchDirs -join ', ')"
  }

  $seen = New-Object Collections.Generic.HashSet[string] ([StringComparer]::OrdinalIgnoreCase)
  $queue = New-Object Collections.Generic.Queue[string]
  $added = New-Object Collections.Generic.List[string]

  # NB: Get-ChildItem -Include is silently ignored when the path is passed via
  # -LiteralPath (PowerShell 5.1), so filter on the extension explicitly.
  foreach ($f in Get-StagedPeFile -StageDir $StageDir) {
    [void]$seen.Add($f.Name)
    $queue.Enqueue($f.FullName)
  }
  if ($queue.Count -eq 0) {
    throw "Copy-RuntimeDllClosure: no .exe/.dll staged under $StageDir"
  }

  while ($queue.Count -gt 0) {
    $path = $queue.Dequeue()
    $pe = Get-PeInfo -Path $path
    if (-not $pe) { continue }

    foreach ($dep in $pe.Imports) {
      if ($seen.Contains($dep)) { continue }
      if (Test-SystemDll -Name $dep) { [void]$seen.Add($dep); continue }

      $source = $null
      foreach ($d in $dirs) {
        $candidate = Join-Path $d $dep
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $source = $candidate; break }
      }
      if (-not $source) {
        throw ("Unresolved runtime dependency '$dep' required by " +
          "$([IO.Path]::GetFileName($path)). Searched: $($dirs -join ', '). " +
          'Shipping without it produces 0xC0000135 on a clean machine, or ' +
          '0xC000007B if the player has a 32-bit copy on PATH.')
      }

      Copy-Item -LiteralPath $source -Destination $StageDir -Force
      [void]$seen.Add($dep)
      $added.Add($dep)
      $queue.Enqueue((Join-Path $StageDir $dep))
    }
  }

  return $added.ToArray()
}

function Assert-RuntimeDllClosure {
  <#
    .SYNOPSIS
    Verify the staged package is self-contained and uniformly one architecture.

    .DESCRIPTION
    Re-reads the staged tree rather than trusting the copy pass, so a stale
    stage directory or a hand-edited zip layout is caught too. Two invariants:

      1. every non-OS import of every staged PE is present in the stage
      2. every staged PE has the same machine type

    Invariant 2 is what a hand-maintained DLL list can never check: a 32-bit
    DLL dropped into an x64 package is precisely the 0xC000007B failure.
  #>
  param(
    [Parameter(Mandatory = $true)][string]$StageDir,
    [string]$ExpectedMachine = 'x64'
  )

  $staged = @(Get-StagedPeFile -StageDir $StageDir)
  if ($staged.Count -eq 0) {
    throw "Assert-RuntimeDllClosure: no .exe/.dll staged under $StageDir"
  }

  $present = New-Object Collections.Generic.HashSet[string] ([StringComparer]::OrdinalIgnoreCase)
  foreach ($f in $staged) { [void]$present.Add($f.Name) }

  $problems = New-Object Collections.Generic.List[string]
  foreach ($f in $staged) {
    $pe = Get-PeInfo -Path $f.FullName
    if (-not $pe) { $problems.Add("$($f.Name): not a valid PE image"); continue }

    if ($pe.Machine -ne $ExpectedMachine) {
      $problems.Add("$($f.Name): machine $($pe.Machine), expected $ExpectedMachine")
    }
    foreach ($dep in $pe.Imports) {
      if ($present.Contains($dep)) { continue }
      if (Test-SystemDll -Name $dep) { continue }
      $problems.Add("$($f.Name): imports '$dep', which is neither staged nor OS-provided")
    }
  }

  if ($problems.Count -gt 0) {
    throw ("Release stage is not self-contained:`n  " + ($problems -join "`n  "))
  }

  return $staged.Count
}
