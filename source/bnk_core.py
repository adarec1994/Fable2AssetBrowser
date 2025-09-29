# bnk_core.py
"""
Pure-Python BNK reader/extractor with decompression support.
Keeps the same API: list_bnk, extract_one, extract_all, find_bnks, BNKItem.
No longer needs external CLI for extraction.
"""
from __future__ import annotations

import os
import zlib
import struct
from dataclasses import dataclass
from typing import List, Iterable, Tuple, Optional


@dataclass
class BNKItem:
    index: int
    size: int  # uncompressed size
    name: str


# ---------- low-level helpers ----------

def _u32be(b: bytes, off: int) -> int:
    return struct.unpack('>I', b[off:off + 4])[0]


def _read_cstr(b: bytes, off: int, n: int) -> str:
    s = b[off:off + n]
    # Remove null terminator and convert backslashes
    s = s.rstrip(b'\x00')
    return s.decode('utf-8', errors='ignore').replace('\\', '/')


class _BNK:
    """Enhanced BNK loader with decompression support."""

    CHUNK_SIZE = 32768  # 32KB chunks for compressed files

    def __init__(self, data: bytes):
        self.data = data
        self.version = _u32be(data, 4)
        self.data_offset = _u32be(data, 0)  # start of data region
        self.files_are_compressed = 0
        self.file_table: bytes = b''
        # (name, rel_data_off, uncomp_size, comp_size, chunk_count, uncmp_chunksizes[])
        self.entries: List[Tuple[str, int, int, int, int, List[int]]] = []

        if self.version == 2:
            self.files_are_compressed = self.data[8]
            # For V2, file table might be at offset 16 or at data_offset
            if self.data_offset > 16:
                # Try to decompress file table at offset 16
                self._load_file_table_v2()
            else:
                # File table is uncompressed
                table_off = 16
                self._load_file_table_uncompressed(table_off)
        elif self.version == 3:
            self.files_are_compressed = self.data[8]
            comp_hdr_sz = _u32be(self.data, 9)
            uncomp_hdr_sz = _u32be(self.data, 13)

            if comp_hdr_sz == 0:
                self.file_table = b''
            else:
                # Read and decompress file table
                comp_data = self.data[17:17 + comp_hdr_sz]
                self.file_table = self._decompress_zlib(comp_data)

                # Check for continuation chunks
                pos = 17 + comp_hdr_sz
                while pos < len(self.data):
                    if pos + 8 > len(self.data):
                        break
                    next_comp_sz = _u32be(self.data, pos)
                    if next_comp_sz == 0:
                        break
                    next_uncomp_sz = _u32be(self.data, pos + 4)
                    pos += 8
                    if pos + next_comp_sz > len(self.data):
                        break
                    chunk_data = self.data[pos:pos + next_comp_sz]
                    self.file_table += self._decompress_zlib(chunk_data)
                    pos += next_comp_sz

            self._parse_file_table(self.file_table)
        else:
            raise ValueError(f"Unsupported BNK version {self.version}")

    def _decompress_zlib(self, data: bytes) -> bytes:
        """Decompress a zlib chunk."""
        if len(data) < 2:
            return b''

        # Check for valid zlib header
        if data[0] != 0x78:
            return b''

        try:
            return zlib.decompress(data)
        except zlib.error:
            # Try raw deflate
            try:
                return zlib.decompress(data, -15)
            except:
                return b''

    def _load_file_table_v2(self):
        """Load file table for version 2 BNK."""
        # Try to read compressed file table at offset 16
        pos = 16
        if pos + 8 <= len(self.data):
            comp_sz = _u32be(self.data, pos)
            uncomp_sz = _u32be(self.data, pos + 4)
            pos += 8

            if comp_sz > 0 and pos + comp_sz <= len(self.data):
                comp_data = self.data[pos:pos + comp_sz]
                self.file_table = self._decompress_zlib(comp_data)

                if self.file_table:
                    self._parse_file_table(self.file_table)
                    return

        # Fall back to uncompressed
        self._load_file_table_uncompressed(16)

    def _load_file_table_uncompressed(self, table_off: int):
        # V2: assume file table uncompressed; extends up to data_offset or EOF
        hi = self.data_offset if self.data_offset > table_off else len(self.data)
        self.file_table = self.data[table_off:hi]
        self._parse_file_table(self.file_table)

    def _parse_file_table(self, ft: bytes):
        """Parse the uncompressed file table data."""
        if len(ft) < 4:
            return

        off = 0
        file_count = _u32be(ft, off)
        off += 4

        self.entries.clear()

        for _ in range(min(file_count, 100000)):  # Sanity limit
            if off + 4 > len(ft):
                break

            name_len = _u32be(ft, off)
            off += 4

            if off + name_len > len(ft):
                break

            name = _read_cstr(ft, off, name_len)
            off += name_len

            if off + 8 > len(ft):
                break

            data_off = _u32be(ft, off)
            off += 4
            uncomp_size = _u32be(ft, off)
            off += 4

            comp_size = 0
            chunk_count = 0
            chunks = []

            if self.files_are_compressed:
                if off + 8 > len(ft):
                    break
                comp_size = _u32be(ft, off)
                off += 4
                chunk_count = _u32be(ft, off)
                off += 4

                for _ in range(min(chunk_count, 10000)):  # Sanity limit
                    if off + 4 > len(ft):
                        break
                    chunks.append(_u32be(ft, off))
                    off += 4
            else:
                comp_size = uncomp_size

            self.entries.append((name, data_off, uncomp_size, comp_size, chunk_count, chunks))

    def list(self) -> List[BNKItem]:
        return [BNKItem(i, usize, nm) for i, (nm, _o, usize, _c, _cc, _chs) in enumerate(self.entries)]

    def get_file_data(self, index: int) -> bytes:
        """Extract and decompress file data."""
        if index >= len(self.entries):
            return b''

        nm, rel_off, usize, csize, cc, chs = self.entries[index]

        # Calculate actual offset
        if self.version == 2:
            abs_off = rel_off  # V2 uses absolute offsets
        else:
            abs_off = self.data_offset + rel_off

        if abs_off >= len(self.data):
            return b''

        if not self.files_are_compressed or cc == 0:
            # Uncompressed file
            end_off = min(abs_off + usize, len(self.data))
            return self.data[abs_off:end_off]
        else:
            # Compressed file - decompress chunks
            result = bytearray()
            pos = abs_off

            for chunk_idx in range(cc):
                if pos >= len(self.data):
                    break

                # Calculate chunk size
                chunk_size = min(self.CHUNK_SIZE, csize - (pos - abs_off))
                if chunk_size <= 0:
                    break

                # Read chunk
                end_pos = min(pos + chunk_size, len(self.data))
                chunk_data = self.data[pos:end_pos]
                pos = end_pos

                # Decompress chunk
                try:
                    decompressed = self._decompress_zlib(chunk_data)
                    result.extend(decompressed)
                except:
                    # If decompression fails, return what we have
                    break

            # Trim to expected size
            return bytes(result[:usize])

    def get_raw_bytes(self, index: int) -> bytes:
        """Get raw bytes for compatibility - now returns decompressed data."""
        return self.get_file_data(index)


def _open_bnk(path: str) -> _BNK:
    with open(path, 'rb') as f:
        return _BNK(f.read())


# ---------- public API used by GUI ----------

def list_bnk(bnk_path: str) -> List[BNKItem]:
    return _open_bnk(bnk_path).list()


def extract_one(bnk_path: str, index: int, out_path: str) -> str:
    """Extract a single file by index."""
    bnk = _open_bnk(bnk_path)
    data = bnk.get_file_data(index)

    out_path = os.path.abspath(out_path)
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)

    with open(out_path, 'wb') as f:
        f.write(data)

    return out_path


# alias for clarity
extract_one_raw = extract_one


def extract_all(bnk_path: str, out_dir: str) -> None:
    """Extract all files from a BNK."""
    b = _open_bnk(bnk_path)
    out_dir = os.path.abspath(out_dir)
    os.makedirs(out_dir, exist_ok=True)

    for item in b.list():
        stem = item.name or f"entry_{item.index:04d}"
        if os.path.splitext(stem)[1] == "":
            stem += ".bin"

        out_file = os.path.join(out_dir, stem)
        os.makedirs(os.path.dirname(out_file) or ".", exist_ok=True)

        data = b.get_file_data(item.index)
        with open(out_file, 'wb') as f:
            f.write(data)


def find_bnks(root: str, exts: Iterable[str] = (".bnk",)) -> List[str]:
    """Find all BNK files in a directory tree."""
    root = os.path.abspath(root)
    hits: List[str] = []
    exts_lc = tuple(e.lower() for e in exts)

    for dirpath, _dirs, files in os.walk(root):
        for fn in files:
            if os.path.splitext(fn)[1].lower() in exts_lc:
                hits.append(os.path.join(dirpath, fn))

    return hits