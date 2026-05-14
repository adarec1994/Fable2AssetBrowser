"""Decode PF=99 partial output as BC3 (DXT5) at 1664x960 and as
X360-tiled BC3 with untiling — see if either produces a coherent
image."""
from PIL import Image
import struct

raw = open("pf99_partial.bin", "rb").read()
# Pad to expected 1,597,440 if short
TGT = 1597440
if len(raw) < TGT:
    raw = raw + b"\x00" * (TGT - len(raw))

W, H = 1664, 960
BW, BH = W // 4, H // 4

def rgb565_to_rgb(c):
    r = ((c >> 11) & 0x1F) * 255 // 31
    g = ((c >>  5) & 0x3F) * 255 // 63
    b = ( c        & 0x1F) * 255 // 31
    return (r, g, b)

def decode_bc3(blocks, w, h):
    bw = w // 4; bh = h // 4
    out = bytearray(w * h * 4)
    for by in range(bh):
        for bx in range(bw):
            off = (by * bw + bx) * 16
            blk = blocks[off:off+16]
            # Alpha: 2 bytes endpoints + 6 bytes indices
            a0 = blk[0]; a1 = blk[1]
            a_idx = int.from_bytes(blk[2:8], "little")
            if a0 > a1:
                ramp = [a0, a1] + [((7-i)*a0 + i*a1)//7 for i in range(1, 7)]
            else:
                ramp = [a0, a1] + [((5-i)*a0 + i*a1)//5 for i in range(1, 5)] + [0, 255]
            c0, c1 = struct.unpack_from("<HH", blk, 8)
            cidx = int.from_bytes(blk[12:16], "little")
            c0_rgb = rgb565_to_rgb(c0)
            c1_rgb = rgb565_to_rgb(c1)
            c2 = tuple((2*c0_rgb[k] + c1_rgb[k])//3 for k in range(3))
            c3 = tuple((c0_rgb[k] + 2*c1_rgb[k])//3 for k in range(3))
            colors = [c0_rgb, c1_rgb, c2, c3]
            for py in range(4):
                for px in range(4):
                    ci = (cidx >> (2*(py*4+px))) & 0x3
                    ai = (a_idx >> (3*(py*4+px))) & 0x7
                    r, g, b = colors[ci]
                    a = ramp[ai]
                    x = bx*4 + px; y = by*4 + py
                    if x < w and y < h:
                        i = (y*w + x) * 4
                        out[i+0] = r; out[i+1] = g; out[i+2] = b; out[i+3] = a
    return bytes(out)

print("Decoding as BC3 at 1664x960...")
rgba = decode_bc3(raw[:BW*BH*16], W, H)
Image.frombytes("RGBA", (W, H), rgba).save("pf99_bc3_decoded.png")
print("  wrote pf99_bc3_decoded.png")

# Also save just alpha channel
print("Saving alpha plane as grayscale...")
a_only = bytes(rgba[3::4])
Image.frombytes("L", (W, H), a_only).save("pf99_bc3_alpha.png")
print("  wrote pf99_bc3_alpha.png")
