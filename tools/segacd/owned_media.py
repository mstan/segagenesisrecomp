"""Shared, bounded program-area reads from owned CUE/BIN or SGCD media.

Uses the same CUE timeline and SGCD integrity checks as the native Sega CD
runner. Reads do not create an intermediate disc copy. Pregaps are excluded
from program-area reads; explicit postgaps retain their normalized sectors.
"""
from pathlib import Path

try:
    from .disc_image import RAW, encode_zero_mode1_sector, read_cue
    from .sgcd_owned_extract import SGCD
except ImportError:
    from disc_image import RAW, encode_zero_mode1_sector, read_cue
    from sgcd_owned_extract import SGCD

MAX_READ = 16 * 1024 * 1024


class OwnedMedia:
    def __init__(self, path):
        self.path = Path(path).resolve(strict=True)
        self.image = None
        self.sources = None
        if self.path.suffix.lower() == '.sgcd':
            self.image = SGCD(self.path)
            self.tracks = self.image.tracks
            self.input_paths = {self.path}
        elif self.path.suffix.lower() == '.cue':
            self.sources = read_cue(self.path)
            self.tracks = [dict(number=t.number, data=t.data,
                                index00=t.index00, start=t.start, end=t.end)
                           for t in self.sources]
            self.input_paths = {self.path} | {t.path for t in self.sources}
        else:
            raise ValueError('select a CUE/BIN disc or a verified .sgcd image; extract archives first')

    def read_raw_sectors(self, lba, count=1, track=None):
        if (not isinstance(lba, int) or not isinstance(count, int) or count < 0
                or count > MAX_READ // RAW):
            raise ValueError('invalid sector range or allocation limit')
        if track is None:
            track = next((t for t in self.tracks if t['start'] <= lba < t['end']), None)
        if (track is None or not any(track is t for t in self.tracks)
                or not track['start'] <= lba <= track['end']
                or count > track['end'] - lba):
            raise ValueError('sector range outside selected track program area')
        if not count:
            return b''
        if self.image:
            offset = self.image.payload_offset + (lba - self.image.first_lba) * RAW
            with self.path.open('rb') as stream:
                stream.seek(offset)
                blob = stream.read(count * RAW)
        else:
            source = self.sources[track['number'] - 1]
            stored_count = source.stored_end - source.indexes[1]
            relative = lba - source.start
            stored = max(0, min(count, stored_count - relative))
            blob = b''
            if stored:
                with source.path.open('rb') as stream:
                    stream.seek((source.indexes[1] + relative) * RAW)
                    blob = stream.read(stored * RAW)
                if len(blob) != stored * RAW:
                    raise ValueError('source changed or truncated while reading track')
            gap_count = count - stored
            if gap_count:
                blob += (b''.join(encode_zero_mode1_sector(n)
                                  for n in range(lba + stored, lba + count))
                         if source.data else bytes(gap_count * RAW))
        if len(blob) != count * RAW:
            raise ValueError('source changed or truncated while reading track')
        return blob

    def read_raw_sector(self, lba, track=None):
        return self.read_raw_sectors(lba, 1, track)
