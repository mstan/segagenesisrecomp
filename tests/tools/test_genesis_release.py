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
    struct.pack_into("<II", data, 0x98 + 56, 0x2000, 0x200)
    struct.pack_into("<I", data, 0x98 + 108, 16)
    struct.pack_into("<II", data, 0x98 + 112 + 8, 0x1000 if imports else 0, 20 * (len(imports) + 1) if imports else 0)
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
    def test_malformed_pe_metadata_is_rejected(self):
        provider = Path(__file__).resolve().parents[2] / "tools/release/RuntimeDllClosure.ps1"
        with tempfile.TemporaryDirectory(prefix="genesis-malformed-pe-") as work:
            root = Path(work)
            valid = root / "valid.dll"
            pe(valid, ["dep.dll"])
            original = valid.read_bytes()
            cases = {}
            def changed(name, offset, fmt, value):
                data = bytearray(original)
                struct.pack_into(fmt, data, offset, value)
                cases[name] = data
            changed("signature", 0x82, "<B", 1)
            changed("x64-pe32", 0x98, "<H", 0x10b)
            changed("optional-magic", 0x98, "<H", 0x999)
            changed("short-optional", 0x94, "<H", 64)
            changed("optional-past-eof", 0x94, "<H", 0xffff)
            changed("section-table", 0x86, "<H", 0xffff)
            changed("raw-section", 0x188 + 20, "<I", 0xffff)
            changed("directory-count", 0x98 + 108, "<I", 17)
            changed("import-rva", 0x98 + 112 + 8, "<I", 0x3000)
            changed("name-rva", 0x20c, "<I", 0x3000)
            changed("unterminated-descriptors", 0x98 + 112 + 12, "<I", 20)
            data = bytearray(original)
            struct.pack_into("<I", data, 0x200, 1)
            struct.pack_into("<I", data, 0x20c, 0)
            cases["missing-name"] = data
            data = bytearray(original); data[0x300:0x600] = b"A" * 0x300
            cases["unterminated-name"] = data
            data = bytearray(original)
            struct.pack_into("<I", data, 0x188 + 8, 0x1000)
            struct.pack_into("<I", data, 0x20c, 0x1700)
            cases["virtual-only-name"] = data
            cases["truncated-section-table"] = original[:0x188 + 39]
            for name, data in cases.items():
                (root / (name + ".bad")).write_bytes(data)
            script = root / "check.ps1"
            script.write_text(r'''param($Provider, $Root)
$ErrorActionPreference = 'Stop'
. $Provider
if (-not (Get-PeInfo -Path (Join-Path $Root 'valid.dll'))) { throw 'Rejected valid PE32+ fixture' }
foreach ($file in Get-ChildItem -LiteralPath $Root -Filter '*.bad' -File) {
  if ($null -ne (Get-PeInfo -Path $file.FullName)) { throw "Accepted malformed PE: $($file.Name)" }
}
''', encoding="utf-8")
            result = subprocess.run(["powershell.exe", "-NoProfile", "-File", str(script), str(provider), str(root)],
                                    text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            self.assertEqual(result.returncode, 0, result.stdout)

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
