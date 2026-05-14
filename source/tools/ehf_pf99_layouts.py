"""Try several PF=99 layout interpretations side-by-side.  Whichever
produces a CLEAN material splat map (smooth regions, no block-checker
artifacts) is the right one.
"""
from __future__ import annotations
import colorsys, struct, zlib
from pathlib import Path

EHF = Path("cmake-build-debug/extracted/bl_chapter3_heightfield_id_9501a1af.ehf")
PAL_END = 0x1ef6fe
W_LOGICAL, H_LOGICAL = 1650, 1815
W_PAD, H_PAD = 1664, 1920


def be_u32(b, off): return struct.unpack_from(">I", b, off)[0]
blob = EHF.read_bytes()
splat_off = PAL_END + 4
mt = be_u32(blob, splat_off + 0x20)
raw_size = be_u32(blob, splat_off + mt)
comp_size = be_u32(blob, splat_off + mt + 4)
zlib_off = splat_off + mt + 8
d = zlib.decompressobj(15)
raw = d.decompress(blob[zlib_off:zlib_off + comp_size], max_length=raw_size)
assert len(raw) == raw_size, len(raw)
print(f"raw inflated: {len(raw):,}")


# 16-color palette for visualisation.
def pal16():
    out = []
    for i in range(16):
        h = i / 16.0
        s, v = 0.85, (0.95 if i % 2 == 0 else 0.7)
        r, g, b = colorsys.hsv_to_rgb(h, s, v)
        out.append((int(r*255), int(g*255), int(b*255)))
    return out
PAL = pal16()


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


def render_indices(indices, W, H, label):
    rgb = bytearray(W * H * 3)
    for i, v in enumerate(indices[:W*H]):
        c = PAL[v & 0xF]
        rgb[i*3:i*3+3] = bytes(c)
    out = EHF.parent / f"{EHF.stem}_pf99_LAYOUT_{label}.png"
    write_png(out, W, H, rgb)
    print(f"  wrote {out.name}")


# --- Layout A: no tile, plain row-major 4-bit nibbles ---------------
# nibbles[y * W_pad + x] = raw[(y * W_pad + x) // 2] {hi or lo}
def layout_no_tile_hi():
    out = bytearray(W_PAD * H_PAD)
    for i in range(W_PAD * H_PAD // 2):
        b = raw[i]
        out[i*2]     = (b >> 4) & 0xF
        out[i*2 + 1] = b & 0xF
    # crop to logical W×H
    cropped = bytearray(W_LOGICAL * H_LOGICAL)
    for y in range(H_LOGICAL):
        cropped[y*W_LOGICAL:(y+1)*W_LOGICAL] = out[y*W_PAD : y*W_PAD + W_LOGICAL]
    return bytes(cropped)


def layout_no_tile_lo():
    out = bytearray(W_PAD * H_PAD)
    for i in range(W_PAD * H_PAD // 2):
        b = raw[i]
        out[i*2]     = b & 0xF
        out[i*2 + 1] = (b >> 4) & 0xF
    cropped = bytearray(W_LOGICAL * H_LOGICAL)
    for y in range(H_LOGICAL):
        cropped[y*W_LOGICAL:(y+1)*W_LOGICAL] = out[y*W_PAD : y*W_PAD + W_LOGICAL]
    return bytes(cropped)


render_indices(layout_no_tile_hi(), W_LOGICAL, H_LOGICAL, "A_no_tile_hi")
render_indices(layout_no_tile_lo(), W_LOGICAL, H_LOGICAL, "A_no_tile_lo")


# --- Layout B: 8×2 pixel blocks, each block = 8 bytes ---------------
# 8 wide × 2 tall, row-major within block, row-major between blocks.
def layout_8x2():
    BW = W_PAD // 8
    BH = H_PAD // 2
    out = bytearray(W_PAD * H_PAD)
    for by in range(BH):
        for bx in range(BW):
            blk = raw[(by * BW + bx) * 8 : (by * BW + bx) * 8 + 8]
            nibs = []
            for b in blk:
                nibs.append((b >> 4) & 0xF)
                nibs.append(b & 0xF)
            for ly in range(2):
                for lx in range(8):
                    yy = by * 2 + ly
                    xx = bx * 8 + lx
                    if yy < H_PAD and xx < W_PAD:
                        out[yy * W_PAD + xx] = nibs[ly * 8 + lx]
    cropped = bytearray(W_LOGICAL * H_LOGICAL)
    for y in range(H_LOGICAL):
        cropped[y*W_LOGICAL:(y+1)*W_LOGICAL] = out[y*W_PAD : y*W_PAD + W_LOGICAL]
    return bytes(cropped)


render_indices(layout_8x2(), W_LOGICAL, H_LOGICAL, "B_8x2_blocks")


# --- Layout C: 4×4 pixel blocks but COLUMN-MAJOR within block -----
def layout_4x4_colmajor():
    BW = W_PAD // 4
    BH = H_PAD // 4
    out = bytearray(W_PAD * H_PAD)
    for by in range(BH):
        for bx in range(BW):
            blk = raw[(by * BW + bx) * 8 : (by * BW + bx) * 8 + 8]
            nibs = []
            for b in blk:
                nibs.append((b >> 4) & 0xF)
                nibs.append(b & 0xF)
            # column-major: pixel 0 = (0,0), 1 = (1,0), 2 = (2,0), 3 = (3,0),
            # 4 = (0,1), 5 = (1,1), ...
            for k in range(16):
                col = k // 4
                row = k % 4
                yy = by * 4 + row
                xx = bx * 4 + col
                if yy < H_PAD and xx < W_PAD:
                    out[yy * W_PAD + xx] = nibs[k]
    cropped = bytearray(W_LOGICAL * H_LOGICAL)
    for y in range(H_LOGICAL):
        cropped[y*W_LOGICAL:(y+1)*W_LOGICAL] = out[y*W_PAD : y*W_PAD + W_LOGICAL]
    return bytes(cropped)


render_indices(layout_4x4_colmajor(), W_LOGICAL, H_LOGICAL, "C_4x4_colmajor")


# --- Layout D: 4×4 blocks, Morton/Z-order within ------------------
def layout_4x4_morton():
    BW = W_PAD // 4
    BH = H_PAD // 4
    out = bytearray(W_PAD * H_PAD)
    # Morton order for a 4x4 grid (16 pixels):
    # Pixel index -> (x, y):
    # 0=(0,0) 1=(1,0) 2=(0,1) 3=(1,1) | 4=(2,0) 5=(3,0) 6=(2,1) 7=(3,1)
    # 8=(0,2) 9=(1,2) A=(0,3) B=(1,3) | C=(2,2) D=(3,2) E=(2,3) F=(3,3)
    morton_to_xy = [
        (0,0),(1,0),(0,1),(1,1),(2,0),(3,0),(2,1),(3,1),
        (0,2),(1,2),(0,3),(1,3),(2,2),(3,2),(2,3),(3,3),
    ]
    for by in range(BH):
        for bx in range(BW):
            blk = raw[(by * BW + bx) * 8 : (by * BW + bx) * 8 + 8]
            nibs = []
            for b in blk:
                nibs.append((b >> 4) & 0xF)
                nibs.append(b & 0xF)
            for k in range(16):
                lx, ly = morton_to_xy[k]
                yy = by * 4 + ly
                xx = bx * 4 + lx
                if yy < H_PAD and xx < W_PAD:
                    out[yy * W_PAD + xx] = nibs[k]
    cropped = bytearray(W_LOGICAL * H_LOGICAL)
    for y in range(H_LOGICAL):
        cropped[y*W_LOGICAL:(y+1)*W_LOGICAL] = out[y*W_PAD : y*W_PAD + W_LOGICAL]
    return bytes(cropped)


render_indices(layout_4x4_morton(), W_LOGICAL, H_LOGICAL, "D_4x4_morton")
