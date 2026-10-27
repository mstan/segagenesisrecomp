param(
    [string]$DataRoot = "",
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
try { $host.UI.RawUI.WindowTitle = 'Sonic CD' } catch { }
$packageRoot = [IO.Path]::GetFullPath((Split-Path -Parent $MyInvocation.MyCommand.Path))
if ([string]::IsNullOrWhiteSpace($DataRoot)) {
    $DataRoot = Join-Path $packageRoot 'userdata'
}
$dataRootFull = [IO.Path]::GetFullPath($DataRoot)
$driveRoot = [IO.Path]::GetPathRoot($dataRootFull)
if ($driveRoot -and $driveRoot.Equals('C:\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Sonic CD user data on C: is refused. Pass -DataRoot with an F: or G: path."
}

$exe = Join-Path $packageRoot 'SonicCD.exe'
$sdl = Join-Path $packageRoot 'SDL2.dll'
$converter = Join-Path $packageRoot 'tools\segacd\disc_image.py'
$packageManifest = Join-Path $packageRoot 'package-manifest.json'
foreach ($required in @($exe, $sdl, $converter, $packageManifest)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Package file is missing: $required"
    }
}

$configPath = Join-Path $dataRootFull 'portable-config.json'
if ($ValidateOnly) {
    $manifest = Get-Content -LiteralPath $packageManifest -Raw | ConvertFrom-Json
    if ($manifest.schema -ne 'SegaCDWindowsPortablePackage1' -or
        -not ($manifest.PSObject.Properties.Name -contains 'owner_media_copied') -or
        $manifest.owner_media_copied -ne $false -or -not $manifest.files) {
        throw 'Package manifest schema, file list, or owner-media policy is invalid.'
    }
    $packagePrefix = $packageRoot.TrimEnd('\') + '\'
    $verified = @{}
    foreach ($entry in $manifest.files) {
        $relative = [string]$entry.path
        $relativeWindows = $relative.Replace('/', '\')
        if ([string]::IsNullOrWhiteSpace($relative) -or
            [IO.Path]::IsPathRooted($relativeWindows) -or
            $relativeWindows -match '(^|\\)\.\.(\\|$)' -or
            $relativeWindows.Contains(':') -or
            $null -eq $entry.bytes -or [long]$entry.bytes -lt 0 -or
            [string]$entry.sha256 -notmatch '^[0-9a-fA-F]{64}$') {
            throw "Unsafe or malformed path in package manifest: $relative"
        }
        $path = [IO.Path]::GetFullPath((Join-Path $packageRoot $relativeWindows))
        if (-not $path.StartsWith($packagePrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Package path escapes its directory: $relative"
        }
        $normalizedRelative = $relativeWindows.Replace('\', '/').ToLowerInvariant()
        if ($verified.ContainsKey($normalizedRelative)) {
            throw "Duplicate package path in manifest: $relative"
        }
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Package file is missing: $($entry.path)"
        }
        $resolvedPath = (Resolve-Path -LiteralPath $path).ProviderPath
        if (-not $resolvedPath.StartsWith($packagePrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Package file resolves outside its directory: $relative"
        }
        $item = Get-Item -LiteralPath $path
        if ($item.Length -ne [long]$entry.bytes) {
            throw "Package file size mismatch: $relative"
        }
        $actualHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -ne ([string]$entry.sha256).ToLowerInvariant()) {
            throw "Package file hash mismatch: $relative"
        }
        $verified[$normalizedRelative] = $actualHash
    }
    foreach ($requiredRelative in @('soniccd.exe', 'sdl2.dll', 'soniccd.ps1', 'soniccd.cmd', 'readme-portable-windows.md', 'tools/segacd/disc_image.py', 'license', 'third-party-licenses.md')) {
        if (-not $verified.ContainsKey($requiredRelative)) {
            throw "Required package file is not recorded: $requiredRelative"
        }
    }
    if ($verified['soniccd.exe'] -ne ([string]$manifest.runner_sha256).ToLowerInvariant() -or
        $verified['sdl2.dll'] -ne ([string]$manifest.sdl2_sha256).ToLowerInvariant()) {
        throw 'Runner or SDL2 top-level hash does not match the package manifest.'
    }
    $packageConfigPath = Join-Path $packageRoot 'userdata\portable-config.json'
    if (-not (Test-Path -LiteralPath $packageConfigPath -PathType Leaf)) {
        throw 'Portable package configuration is missing.'
    }
    foreach ($checkConfigPath in @($packageConfigPath, $configPath)) {
        if (Test-Path -LiteralPath $checkConfigPath -PathType Leaf) {
            $config = Get-Content -LiteralPath $checkConfigPath -Raw | ConvertFrom-Json
            if ($config.schema -ne 'SegaCDWindowsPortablePackage1') {
                throw "Portable configuration schema is invalid: $checkConfigPath"
            }
        }
    }
    Write-Output "Package validation passed: $packageRoot"
    exit 0
}

$dirs = @(
    $dataRootFull,
    (Join-Path $dataRootFull 'temp'),
    (Join-Path $dataRootFull 'appdata\Local'),
    (Join-Path $dataRootFull 'appdata\Roaming'),
    (Join-Path $dataRootFull 'xdg\cache'),
    (Join-Path $dataRootFull 'xdg\config'),
    (Join-Path $dataRootFull 'xdg\data'),
    (Join-Path $dataRootFull 'xdg\state'),
    (Join-Path $dataRootFull 'python-cache'),
    (Join-Path $dataRootFull 'logs')
)
foreach ($dir in $dirs) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}
$env:TEMP = Join-Path $dataRootFull 'temp'
$env:TMP = $env:TEMP
$env:LOCALAPPDATA = Join-Path $dataRootFull 'appdata\Local'
$env:APPDATA = Join-Path $dataRootFull 'appdata\Roaming'
$env:XDG_CACHE_HOME = Join-Path $dataRootFull 'xdg\cache'
$env:XDG_CONFIG_HOME = Join-Path $dataRootFull 'xdg\config'
$env:XDG_DATA_HOME = Join-Path $dataRootFull 'xdg\data'
$env:XDG_STATE_HOME = Join-Path $dataRootFull 'xdg\state'
$env:PYTHONPYCACHEPREFIX = Join-Path $dataRootFull 'python-cache'
# Unattended QA may leave SDL's dummy driver in its environment. Owner launches
# always use the normal SDL audio device and runner volume defaults.
Remove-Item Env:SDL_AUDIODRIVER -ErrorAction SilentlyContinue

if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
    $seedConfig = Join-Path $packageRoot 'userdata\portable-config.json'
    if (Test-Path -LiteralPath $seedConfig -PathType Leaf) {
        Copy-Item -LiteralPath $seedConfig -Destination $configPath
    }
}
if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
    $defaultConfig = [ordered]@{
        schema = 'SegaCDWindowsPortablePackage1'
        bios_path = $null
        disc_path = $null
    }
    $json = $defaultConfig | ConvertTo-Json
    [IO.File]::WriteAllText($configPath, $json + "`n", [Text.UTF8Encoding]::new($false))
}
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
if ($config.schema -ne 'SegaCDWindowsPortablePackage1') {
    throw "Invalid portable configuration: $configPath"
}

Add-Type -AssemblyName System.Windows.Forms
function Select-InputFile([string]$title, [string]$filter) {
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = $title
    $dialog.Filter = $filter
    $dialog.CheckFileExists = $true
    $dialog.Multiselect = $false
    if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
        return $dialog.FileName
    }
    return $null
}

$biosPath = [string]$config.bios_path
if (-not $biosPath -or -not (Test-Path -LiteralPath $biosPath -PathType Leaf)) {
    $biosPath = Select-InputFile 'Select your Sega CD BIOS file' 'BIOS files (*.bin;*.rom)|*.bin;*.rom|All files (*.*)|*.*'
    if (-not $biosPath) { throw 'No BIOS was selected.' }
}
$discPath = [string]$config.disc_path
if (-not $discPath -or -not (Test-Path -LiteralPath $discPath -PathType Leaf)) {
    $discPath = Select-InputFile 'Select your verified Sega CD SGCD image or CUE' 'Sega CD image (*.sgcd;*.cue)|*.sgcd;*.cue|All files (*.*)|*.*'
    if (-not $discPath) { throw 'No disc image was selected.' }
}

$runDisc = [IO.Path]::GetFullPath($discPath)
if ([IO.Path]::GetExtension($runDisc).Equals('.cue', [StringComparison]::OrdinalIgnoreCase)) {
    $cuePath = $runDisc
    $python = $null
    $pythonArgs = @()
    $pythonCandidates = @(
        @{ command = (Get-Command py.exe -ErrorAction SilentlyContinue); args = @('-3') },
        @{ command = (Get-Command python.exe -ErrorAction SilentlyContinue); args = @() },
        @{ command = (Get-Command python -ErrorAction SilentlyContinue); args = @() }
    )
    foreach ($candidate in $pythonCandidates) {
        if (-not $candidate.command) { continue }
        $versionOutput = & $candidate.command.Source @($candidate.args) --version 2>$null
        if ($LASTEXITCODE -eq 0 -and ($versionOutput -join ' ') -match '^Python 3\.') {
            $python = $candidate.command
            $pythonArgs = @($candidate.args)
            break
        }
    }
    if (-not $python) { throw 'CUE conversion needs Python 3. Install Python or choose an SGCD image.' }
    $identityOutput = & $python.Source @pythonArgs $converter inspect-cue $cuePath
    if ($LASTEXITCODE -ne 0) { throw 'CUE source inspection failed; check that the CUE and referenced tracks are supported and readable.' }
    $identityJson = (@($identityOutput) -join "`n").Trim()
    if (-not $identityJson) { throw 'CUE source inspection returned no identity.' }
    try { $sourceIdentity = $identityJson | ConvertFrom-Json } catch { throw 'CUE source inspection returned invalid JSON.' }
    if ($sourceIdentity.schema -ne 'SGCDCueSourceIdentity1') { throw 'CUE source identity schema is invalid.' }
    $hashAlgorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $identityBytes = [Text.Encoding]::UTF8.GetBytes($identityJson)
        $identityHash = [BitConverter]::ToString($hashAlgorithm.ComputeHash($identityBytes)).Replace('-', '').ToLowerInvariant()
    } finally {
        $hashAlgorithm.Dispose()
    }
    $discDir = Join-Path $dataRootFull 'disc'
    New-Item -ItemType Directory -Force -Path $discDir | Out-Null
    $runDisc = Join-Path $discDir "owned-$identityHash.sgcd"
    $cacheManifestPath = Join-Path $discDir "owned-$identityHash.json"
    if (Test-Path -LiteralPath $runDisc -PathType Leaf) {
        if (-not (Test-Path -LiteralPath $cacheManifestPath -PathType Leaf)) {
            throw "A converted-disc cache exists without its source manifest: $runDisc"
        }
        $cacheManifest = Get-Content -LiteralPath $cacheManifestPath -Raw | ConvertFrom-Json
        if ($cacheManifest.schema -ne 'SegaCDCueCache1' -or
            $cacheManifest.source_identity_sha256 -ne $identityHash -or
            $cacheManifest.source_identity_json -cne $identityJson) {
            throw "Converted-disc cache source identity is invalid: $runDisc"
        }
        $cachedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $runDisc).Hash.ToLowerInvariant()
        if ($cachedHash -ne ([string]$cacheManifest.sgcd_sha256).ToLowerInvariant()) {
            throw "Converted-disc cache content has changed: $runDisc"
        }
    } elseif (Test-Path -LiteralPath $cacheManifestPath -PathType Leaf) {
        throw "Converted-disc cache manifest exists without its image: $runDisc"
    } else {
        & $python.Source @pythonArgs $converter $cuePath $runDisc
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $runDisc -PathType Leaf)) {
            throw 'CUE conversion failed; see the console output.'
        }
        $identityOutputAfter = & $python.Source @pythonArgs $converter inspect-cue $cuePath
        if ($LASTEXITCODE -ne 0 -or (@($identityOutputAfter) -join "`n").Trim() -cne $identityJson) {
            Remove-Item -LiteralPath $runDisc -ErrorAction SilentlyContinue
            throw 'CUE or referenced track files changed during conversion; the generated cache was not accepted.'
        }
        $convertedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $runDisc).Hash.ToLowerInvariant()
        $cacheManifest = [ordered]@{
            schema = 'SegaCDCueCache1'
            source_identity_sha256 = $identityHash
            source_identity_json = $identityJson
            sgcd_sha256 = $convertedHash
        }
        $manifestTemp = $cacheManifestPath + '.tmp'
        [IO.File]::WriteAllText($manifestTemp, ($cacheManifest | ConvertTo-Json -Depth 8) + "`n", [Text.UTF8Encoding]::new($false))
        [IO.File]::Move($manifestTemp, $cacheManifestPath)
    }
}

$config.bios_path = [IO.Path]::GetFullPath($biosPath)
$config.disc_path = [IO.Path]::GetFullPath($discPath)
$config | ConvertTo-Json | ForEach-Object {
    [IO.File]::WriteAllText($configPath, $_ + "`n", [Text.UTF8Encoding]::new($false))
}

$settingsPath = Join-Path $dataRootFull 'sonic-cd-settings.ini'
if (-not (Test-Path -LiteralPath $settingsPath -PathType Leaf)) {
    $settings = "fullscreen=0`nwindow_scale=3`nlinear_filter=0`naudio_enabled=1`nvolume=100`nview_mode=2`n"
    [IO.File]::WriteAllText($settingsPath, $settings, [Text.UTF8Encoding]::new($false))
}
$backupPath = Join-Path $dataRootFull 'sonic-cd.brm'
$seedPath = Join-Path $packageRoot 'seed\sonic-cd.brm'
if (-not (Test-Path -LiteralPath $backupPath -PathType Leaf) -and
    (Test-Path -LiteralPath $seedPath -PathType Leaf)) {
    Copy-Item -LiteralPath $seedPath -Destination $backupPath
}

$logPath = Join-Path $dataRootFull 'logs\SonicCD.log'
$arguments = @('--bios', $config.bios_path, '--disc', $runDisc,
               '--backup', $backupPath, '--settings', $settingsPath)
Push-Location $dataRootFull
try {
    & $exe @arguments *>> $logPath
    $exitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
if ($exitCode -ne 0) {
    Write-Error "Sonic CD exited with code $exitCode. Diagnostic output: $logPath"
    Get-Content -LiteralPath $logPath -Tail 20
    exit $exitCode
}
