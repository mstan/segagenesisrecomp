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
USER = 2048


class OwnedMedia:
    def __init__(self, path):
        self.path = Path(path).resolve(strict=True)
        self.image = None
        self.sources = None
        self.data_image = None
        if self.path.suffix.lower() == '.sgcd':
            self.image = SGCD(self.path)
            self.tracks = self.image.tracks
            self.input_paths = {self.path}
        elif self.path.suffix.lower() == '.cue':
            self.sources = read_cue(self.path, allow_user_data=True)
            self.tracks = [dict(number=t.number, data=t.data,
                                index00=t.index00, start=t.start, end=t.end)
                           for t in self.sources]
            self.input_paths = {self.path} | {t.path for t in self.sources}
            for row, source in zip(self.tracks, self.sources):
                if source.sector_size != RAW:
                    row['sector_size'] = source.sector_size
        elif self.path.suffix.lower() in ('.iso', '.bin', '.img'):
            size = self.path.stat().st_size
            with self.path.open('rb') as stream:
                header = stream.read(16)
            raw = header[:12] == b'\0' + b'\xff' * 10 + b'\0' and header[15:16] == b'\1'
            sector_size = RAW if raw else USER
            if size % sector_size or not 17 <= size // sector_size <= 450000:
                raise ValueError('truncated or oversized MODE1 data image')
            self.data_image = self.path
            self.tracks = [dict(number=1, data=True, index00=0, start=0,
                                end=size // sector_size, sector_size=sector_size)]
            self.input_paths = {self.path}
        else:
            raise ValueError('select a CUE/BIN disc, MODE1 ISO/BIN/IMG or verified .sgcd image')

    def read_user_sectors(self, lba, count=1, track=None):
        """Bounded MODE1 payload reads; 2048-byte ISOs have no fabricated parity."""
        if track is None:
            track = next((t for t in self.tracks if t['data'] and t['start'] <= lba <= t['end']), None)
        if (track is None or not any(track is t for t in self.tracks) or not track['data']
                or not isinstance(lba, int) or not isinstance(count, int) or count < 0
                or count > MAX_READ // RAW or not track['start'] <= lba <= track['end']
                or count > track['end'] - lba):
            raise ValueError('user-sector range outside selected data track or allocation limit')
        if track.get('sector_size', RAW) == USER:
            relative = lba - track['start']
            source = self.sources[track['number'] - 1] if self.sources else None
            stored_count = source.stored_end - source.indexes[1] if source else track['end']
            stored = max(0, min(count, stored_count - relative))
            path = source.path if source else self.data_image
            with path.open('rb') as stream:
                stream.seek(((source.indexes[1] if source else 0) + relative) * USER)
                data = stream.read(stored * USER)
            if len(data) != stored * USER:
                raise ValueError('data image changed/truncated while reading')
            return data + bytes((count - stored) * USER)
        raw = self.read_raw_sectors(lba, count, track)
        result = bytearray()
        for offset in range(0, len(raw), RAW):
            sector = raw[offset:offset + RAW]
            if sector[:12] != b'\0' + b'\xff' * 10 + b'\0' or sector[15] != 1:
                raise ValueError('invalid MODE1/2352 data sector')
            result.extend(sector[16:16 + USER])
        return bytes(result)

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
        if track.get('sector_size', RAW) == USER:
            raise ValueError('2048-byte ISO data has no raw sector/parity representation')
        if self.data_image:
            with self.data_image.open('rb') as stream:
                stream.seek(lba * RAW)
                blob = stream.read(count * RAW)
        elif self.image:
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
