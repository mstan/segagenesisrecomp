"""Synthetic raw-media contracts; no owned data is required or embedded."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock
import sys

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('segacd_disc_image', ROOT / 'tools/segacd/disc_image.py')
disc = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = disc
spec.loader.exec_module(disc)
RAW = 2352
MODE1_ZERO_SECTOR_SHA256 = {
    # These fixed sectors were independently checked with pinned libchdr;
    # converter tests need no runtime dependency on that external library.
    -2: '6459524bb47e2df71087765858b8c7d234b566d6bbd8c77d36bb72fdfc6ff140',
    -1: '009dd77f45c7be04746fd06ddc55a91e2fda4bc9c460e1cff36c5a3abe5941ae',
    0: 'b4f18ab66709c9b3fdef2721cc323e031b6728f3ca6c57b7c435c96189222250',
}


def bitwise_edc(data):
    value = 0
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0xD8018001 if value & 1 else 0)
    return value


class DiscImageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='segacd-media-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.cue, self.output = self.root / 'disc.cue', self.root / 'disc.sgcd'

    def source(self, name, values):
        path = self.root / name
        path.write_bytes(b''.join(bytes([value]) * RAW for value in values))
        return path

    def convert(self, cue):
        self.cue.write_text(cue, encoding='utf-8')
        result = disc.convert(self.cue, self.output)
        data = self.output.read_bytes()
        header = struct.unpack('<8s4I2i2Q32s16s', data[:96])
        magic, version, size, sector, count, first, end, offset, length, digest, reserved = header
        self.assertEqual((magic, version, sector, first, reserved), (b'SGCDIMG\0', 1, RAW, -150, bytes(16)))
        self.assertEqual(size, 96 + count * 32)
        self.assertEqual(offset, size)
        self.assertEqual(length, (end + 150) * RAW)
        self.assertEqual(len(data), offset + length)
        self.assertEqual(digest, hashlib.sha256(data[offset:]).digest())
        self.assertEqual(result['payload_sha256'], digest.hex())
        tracks = [struct.unpack('<IIiii12s', data[96+i*32:128+i*32]) for i in range(count)]
        self.assertTrue(all(t[-1] == bytes(12) for t in tracks))
        return tracks, data[offset:]

    def test_multiple_files_preserve_stored_audio_pregap(self):
        self.source('data.bin', [1, 2])
        self.source('audio.bin', [3, 4, 5, 6])
        tracks, payload = self.convert('FILE "data.bin" BINARY\n TRACK 01 MODE1/2352\n INDEX 01 00:00:00\n'
                                       'FILE "audio.bin" BINARY\n TRACK 02 AUDIO\n INDEX 00 00:00:00\n INDEX 01 00:00:02\n')
        self.assertEqual([t[:-1] for t in tracks], [(1, 1, -150, 0, 2), (2, 0, 2, 4, 6)])
        virtual_pregap = b''.join(disc.encode_zero_mode1_sector(lba) for lba in range(-150, 0))
        self.assertEqual(payload, virtual_pregap + b''.join(bytes([i])*RAW for i in range(1, 7)))

    def test_shared_bin_virtual_and_stored_gaps(self):
        self.source('shared.bin', [1, 2, 3, 4, 5])
        tracks, payload = self.convert('FILE "shared.bin" BINARY\n TRACK 01 MODE1/2352\n INDEX 01 00:00:00\n'
                                       ' TRACK 02 AUDIO\n PREGAP 00:00:01\n INDEX 00 00:00:02\n INDEX 01 00:00:03\n POSTGAP 00:00:02\n')
        self.assertEqual([t[:-1] for t in tracks], [(1, 1, -150, 0, 2), (2, 0, 2, 4, 8)])
        virtual_pregap = b''.join(disc.encode_zero_mode1_sector(lba) for lba in range(-150, 0))
        self.assertEqual(payload, virtual_pregap + bytes([1])*RAW + bytes([2])*RAW + bytes(RAW)
                         + bytes([3])*RAW + bytes([4])*RAW + bytes([5])*RAW + bytes(2*RAW))

    def test_first_stored_pregap_occupies_end_of_first_track_pregap(self):
        self.source('a.bin', [11, 12, 13])
        tracks, payload = self.convert('FILE "a.bin" BINARY\n TRACK 01 AUDIO\n PREGAP 00:00:01\n'
                                       ' INDEX 00 00:00:00\n INDEX 01 00:00:02\n')
        self.assertEqual(tracks[0][:-1], (1, 0, -150, 0, 1))
        self.assertEqual(payload, bytes(148*RAW) + b''.join(bytes([i])*RAW for i in (11, 12, 13)))

    def test_unsupported_or_invalid_cue_does_not_publish(self):
        self.source('a.bin', [1, 2, 3])
        base = 'FILE "a.bin" BINARY\n TRACK 01 MODE1/2352\n INDEX 01 00:00:00\n'
        bad = [base.replace('MODE1/2352', 'MODE1/2048'), base.replace('01 MODE', '02 MODE'),
               base.replace('00:00:00', '00:60:00'), base + ' INDEX 01 00:00:01\n',
               base + ' INDEX 02 00:00:01\n', base.replace(' INDEX 01 00:00:00\n', ''),
               base.replace('00:00:00', '00:00:03'),
               base + ' PREGAP 00:02:01\n', base + ' FLAGS PRE\n',
               base + ' TRACK 02 AUDIO\n INDEX 01 00:00:00\n',
               base + ' TRACK 02 AUDIO\n INDEX 00 00:00:02\n INDEX 01 00:00:01\n']
        for cue in bad:
            with self.subTest(cue=cue):
                self.cue.write_text(cue, encoding='utf-8')
                with self.assertRaises(ValueError):
                    disc.convert(self.cue, self.output)
                self.assertFalse(self.output.exists())
                self.assertFalse(list(self.root.glob('*.tmp')))

    def test_mode1_encoder_boundaries_and_initial_virtual_pregap(self):
        source = self.source('data.bin', [0xA5])
        tracks, payload = self.convert('FILE "data.bin" BINARY\n TRACK 01 MODE1/2352\n INDEX 01 00:00:00\n')
        self.assertEqual(tracks[0][:-1], (1, 1, -150, 0, 1))
        for lba, msf, expected_hash in [(-2, bytes((0x00, 0x01, 0x73, 0x01)), MODE1_ZERO_SECTOR_SHA256[-2]),
                                        (-1, bytes((0x00, 0x01, 0x74, 0x01)), MODE1_ZERO_SECTOR_SHA256[-1])]:
            sector = payload[(lba + 150) * RAW:(lba + 151) * RAW]
            self.assertEqual(sector[12:16], msf)
            self.assertEqual(sector[16:2064], bytes(2048))
            self.assertEqual(int.from_bytes(sector[2064:2068], 'little'), bitwise_edc(sector[:2064]))
            self.assertEqual(hashlib.sha256(sector).hexdigest(), expected_hash)
        self.assertEqual(payload[150 * RAW:151 * RAW], bytes([0xA5]) * RAW)
        self.assertEqual(source.read_bytes(), bytes([0xA5]) * RAW)

        zero = disc.encode_zero_mode1_sector(0)
        self.assertEqual(zero[12:16], bytes((0x00, 0x02, 0x00, 0x01)))
        self.assertEqual(int.from_bytes(zero[2064:2068], 'little'), bitwise_edc(zero[:2064]))
        self.assertEqual(hashlib.sha256(zero).hexdigest(), MODE1_ZERO_SECTOR_SHA256[0])

    def test_mode1_virtual_gaps_encoded_and_stored_sectors_unchanged(self):
        source = self.source('shared.bin', [0x10, 0x20, 0x30, 0x40])
        cue = ('FILE "shared.bin" BINARY\n TRACK 01 MODE1/2352\n INDEX 01 00:00:00\n'
               ' TRACK 02 MODE1/2352\n PREGAP 00:00:02\n INDEX 00 00:00:01\n'
               ' INDEX 01 00:00:02\n POSTGAP 00:00:02\n')
        tracks, payload = self.convert(cue)
        self.assertEqual([t[:-1] for t in tracks], [(1, 1, -150, 0, 1), (2, 1, 1, 4, 8)])
        for lba in (-2, -1, 1, 2, 6, 7):
            offset = (lba + 150) * RAW
            sector = payload[offset:offset + RAW]
            self.assertEqual(sector, disc.encode_zero_mode1_sector(lba))
            self.assertEqual(int.from_bytes(sector[2064:2068], 'little'), bitwise_edc(sector[:2064]))
        for lba, value in [(0, 0x10), (3, 0x20), (4, 0x30), (5, 0x40)]:
            offset = (lba + 150) * RAW
            self.assertEqual(payload[offset:offset + RAW], bytes([value]) * RAW)
        self.assertEqual(source.read_bytes(), b''.join(bytes([v]) * RAW for v in (0x10, 0x20, 0x30, 0x40)))

    def test_truncated_raw_and_noncontiguous_reuse(self):
        self.source('a.bin', [1])
        self.source('b.bin', [2])
        cue = 'FILE "a.bin" BINARY\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n'
        self.cue.write_text(cue + 'FILE "b.bin" BINARY\n TRACK 02 AUDIO\n INDEX 01 00:00:00\n'
                           + 'FILE "a.bin" BINARY\n TRACK 03 AUDIO\n INDEX 01 00:00:00\n', encoding='utf-8')
        with self.assertRaises(ValueError):
            disc.convert(self.cue, self.output)
        self.cue.write_text(cue, encoding='utf-8')
        (self.root/'a.bin').write_bytes(b'not a sector')
        with self.assertRaises(ValueError):
            disc.convert(self.cue, self.output)

    def test_existing_output_and_source_are_preserved(self):
        source = self.source('a.bin', [7])
        self.cue.write_text('FILE "a.bin" BINARY\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n', encoding='utf-8')
        self.output.write_bytes(b'existing')
        with self.assertRaises(FileExistsError):
            disc.convert(self.cue, self.output)
        self.assertEqual(self.output.read_bytes(), b'existing')
        for output in [self.cue, source]:
            before = output.read_bytes()
            with self.assertRaises(ValueError):
                disc.convert(self.cue, output, overwrite=True)
            self.assertEqual(output.read_bytes(), before)
        disc.convert(self.cue, self.output, overwrite=True)
        self.assertEqual(self.output.read_bytes()[:8], b'SGCDIMG\0')

    def test_competing_output_cannot_be_replaced(self):
        self.source('a.bin', [7])
        self.cue.write_text('FILE "a.bin" BINARY\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n', encoding='utf-8')

        def compete(source, destination):
            Path(destination).write_bytes(b'competing writer')
            raise FileExistsError(destination)

        with mock.patch.object(disc.os, 'link', side_effect=compete):
            with self.assertRaises(FileExistsError):
                disc.convert(self.cue, self.output)
        self.assertEqual(self.output.read_bytes(), b'competing writer')
        self.assertFalse(list(self.root.glob('*.tmp')))


if __name__ == '__main__':
    unittest.main()
