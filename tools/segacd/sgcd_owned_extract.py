#!/usr/bin/env python3
"""Safely inventory and extract program files from a verified SGCD image.

This reads SGCDIMG1 images produced by ``disc_image.py`` and a bounded ISO9660
primary volume. It extracts only ISO files ending in .MMD or .BIN; it does not
extract audio tracks, execute content, or infer BIOS load addresses.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import sys


try:
    from .disc_image import ENTRY, HEADER, MAGIC, RAW as RAW_SECTOR
except ImportError:  # Direct invocation: python tools/segacd/sgcd_owned_extract.py
    from disc_image import ENTRY, HEADER, MAGIC, RAW as RAW_SECTOR

USER_SECTOR = 2048
MAX_PAYLOAD = 4 * 1024 * 1024 * 1024
MAX_VOLUME_SECTORS = 100 * 60 * 75
MAX_FILE_BYTES = 64 * 1024 * 1024
MAX_DIRECTORY_BYTES = 8 * 1024 * 1024
MAX_EXTRACTED_BYTES = 512 * 1024 * 1024
MAX_FILES = 16384
MAX_DEPTH = 16
HASH_CHUNK = 1024 * 1024


def _both(data: bytes, offset: int, width: int) -> int:
    if offset < 0 or offset + width * 2 > len(data):
        raise ValueError("truncated ISO both-endian field")
    little = int.from_bytes(data[offset:offset + width], "little")
    big = int.from_bytes(data[offset + width:offset + width * 2], "big")
    if little != big:
        raise ValueError("inconsistent ISO both-endian field")
    return little


def _safe_component(name: str) -> str:
    """Map an ISO identifier to one conservative host filename component."""
    if any(ord(ch) < 0x20 or ch in "/\\:\0" for ch in name):
        raise ValueError("unsafe path separator/control byte in ISO filename")
    if not name or name in (".", "..") or name.startswith("."):
        raise ValueError("unsafe empty, dot, or hidden ISO filename")
    sanitized = re.sub(r"[^A-Za-z0-9._+-]", "_", name)
    sanitized = sanitized.rstrip(" .")
    if not sanitized or sanitized in (".", ".."):
        raise ValueError("ISO filename sanitizes to an unsafe component")
    device_base = sanitized.split(".", 1)[0].rstrip(" .").upper()
    if (device_base in {"CON", "PRN", "AUX", "NUL"} or
            re.fullmatch(r"(?:COM|LPT)[1-9]", device_base)):
        raise ValueError("ISO filename maps to a reserved Windows device name")
    return sanitized


class SGCD:
    """Validated SGCDIMG1 metadata and bounded raw-sector reader."""

    def __init__(self, path: os.PathLike[str] | str):
        self.path = Path(path).resolve(strict=True)
        if not self.path.is_file():
            raise ValueError("disc image is not a regular file")
        self._file_size = self.path.stat().st_size
        if self._file_size < HEADER.size:
            raise ValueError("truncated SGCD header")
        with self.path.open("rb") as stream:
            raw_header = stream.read(HEADER.size)
        (magic, version, header_payload_offset, sector_bytes, track_count, first_lba,
         leadout, payload_offset, payload_bytes, payload_sha, reserved) = HEADER.unpack(raw_header)
        if magic != MAGIC or version != 1:
            raise ValueError("unsupported SGCD magic or version")
        if sector_bytes != RAW_SECTOR or track_count < 1 or track_count > 99:
            raise ValueError("invalid SGCD sector size or track count")
        expected_table = HEADER.size
        expected_payload = expected_table + track_count * ENTRY.size
        if header_payload_offset != expected_payload or payload_offset != expected_payload:
            raise ValueError("invalid SGCD table or payload offset")
        if reserved != bytes(16):
            raise ValueError("nonzero SGCD reserved header bytes")
        if first_lba != -150 or leadout <= first_lba or leadout > MAX_VOLUME_SECTORS:
            raise ValueError("invalid SGCD LBA bounds")
        expected_payload_bytes = (leadout - first_lba) * RAW_SECTOR
        if payload_bytes != expected_payload_bytes or payload_bytes > MAX_PAYLOAD:
            raise ValueError("invalid SGCD payload geometry or allocation bound")
        if payload_offset + payload_bytes != self._file_size:
            raise ValueError("SGCD file size does not match its payload bounds")
        self.first_lba = first_lba
        self.leadout = leadout
        self.payload_offset = payload_offset
        self.payload_bytes = payload_bytes
        self.payload_sha256 = payload_sha.hex()
        self.tracks = []
        with self.path.open("rb") as stream:
            stream.seek(expected_table)
            entries = [stream.read(ENTRY.size) for _ in range(track_count)]
        self._header_bytes = raw_header
        self._table_bytes = b"".join(entries)
        previous_end = first_lba
        for expected_number, entry in enumerate(entries, 1):
            if len(entry) != ENTRY.size:
                raise ValueError("truncated SGCD track table")
            number, data_flag, index00, start, end, track_reserved = ENTRY.unpack(entry)
            if (number != expected_number or data_flag not in (0, 1) or
                    track_reserved != bytes(12)):
                raise ValueError("invalid SGCD track number, type, or reserved bytes")
            if not (first_lba <= index00 <= start < end <= leadout):
                raise ValueError("SGCD track extent is outside the image bounds")
            if index00 != previous_end:
                raise ValueError("SGCD track extents are not contiguous")
            self.tracks.append({"number": number, "data": bool(data_flag),
                                "index00": index00, "start": start, "end": end})
            previous_end = end
        if previous_end != leadout or self.tracks[0]["index00"] != first_lba:
            raise ValueError("SGCD track table does not cover the declared image")
        self._verify_payload_hash()

    def _verify_payload_hash(self) -> None:
        digest = hashlib.sha256()
        whole_digest = hashlib.sha256()
        whole_digest.update(self._header_bytes)
        whole_digest.update(self._table_bytes)
        remaining = self.payload_bytes
        with self.path.open("rb") as stream:
            stream.seek(self.payload_offset)
            while remaining:
                block = stream.read(min(HASH_CHUNK, remaining))
                if not block:
                    raise ValueError("truncated SGCD payload")
                digest.update(block)
                whole_digest.update(block)
                remaining -= len(block)
        if digest.hexdigest() != self.payload_sha256:
            raise ValueError("SGCD payload SHA-256 mismatch")
        self.image_sha256 = whole_digest.hexdigest()

    def read_raw_sector(self, absolute_lba: int, track: dict | None = None) -> bytes:
        if not isinstance(absolute_lba, int) or not self.first_lba <= absolute_lba < self.leadout:
            raise ValueError("sector LBA outside SGCD payload")
        if track is not None and not track["start"] <= absolute_lba < track["end"]:
            raise ValueError("sector outside selected SGCD track")
        offset = self.payload_offset + (absolute_lba - self.first_lba) * RAW_SECTOR
        with self.path.open("rb") as stream:
            stream.seek(offset)
            sector = stream.read(RAW_SECTOR)
        if len(sector) != RAW_SECTOR:
            raise ValueError("truncated SGCD sector")
        return sector


class ISO9660:
    def __init__(self, image: SGCD):
        self.image = image
        self.track = next((t for t in image.tracks if t["data"]), None)
        if self.track is None:
            raise ValueError("SGCD contains no data track")
        self.track_sectors = self.track["end"] - self.track["start"]
        pvd = None
        for relative_lba in range(16, min(self.track_sectors, 16 + 256)):
            descriptor = self._user_sector(relative_lba)
            if descriptor[1:6] != b"CD001" or descriptor[6] != 1:
                raise ValueError("invalid ISO9660 volume descriptor")
            if descriptor[0] == 1:
                pvd = descriptor
                break
            if descriptor[0] == 255:
                break
        if pvd is None:
            raise ValueError("ISO9660 primary volume descriptor not found")
        if _both(pvd, 128, 2) != USER_SECTOR:
            raise ValueError("unsupported ISO9660 logical block size")
        self.volume_sectors = _both(pvd, 80, 4)
        if not 17 <= self.volume_sectors <= self.track_sectors:
            raise ValueError("ISO9660 volume exceeds the first data track")
        try:
            self.volume_id = pvd[40:72].decode("ascii").rstrip(" \0")
        except UnicodeDecodeError as error:
            raise ValueError("non-ASCII ISO9660 volume identifier") from error
        root_record_length = pvd[156]
        if root_record_length < 34 or 156 + root_record_length > len(pvd):
            raise ValueError("truncated ISO9660 root directory record")
        root_record = pvd[156:156 + root_record_length]
        if root_record[32:34] != b"\x01\x00" or not root_record[25] & 2:
            raise ValueError("invalid ISO9660 root directory record")
        self.files: dict[str, tuple[int, int]] = {}
        self._visited: set[tuple[int, int]] = set()
        self._walk(root_record, "", 0)

    def _user_sector(self, relative_lba: int) -> bytes:
        limit = getattr(self, "volume_sectors", self.track_sectors)
        if not isinstance(relative_lba, int) or not 0 <= relative_lba < limit:
            raise ValueError("ISO sector outside declared data volume")
        sector = self.image.read_raw_sector(self.track["start"] + relative_lba, self.track)
        if sector[:12] != b"\0" + b"\xff" * 10 + b"\0" or sector[15] != 1:
            raise ValueError("invalid MODE1/2352 data sector")
        return sector[16:16 + USER_SECTOR]

    def _extent(self, record: bytes, *, directory: bool = False) -> tuple[int, int]:
        if (len(record) < 34 or record[0] != len(record) or record[1] != 0 or
                record[26] != 0 or record[27] != 0 or record[25] & ~0x02):
            raise ValueError("invalid or unsupported ISO9660 extent record")
        if bool(record[25] & 2) != directory:
            raise ValueError("inconsistent ISO9660 file/directory flags")
        lba, size = _both(record, 2, 4), _both(record, 10, 4)
        if _both(record, 28, 2) != 1:
            raise ValueError("unsupported ISO9660 volume sequence")
        sectors = (size + USER_SECTOR - 1) // USER_SECTOR
        if lba > self.volume_sectors or sectors > self.volume_sectors - lba:
            raise ValueError("ISO9660 extent outside declared volume")
        limit = MAX_DIRECTORY_BYTES if directory else MAX_FILE_BYTES
        if size > limit:
            raise ValueError("ISO9660 extent exceeds allocation limit")
        return lba, size

    def _read_extent(self, lba: int, size: int) -> bytes:
        count = (size + USER_SECTOR - 1) // USER_SECTOR
        return self.read_sectors(lba, count)[:size]

    def read_sectors(self, lba: int, count: int = 1) -> bytes:
        """Read bounded ISO user sectors, batching media that supports ranges."""
        if (not isinstance(lba, int) or not isinstance(count, int) or count < 0
                or lba < 0 or lba > self.volume_sectors
                or count > self.volume_sectors - lba
                or count > MAX_FILE_BYTES // USER_SECTOR):
            raise ValueError('ISO sector range outside volume or allocation limit')
        result = bytearray()
        for first in range(lba, lba + count, 128):
            n = min(128, lba + count - first)
            if hasattr(self.image, 'read_raw_sectors'):
                raw = self.image.read_raw_sectors(self.track['start'] + first, n, self.track)
                for offset in range(0, len(raw), RAW_SECTOR):
                    sector = raw[offset:offset + RAW_SECTOR]
                    if sector[:12] != b'\0' + b'\xff' * 10 + b'\0' or sector[15] != 1:
                        raise ValueError('invalid MODE1/2352 data sector')
                    result.extend(sector[16:16 + USER_SECTOR])
            else:
                for relative in range(first, first + n):
                    result.extend(self._user_sector(relative))
        return bytes(result)

    def _walk(self, record: bytes, prefix: str, depth: int) -> None:
        if depth > MAX_DEPTH:
            raise ValueError("ISO9660 directory nesting exceeds limit")
        lba, size = self._extent(record, directory=True)
        key = (lba, size)
        if key in self._visited or len(self._visited) >= MAX_FILES:
            raise ValueError("cyclic, duplicate, or excessive ISO9660 directories")
        self._visited.add(key)
        blob = self._read_extent(lba, size)
        position = 0
        while position < size:
            length = blob[position]
            if length == 0:
                position = (position // USER_SECTOR + 1) * USER_SECTOR
                continue
            if (length < 34 or length > size - position or
                    length > USER_SECTOR - position % USER_SECTOR):
                raise ValueError("malformed or sector-crossing ISO9660 directory record")
            entry = blob[position:position + length]
            name_length = entry[32]
            if not name_length or 33 + name_length > length:
                raise ValueError("invalid ISO9660 filename length")
            raw_name = entry[33:33 + name_length]
            self._extent(entry, directory=bool(entry[25] & 2))
            if raw_name not in (b"\0", b"\1"):
                try:
                    name = raw_name.decode("ascii")
                except UnicodeDecodeError as error:
                    raise ValueError("non-ASCII ISO9660 filename is unsupported") from error
                name = re.sub(r";[0-9]+$", "", name)
                component = _safe_component(name)
                path = f"{prefix}/{component}" if prefix else component
                if entry[25] & 2:
                    self._walk(entry, path, depth + 1)
                else:
                    if path in self.files or len(self.files) >= MAX_FILES:
                        raise ValueError("duplicate or excessive ISO9660 files")
                    self.files[path] = self._extent(entry, directory=False)
            position += length

    def read_file(self, path: str) -> bytes:
        lba, size = self.files[path]
        return self._read_extent(lba, size)


def extract_programs(disc: os.PathLike[str] | str,
                     output_dir: os.PathLike[str] | str) -> dict:
    image = SGCD(disc)
    iso = ISO9660(image)
    output = Path(output_dir).expanduser().resolve(strict=False)
    if output.exists():
        raise FileExistsError(f"output directory already exists: {output}")
    candidates = []
    skipped = []
    for path, (lba, size) in sorted(iso.files.items()):
        suffix = PurePosixPath(path).suffix.upper()
        if suffix not in (".MMD", ".BIN"):
            continue
        if size > MAX_FILE_BYTES:
            skipped.append({"iso_path": path, "bytes": size,
                            "reason": "candidate exceeds per-file extraction limit"})
        else:
            candidates.append((path, lba, size))
    if len(candidates) > MAX_FILES:
        raise ValueError("program candidate count exceeds limit")
    total = sum(size for _, _, size in candidates)
    if total > MAX_EXTRACTED_BYTES:
        raise ValueError("total extracted program bytes exceed limit")

    # Preflight all names and collisions before creating any output.
    destinations: dict[str, str] = {}
    directory_keys: set[str] = set()
    for path, _, _ in candidates:
        components = [_safe_component(part) for part in path.split("/")]
        relative = Path(*components)
        key = str(relative).casefold()
        if key in destinations:
            raise ValueError(f"output filename collision: {path} and {destinations[key]}")
        destinations[key] = path
        for index in range(1, len(components)):
            directory_keys.add(str(Path(*components[:index])).casefold())
    file_directory_collisions = set(destinations) & directory_keys
    if file_directory_collisions:
        collision = sorted(file_directory_collisions)[0]
        raise ValueError(f"output file collides with a directory ancestor: {collision}")

    output.mkdir(parents=True, exist_ok=False)
    records = []
    for source_path, relative_lba, size in candidates:
        data = iso.read_file(source_path)
        if len(data) != size:
            raise ValueError(f"truncated ISO9660 program file: {source_path}")
        relative = Path(*(_safe_component(part) for part in source_path.split("/")))
        destination = output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        # Exclusive creation prevents replacing a file even if a name races
        # with another writer after the output directory was created.
        with destination.open("xb") as stream:
            stream.write(data)
        records.append({"iso_path": source_path, "output_path": relative.as_posix(),
                        "extension": relative.suffix.upper(), "bytes": size,
                        "iso_relative_lba": relative_lba,
                        "disc_lba": iso.track["start"] + relative_lba,
                        "sha256": hashlib.sha256(data).hexdigest()})
    manifest = {
        "format": "SegaCDOwnedProgramInventory1",
        "source_disc": str(image.path),
        "source_sgcd_sha256": image.image_sha256,
        "source_sgcd_payload_sha256": image.payload_sha256,
        "source_sgcd_payload_bytes": image.payload_bytes,
        "first_data_track": {"number": iso.track["number"], "start_lba": iso.track["start"],
                             "end_lba": iso.track["end"], "iso_volume_sectors": iso.volume_sectors},
        "iso_volume_id": iso.volume_id,
        "program_extensions": [".BIN", ".MMD"],
        "files": records,
        "skipped_candidates": skipped,
        "boot_slices": [],
        "boot_slice_note": "No load-address provenance is encoded by ISO9660 extents; no boot slices inferred.",
    }
    manifest_path = output / "owned-program-manifest.json"
    with manifest_path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(manifest, stream, indent=2)
        stream.write("\n")
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", required=True, type=Path, help="verified SGCDIMG1 image")
    parser.add_argument("--output-dir", required=True, type=Path,
                        help="new, explicit extraction directory (must not already exist)")
    args = parser.parse_args(argv)
    try:
        manifest = extract_programs(args.disc, args.output_dir)
    except (OSError, ValueError) as error:
        parser.exit(1, f"SGCD program extraction: {error}\n")
    print(json.dumps({"output_dir": str(args.output_dir.absolute()),
                      "files": len(manifest["files"]),
                      "bytes": sum(row["bytes"] for row in manifest["files"]),
                      "manifest": str((args.output_dir / "owned-program-manifest.json").absolute())},
                     indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
