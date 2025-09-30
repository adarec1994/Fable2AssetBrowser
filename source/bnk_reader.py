from __future__ import annotations
import io, os, struct, zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import BinaryIO, List, Optional

@dataclass
class FileEntry:
    name: str
    offset: int
    uncompressed_size: int
    compressed_size: int
    is_compressed: bool
    decompressed_chunk_sizes: List[int] = field(default_factory=list)
    @property
    def size(self) -> int:
        return self.uncompressed_size

class BNKReader:
    def __init__(self, path: str):
        self.path = path
        self._fh: Optional[BinaryIO] = None
        self._size: int = 0
        self.base_offset: int = 0
        self.compress_file_data: int = 0
        self._file_table_blob: Optional[bytes] = None
        self.file_entries: List[FileEntry] = []
        self._open_and_load()

    def _open_and_load(self):
        self._fh = open(self.path, "rb")
        self._fh.seek(0, os.SEEK_END)
        self._size = self._fh.tell()
        self._fh.seek(0)
        self._read_header_continuous_stream()
        if not self._file_table_blob:
            raise RuntimeError("Failed to read BNK header (decompressed file table is empty).")
        self._parse_tables()

    def _read(self, n: int) -> bytes:
        b = self._fh.read(n)
        if len(b) != n:
            raise EOFError("Premature EOF")
        return b

    def _read_u32_be(self) -> int:
        return struct.unpack(">I", self._read(4))[0]

    def _read_u8(self) -> int:
        return self._read(1)[0]

    def _read_header_continuous_stream(self):
        self.base_offset = self._read_u32_be()
        _ = self._read(4)
        self.compress_file_data = self._read_u8()
        chunks: List[tuple[int, int, bytes]] = []
        while True:
            comp_size = self._read_u32_be()
            decomp_size = self._read_u32_be()
            if comp_size == 0:
                break
            comp = self._read(comp_size)
            chunks.append((comp_size, decomp_size, comp))
        for wbits in (15, -15, 31):
            try:
                self._file_table_blob = self._inflate_header_chunks_as_one_stream(chunks, wbits)
                return
            except zlib.error:
                pass
        if chunks and all(csz == dsz for csz, dsz, _ in chunks if dsz != 0):
            self._file_table_blob = b"".join(comp for _, _, comp in chunks)
            return
        raise RuntimeError("Header chunk decompress failed")

    def _inflate_header_chunks_as_one_stream(self, chunks: List[tuple[int, int, bytes]], wbits: int) -> bytes:
        d = zlib.decompressobj(wbits=wbits)
        out = io.BytesIO()
        for idx, (csz, dsz, comp) in enumerate(chunks):
            if dsz > 0:
                piece = d.decompress(comp, dsz)
                rem = dsz - len(piece)
                if rem > 0:
                    piece += d.decompress(b"", rem)
                if len(piece) != dsz:
                    raise zlib.error("chunk size mismatch")
            else:
                piece = d.decompress(comp)
            out.write(piece)
        tail = d.flush()
        if tail:
            out.write(tail)
        return out.getvalue()

    def _parse_tables(self):
        bio = io.BytesIO(self._file_table_blob)
        def r_u32_be() -> int:
            b = bio.read(4)
            if len(b) != 4:
                raise EOFError
            return struct.unpack(">I", b)[0]
        def r_name_be() -> str:
            n = r_u32_be()
            if n > 1_000_000:
                raise ValueError("Unreasonable name length")
            s = bio.read(n)
            if len(s) != n:
                raise EOFError
            return s.decode("utf-8", errors="replace")
        entries: List[FileEntry] = []
        file_count = r_u32_be()
        if self.compress_file_data == 1:
            for _ in range(file_count):
                name = r_name_be()
                rel_off = r_u32_be()
                decomp_size = r_u32_be()
                comp_size = r_u32_be()
                chunk_count = r_u32_be()
                chunks = [r_u32_be() for __ in range(chunk_count)]
                entries.append(FileEntry(
                    name=name,
                    offset=self.base_offset + rel_off,
                    uncompressed_size=decomp_size,
                    compressed_size=comp_size,
                    is_compressed=True,
                    decompressed_chunk_sizes=chunks
                ))
        else:
            for _ in range(file_count):
                name = r_name_be()
                rel_off = r_u32_be()
                size = r_u32_be()
                entries.append(FileEntry(
                    name=name,
                    offset=self.base_offset + rel_off,
                    uncompressed_size=size,
                    compressed_size=0,
                    is_compressed=False,
                    decompressed_chunk_sizes=[]
                ))
        self.file_entries = entries

    def list_files(self) -> List[dict]:
        return [
            {
                "name": e.name,
                "offset": e.offset,
                "size": e.uncompressed_size,
                "compressed_size": e.compressed_size,
                "is_compressed": e.is_compressed,
                "decompressed_chunk_sizes": list(e.decompressed_chunk_sizes),
            }
            for e in self.file_entries
        ]

    def extract_file(self, name: str, out_path: str) -> None:
        entry = next((e for e in self.file_entries if e.name == name), None)
        if entry is None:
            raise FileNotFoundError(name)
        Path(out_path).parent.mkdir(parents=True, exist_ok=True)
        with open(out_path, "wb") as f:
            self._extract_entry_to(entry, f)

    def extract_all(self, out_dir: str | os.PathLike) -> None:
        out = Path(out_dir)
        out.mkdir(parents=True, exist_ok=True)
        for e in self.file_entries:
            target = out / (e.name if e.name else f"file_{e.offset:08X}.bin")
            target.parent.mkdir(parents=True, exist_ok=True)
            with open(target, "wb") as f:
                self._extract_entry_to(e, f)

    def _extract_entry_to(self, e: FileEntry, out_fh):
        self._fh.seek(e.offset, os.SEEK_SET)
        if not e.is_compressed:
            out_fh.write(self._read(e.uncompressed_size))
            return
        comp_blob = self._read(e.compressed_size)
        for i, out_len in enumerate(e.decompressed_chunk_sizes):
            start = i * 0x8000
            end = min(start + 0x8000, len(comp_blob))
            if start >= end:
                raise RuntimeError("Compressed data slice out of range")
            comp_slice = comp_blob[start:end]
            chunk = None
            for wbits in (15, -15, 31):
                try:
                    chunk = zlib.decompress(comp_slice, wbits=wbits)
                    break
                except zlib.error:
                    continue
            if chunk is None:
                raise RuntimeError("Failed to inflate payload slice")
            if len(chunk) != out_len:
                if len(chunk) > out_len:
                    chunk = chunk[:out_len]
                else:
                    chunk = chunk + (b"\x00" * (out_len - len(chunk)))
            out_fh.write(chunk)

    def close(self):
        if self._fh:
            try:
                self._fh.close()
            finally:
                self._fh = None

    def __enter__(self):
        return self

    def __exit__(self, et, ev, tb):
        self.close()
