#!/usr/bin/env python3
"""Normalize an owned raw BIN/CUE into a bounded, portable Sega CD image.

Preserves stored INDEX 00 sectors and inserts virtual PREGAP/POSTGAP silence.
Supported source formats are BINARY MODE1/2352 and AUDIO; other formats fail
explicitly. No ISO extraction, game filenames or game-content adaptation.
"""
from dataclasses import dataclass, field
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import struct
import tempfile

RAW = 2352
FIRST_LBA = -150
MAX_LBA = 100 * 60 * 75 - 151
MAGIC = b'SGCDIMG\0'
HEADER = struct.Struct('<8s4I2i2Q32s16s')
ENTRY = struct.Struct('<IIiii12s')


@dataclass
class SourceTrack:
    number: int
    path: Path
    data: bool
    indexes: dict = field(default_factory=dict)
    pregap: int = 0
    postgap: int = 0
    stored_start: int = 0
    stored_end: int = 0
    index00: int = 0
    start: int = 0
    end: int = 0


def frames(value):
    match = re.fullmatch(r'(\d{1,3}):(\d{2}):(\d{2})', value)
    if not match:
        raise ValueError('Invalid CUE timestamp: ' + value)
    minute, second, frame = map(int, match.groups())
    if second >= 60 or frame >= 75:
        raise ValueError('Invalid CUE timestamp: ' + value)
    return (minute * 60 + second) * 75 + frame


def read_cue(cue):
    cue = Path(cue).resolve(strict=True)
    tracks, current, filename = [], None, None
    for line_no, line in enumerate(cue.read_text(encoding='utf-8-sig').splitlines(), 1):
        parts = line.strip().split(None, 1)
        if not parts:
            continue
        command, arg = parts[0].upper(), parts[1] if len(parts) == 2 else ''
        if command in {'REM', 'TITLE', 'PERFORMER', 'SONGWRITER', 'CATALOG', 'ISRC'}:
            continue
        if command == 'FILE':
            match = re.fullmatch(r'"([^"]+)"\s+BINARY', arg, re.I)
            if not match:
                raise ValueError('Only quoted BINARY FILE sources are supported')
            filename = (cue.parent / match[1]).resolve(strict=True)
            if not filename.is_file():
                raise ValueError('CUE FILE must be a regular file')
            current = None
        elif command == 'TRACK':
            match = re.fullmatch(r'(\d+)\s+(MODE1/2352|AUDIO)', arg, re.I)
            if not match or filename is None or len(tracks) >= 99 or int(match[1]) != len(tracks) + 1:
                raise ValueError('Expected consecutive raw MODE1/2352 or AUDIO tracks from 01')
            current = SourceTrack(len(tracks) + 1, filename, match[2].upper() != 'AUDIO')
            tracks.append(current)
        elif command == 'INDEX':
            match = re.fullmatch(r'(00|01)\s+(\S+)', arg)
            if not match or current is None or int(match[1]) in current.indexes:
                raise ValueError('Missing track, duplicate or unsupported INDEX')
            current.indexes[int(match[1])] = frames(match[2])
        elif command in {'PREGAP', 'POSTGAP'}:
            if current is None or getattr(current, '_' + command, False):
                raise ValueError('Missing track or duplicate ' + command)
            setattr(current, '_' + command, True)
            setattr(current, command.lower(), frames(arg))
        else:
            raise ValueError(f'Unsupported CUE command at line {line_no}: {command}')
    if not tracks:
        raise ValueError('Empty CUE')
    finished_files, cursor = set(), 0
    for i, track in enumerate(tracks):
        if 1 not in track.indexes:
            raise ValueError('Missing INDEX 01')
        size = track.path.stat().st_size
        if size % RAW or track.path in finished_files:
            raise ValueError('Truncated raw sector or non-contiguous FILE reuse')
        track.stored_start = track.indexes.get(0, track.indexes[1])
        track.stored_end = size // RAW
        if i + 1 < len(tracks) and tracks[i + 1].path == track.path:
            following = tracks[i + 1]
            if 1 not in following.indexes:
                raise ValueError('Missing following INDEX 01')
            track.stored_end = following.indexes.get(0, following.indexes[1])
        else:
            finished_files.add(track.path)
        if not 0 <= track.stored_start <= track.indexes[1] < track.stored_end <= size // RAW:
            raise ValueError('Overlapping, empty, reversed or out-of-file track bounds')
        stored_gap = track.indexes[1] - track.stored_start
        if i == 0:
            if stored_gap + track.pregap > 150:
                raise ValueError('First pregap exceeds the represented 150-sector lead-in')
            track.index00, track.start = FIRST_LBA, 0
        else:
            track.index00 = cursor
            track.start = cursor + track.pregap + stored_gap
        track.end = track.start + track.stored_end - track.indexes[1] + track.postgap
        cursor = track.end
        if track.data and (track.postgap or (i and track.pregap)):
            raise ValueError('Virtual data-track gaps need a raw sector encoder; supply stored raw sectors')
    if cursor > MAX_LBA:
        raise ValueError('Disc exceeds the two-digit MSF range')
    return tracks


def convert(cue, output, overwrite=False):
    cue, output = Path(cue).resolve(strict=True), Path(output).resolve()
    tracks = read_cue(cue)
    if output == cue or any(output == track.path for track in tracks):
        raise ValueError('Output must not replace a source CUE/BIN')
    if output.exists() and not overwrite:
        raise FileExistsError('Output exists; select another path or explicitly overwrite')
    output.parent.mkdir(parents=True, exist_ok=True)
    offset = HEADER.size + ENTRY.size * len(tracks)
    payload_bytes = (tracks[-1].end - FIRST_LBA) * RAW
    digest = hashlib.sha256()
    handle = tempfile.NamedTemporaryFile(prefix=output.name + '.', suffix='.tmp', dir=output.parent, delete=False)
    temporary = Path(handle.name)
    try:
        with handle:
            handle.write(bytes(offset))

            def write(blob):
                handle.write(blob)
                digest.update(blob)

            def silence(count):
                block = bytes(32 * RAW)
                while count:
                    n = min(count, 32)
                    write(block[:n * RAW])
                    count -= n

            for i, track in enumerate(tracks):
                gap = track.indexes[1] - track.stored_start
                silence(150 - gap if i == 0 else track.pregap)
                remaining = (track.stored_end - track.stored_start) * RAW
                with track.path.open('rb') as source:
                    source.seek(track.stored_start * RAW)
                    while remaining:
                        blob = source.read(min(remaining, 256 * RAW))
                        if not blob or len(blob) % RAW:
                            raise ValueError('Source changed/truncated while converting')
                        write(blob)
                        remaining -= len(blob)
                silence(track.postgap)
            if handle.tell() != offset + payload_bytes:
                raise ValueError('Disc timeline/payload length mismatch')
            payload_hash = digest.digest()
            handle.seek(0)
            handle.write(HEADER.pack(MAGIC, 1, offset, RAW, len(tracks), FIRST_LBA,
                                     tracks[-1].end, offset, payload_bytes, payload_hash, bytes(16)))
            for track in tracks:
                handle.write(ENTRY.pack(track.number, int(track.data), track.index00, track.start, track.end, bytes(12)))
            handle.flush()
            os.fsync(handle.fileno())
        if overwrite:
            os.replace(temporary, output)
        else:
            # Atomic create-if-absent, including a competing converter. Both
            # paths are on the same volume; the finally block removes our link.
            os.link(temporary, output)
    finally:
        if temporary.exists():
            temporary.unlink()
    return {'format': 'SGCDIMG1', 'payload_sha256': payload_hash.hex(), 'tracks': len(tracks),
            'first_lba': FIRST_LBA, 'leadout': tracks[-1].end, 'bytes': output.stat().st_size}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cue', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--overwrite', action='store_true')
    args = parser.parse_args()
    try:
        result = convert(args.cue, args.output, args.overwrite)
    except (OSError, ValueError) as error:
        parser.exit(1, f'Sega CD image: {error}\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
