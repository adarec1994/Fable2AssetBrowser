"""Map the structural regions of a .ehf file: walk all the .tex
blobs, zlib streams, and palette area to identify where the
per-cell material index data lives.

Output for chapter3 should be roughly:
  +0x00000000  HFG header (63B)
  +0x0000003F  .tex PF=24  lightmap (zlib, body_offset=0x3F)
  +0x000eab81  .tex PF=40  BC5 normal map
  +0x... ?     UNKNOWN BLOCK (likely the per-cell material data)
  +0x001ee4d7  palette (art\\ strings)
  +0x... ?     possibly more UNKNOWN
  +0x00235f1b  body_end — outside body now
  +0x00235f1b  .tex PF=35 (BC1 diffuse mip 0)
  ...

Usage:  python tools/ehf_region_map.py <path/to/file.ehf>
"""

from __future__ import annotations
import struct
import sys
import zlib
from pathlib import Path

MAGIC = b"HeightFieldGraphicsFile"


def be_u32(b: bytes, off: int) -> int:
    return struct.unpack_from(">I", b, off)[0]


def scan_zlib_streams(blob: bytes, search_start: int, search_end: int):
    """Return list of (offset, decompressed_size) for every byte
    that starts a valid zlib stream (78 DA / 78 9C / 78 5E / 78 01)
    in [search_start, search_end)."""
    streams = []
    i = search_start
    while i < search_end - 2:
        if blob[i] == 0x78 and blob[i+1] in (0xDA, 0x9C, 0x5E, 0x01):
            d = zlib.decompressobj(15)
            try:
                # Bound the feed so we don't run forever on falsies.
                out = d.decompress(blob[i:i + min(2_000_000, search_end - i)],
                                   max_length=8_000_000)
                if len(out) >= 64:  # ignore tiny streams (false positives)
                    streams.append((i, len(out)))
                    # Try to advance past the stream end.  We can't
                    # know the consumed length exactly without
                    # extra work; just step by 16 to skip ahead a bit.
                    i += 16
                    continue
            except zlib.error:
                pass
        i += 1
    return streams


def find_tex_blobs(blob: bytes):
    """Find every 0xFFFFFFFE in the file and parse the (W,H,PF,mt)
    header.  Returns list of dicts with offset + parsed fields,
    filtering wildly implausible ones (raw_size > 16 MB etc.)."""
    out = []
    i = 0
    n = len(blob)
    while i + 0x60 < n:
        if (blob[i]   == 0xFF and blob[i+1] == 0xFF and
            blob[i+2] == 0xFF and blob[i+3] == 0xFE):
            try:
                W = be_u32(blob, i + 0x10)
                H = be_u32(blob, i + 0x14)
                PF = be_u32(blob, i + 0x18)
                mt = be_u32(blob, i + 0x20)
                if W < 16384 and H < 16384 and mt < 0x100 and (PF in (24, 35, 39, 40)):
                    raw  = be_u32(blob, i + mt)
                    comp = be_u32(blob, i + mt + 4)
                    if raw < 8_000_000 and comp < 8_000_000:
                        out.append(dict(
                            off=i, W=W, H=H, PF=PF, mt=mt,
                            raw=raw, comp=comp,
                            blob_end=i + mt + 8 + comp,
                        ))
            except Exception:
                pass
        i += 1
    return out


def find_palette_start(blob: bytes, search_start: int = 4):
    """Same heuristic as EhfPalette::Parse — first 'art\\' preceded
    by a u32 BE count in (1, 200)."""
    for i in range(search_start, len(blob) - 4):
        if (blob[i]   == ord('a') and blob[i+1] == ord('r') and
            blob[i+2] == ord('t') and blob[i+3] == ord('\\')):
            count = be_u32(blob, i - 4)
            if 1 < count < 200:
                return i, count
    return None, 0


def fmt(off):
    return f"0x{off:08x} ({off:,})"


def main(ehf_path: Path) -> int:
    blob = ehf_path.read_bytes()
    if blob[:23] != MAGIC:
        print(f"!! not an .ehf"); return 1

    body_off = be_u32(blob, 55)
    body_size = be_u32(blob, 59)
    body_end = body_off + body_size
    n = len(blob)

    print(f"# {ehf_path.name}   file_size={n:,}")
    print(f"# header_end=0x3f  body=[0x{body_off:x}..0x{body_end:x})  "
          f"body_size={body_size:,}  leftover_after_body={n - body_end:,}")
    print()

    blobs = find_tex_blobs(blob)
    pal_off, pal_count = find_palette_start(blob)
    print(f"# palette: offset={fmt(pal_off) if pal_off else '?'}  "
          f"declared_count={pal_count}")
    print()

    # Build a sorted timeline of "events" inside the file.
    events = []
    events.append((0, f"HFG header (63B), body_offset=0x3F"))
    for b in blobs:
        end = b['blob_end']
        events.append((b['off'],
                       f".tex blob  PF={b['PF']:>3}  {b['W']:>4}x{b['H']:<4}"
                       f"  raw={b['raw']:>10,}  comp={b['comp']:>10,}"
                       f"  end={fmt(end)}"))
        events.append((end, f"  -- end of PF={b['PF']} blob --"))
    if pal_off:
        events.append((pal_off, f"PALETTE START  (declared_count={pal_count})"))
    events.append((body_end, f"BODY_END  (header-stated body_size={body_size:,})"))
    events.append((n, f"FILE_END  ({n:,})"))

    events.sort(key=lambda x: x[0])

    last = -1
    for off, label in events:
        if off > last:
            gap = off - last
            if last >= 0 and gap > 8:
                print(f"   [gap of {gap:,}B at +0x{last:08x}]")
        marker = "INSIDE-BODY " if body_off <= off < body_end else "after-body  "
        print(f" {fmt(off):<28} {marker} {label}")
        last = off
    return 0


if __name__ == "__main__":
    ehf = Path(sys.argv[1]) if len(sys.argv) > 1 else \
          Path("cmake-build-debug/extracted/bl_chapter3_heightfield_id_9501a1af.ehf")
    sys.exit(main(ehf))
