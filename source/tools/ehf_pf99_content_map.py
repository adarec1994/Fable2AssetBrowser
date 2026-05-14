"""Map WHERE the non-padding BC1 blocks live in PF=99 under different
untile interpretations.  Whichever interpretation puts the content in
a tight rectangular cluster (matching the logical 1650x1815 dims) is
the right untile.
"""
from __future__ import annotations
import struct
import zlib
from pathlib import Path

EHF = Path("cmake-build-debug/extracted/bl_chapter3_heightfield_id_9501a1af.ehf")
PAL_END = 0x1ef6fe
W_LOGICAL, H_LOGICAL = 1650, 1815
W_PAD,     H_PAD     = 1664, 1920
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
assert len(raw) == raw_size

# Define block "content" = ALL 8 bytes != 00 and != FF, i.e. real BC1 data.
def is_content(b8):
    if all(b == 0 for b in b8): return False
    if all(b == 0xFF for b in b8): return False
    return True


def hist_2d(blocks, bw, bh, label):
    """Render a binary map showing where the non-padding blocks land
    when the storage is interpreted as `blocks` (linear stream of
    8-byte block records) laid out in row-major (bw, bh)."""
    # Project to a small thumbnail (each bw/bh block -> 1 pixel).
    thumb_w, thumb_h = bw, bh
    rgb = bytearray(thumb_w * thumb_h * 3)
    for i in range(thumb_w * thumb_h):
        if i * 8 + 8 > len(blocks): continue
        b8 = blocks[i*8:i*8+8]
        if is_content(b8):
            rgb[i*3+0] = 200
            rgb[i*3+1] = 250
            rgb[i*3+2] = 200
    out = EHF.parent / f"{EHF.stem}_pf99_content_{label}.png"
    write_png(out, thumb_w, thumb_h, rgb)
    print(f"  wrote {out.name} ({thumb_w}x{thumb_h})")


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


# (1) Raw storage, no untile at all - assume bytes are already in
#     row-major linear block order.
hist_2d(raw, BW, BH, "raw_linear")


# (2) Tile-major untile (32x32 macro tiles)
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


hist_2d(untile_tile_major(raw, BW, BH, 32), BW, BH, "tilemajor_32")
hist_2d(untile_tile_major(raw, BW, BH, 16), BW, BH, "tilemajor_16")
hist_2d(untile_tile_major(raw, BW, BH, 8), BW, BH, "tilemajor_8")


# (3) ImageHeat untile.
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


def untile_imageheat(src, blocks_w, blocks_h):
    pad_w = (blocks_w + 31) & ~31
    pad_h = (blocks_h + 31) & ~31
    out = bytearray(blocks_w * blocks_h * 8)
    for off in range(pad_w * pad_h):
        x = xg_x(off, pad_w, 8)
        y = xg_y(off, pad_w, 8)
        if x >= blocks_w or y >= blocks_h: continue
        s = off * 8
        if s + 8 > len(src): continue
        d = (y * blocks_w + x) * 8
        out[d:d+8] = src[s:s+8]
    return bytes(out)


hist_2d(untile_imageheat(raw, BW, BH), BW, BH, "imageheat")
