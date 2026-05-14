"""Extract the raw inflated bytes of every .ehf's PF=99 blob so
ImageHeat (or similar) can be used to figure out the right
tile / pixel-layout interpretation experimentally.

For each .ehf in cmake-build-debug/extracted/:
  - find the PF=99 .tex blob inside the body (it sits right after
    the palette, prefixed with a `00 00 00 01` sentinel)
  - parse its .tex sub-header to get logical W, H, raw_size
  - inflate the zlib stream
  - write the inflated bytes to a sibling .bin file, naming it
    after the level + logical dims:
        <stem>_pf99_<W>x<H>_<padded_W>x<padded_H>_4bpp.bin

  Also emit a small `pf99_dump_info.txt` summarising:
    - logical W x H
    - inferred padded W x H (where padded_W * padded_H / 2 == raw_size)
    - file path of the .bin

  In ImageHeat you'd typically open the .bin and try:
    - "Indexed 4 bpp" with the padded width
    - tile / swizzle modes: Xbox 360, PC, raw linear, etc.

Run:  python tools/ehf_pf99_extract.py
"""

from __future__ import annotations
import struct
import sys
import zlib
from pathlib import Path

MAGIC = b"HeightFieldGraphicsFile"
ART = b"art\\"


def be_u32(b, off): return struct.unpack_from(">I", b, off)[0]


def find_palette_start(blob, body_start, body_end):
    for i in range(body_start, body_end - 4):
        if blob[i:i+4] == ART:
            count = be_u32(blob, i - 4)
            if 1 < count < 200:
                return i
    return None


def walk_palette_end(blob, pal_start, body_end):
    i = pal_start
    while i + 4 < body_end:
        if blob[i:i+4] != ART: break
        end_d = blob.index(b"\x00", i)
        if blob[end_d+1:end_d+5] != ART: break
        end_n = blob.index(b"\x00", end_d + 1)
        i = end_n + 1 + 13
    return i


def find_pf99(blob, scan_from, body_end):
    for i in range(scan_from, body_end - 16):
        if (blob[i]   == 0xFF and blob[i+1] == 0xFF and
            blob[i+2] == 0xFF and blob[i+3] == 0xFE):
            pf = be_u32(blob, i + 0x18)
            if pf == 99:
                return i
    return None


def best_padding(raw_bytes, w, h):
    """Find (padW, padH) such that padW*padH/2 == raw_bytes, with padW
    multiple of {32, 64, 128, ...} and padW>=w, padH>=h."""
    target = raw_bytes * 2
    for sw in (8, 16, 32, 64, 128, 256):
        pw = ((w + sw - 1) // sw) * sw
        if pw < w: continue
        if target % pw == 0:
            ph = target // pw
            if ph >= h:
                return pw, ph
    return w, h


def process(ehf_path: Path, info_lines: list[str]):
    blob = ehf_path.read_bytes()
    if blob[:23] != MAGIC:
        return

    body_offset = be_u32(blob, 55)
    body_size   = be_u32(blob, 59)
    body_end    = body_offset + body_size
    if body_end > len(blob):
        info_lines.append(f"{ehf_path.name}: bad body extent")
        return

    # Find palette.
    pal_start = find_palette_start(blob, body_offset, body_end)
    if pal_start is None:
        info_lines.append(f"{ehf_path.name}: no palette found")
        return
    pal_end = walk_palette_end(blob, pal_start, body_end)

    # PF=99 sits right after the palette.
    pf99_off = find_pf99(blob, pal_end, body_end)
    if pf99_off is None:
        info_lines.append(f"{ehf_path.name}: no PF=99 blob")
        return

    W  = be_u32(blob, pf99_off + 0x10)
    H  = be_u32(blob, pf99_off + 0x14)
    mt = be_u32(blob, pf99_off + 0x20)
    raw_size  = be_u32(blob, pf99_off + mt)
    comp_size = be_u32(blob, pf99_off + mt + 4)
    zlib_off  = pf99_off + mt + 8

    if comp_size > body_end - zlib_off or raw_size > 32_000_000:
        info_lines.append(f"{ehf_path.name}: implausible sizes "
                          f"(raw={raw_size}, comp={comp_size})")
        return

    d = zlib.decompressobj(15)
    inflated = d.decompress(blob[zlib_off:zlib_off + comp_size],
                            max_length=raw_size)
    if len(inflated) != raw_size:
        info_lines.append(f"{ehf_path.name}: inflate short "
                          f"({len(inflated)} of {raw_size})")
        return

    padW, padH = best_padding(raw_size, W, H)

    out_name = f"{ehf_path.stem}_pf99_{W}x{H}_pad{padW}x{padH}_4bpp.bin"
    out_path = ehf_path.parent / out_name
    out_path.write_bytes(inflated)

    info_lines.append(
        f"{ehf_path.name}\n"
        f"  logical:    {W} x {H}\n"
        f"  padded:     {padW} x {padH}  (raw_size = padW*padH/2 = {padW*padH//2})\n"
        f"  raw_size:   {raw_size:,} bytes (= {raw_size//1024} KB)\n"
        f"  comp_size:  {comp_size:,} bytes\n"
        f"  out:        {out_name}\n"
    )


def main():
    extracted = Path("cmake-build-debug/extracted")
    if not extracted.exists():
        print(f"!! {extracted} not found", file=sys.stderr)
        return 1

    ehfs = sorted(extracted.glob("*.ehf"))
    if not ehfs:
        print(f"!! no .ehf files in {extracted}", file=sys.stderr)
        return 1

    info_lines = []
    info_lines.append(f"# PF=99 raw dump for {len(ehfs)} .ehf files")
    info_lines.append(f"# Try these in ImageHeat:")
    info_lines.append(f"#   Format: 'Indexed 4 bpp' (or '4-bit palette')")
    info_lines.append(f"#   Width:  use the PADDED width (in the filename)")
    info_lines.append(f"#   Swizzle: try Xbox 360 modes first")
    info_lines.append(f"")

    for ehf in ehfs:
        try:
            process(ehf, info_lines)
        except Exception as e:
            info_lines.append(f"{ehf.name}: ERROR {e}")

    info_path = extracted / "pf99_dump_info.txt"
    info_path.write_text("\n".join(info_lines), encoding="utf-8")
    print(f"wrote {info_path}")
    print()
    print("\n".join(info_lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
