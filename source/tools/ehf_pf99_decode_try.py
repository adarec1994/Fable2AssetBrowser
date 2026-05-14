"""Inflate and visualize the PF=99 DB0[0] blob from a .ehf to figure
out what data it actually contains.

Tries several common Xbox360 texture/array layouts and saves whichever
parse as PNGs in the current directory, so we can eyeball which
interpretation looks like terrain albedo / splat / something else.
"""
from __future__ import annotations
import struct, sys, zlib
from pathlib import Path

def be_u32(b, o): return struct.unpack_from(">I", b, o)[0]

def walk_to_db0(blob):
    body_off = be_u32(blob, 55)
    body_size = be_u32(blob, 59)
    body = blob[body_off:body_off + body_size]
    pos = 0

    def skip_tex(p):
        magic = be_u32(body, p)
        assert magic == 0xFFFFFFFE
        PF  = be_u32(body, p + 0x18)
        mt  = be_u32(body, p + 0x20)
        raw = be_u32(body, p + mt)
        if PF == 98:
            return p + mt + 4 + raw
        comp = be_u32(body, p + mt + 4)
        return p + mt + 8 + comp

    pos = skip_tex(pos)
    pos = skip_tex(pos)
    pos += 4
    cnt = be_u32(body, pos); pos += 4
    for k in range(cnt):
        pos += 8
        w_sub = be_u32(body, pos); pos += 4
        h_sub = be_u32(body, pos); pos += 4
        pos += w_sub * h_sub * 160 + 24
    pos += 4
    cnt2 = be_u32(body, pos); pos += 4
    pos += cnt2 * 18

    art = body.find(b"art\\")
    pos = art - 4
    lc = be_u32(body, pos); pos += 4
    for k in range(lc):
        for _ in range(3):
            end = body.index(b"\x00", pos); pos = end + 1
        pos += 12
        for _ in range(3):
            end = body.index(b"\x00", pos); pos = end + 1
        pos += 12

    db0_cnt = be_u32(body, pos); pos += 4
    print(f"  DB0 count = {db0_cnt}")
    return body, pos


def decode_tex(body, pos):
    W   = be_u32(body, pos + 0x10)
    H   = be_u32(body, pos + 0x14)
    PF  = be_u32(body, pos + 0x18)
    mt  = be_u32(body, pos + 0x20)
    raw_size = be_u32(body, pos + mt)
    comp_size = be_u32(body, pos + mt + 4)
    print(f"  W={W} H={H} PF={PF} raw_size={raw_size:,} comp_size={comp_size:,}")
    print(f"  header @ 0x{pos:x}  mt={hex(mt)}  data nominal start @ 0x{pos+mt+8:x}")

    # Hex-dump the 32 bytes around the nominal data start so we can see
    # what's there.
    region_start = pos + mt
    print(f"  bytes around mt-area (header tail + start of payload):")
    for i in range(0, 64, 32):
        b = body[region_start + i : region_start + i + 32]
        print(f"    +0x{i:04x}: " + " ".join(f"{c:02x}" for c in b))

    # Scan forward from data nominal start for zlib magic (0x78 0xDA / 9C / 01)
    nominal = pos + mt + 8
    for ofs in range(-16, 64):
        p = nominal + ofs
        if p < 0 or p + 2 > len(body): continue
        if body[p] == 0x78 and body[p+1] in (0xDA, 0x9C, 0x01):
            print(f"  zlib magic at offset {ofs:+d} from nominal -> 0x{p:x}")
            data = body[p : p + comp_size + 32]
            try:
                d = zlib.decompressobj()
                inflated = d.decompress(data)
                inflated += d.flush()
                consumed = len(data) - len(d.unused_data)
                print(f"  inflated {len(inflated):,}B from {consumed:,}B at +{ofs:+d}")
                return W, H, PF, inflated
            except Exception as e:
                print(f"  inflate failed @ +{ofs:+d}: {e}")
                continue
    raise RuntimeError("could not find zlib stream")


def save_gray(name, w, h, raw):
    try:
        from PIL import Image
        if len(raw) < w * h:
            print(f"  {name}: not enough data ({len(raw)} < {w*h})")
            return
        img = Image.frombytes("L", (w, h), bytes(raw[:w*h]))
        img.save(name)
        print(f"  wrote {name}")
    except ImportError:
        print(f"  (PIL not installed)")

def save_rgb_planar(name, w, h, raw):
    """Try 3 planes (RGB) of W*H each."""
    try:
        from PIL import Image
        plane = w * h
        if len(raw) < 3 * plane:
            return
        out = bytearray(w * h * 3)
        for i in range(plane):
            out[i*3+0] = raw[0*plane + i]
            out[i*3+1] = raw[1*plane + i]
            out[i*3+2] = raw[2*plane + i]
        img = Image.frombytes("RGB", (w, h), bytes(out))
        img.save(name)
        print(f"  wrote {name}")
    except ImportError:
        pass

def save_dxt(name, w, h, raw, mode):
    """Decode BC1/BC3 and save."""
    try:
        from PIL import Image
        import struct as st

        block = 8 if mode == "bc1" else 16
        bx, by = w // 4, h // 4
        expected = bx * by * block
        if len(raw) < expected:
            print(f"  {name}: not enough data ({len(raw)} < {expected})")
            return

        out = bytearray(w * h * 4)
        for by_i in range(by):
            for bx_i in range(bx):
                off = (by_i * bx + bx_i) * block
                # BC1/BC3 color block layout:
                # bc1: 2 u16 colors + 32-bit indices = 8 bytes
                # bc3: 8 bytes alpha + bc1-style 8 bytes color = 16 bytes
                color_off = off + (8 if mode == "bc3" else 0)
                c0, c1, idx = st.unpack_from("<HHI", raw, color_off)
                # Convert RGB565 -> RGB
                def rgb565_to_rgb(c):
                    r = ((c >> 11) & 0x1F) * 255 // 31
                    g = ((c >>  5) & 0x3F) * 255 // 63
                    b = ( c        & 0x1F) * 255 // 31
                    return r, g, b
                c0_rgb = rgb565_to_rgb(c0)
                c1_rgb = rgb565_to_rgb(c1)
                if c0 > c1:
                    c2 = tuple((2*c0_rgb[k] + c1_rgb[k]) // 3 for k in range(3))
                    c3 = tuple((c0_rgb[k] + 2*c1_rgb[k]) // 3 for k in range(3))
                else:
                    c2 = tuple((c0_rgb[k] + c1_rgb[k]) // 2 for k in range(3))
                    c3 = (0, 0, 0)
                colors = [c0_rgb, c1_rgb, c2, c3]
                for py in range(4):
                    for px in range(4):
                        ci = (idx >> (2 * (py * 4 + px))) & 0x3
                        r, g, b = colors[ci]
                        x = bx_i * 4 + px
                        y = by_i * 4 + py
                        i = (y * w + x) * 4
                        out[i+0] = r
                        out[i+1] = g
                        out[i+2] = b
                        out[i+3] = 255
        img = Image.frombytes("RGBA", (w, h), bytes(out))
        img.save(name)
        print(f"  wrote {name}")
    except ImportError:
        print(f"  (PIL missing for {name})")
    except Exception as e:
        print(f"  {name} failed: {e}")


def main():
    p = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(
        "cmake-build-debug/extracted/bl_chapter3_heightfield_id_9501a1af.ehf")
    blob = p.read_bytes()
    print(f"# {p.name}")
    body, db0_pos = walk_to_db0(blob)
    W, H, PF, raw = decode_tex(body, db0_pos)

    print()
    print(f"  raw[0..32]: {raw[:32].hex()}")
    print(f"  raw[len-32..]: {raw[-32:].hex()}")
    print()

    print("Try interpretations:")
    # Just save various candidates
    save_gray(f"pf99_g_{W}x{H}.png", W, H, raw)
    save_gray(f"pf99_g_1664x960.png", 1664, 960, raw)
    save_gray(f"pf99_g_1650x968.png", 1650, 968, raw)  # 1650 * 968 = 1597200 — close
    save_rgb_planar(f"pf99_rgbplanar_{W}x{H}.png", W, H, raw)

    # BC3 at 1664x960 fits exactly (416*240*16 = 1597440)
    save_dxt(f"pf99_bc3_1664x960.png", 1664, 960, raw, "bc3")
    save_dxt(f"pf99_bc1_1664x960.png", 1664, 960, raw, "bc1")

    # Maybe data is in 64x64 tiled order (Xbox 360 texture tiling)?
    # Save first 256x256 chunk as gray.
    save_gray("pf99_g_first256.png", 256, 256, raw[:256*256])

    Path("pf99_raw.bin").write_bytes(raw)
    print(f"  saved pf99_raw.bin ({len(raw):,} bytes)")


if __name__ == "__main__":
    main()
