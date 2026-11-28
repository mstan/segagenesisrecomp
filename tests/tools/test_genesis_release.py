"""Exercise release closure with synthetic PE inputs; no game data is needed."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


def pe(path, imports=(), machine=0x8664):
    data = bytearray(2048)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3c, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", data, 0x84, machine, 1)
    struct.pack_into("<H", data, 0x94, 0xf0)
    struct.pack_into("<H", data, 0x98, 0x20b)
    struct.pack_into("<I", data, 0x98 + 112 + 8, 0x1000 if imports else 0)
    struct.pack_into("<IIII", data, 0x188 + 8, 1024, 0x1000, 1024, 0x200)
    offset = 0x300
    for index, name in enumerate(imports):
        struct.pack_into("<I", data, 0x200 + 20 * index + 12, 0x1000 + offset - 0x200)
        raw = name.encode("ascii") + b"\0"
        data[offset:offset + len(raw)] = raw
        offset += len(raw)
    path.write_bytes(data)


@unittest.skipUnless(os.name == "nt", "Windows PowerShell release contract")
class GenesisReleaseTests(unittest.TestCase):
    def test_transitive_crt_closure_archive_and_rejections(self):
        provider = Path(__file__).resolve().parents[2] / "tools/release/GenesisRelease.ps1"
        with tempfile.TemporaryDirectory(prefix="genesis-release-") as work:
            root = Path(work)
            source = root / "source"; source.mkdir()
            stage = root / "stage"; stage.mkdir()
            pe(stage / "Game.exe", ["SDL2.dll", "VCRUNTIME140.dll", "api-ms-win-core-test.dll"])
            pe(source / "SDL2.dll", ["helper.dll"])
            pe(source / "helper.dll")
            pe(source / "VCRUNTIME140.dll")
            script = root / "check.ps1"
            script.write_text(r'''param($Provider, $Root)
$ErrorActionPreference = 'Stop'
. $Provider
$stage = Join-Path $Root 'stage'
$source = Join-Path $Root 'source'
$added = @(Copy-RuntimeDllClosure -StageDir $stage -SearchDirs @($source))
if ($added.Count -ne 3) { throw "Transitive closure differs: $added" }
if ((Assert-RuntimeDllClosure -StageDir $stage) -ne 4) { throw 'Expected four PE files' }
if (Test-SystemDll -Name 'vccorlib140.dll') { throw 'VC runtime classified as OS' }
$zip = Join-Path $Root 'Game.zip'
Write-GenesisReleaseArchive -StageDir $stage -Archive $zip
# Replacing an already verified archive is supported.
Write-GenesisReleaseArchive -StageDir $stage -Archive $zip
$z = [IO.Compression.ZipFile]::OpenRead($zip)
try { if ($z.Entries.Count -ne 4) { throw 'Archive differs from dependency-closed stage' } }
finally { $z.Dispose() }
foreach ($name in @('settings.ini', 'progress.srm', 'capture.replay.json', 'rom-Game.cfg')) {
  $bad = Join-Path $stage $name
  [IO.File]::WriteAllText($bad, 'private')
  $rejected = $false
  try { Write-GenesisReleaseArchive -StageDir $stage -Archive (Join-Path $Root 'bad.zip') }
  catch { $rejected = $true }
  Remove-Item -LiteralPath $bad
  if (-not $rejected) { throw "Accepted forbidden file: $name" }
}
$helper = Join-Path $stage 'helper.dll'
$bytes = [IO.File]::ReadAllBytes($helper)
$bytes[0x84] = 0x4c; $bytes[0x85] = 0x01
[IO.File]::WriteAllBytes($helper, $bytes)
$rejected = $false
try { Assert-RuntimeDllClosure -StageDir $stage | Out-Null } catch { $rejected = $true }
if (-not $rejected) { throw 'Accepted mixed-architecture DLL' }
Remove-Item -LiteralPath $helper
$rejected = $false
try { Assert-RuntimeDllClosure -StageDir $stage | Out-Null } catch { $rejected = $true }
if (-not $rejected) { throw 'Accepted unresolved transitive import' }
''', encoding="utf-8")
            result = subprocess.run(["powershell.exe", "-NoProfile", "-File", str(script), str(provider), str(root)],
                                    text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            self.assertEqual(result.returncode, 0, result.stdout)

if __name__ == "__main__":
    unittest.main()
