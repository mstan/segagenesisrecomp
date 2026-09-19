param(
    [Parameter(Mandatory=$true)][string]$Archive,
    [Parameter(Mandatory=$true)][string]$RuntimeDirectory
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archivePath = (Resolve-Path -LiteralPath $Archive).Path
$runtimePath = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
$zip = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    $entries = @($zip.Entries | Where-Object { $_.Name -match '\.(bin|md)$' })
    if ($entries.Count -ne 1 -or $entries[0].Length -ne 2097152) {
        throw 'Expected exactly one 2 MiB Amy Rev 1.7.1 donor ROM.'
    }
    $data = New-Object System.IO.MemoryStream
    $inputStream = $entries[0].Open()
    try { $inputStream.CopyTo($data) } finally { $inputStream.Dispose() }
    $bytes = $data.ToArray()
    $data.Dispose()
} finally { $zip.Dispose() }
$sha = [System.Security.Cryptography.SHA256]::Create()
try { $actual = [BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-', '').ToLowerInvariant() }
finally { $sha.Dispose() }
$expected = '9c028944730128f6b9999fc74babf69694b0edab50e3f42cc6b60a185d0b1457'
if ($actual -ne $expected) { throw "Wrong donor revision: SHA-256 $actual" }
$assetDir = Join-Path $runtimePath 'assets/characters'
New-Item -ItemType Directory -Path $assetDir -Force | Out-Null
$destination = Join-Path $assetDir 'amy-1.7.1.bin'
if (Test-Path -LiteralPath $destination) {
    if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
        throw "Refusing to overwrite a different file: $destination"
    }
} else {
    $outputStream = [System.IO.File]::Open($destination, [System.IO.FileMode]::CreateNew)
    try { $outputStream.Write($bytes, 0, $bytes.Length) } finally { $outputStream.Dispose() }
}
Write-Output "Verified private Amy donor staged: $destination"
Write-Output 'Do not commit or redistribute this owner-supplied binary.'
