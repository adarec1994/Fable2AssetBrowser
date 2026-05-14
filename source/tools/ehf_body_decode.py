"""Decode the body of a .ehf (PixelFormat = 24, 16-bpp) as a control
map and dump each byte channel as a separate PGM image.

Hypothesis: the body is a 1-texel-per-heightfield-cell control map
where the two bytes per pixel are (material_index, blend_amount).
If that's right, channel 0 should show smooth, spatially-clustered
regions of integer indices (a few distinct values per area), and
channel 1 should be a smoother gradient of blend amounts.

Xbox 360 16-bpp tiled storage: 32x32-texel tiles, texel_byte_pitch=2.
Re-uses the ImageHeat xg_address_2d_tiled_* formulas (we already
have a working C++ port — this is a 1:1 Python rewrite to verify).

Usage:
  python tools/ehf_body_decode.py <path/to/file.ehf>
"""

from __future__ import annotations
import struct
import sys
import zlib
from pathlib import Path

MAGIC = b"HeightFieldGraphicsFile"


def be_u32(b: bytes, off: int) -> int:
    return struct.unpack_from(">I", b, off)[0]


def xg_address_2d_tiled_x(block_offset: int,
                          width_in_blocks: int,
                          texel_byte_pitch: int) -> int:
    aligned_w = (width_in_blocks + 31) & ~31
    log_bpp = (texel_byte_pitch >> 2) + ((texel_byte_pitch >> 1) >> (texel_byte_pitch >> 2))
    off_byte = block_offset << log_bpp
    off_tile = ((off_byte & ~0xFFF) >> 3) + ((off_byte & 0x700) >> 2) + (off_byte & 0x3F)
    off_macro = off_tile >> (7 + log_bpp)
    macro_x = (off_macro % (aligned_w >> 5)) << 2
    tile = (((off_tile >> (5 + log_bpp)) & 2) + (off_byte >> 6)) & 3
    macro = (macro_x + tile) << 3
    micro = ((((off_tile >> 1) & ~0xF) + (off_tile & 0xF))
             & ((texel_byte_pitch << 3) - 1)) >> log_bpp
    return macro + micro


def xg_address_2d_tiled_y(block_offset: int,
                          width_in_blocks: int,
                          texel_byte_pitch: int) -> int:
    aligned_w = (width_in_blocks + 31) & ~31
    log_bpp = (texel_byte_pitch >> 2) + ((texel_byte_pitch >> 1) >> (texel_byte_pitch >> 2))
    off_byte = block_offset << log_bpp
    off_tile = ((off_byte & ~0xFFF) >> 3) + ((off_byte & 0x700) >> 2) + (off_byte & 0x3F)
    off_macro = off_tile >> (7 + log_bpp)
    macro_y = (off_macro // (aligned_w >> 5)) << 2
    tile = ((off_tile >> (6 + log_bpp)) & 1) + ((off_byte & 0x800) >> 10)
    macro = (macro_y + tile) << 3
    micro = (((off_tile & (((texel_byte_pitch << 6) - 1) & ~0x1F))
              + ((off_tile & 0xF) << 1)) >> (3 + log_bpp)) & ~1
    return macro + micro + ((off_tile & 0x10) >> 4)


def untile_16bpp(tiled: bytes, width: int, height: int) -> bytes:
    """Untile a tiled 16-bpp Xbox 360 storage buffer into linear
    row-major texel order.  block_pixel_size = 1, texel_byte_pitch = 2."""
    padded_w = (width  + 31) & ~31
    padded_h = (height + 31) & ~31
    total = padded_w * padded_h

    out = bytearray(width * height * 2)
    for off in range(total):
        x = xg_address_2d_tiled_x(off, padded_w, 2)
        y = xg_address_2d_tiled_y(off, padded_w, 2)
        if x >= width or y >= height:
            continue
        src = off * 2
        if src + 2 > len(tiled):
            continue
        dst = (y * width + x) * 2
        out[dst] = tiled[src]
        out[dst + 1] = tiled[src + 1]
    return bytes(out)


def write_pgm(path: Path, data: bytes, w: int, h: int) -> None:
    with open(path, "wb") as f:
        f.write(f"P5\n{w} {h}\n255\n".encode("ascii"))
        f.write(data)


def main(ehf_path: Path) -> int:
    blob = ehf_path.read_bytes()
    if blob[:23] != MAGIC:
        print(f"!! {ehf_path.name}: not an .ehf (magic mismatch)")
        return 1

    # Header — 63 bytes
    body_offset = be_u32(blob, 55)
    body_size   = be_u32(blob, 59)

    # .tex sub-header inside the body
    tex_w  = be_u32(blob, body_offset + 0x10)
    tex_h  = be_u32(blob, body_offset + 0x14)
    tex_pf = be_u32(blob, body_offset + 0x18)
    tex_mt = be_u32(blob, body_offset + 0x20)
    if tex_pf != 24:
        print(f"!! PixelFormat is {tex_pf}, not 24 — skipping")
        return 1

    raw_size  = be_u32(blob, body_offset + tex_mt)
    comp_size = be_u32(blob, body_offset + tex_mt + 4)
    zlib_off  = body_offset + tex_mt + 8

    print(f"# {ehf_path.name}")
    print(f"  body: offset=0x{body_offset:x}  size={body_size:,}")
    print(f"  tex : W={tex_w} H={tex_h} PF={tex_pf}  raw={raw_size:,}  comp={comp_size:,}")
    print(f"  zlib at 0x{zlib_off:x}")

    # Inflate.  Feed EXACTLY comp_size bytes — anything beyond that
    # is the next .tex blob in the .ehf and confuses zlib's
    # readahead.  decompressobj with max_length bounds the output.
    d = zlib.decompressobj(15)
    raw = d.decompress(blob[zlib_off:zlib_off + comp_size],
                       max_length=raw_size)
    print(f"  inflated to {len(raw):,}B (expected {raw_size:,})  "
          f"unconsumed_tail={len(d.unconsumed_tail)}  eof={d.eof}")
    if len(raw) != raw_size:
        print(f"  WARNING: inflate size mismatch")
        return 2
    raw = bytes(raw)

    # Untile -> linear 16-bpp
    linear = untile_16bpp(raw, tex_w, tex_h)
    assert len(linear) == tex_w * tex_h * 2

    # Split channels
    ch0 = bytes(linear[i] for i in range(0, len(linear), 2))
    ch1 = bytes(linear[i] for i in range(1, len(linear), 2))

    out_dir = ehf_path.parent
    out0 = out_dir / f"{ehf_path.stem}_ch0_idx.pgm"
    out1 = out_dir / f"{ehf_path.stem}_ch1_blend.pgm"
    write_pgm(out0, ch0, tex_w, tex_h)
    write_pgm(out1, ch1, tex_w, tex_h)

    # Distribution stats — what's the cardinality of each channel?
    counts_ch0 = {}
    counts_ch1 = {}
    for b in ch0:
        counts_ch0[b] = counts_ch0.get(b, 0) + 1
    for b in ch1:
        counts_ch1[b] = counts_ch1.get(b, 0) + 1

    print(f"  ch0 distinct values: {len(counts_ch0)}")
    top_ch0 = sorted(counts_ch0.items(), key=lambda x: -x[1])[:10]
    print(f"    top: {top_ch0}")
    print(f"  ch1 distinct values: {len(counts_ch1)}")
    top_ch1 = sorted(counts_ch1.items(), key=lambda x: -x[1])[:10]
    print(f"    top: {top_ch1}")

    print(f"  wrote {out0.name}, {out1.name}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        sys.exit(1)
    sys.exit(main(Path(sys.argv[1])))
