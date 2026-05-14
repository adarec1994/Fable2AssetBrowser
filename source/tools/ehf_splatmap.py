"""Decode the PF=99 'splat map' blob that lives right after the
palette in a .ehf, then visualize it.

Layout (confirmed against bl_chapter3_heightfield_id_9501a1af.ehf):

  immediately after the last palette entry's metadata:
    +0  u32 BE  '00 00 00 01'        — count? sentinel?
    +4  .tex blob header (magic 0xFFFFFFFE, mt=0x54)
          W = u0_splat (1650 for chapter3)
          H = v0_splat (1815 for chapter3)
          PF = 99
          mip_table at +0x54: [u32 raw_size][u32 comp_size][zlib stream]

  raw_size / (W * H) ≈ 0.534 ⇒ ~4 bits per texel ⇒ it's a u4
  per-texel splat map indexing into the palette (≤16 of the palette's
  entries reachable per region; the palette has 40 entries with
  duplicates, so the effective unique material set is ≤16).

Usage:  python tools/ehf_splatmap.py <path/to/file.ehf>
"""

from __future__ import annotations
import colorsys
import struct
import sys
import zlib
from pathlib import Path

MAGIC = b"HeightFieldGraphicsFile"
ART = b"art\\"


def be_u32(b: bytes, off: int) -> int:
    return struct.unpack_from(">I", b, off)[0]


# ---- minimal Xbox 360 (1, 4) tile/untile -----------------------
# (block_pixel_size=1, texel_byte_pitch=4) is for 32-bpp.
# (block_pixel_size=1, texel_byte_pitch=2) for 16-bpp.
# For 4 bits per texel we'll treat the data as raw row-major
# 4-bit pairs and see if it untiles correctly under the standard
# Xbox 360 1-byte-per-2-texel tile formula.
# Start by NOT untiling and see if the linear stream already
# makes spatial sense — the engine may store this as linear.

def find_palette_end(blob: bytes, pal_start: int, body_end: int) -> int:
    i = pal_start
    while i + 4 < body_end:
        if blob[i:i+4] != ART:
            break
        end_d = blob.index(b"\x00", i)
        if blob[end_d+1:end_d+5] != ART:
            break
        end_n = blob.index(b"\x00", end_d + 1)
        i = end_n + 1 + 13
    return i


def find_palette_start(blob: bytes, search_start: int = 4):
    for i in range(search_start, len(blob) - 4):
        if (blob[i] == ord('a') and blob[i+1] == ord('r') and
            blob[i+2] == ord('t') and blob[i+3] == ord('\\')):
            count = be_u32(blob, i - 4)
            if 1 < count < 200:
                return i
    return None


def palette_count_16(palette_entries: int) -> int:
    """Effective number of distinct materials reachable through 4-bit indices."""
    return min(palette_entries, 16)


def hash_palette_16():
    pal = []
    for i in range(16):
        h = i / 16.0
        s, v = 0.85, (0.95 if i % 2 == 0 else 0.7)
        r, g, b = colorsys.hsv_to_rgb(h, s, v)
        pal.append((int(r * 255), int(g * 255), int(b * 255)))
    return pal


def write_png(path, w, h, rgb_iter):
    import struct as _s
    def chunk(tag, data):
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return _s.pack(">I", len(data)) + tag + data + _s.pack(">I", crc)

    rgb_bytes = bytes(b for px in rgb_iter for b in px)
    stride = w * 3
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgb_bytes[y * stride: (y + 1) * stride]
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = _s.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    idat = zlib.compress(bytes(raw), 6)
    with open(path, "wb") as f:
        f.write(sig)
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", idat))
        f.write(chunk(b"IEND", b""))


def main(ehf_path: Path) -> int:
    blob = ehf_path.read_bytes()
    if blob[:23] != MAGIC:
        print("!! not an .ehf"); return 1

    body_off = be_u32(blob, 55)
    body_size = be_u32(blob, 59)
    body_end = body_off + body_size

    pal_start = find_palette_start(blob)
    if pal_start is None:
        print("!! no palette found"); return 1
    pal_end = find_palette_end(blob, pal_start, body_end)
    print(f"# {ehf_path.name}")
    print(f"  palette: 0x{pal_start:x} .. 0x{pal_end:x}")
    print(f"  body_end: 0x{body_end:x}")

    # Look for the PF=99 blob right after the palette.
    # Skip the leading u32 sentinel and find the magic.
    scan_from = pal_end
    splat_off = None
    for i in range(scan_from, body_end - 16):
        if (blob[i]   == 0xFF and blob[i+1] == 0xFF and
            blob[i+2] == 0xFF and blob[i+3] == 0xFE):
            splat_off = i
            break
    if splat_off is None:
        print("!! no splat-map .tex magic found in gap 2"); return 1

    W  = be_u32(blob, splat_off + 0x10)
    H  = be_u32(blob, splat_off + 0x14)
    PF = be_u32(blob, splat_off + 0x18)
    mt = be_u32(blob, splat_off + 0x20)
    raw_size  = be_u32(blob, splat_off + mt)
    comp_size = be_u32(blob, splat_off + mt + 4)
    zlib_off  = splat_off + mt + 8

    print(f"  splat blob: off=0x{splat_off:x}  W={W} H={H} PF={PF}")
    print(f"  raw={raw_size:,}  comp={comp_size:,}  zlib@0x{zlib_off:x}")
    print(f"  bytes per texel: {raw_size/(W*H):.3f}  "
          f"(0.5 = 4-bit indices, 1.0 = 8-bit)")

    # Inflate with the same "feed exact comp_size, bound output" pattern.
    d = zlib.decompressobj(15)
    raw = d.decompress(blob[zlib_off:zlib_off + comp_size], max_length=raw_size)
    print(f"  inflated to {len(raw):,}B (expected {raw_size:,})")
    if len(raw) != raw_size:
        print("  !! inflate undershot")
        return 2

    # If bytes-per-texel ≈ 0.5, unpack as 4-bit indices.  Order
    # of nibbles: try BOTH high-then-low and low-then-high.
    bpt = raw_size / (W * H)
    out_dir = ehf_path.parent
    stem = ehf_path.stem
    pal16 = hash_palette_16()

    # PF=99 ratio 0.533 bytes/texel matches BC1 (8 bytes per 4x4 block
    # = 0.5 bytes/texel) with tile padding to next 64-pixel multiple
    # — bl_chapter3 confirms: 1664 x 1920 / 2 = raw_size exactly.
    # Try the BC1 decode path BEFORE the 4-bit-index hypothesis.
    if bpt < 0.8 and PF == 99:
        # BC1 decode: each 4x4 block is 8 bytes.
        # Padded dimensions = next-mul-of-{4} for blocks, then maybe
        # extra tile padding.  Use raw_size to derive padded block grid.
        # Try padded_w = next-mul-of-{32,64,128} that divides raw_size/8.
        total_blocks = raw_size // 8
        padW, padH = None, None
        for sw in (128, 64, 32, 16, 8, 4):
            pw = ((W + sw - 1) // sw) * sw
            if pw % 4 != 0: continue
            bw = pw // 4
            if total_blocks % bw == 0:
                bh = total_blocks // bw
                ph = bh * 4
                if ph >= H:
                    padW, padH = pw, ph
                    break
        if padW is None:
            print("  !! couldn't factor padded BC1 dimensions; skipping")
            return 2
        print(f"  BC1 padded: {padW} x {padH}  (logical {W} x {H})")

        # Decode raw BC1 blocks (after BE→LE endpoint+index swap,
        # mirroring what the existing C++ atlas decoder does for PF=35).
        def be_to_le_bc1(data: bytes) -> bytes:
            out = bytearray(data)
            for i in range(0, len(out), 8):
                # swap c0 (bytes 0,1)
                out[i], out[i+1] = out[i+1], out[i]
                # swap c1 (bytes 2,3)
                out[i+2], out[i+3] = out[i+3], out[i+2]
                # reverse 4-byte index dword
                a = out[i+4]; b = out[i+5]; c = out[i+6]; d = out[i+7]
                out[i+4] = d; out[i+5] = c; out[i+6] = b; out[i+7] = a
            return bytes(out)

        bc1_le = be_to_le_bc1(raw)
        # Also try WITHOUT the BE→LE swap — PF=99 may store
        # endpoints + indices in native LE already.
        bc1_raw = bytes(raw)

        # Untile Xbox 360 (4, 8) — block_pixel_size=4, texel_byte_pitch=8.
        # Standard formula already proven in tools/ehf_body_decode.py.
        def xg_x(off, w, p=8):
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
        def xg_y(off, w, p=8):
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

        # Untile BC1 blocks from tiled-storage layout into row-major.
        bw_pad = padW // 4
        bh_pad = padH // 4

        def untile(src_bytes):
            out = bytearray(bw_pad * bh_pad * 8)
            for off in range(bw_pad * bh_pad):
                x = xg_x(off, bw_pad, 8)
                y = xg_y(off, bw_pad, 8)
                if x >= bw_pad or y >= bh_pad:
                    continue
                src = off * 8
                if src + 8 > len(src_bytes):
                    continue
                dst = (y * bw_pad + x) * 8
                out[dst:dst+8] = src_bytes[src:src+8]
            return out

        # Decode block-by-block to RGBA from linear (untiled) buffer.
        def decode_block(b: bytes) -> list:
            c0 = b[0] | (b[1] << 8)
            c1 = b[2] | (b[3] << 8)
            def ex(v, b):
                if b == 5: return ((v << 3) | (v >> 2)) & 0xFF
                else:      return ((v << 2) | (v >> 4)) & 0xFF
            r0 = ex((c0 >> 11) & 31, 5); g0 = ex((c0 >> 5) & 63, 6); b0 = ex(c0 & 31, 5)
            r1 = ex((c1 >> 11) & 31, 5); g1 = ex((c1 >> 5) & 63, 6); b1 = ex(c1 & 31, 5)
            cols = [(r0, g0, b0), (r1, g1, b1)]
            if c0 > c1:
                cols.append(((2*r0+r1+1)//3, (2*g0+g1+1)//3, (2*b0+b1+1)//3))
                cols.append(((r0+2*r1+1)//3, (g0+2*g1+1)//3, (b0+2*b1+1)//3))
            else:
                cols.append(((r0+r1)//2, (g0+g1)//2, (b0+b1)//2))
                cols.append((0, 0, 0))
            idx = b[4] | (b[5] << 8) | (b[6] << 16) | (b[7] << 24)
            return [cols[(idx >> (2*i)) & 3] for i in range(16)]

        for variant_name, src in (("swapBE", bc1_le), ("noswap", bc1_raw)):
            linear_blocks = untile(src)
            rgb = [(0,0,0)] * (W * H)
            for by in range(bh_pad):
                for bx in range(bw_pad):
                    block_off = (by * bw_pad + bx) * 8
                    px = decode_block(linear_blocks[block_off:block_off+8])
                    for py in range(4):
                        yy = by * 4 + py
                        if yy >= H: break
                        for pxn in range(4):
                            xx = bx * 4 + pxn
                            if xx >= W: continue
                            rgb[yy * W + xx] = px[py * 4 + pxn]
            png = out_dir / f"{stem}_splat_bc1_{variant_name}.png"
            write_png(png, W, H, rgb)
            print(f"  wrote {png.name} ({variant_name})")

        # Also try treating raw as already-untiled (linear row-major
        # block storage) — engine might not tile PF=99 the way it
        # tiles PF=35.
        for variant_name, src in (("linear_swapBE", bc1_le),
                                  ("linear_noswap", bc1_raw)):
            rgb = [(0,0,0)] * (W * H)
            for by in range(bh_pad):
                for bx in range(bw_pad):
                    block_off = (by * bw_pad + bx) * 8
                    if block_off + 8 > len(src): break
                    px = decode_block(src[block_off:block_off+8])
                    for py in range(4):
                        yy = by * 4 + py
                        if yy >= H: break
                        for pxn in range(4):
                            xx = bx * 4 + pxn
                            if xx >= W: continue
                            rgb[yy * W + xx] = px[py * 4 + pxn]
            png = out_dir / f"{stem}_splat_bc1_{variant_name}.png"
            write_png(png, W, H, rgb)
            print(f"  wrote {png.name} ({variant_name})")

        # Also try with Xbox 360 (4, 8) untile applied first.
        # That's the standard atlas decode path.
        return 0

    if bpt < 0.8:
        # 4-bit per texel — and raw_size > W*H/2 implies padded
        # storage.  Factor raw_size as padded_w * padded_h / 2 with
        # both padded dims rounded to next mul of 32 / 64 / 128.
        # For chapter3: raw=1,597,440  W=1650 H=1815
        #   1664 x 1920 / 2 = 1,597,440  (padded_w=1664, padded_h=1920)
        # → padded_w = ceil(W / 64) * 64? 1650→1664 (next 32-aligned),
        #   1815→1920 (next 64-aligned? no, 1815→1824).  Search.
        def best_padding(raw_bytes, w, h):
            # Try plausible (padW, padH) combos where padW>=w, padH>=h,
            # padW % 32 == 0 (or 64, 128), padH % 32 == 0 likewise,
            # and padW * padH / 2 == raw_bytes.
            target = raw_bytes * 2
            for sw in (8, 16, 32, 64, 128, 256):
                pw = ((w + sw - 1) // sw) * sw
                if pw < w: continue
                if target % pw == 0:
                    ph = target // pw
                    if ph >= h:
                        return pw, ph
            return w, h
        padW, padH = best_padding(raw_size, W, H)
        print(f"  padded storage: {padW} x {padH}  (logical {W} x {H})")

        # Unpack the full padded grid then crop to W x H.
        full = bytearray(padW * padH)
        for i in range(raw_size):
            hi = (raw[i] >> 4) & 0xF
            lo =  raw[i]       & 0xF
            full[i * 2]     = hi
            full[i * 2 + 1] = lo

        # Two unpacking orders: hi-then-lo, or lo-then-hi.
        for label, nib_order in (("hi_first", (0, 1)), ("lo_first", (1, 0))):
            buf = bytearray(W * H)
            for y in range(H):
                src_row = y * padW
                dst_row = y * W
                for x in range(W // 2):
                    b = raw[src_row // 2 + x]
                    hi = (b >> 4) & 0xF
                    lo =  b       & 0xF
                    if nib_order == (0, 1):
                        buf[dst_row + x*2]     = hi
                        buf[dst_row + x*2 + 1] = lo
                    else:
                        buf[dst_row + x*2]     = lo
                        buf[dst_row + x*2 + 1] = hi
                if W % 2:
                    # last odd texel
                    b = raw[src_row // 2 + W // 2]
                    buf[dst_row + W - 1] = (b >> 4) if nib_order == (0,1) else (b & 0xF)
            png = out_dir / f"{stem}_splat_4bit_{label}.png"
            write_png(png, W, H, (pal16[v & 0xF] for v in buf))
            print(f"  wrote {png.name}  ({W}x{H})")
    else:
        # 8-bit per texel — pseudocolor.
        pal256 = []
        import colorsys as _c
        for v in range(256):
            r, g, b = _c.hsv_to_rgb((v * 13 % 256) / 256.0, 0.6, 0.9)
            pal256.append((int(r*255), int(g*255), int(b*255)))
        png = out_dir / f"{stem}_splat_8bit.png"
        write_png(png, W, H, (pal256[v] for v in raw[:W*H]))
        print(f"  wrote {png.name}")
    return 0


if __name__ == "__main__":
    ehf = Path(sys.argv[1]) if len(sys.argv) > 1 else \
          Path("cmake-build-debug/extracted/bl_chapter3_heightfield_id_9501a1af.ehf")
    sys.exit(main(ehf))
