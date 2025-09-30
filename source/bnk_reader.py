from __future__ import annotations
import io, os, struct, zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import BinaryIO, List, Optional, Tuple

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
        self.base_offset: int = 0              # v3: file-data base; v2: unused (0)
        self.compress_file_data: int = 0       # header flag (v2/v3)
        self._file_table_blob: Optional[bytes] = None
        self.file_entries: List[FileEntry] = []
        self._is_v2: bool = False
        self._open_and_load()

    def _open_and_load(self):
        self._fh = open(self.path, "rb")
        self._fh.seek(0, os.SEEK_END)
        self._size = self._fh.tell()
        self._fh.seek(0)

        # Peek base + version (BE)
        head = self._read(8)
        self._fh.seek(0)
        header_offset = struct.unpack(">I", head[:4])[0]   # meaning differs by version
        ver           = struct.unpack(">I", head[4:8])[0]

        if ver == 2:
            self._is_v2 = True
            self._read_header_v2(header_offset)
        else:
            self._is_v2 = False
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
        self.base_offset = self._read_u32_be()  # v3: file-data base
        _ = self._read(4)                       # v3 "version-ish/unknown" field
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
        for (_csz, dsz, comp) in chunks:
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

    def _read_header_v2(self, file_table_offset: int):
 
        if not (0 < file_table_offset <= self._size - 8):
            raise RuntimeError(f"V2: invalid file-table start pointer 0x{file_table_offset:08X}")
            
        self._fh.seek(8, os.SEEK_SET)
        self.compress_file_data = 1 if self._read_u8() != 0 else 0
        _ = self._read(7)  # padding

        keep base_offset = 0
        self.base_offset = 0

        chunks_meta: List[Tuple[int, int, int]] = []  # (offset_of_data, comp_size, uncomp_size)
        cur = file_table_offset
        while cur + 8 <= self._size:
            self._fh.seek(cur, os.SEEK_SET)
            comp_size = self._read_u32_be()
            uncomp_size = self._read_u32_be()
            if comp_size == 0: 
                break

            data_off = cur + 8
            if data_off + comp_size > self._size:
                break

            chunks_meta.append((data_off, comp_size, uncomp_size))
            cur = data_off + comp_size

        if not chunks_meta:
            self._file_table_blob = struct.pack(">I", 0)
            return

        last_err = None
        for wbits in (15, -15, 31):
            try:
                d = zlib.decompressobj(wbits=wbits)
                out = io.BytesIO()
                for data_off, comp_size, _uncomp in chunks_meta:
                    self._fh.seek(data_off, os.SEEK_SET)
                    comp = self._read(comp_size)
                    out.write(d.decompress(comp))
                tail = d.flush()
                if tail:
                    out.write(tail)
                self._file_table_blob = out.getvalue()
                return
            except zlib.error as e:
                last_err = e
                continue

        raise RuntimeError(f"V2: file-table decompression failed: {last_err}")

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
            # v2 names may include a trailing '\0'; harmless to keep or strip
            if n and s[-1] == 0:
                s = s[:-1]
            return s.decode("utf-8", errors="replace")

        def make_offset(rel_off: int) -> int:
            return rel_off if self._is_v2 else (self.base_offset + rel_off)

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
                    offset=make_offset(rel_off),
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
                    offset=make_offset(rel_off),
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

        chunk_size = 0x8000
        written = 0
        pos = 0
        for idx, out_len in enumerate(e.decompressed_chunk_sizes):
            # Slice the next 32KB (or remaining) of compressed blob
            comp_slice = comp_blob[pos:pos+chunk_size]
            pos += len(comp_slice)

            chunk = None
            for wbits in (15, -15, 31):
                try:
                    # In BNK v2 files, each chunk typically includes a 2-byte zlib header.
                    # zlib will handle that when wbits=15. Fallbacks keep robustness.
                    chunk = zlib.decompress(comp_slice, wbits=wbits)
                    break
                except zlib.error:
                    continue
            if chunk is None:
                raise RuntimeError(f"Failed to inflate payload slice #{idx}")

            # Tighten/loosen to the expected length
            if len(chunk) != out_len:
                if len(chunk) > out_len:
                    chunk = chunk[:out_len]
                else:
                    chunk = chunk + (b"\x00" * (out_len - len(chunk)))

            out_fh.write(chunk)
            written += len(chunk)

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
