"""Synthetic SGCD/ISO tests; contains no owned disc or program bytes."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest


MODULE_PATH = Path(__file__).with_name("sgcd_owned_extract.py")
sys.path.insert(0, str(MODULE_PATH.parent))
import sgcd_owned_extract as extractor


def put_both(blob: bytearray, offset: int, value: int, width: int) -> None:
    blob[offset:offset + width] = value.to_bytes(width, "little")
    blob[offset + width:offset + 2 * width] = value.to_bytes(width, "big")


def directory_record(lba: int, size: int, name: bytes, directory: bool = False) -> bytes:
    padding = 1 if len(name) % 2 == 0 else 0
    length = 33 + len(name) + padding
    record = bytearray(length)
    record[0] = length
    put_both(record, 2, lba, 4)
    put_both(record, 10, size, 4)
    record[25] = 2 if directory else 0
    put_both(record, 28, 1, 2)
    record[32] = len(name)
    record[33:33 + len(name)] = name
    return bytes(record)


def synthetic_disc(path: Path, *, filename: bytes = b"PROGRAM.MMD;1",
                   data: bytes = b"synthetic program bytes\0",
                   volume_sectors: int = 30,
                   directory_collision: bool = False) -> None:
    sector_count = 180
    payload = bytearray(sector_count * extractor.RAW_SECTOR)

    def sector(lba: int) -> memoryview:
        start = (lba + 150) * extractor.RAW_SECTOR
        return memoryview(payload)[start:start + extractor.RAW_SECTOR]

    for lba in range(0, 30):
        raw = sector(lba)
        raw[:12] = b"\0" + b"\xff" * 10 + b"\0"
        raw[15] = 1

    pvd = sector(16)[16:16 + extractor.USER_SECTOR]
    pvd[0:7] = b"\x01CD001\x01"
    pvd[40:72] = b"SYNTHETIC TEST DISC".ljust(32, b" ")
    put_both(pvd, 80, volume_sectors, 4)
    put_both(pvd, 128, extractor.USER_SECTOR, 2)
    root = directory_record(20, extractor.USER_SECTOR, b"\0", directory=True)
    pvd[156:156 + len(root)] = root

    directory = sector(20)[16:16 + extractor.USER_SECTOR]
    dot = directory_record(20, extractor.USER_SECTOR, b"\0", directory=True)
    dotdot = directory_record(20, extractor.USER_SECTOR, b"\1", directory=True)
    program = directory_record(22, len(data), filename)
    directory[:len(dot)] = dot
    directory[len(dot):len(dot) + len(dotdot)] = dotdot
    start = len(dot) + len(dotdot)
    directory[start:start + len(program)] = program
    if directory_collision:
        folder = directory_record(21, extractor.USER_SECTOR, b"PROGRAM.MMD", directory=True)
        directory[start + len(program):start + len(program) + len(folder)] = folder
        nested = sector(21)[16:16 + extractor.USER_SECTOR]
        nested_dot = directory_record(21, extractor.USER_SECTOR, b"\0", directory=True)
        nested_dotdot = directory_record(20, extractor.USER_SECTOR, b"\1", directory=True)
        child = directory_record(23, 5, b"CHILD.BIN;1")
        nested[:len(nested_dot)] = nested_dot
        nested[len(nested_dot):len(nested_dot) + len(nested_dotdot)] = nested_dotdot
        child_start = len(nested_dot) + len(nested_dotdot)
        nested[child_start:child_start + len(child)] = child
        sector(23)[16:21] = b"child"
    for index, offset in enumerate(range(0, len(data), extractor.USER_SECTOR)):
        piece = data[offset:offset + extractor.USER_SECTOR]
        sector(22 + index)[16:16 + len(piece)] = piece

    payload_digest = hashlib.sha256(payload).digest()
    table_offset = extractor.HEADER.size
    payload_offset = table_offset + extractor.ENTRY.size
    header = extractor.HEADER.pack(extractor.MAGIC, 1, payload_offset,
                                   extractor.RAW_SECTOR, 1, -150, 30,
                                   payload_offset, len(payload), payload_digest,
                                   bytes(16))
    track = extractor.ENTRY.pack(1, 1, -150, 0, 30, bytes(12))
    path.write_bytes(header + track + payload)


class SGCDOwnedExtractTests(unittest.TestCase):
    def test_extracts_exact_program_and_hash_manifest(self):
        data = bytes(range(251)) * 9
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            disc = root / "synthetic.sgcd"
            out = root / "extracted"
            synthetic_disc(disc, data=data)
            manifest = extractor.extract_programs(disc, out)
            extracted = out / "PROGRAM.MMD"
            self.assertEqual(extracted.read_bytes(), data)
            self.assertEqual(manifest["files"][0]["iso_relative_lba"], 22)
            self.assertEqual(manifest["files"][0]["disc_lba"], 22)
            self.assertEqual(manifest["files"][0]["sha256"], hashlib.sha256(data).hexdigest())
            self.assertEqual(manifest["source_sgcd_sha256"],
                             hashlib.sha256(disc.read_bytes()).hexdigest())
            self.assertEqual(manifest["source_sgcd_payload_sha256"],
                             hashlib.sha256(disc.read_bytes()[extractor.HEADER.size +
                                                              extractor.ENTRY.size:]).hexdigest())
            self.assertEqual(manifest["boot_slices"], [])
            self.assertEqual(json.loads((out / "owned-program-manifest.json").read_text()), manifest)
            with self.assertRaises(FileExistsError):
                extractor.extract_programs(disc, out)

    def test_rejects_invalid_sgcd_track_geometry_before_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            disc = root / "bad-geometry.sgcd"
            synthetic_disc(disc)
            raw = bytearray(disc.read_bytes())
            entry_offset = extractor.HEADER.size
            number, flag, index00, start, end, reserved = extractor.ENTRY.unpack_from(raw, entry_offset)
            extractor.ENTRY.pack_into(raw, entry_offset, number, flag, index00, start, 31, reserved)
            disc.write_bytes(raw)
            out = root / "must-not-exist"
            with self.assertRaisesRegex(ValueError, "track extent"):
                extractor.extract_programs(disc, out)
            self.assertFalse(out.exists())

    def test_rejects_payload_hash_mismatch(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            disc = root / "bad-hash.sgcd"
            synthetic_disc(disc)
            raw = bytearray(disc.read_bytes())
            raw[-1] ^= 1
            disc.write_bytes(raw)
            with self.assertRaisesRegex(ValueError, "payload SHA-256 mismatch"):
                extractor.SGCD(disc)

    def test_rejects_iso_volume_exceeding_data_track(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            disc = root / "bad-iso-volume.sgcd"
            synthetic_disc(disc, volume_sectors=31)
            with self.assertRaisesRegex(ValueError, "volume exceeds the first data track"):
                extractor.extract_programs(disc, root / "extracted")
            self.assertFalse((root / "extracted").exists())

    def test_rejects_iso_traversal_name_without_writing_outside(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            disc = root / "traversal.sgcd"
            synthetic_disc(disc, filename=b"../ESCAPE.BIN;1", data=b"should not escape")
            out = root / "extracted"
            with self.assertRaisesRegex(ValueError, "unsafe path separator"):
                extractor.extract_programs(disc, out)
            self.assertFalse(out.exists())
            self.assertFalse((root / "ESCAPE.BIN").exists())

    def test_rejects_windows_reserved_device_basename(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            disc = root / "reserved-name.sgcd"
            synthetic_disc(disc, filename=b"CON.BIN;1")
            out = root / "extracted"
            with self.assertRaisesRegex(ValueError, "reserved Windows device name"):
                extractor.extract_programs(disc, out)
            self.assertFalse(out.exists())

    def test_rejects_candidate_file_that_is_directory_ancestor(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            disc = root / "file-directory-collision.sgcd"
            synthetic_disc(disc, directory_collision=True)
            out = root / "extracted"
            with self.assertRaisesRegex(ValueError, "collides with a directory ancestor"):
                extractor.extract_programs(disc, out)
            self.assertFalse(out.exists())


if __name__ == "__main__":
    unittest.main()
