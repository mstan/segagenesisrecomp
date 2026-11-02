"""Program-area CUE/SGCD parity, gaps, corruption and allocation contracts."""
from pathlib import Path
import tempfile
import unittest

from disc_image import RAW, convert, encode_zero_mode1_sector
from owned_media import OwnedMedia, MAX_READ


class OwnedMediaTests(unittest.TestCase):
    def test_iso_and_raw_data_images_have_equal_bounded_payload_reads(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            raw = b''.join(encode_zero_mode1_sector(n) for n in range(20))
            payload = b''.join(raw[n + 16:n + 2064] for n in range(0, len(raw), RAW))
            (root / 'data.iso').write_bytes(payload)
            (root / 'data.bin').write_bytes(raw)
            a, b = OwnedMedia(root / 'data.iso'), OwnedMedia(root / 'data.bin')
            self.assertEqual(a.read_user_sectors(0, 20), b.read_user_sectors(0, 20))
            with self.assertRaises(ValueError):
                a.read_raw_sectors(0)
            for media in (a, b):
                for lba, count in ((-1, 1), (20, 1), (0, -1), (0, MAX_READ)):
                    with self.assertRaises(ValueError):
                        media.read_user_sectors(lba, count)
            (root / 'bad.iso').write_bytes(payload + b'!')
            with self.assertRaises(ValueError):
                OwnedMedia(root / 'bad.iso')

    def test_user_data_cue_keeps_audio_but_native_normalizer_remains_strict(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'data.iso').write_bytes(bytes(2048 * 20))
            (root / 'audio.bin').write_bytes(b'A' * RAW * 2)
            cue = root / 'disc.cue'
            cue.write_text('FILE "data.iso" BINARY\nTRACK 01 MODE1/2048\nINDEX 01 00:00:00\n'
                           'FILE "audio.bin" BINARY\nTRACK 02 AUDIO\nINDEX 01 00:00:00\n')
            media = OwnedMedia(cue)
            self.assertEqual(media.read_user_sectors(0), bytes(2048))
            self.assertEqual(media.read_raw_sectors(20, 2), b'A' * RAW * 2)
            with self.assertRaises(ValueError):
                convert(cue, root / 'disc.sgcd')

    def test_program_areas_match_normalized_image_with_both_gap_types(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data = b''.join(encode_zero_mode1_sector(n) for n in range(4))
            (root / 'data.bin').write_bytes(data)
            (root / 'music.bin').write_bytes(bytes(2 * RAW) + b'A' * (3 * RAW))
            cue = root / 'disc.cue'
            cue.write_text('FILE "data.bin" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n'
                           'POSTGAP 00:00:02\nFILE "music.bin" BINARY\nTRACK 02 AUDIO\n'
                           'PREGAP 00:00:03\nINDEX 00 00:00:00\nINDEX 01 00:00:02\nPOSTGAP 00:00:01\n')
            image = root / 'disc.sgcd'
            convert(cue, image)
            a, b = OwnedMedia(cue), OwnedMedia(image)
            self.assertEqual(a.tracks, b.tracks)
            self.assertEqual(a.input_paths, {cue, root / 'data.bin', root / 'music.bin'})
            for index in range(2):
                ta, tb = a.tracks[index], b.tracks[index]
                length = ta['end'] - ta['start']
                self.assertEqual(a.read_raw_sectors(ta['start'], length, ta),
                                 b.read_raw_sectors(tb['start'], length, tb))
                for lba, count in [(ta['start']-1, 1), (ta['end'], 1),
                                   (ta['start'], -1), (ta['start'], MAX_READ // RAW + 1)]:
                    with self.assertRaises(ValueError):
                        a.read_raw_sectors(lba, count, ta)
                self.assertEqual(a.read_raw_sectors(ta['end'], 0, ta), b'')
            (root / 'music.bin').write_bytes(b'')
            with self.assertRaises(ValueError):
                a.read_raw_sectors(a.tracks[1]['start'], 1, a.tracks[1])


if __name__ == '__main__':
    unittest.main()
