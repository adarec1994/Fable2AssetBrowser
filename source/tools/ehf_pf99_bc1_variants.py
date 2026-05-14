"""PF=99 with tile_major_32 untile is in the right layout — verified by
the content-block distribution.  But the BC1 decode is producing
mostly-black output.  Try every combination of byte-swap/bit-reverse
for the endpoints + index dword to find the right Xbox 360 format."""
from __future__ import annotations
import struct, zlib
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


def decode_bc1_block_variant(b8, swap_c0c1, reverse_idx_bytes, reverse_idx_bits):
    """Decode with the chosen byte-order variants."""
    if swap_c0c1:
        c0 = (b8[0] << 8) | b8[1]
        c1 = (b8[2] << 8) | b8[3]
    else:
        c0 = b8[0] | (b8[1] << 8)
        c1 = b8[2] | (b8[3] << 8)
    if reverse_idx_bytes:
        idx = (b8[4] << 24) | (b8[5] << 16) | (b8[6] << 8) | b8[7]
    else:
        idx = b8[4] | (b8[5] << 8) | (b8[6] << 16) | (b8[7] << 24)
    if reverse_idx_bits:
        new = 0
        for i in range(16):
            sel = (idx >> (2 * i)) & 3
            new |= sel << (2 * (15 - i))
        idx = new

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


for swap_c, rev_b, rev_bits in [(s, b, x) for s in (0, 1) for b in (0, 1) for x in (0, 1)]:
    label = f"swap{swap_c}_revB{rev_b}_revBits{rev_bits}"
    # Down-scaled output: only render every 2nd pixel to speed up.
    W_OUT = W_LOGICAL // 2
    H_OUT = H_LOGICAL // 2
    rgb = bytearray(W_OUT * H_OUT * 3)
    for by in range(BH):
        for bx in range(BW):
            o = (by * BW + bx) * 8
            px = decode_bc1_block_variant(linear[o:o+8], swap_c, rev_b, rev_bits)
            for py in (0, 2):  # sample 2 rows of the 4-block
                yy = (by * 4 + py) // 2
                if yy >= H_OUT: continue
                for pxn in (0, 2):
                    xx = (bx * 4 + pxn) // 2
                    if xx >= W_OUT: continue
                    r, g, b = px[py * 4 + pxn]
                    of = (yy * W_OUT + xx) * 3
                    rgb[of] = r; rgb[of+1] = g; rgb[of+2] = b
    out_path = EHF.parent / f"{EHF.stem}_pf99_v_{label}.png"
    write_png(out_path, W_OUT, H_OUT, rgb)
    print(f"  wrote {out_path.name}")
