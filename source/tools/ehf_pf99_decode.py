"""Try multiple interpretations of the PF=99 blob in a .ehf:

  (A) BC1 with the "tile-major" untile (32x32-block macro tiles, linear
      within tile) — the layout the comment in
      src/Level/TextureAtlasDecoder.cpp claims is for .ehf terrain pages.
      Output as RGBA so we can see if it produces recognizable terrain.

  (B) "Material-blend splat map" hypothesis: each 8-byte block encodes
        +0..1  u16  mat0  (palette index 0)
        +2..3  u16  mat1  (palette index 1)
        +4..7  4B   16 × 2-bit per-pixel selectors
      For each 4x4 block we pick the dominant material and render a
      pseudocolor map.  If PF=99 is a splat map, this view should
      show smooth large regions (grass / dirt / cliff zones).

Run:  python tools/ehf_pf99_decode.py
"""
from __future__ import annotations
import colorsys
import struct
import zlib
from pathlib import Path

EHF = Path("cmake-build-debug/extracted/bl_chapter3_heightfield_id_9501a1af.ehf")
PAL_END = 0x1ef6fe          # offset right after the palette
W_LOGICAL, H_LOGICAL = 1650, 1815
W_PADDED,  H_PADDED  = 1664, 1920
BW, BH = W_PADDED // 4, H_PADDED // 4  # blocks


def be_u32(b, off): return struct.unpack_from(">I", b, off)[0]


# --------------------------------------------------------------------
# Find + inflate the PF=99 blob
# --------------------------------------------------------------------
blob = EHF.read_bytes()
# Magic right after `00 00 00 01` sentinel.
splat_off = PAL_END + 4
assert blob[splat_off:splat_off+4] == b"\xff\xff\xff\xfe", \
       f"expected PF=99 magic at 0x{splat_off:x}"
W  = be_u32(blob, splat_off + 0x10)
H  = be_u32(blob, splat_off + 0x14)
PF = be_u32(blob, splat_off + 0x18)
mt = be_u32(blob, splat_off + 0x20)
raw_size  = be_u32(blob, splat_off + mt)
comp_size = be_u32(blob, splat_off + mt + 4)
zlib_off  = splat_off + mt + 8
print(f"PF={PF} W={W} H={H} raw={raw_size:,} comp={comp_size:,}")

d = zlib.decompressobj(15)
raw = d.decompress(blob[zlib_off:zlib_off + comp_size], max_length=raw_size)
print(f"inflated {len(raw):,} bytes")
assert len(raw) == raw_size


# --------------------------------------------------------------------
# tile_major untile (mirrors src/Level/TextureAtlasDecoder.cpp's
# untile_xbox360_tile_major: 32x32-block macro tiles row-major between
# tiles, linear row-major within each tile).
# --------------------------------------------------------------------
def untile_tile_major(src, blocks_w, blocks_h, block_bytes=8, tile=32):
    out = bytearray(blocks_w * blocks_h * block_bytes)
    tiles_w = (blocks_w + tile - 1) // tile
    tiles_h = (blocks_h + tile - 1) // tile
    src_off = 0
    for ty in range(tiles_h):
        for tx in range(tiles_w):
            for ly in range(tile):
                by = ty * tile + ly
                for lx in range(tile):
                    bx = tx * tile + lx
                    if by < blocks_h and bx < blocks_w and src_off + block_bytes <= len(src):
                        dst = (by * blocks_w + bx) * block_bytes
                        out[dst:dst + block_bytes] = src[src_off:src_off + block_bytes]
                    src_off += block_bytes
    return bytes(out)


linear_blocks = untile_tile_major(raw, BW, BH, 8, 32)
print(f"untiled to {len(linear_blocks)} bytes ({BW}x{BH} blocks)")


# --------------------------------------------------------------------
# (A) BC1 decode (standard, with BE→LE endpoint+index swap).
# --------------------------------------------------------------------
def be_to_le_bc1(data):
    out = bytearray(data)
    for i in range(0, len(out), 8):
        out[i], out[i+1] = out[i+1], out[i]
        out[i+2], out[i+3] = out[i+3], out[i+2]
        a, b, c, dd = out[i+4], out[i+5], out[i+6], out[i+7]
        out[i+4] = dd; out[i+5] = c; out[i+6] = b; out[i+7] = a
    return bytes(out)


def decode_bc1_block(b8):
    c0 = b8[0] | (b8[1] << 8)
    c1 = b8[2] | (b8[3] << 8)
    def ex(v, w):
        if w == 5: return ((v << 3) | (v >> 2)) & 0xFF
        return ((v << 2) | (v >> 4)) & 0xFF
    r0 = ex((c0 >> 11) & 31, 5); g0 = ex((c0 >> 5) & 63, 6); b0 = ex(c0 & 31, 5)
    r1 = ex((c1 >> 11) & 31, 5); g1 = ex((c1 >> 5) & 63, 6); b1 = ex(c1 & 31, 5)
    cols = [(r0, g0, b0), (r1, g1, b1)]
    if c0 > c1:
        cols.append(((2*r0+r1+1)//3, (2*g0+g1+1)//3, (2*b0+b1+1)//3))
        cols.append(((r0+2*r1+1)//3, (g0+2*g1+1)//3, (b0+2*b1+1)//3))
    else:
        cols.append(((r0+r1)//2, (g0+g1)//2, (b0+b1)//2))
        cols.append((0, 0, 0))
    idx = b8[4] | (b8[5] << 8) | (b8[6] << 16) | (b8[7] << 24)
    return [cols[(idx >> (2*i)) & 3] for i in range(16)]


def write_png(path, w, h, rgb_flat):
    def chunk(tag, data):
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)
    raw_rows = bytearray()
    stride = w * 3
    for y in range(h):
        raw_rows.append(0)
        raw_rows += rgb_flat[y * stride: (y + 1) * stride]
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw_rows), 6)))
        f.write(chunk(b"IEND", b""))


# Path A: standard BC1 decode, both with and without endpoint swap.
for label, src in (("bc1_swap", be_to_le_bc1(linear_blocks)),
                   ("bc1_noswap", linear_blocks)):
    rgb = bytearray(W_LOGICAL * H_LOGICAL * 3)
    for by in range(BH):
        for bx in range(BW):
            block_off = (by * BW + bx) * 8
            px = decode_bc1_block(src[block_off:block_off + 8])
            for py in range(4):
                yy = by * 4 + py
                if yy >= H_LOGICAL: break
                for pxn in range(4):
                    xx = bx * 4 + pxn
                    if xx >= W_LOGICAL: continue
                    r, g, b = px[py * 4 + pxn]
                    o = (yy * W_LOGICAL + xx) * 3
                    rgb[o] = r; rgb[o+1] = g; rgb[o+2] = b
    out = EHF.parent / f"{EHF.stem}_pf99_tilemajor_{label}.png"
    write_png(out, W_LOGICAL, H_LOGICAL, rgb)
    print(f"  wrote {out.name}")


# Path B: "material-blend splat map" hypothesis.
# Each 8-byte block: u16 mat0, u16 mat1, 4B 16×2-bit selectors.
# Pseudocolor: pick the *dominant* selector value (most common of 16
# selectors in the block) and use it to choose between mat0 / mat1.
# Then color that material with a 40-entry hash palette.
PALETTE_40 = []
for i in range(64):
    h = (i * 17 % 64) / 64.0
    r, g, b = colorsys.hsv_to_rgb(h, 0.85, 0.95 if i % 2 == 0 else 0.7)
    PALETTE_40.append((int(r*255), int(g*255), int(b*255)))

def block_dominant_mat(b8):
    # Endpoints as BE u16 (Xbox 360 native).
    mat0 = (b8[0] << 8) | b8[1]
    mat1 = (b8[2] << 8) | b8[3]
    idx32 = (b8[4] << 24) | (b8[5] << 16) | (b8[6] << 8) | b8[7]
    sel = [(idx32 >> (2 * i)) & 3 for i in range(16)]
    # 2-bit selectors: 0=use mat0, 1=use mat1, 2/3=interpolate (if c0>c1)
    n0 = sel.count(0)
    n1 = sel.count(1)
    return mat0 if n0 >= n1 else mat1


# Also try LE u16 reading.
def block_dominant_mat_le(b8):
    mat0 = b8[0] | (b8[1] << 8)
    mat1 = b8[2] | (b8[3] << 8)
    idx32 = b8[4] | (b8[5] << 8) | (b8[6] << 16) | (b8[7] << 24)
    sel = [(idx32 >> (2 * i)) & 3 for i in range(16)]
    n0 = sel.count(0)
    n1 = sel.count(1)
    return mat0 if n0 >= n1 else mat1


# Block-level resolution: BW × BH (one color per 4x4 block).
for label, getter in (("blendmat_be", block_dominant_mat),
                      ("blendmat_le", block_dominant_mat_le)):
    rgb = bytearray(BW * BH * 3)
    for by in range(BH):
        for bx in range(BW):
            block_off = (by * BW + bx) * 8
            mat = getter(linear_blocks[block_off:block_off + 8])
            # Color hash — clip to 64 distinct.
            col = PALETTE_40[mat % 64]
            o = (by * BW + bx) * 3
            rgb[o] = col[0]; rgb[o+1] = col[1]; rgb[o+2] = col[2]
    out = EHF.parent / f"{EHF.stem}_pf99_tilemajor_{label}.png"
    write_png(out, BW, BH, rgb)
    print(f"  wrote {out.name} ({BW}x{BH} blocks)")

# Also dump histogram of unique mat0/mat1 values.
from collections import Counter
mat0_be = Counter()
mat1_be = Counter()
for by in range(BH):
    for bx in range(BW):
        o = (by * BW + bx) * 8
        b8 = linear_blocks[o:o+8]
        mat0_be[(b8[0] << 8) | b8[1]] += 1
        mat1_be[(b8[2] << 8) | b8[3]] += 1
print()
print(f"mat0 BE: {len(mat0_be)} distinct; top 10: {mat0_be.most_common(10)}")
print(f"mat1 BE: {len(mat1_be)} distinct; top 10: {mat1_be.most_common(10)}")
