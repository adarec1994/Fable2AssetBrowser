"""Test the hypothesis that the .ehf body's LOW byte (channel G of
the decoded lightmap) is a per-cell material index.

For each .ehf file:
  1. Reuse the body decode from ehf_body_decode.py to get the linear
     16-bpp untiled buffer.
  2. Pseudo-color the low byte under several interpretations:
       (a) raw u8 hashed to a 256-entry pastel palette
       (b) high nibble only (0..15)
       (c) low nibble only (0..15)
       (d) u8 % palette_count (40 for chapter3, 28 for defaultscenario)
  3. Save each as a separate PNG so we can eyeball which encoding
     produces coherent terrain-region patches matching the textures
     listed in EhfPalette.

If (a) looks regional and matches a 8-bit index space, we know the
encoding is just "u8 = palette index" and the C++ renderer can
sample-and-lookup as-is.

Usage:  python tools/ehf_material_viz.py <path/to/file.ehf>
"""

from __future__ import annotations
import colorsys
import struct
import sys
import zlib
from pathlib import Path

MAGIC = b"HeightFieldGraphicsFile"


def be_u32(b: bytes, off: int) -> int:
    return struct.unpack_from(">I", b, off)[0]


# ---- Xbox 360 16-bpp untile (block_pixel_size=1, texel_byte_pitch=2) -----

def xg_x(off, w, p=2):
    aw = (w + 31) & ~31
    lb = (p >> 2) + ((p >> 1) >> (p >> 2))
    ob = off << lb
    ot = ((ob & ~0xFFF) >> 3) + ((ob & 0x700) >> 2) + (ob & 0x3F)
    om = ot >> (7 + lb)
    mx = (om % (aw >> 5)) << 2
    t = (((ot >> (5 + lb)) & 2) + (ob >> 6)) & 3
    macro = (mx + t) << 3
    micro = ((((ot >> 1) & ~0xF) + (ot & 0xF)) & ((p << 3) - 1)) >> lb
    return macro + micro


def xg_y(off, w, p=2):
    aw = (w + 31) & ~31
    lb = (p >> 2) + ((p >> 1) >> (p >> 2))
    ob = off << lb
    ot = ((ob & ~0xFFF) >> 3) + ((ob & 0x700) >> 2) + (ob & 0x3F)
    om = ot >> (7 + lb)
    my = (om // (aw >> 5)) << 2
    t = ((ot >> (6 + lb)) & 1) + ((ob & 0x800) >> 10)
    macro = (my + t) << 3
    micro = (((ot & (((p << 6) - 1) & ~0x1F)) + ((ot & 0xF) << 1)) >> (3 + lb)) & ~1
    return macro + micro + ((ot & 0x10) >> 4)


def untile_16bpp(tiled, w, h):
    pw = (w + 31) & ~31
    ph = (h + 31) & ~31
    out = bytearray(w * h * 2)
    for off in range(pw * ph):
        x, y = xg_x(off, pw), xg_y(off, pw)
        if x >= w or y >= h:
            continue
        s = off * 2
        if s + 2 > len(tiled):
            continue
        d = (y * w + x) * 2
        out[d] = tiled[s]
        out[d + 1] = tiled[s + 1]
    return bytes(out)


def hash_palette_256():
    """256 distinct pastel-ish RGB colors keyed by 8-bit index."""
    pal = []
    for i in range(256):
        h = (i * 13 % 256) / 256.0
        s = 0.55 + (i % 7) / 30.0
        v = 0.65 + (i % 5) / 18.0
        r, g, b = colorsys.hsv_to_rgb(h, min(s, 1), min(v, 1))
        pal.append((int(r * 255), int(g * 255), int(b * 255)))
    return pal


def hash_palette_16():
    """16 distinct vivid colors keyed by 4-bit index."""
    pal = []
    for i in range(16):
        h = (i / 16.0)
        s = 0.85
        v = 0.95 if i % 2 == 0 else 0.7
        r, g, b = colorsys.hsv_to_rgb(h, s, v)
        pal.append((int(r * 255), int(g * 255), int(b * 255)))
    return pal


def write_png(path, w, h, rgb_iter):
    """Tiny PNG writer using stdlib zlib (no PIL needed)."""
    import struct as _s
    def chunk(tag, data):
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return _s.pack(">I", len(data)) + tag + data + _s.pack(">I", crc)

    raw = bytearray()
    pos = 0
    rgb_bytes = bytes(b for px in rgb_iter for b in px)
    stride = w * 3
    for y in range(h):
        raw.append(0)  # filter type none
        raw += rgb_bytes[y * stride: (y + 1) * stride]
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = _s.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)  # 8-bit RGB
    idat = zlib.compress(bytes(raw), 6)
    with open(path, "wb") as f:
        f.write(sig)
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", idat))
        f.write(chunk(b"IEND", b""))


def main(ehf_path: Path, palette_count: int) -> int:
    blob = ehf_path.read_bytes()
    if blob[:23] != MAGIC:
        print(f"!! {ehf_path.name}: not an .ehf")
        return 1

    body_off = be_u32(blob, 55)
    W, H = be_u32(blob, body_off + 0x10), be_u32(blob, body_off + 0x14)
    pf = be_u32(blob, body_off + 0x18)
    mt = be_u32(blob, body_off + 0x20)
    raw_size = be_u32(blob, body_off + mt)
    comp_size = be_u32(blob, body_off + mt + 4)
    zlib_off = body_off + mt + 8

    if pf != 24:
        print(f"!! PF={pf} not 24")
        return 1

    print(f"# {ehf_path.name}: W={W} H={H} raw={raw_size}")
    d = zlib.decompressobj(15)
    raw = d.decompress(blob[zlib_off:zlib_off + comp_size], max_length=raw_size)
    if len(raw) != raw_size:
        print(f"  inflate short: got {len(raw)} of {raw_size}")
        return 2

    linear = untile_16bpp(raw, W, H)
    out_dir = ehf_path.parent
    stem = ehf_path.stem

    # ch1 (low byte) — the candidate material index
    ch1 = bytes(linear[i] for i in range(1, len(linear), 2))

    pal256 = hash_palette_256()
    pal16  = hash_palette_16()

    def hist_top(b, k=15):
        from collections import Counter
        return Counter(b).most_common(k)

    print(f"  ch1 distinct: {len(set(ch1))}  top={hist_top(ch1, 10)}")

    # (a) raw u8 -> 256-pal hash
    rgb_a = (pal256[v] for v in ch1)
    write_png(out_dir / f"{stem}_mat_a_u8.png", W, H, rgb_a)

    # (b) high nibble -> 16-pal hash
    rgb_b = (pal16[v >> 4] for v in ch1)
    write_png(out_dir / f"{stem}_mat_b_hi.png", W, H, rgb_b)

    # (c) low nibble -> 16-pal hash
    rgb_c = (pal16[v & 0x0F] for v in ch1)
    write_png(out_dir / f"{stem}_mat_c_lo.png", W, H, rgb_c)

    # (d) u8 % palette_count
    rgb_d = (pal256[v % palette_count] for v in ch1)
    write_png(out_dir / f"{stem}_mat_d_mod{palette_count}.png", W, H, rgb_d)

    print(f"  wrote 4 visualizations into {out_dir}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        sys.exit(1)
    pal_count = int(sys.argv[2]) if len(sys.argv) > 2 else 40
    sys.exit(main(Path(sys.argv[1]), pal_count))
