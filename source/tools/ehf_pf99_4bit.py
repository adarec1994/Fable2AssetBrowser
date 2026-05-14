"""Hypothesis: PF=99 is a 4-bit-per-pixel palette index splat map,
tile-stored in 4x4-pixel groups (8 bytes per block, same byte
layout as BC1 but reinterpreted).

Each 8-byte block encodes 16 4-bit palette indices in a 4x4 patch.
The whole image is tile-major-32 (32x32 blocks per macro tile,
row-major within), then linear.

This would explain:
  - 65% of blocks are all-zero (= palette entry 0 = "grass" — the
    dominant terrain material for chapter3 — fills most cells)
  - 12% are all-FF (= palette entry 15)
  - 1809 distinct "block values" rather than 1809 distinct individual
    indices (since each block packs 4 nibbles into a u16)
"""
from __future__ import annotations
import colorsys, struct, zlib
from pathlib import Path

EHF = Path("cmake-build-debug/extracted/bl_chapter3_heightfield_id_9501a1af.ehf")
PAL_END = 0x1ef6fe
W_LOGICAL, H_LOGICAL = 1650, 1815
W_PAD, H_PAD = 1664, 1920
BW, BH = W_PAD // 4, H_PAD // 4


def be_u32(b, off): return struct.unpack_from(">I", b, off)[0]
blob = EHF.read_bytes()
splat_off = PAL_END + 4
mt = be_u32(blob, splat_off + 0x20)
raw_size = be_u32(blob, splat_off + mt)
comp_size = be_u32(blob, splat_off + mt + 4)
zlib_off = splat_off + mt + 8
d = zlib.decompressobj(15)
raw = d.decompress(blob[zlib_off:zlib_off + comp_size], max_length=raw_size)


def untile_tile_major(src, blocks_w, blocks_h, tile=32):
    out = bytearray(blocks_w * blocks_h * 8)
    tiles_w = (blocks_w + tile - 1) // tile
    tiles_h = (blocks_h + tile - 1) // tile
    src_off = 0
    for ty in range(tiles_h):
        for tx in range(tiles_w):
            for ly in range(tile):
                by = ty * tile + ly
                for lx in range(tile):
                    bx = tx * tile + lx
                    if by < blocks_h and bx < blocks_w and src_off + 8 <= len(src):
                        dst = (by * blocks_w + bx) * 8
                        out[dst:dst + 8] = src[src_off:src_off + 8]
                    src_off += 8
    return bytes(out)


linear = untile_tile_major(raw, BW, BH, 32)


# Build a 16-color hash palette for pseudocoloring.
def hash_palette(n):
    out = []
    for i in range(n):
        h = (i * 7 % n) / n
        s = 0.85
        v = 0.95 if i % 2 == 0 else 0.65
        r, g, b = colorsys.hsv_to_rgb(h, s, v)
        out.append((int(r*255), int(g*255), int(b*255)))
    return out


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


# Try BOTH nibble orders within each byte:
#   "hi_first" : pixel 0 = byte[k]>>4, pixel 1 = byte[k]&0xF
#   "lo_first" : pixel 0 = byte[k]&0xF, pixel 1 = byte[k]>>4
# Pixel layout inside the 4x4 block: row-major (pixel 0 = top-left,
# pixel 3 = top-right, pixel 4 = row 1 col 0, etc.)
pal = hash_palette(16)
for nibble_order in ("hi_first", "lo_first"):
    # Render at logical 1650x1815 (crop padding).
    rgb = bytearray(W_LOGICAL * H_LOGICAL * 3)
    for by in range(BH):
        for bx in range(BW):
            block_off = (by * BW + bx) * 8
            # 16 nibbles from 8 bytes.
            nibs = []
            for k in range(8):
                b = linear[block_off + k]
                if nibble_order == "hi_first":
                    nibs.append(b >> 4)
                    nibs.append(b & 0xF)
                else:
                    nibs.append(b & 0xF)
                    nibs.append(b >> 4)
            for py in range(4):
                yy = by * 4 + py
                if yy >= H_LOGICAL: continue
                for pxn in range(4):
                    xx = bx * 4 + pxn
                    if xx >= W_LOGICAL: continue
                    idx = nibs[py * 4 + pxn]
                    r, g, b = pal[idx]
                    o = (yy * W_LOGICAL + xx) * 3
                    rgb[o] = r; rgb[o+1] = g; rgb[o+2] = b
    out = EHF.parent / f"{EHF.stem}_pf99_4bit_{nibble_order}.png"
    write_png(out, W_LOGICAL, H_LOGICAL, rgb)
    print(f"  wrote {out.name}")

# Also dump a per-block "average nibble value" thumbnail to see
# spatial distribution of palette indices.
from collections import Counter
all_nibs = Counter()
for k in range(len(linear)):
    all_nibs[linear[k] >> 4] += 1
    all_nibs[linear[k] & 0xF] += 1
print(f"\n4-bit value distribution: {sorted(all_nibs.most_common())}")
