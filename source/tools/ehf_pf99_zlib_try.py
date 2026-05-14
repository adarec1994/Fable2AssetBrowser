"""Try several zlib decompression variants on the PF=99 stream from
chapter3's .ehf.

The stream has a valid 78 DA header but zlib.decompress reports
"invalid stored block lengths" partway through.  This usually means
either (a) the comp_size in the header is wrong and we're feeding too
much data, or (b) there's byte-swapping (X360 is BE).
"""
import struct, zlib, sys
from pathlib import Path

def be_u32(b, o): return struct.unpack_from(">I", b, o)[0]

p = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(
    "cmake-build-debug/extracted/bl_chapter3_heightfield_id_9501a1af.ehf")
blob = p.read_bytes()
body_off = be_u32(blob, 55)
body = blob[body_off:body_off + be_u32(blob, 59)]

# Hard-code the DB0[0] position from earlier inspection: header @ 0x1ef6c3
# Actually walk to it properly.
pos = 0
def skip_tex(p):
    PF  = be_u32(body, p + 0x18)
    mt  = be_u32(body, p + 0x20)
    raw = be_u32(body, p + mt)
    if PF == 98: return p + mt + 4 + raw
    return p + mt + 8 + be_u32(body, p + mt + 4)
pos = skip_tex(pos); pos = skip_tex(pos); pos += 4
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
        pos = body.index(b"\x00", pos) + 1
    pos += 12
    for _ in range(3):
        pos = body.index(b"\x00", pos) + 1
    pos += 12
pos += 4   # DB0 count

tex_pos = pos
W   = be_u32(body, tex_pos + 0x10)
H   = be_u32(body, tex_pos + 0x14)
mt  = be_u32(body, tex_pos + 0x20)
raw_size  = be_u32(body, tex_pos + mt)
comp_size = be_u32(body, tex_pos + mt + 4)
data_start = tex_pos + mt + 8
print(f"W={W} H={H} raw_size={raw_size:,} comp_size={comp_size:,}")
print(f"data starts at 0x{data_start:x}; rest of body = {len(body) - data_start} bytes")

# Variant A: try decompressing using decompressobj with chunked reads
data = body[data_start:]
print()
print("Variant A: decompressobj with chunked reads, ignore comp_size:")
d = zlib.decompressobj()
got = bytearray()
try:
    n = 0
    chunk = 4096
    while True:
        c = data[n:n+chunk]
        if not c: break
        got += d.decompress(c)
        n += chunk
    got += d.flush()
    print(f"  OK: inflated to {len(got):,} bytes; raw_size={raw_size:,}; "
          f"unused={len(d.unused_data)}")
except Exception as e:
    print(f"  FAIL after {len(got):,}B of output, error: {e}")
    # Save partial — maybe enough for visual inspection
    Path("pf99_partial.bin").write_bytes(got)
    print(f"  saved pf99_partial.bin ({len(got):,} bytes)")

    # Try several interpretations of the partial.
    try:
        from PIL import Image
        # Pad to full raw_size to attempt full-size view
        padded = got + b"\x00" * max(0, raw_size - len(got))
        # As BC3 at 1664x960
        # (we know 1664 * 240 blocks * 16 = 1597440)
        # Visualizing as BC3 requires decoding, fall back to gray
        # interpretations:
        for (gw, gh) in [(1664, 960), (1650, 968), (1650, 907),
                         (W, H), (1664, 480), (1024, 1460)]:
            need = gw * gh
            if need <= len(padded):
                img = Image.frombytes("L", (gw, gh), bytes(padded[:need]))
                fn = f"pf99_gray_{gw}x{gh}.png"
                img.save(fn)
                print(f"  wrote {fn}")
            need_rgba = gw * gh * 4
            if need_rgba <= len(padded):
                # As RGBA8 — alpha channel is byte 0 in X360 layout
                rgba = bytearray(gw * gh * 4)
                for i in range(gw * gh):
                    a = padded[i*4 + 0]
                    r = padded[i*4 + 1]
                    g = padded[i*4 + 2]
                    b = padded[i*4 + 3]
                    rgba[i*4 + 0] = r
                    rgba[i*4 + 1] = g
                    rgba[i*4 + 2] = b
                    rgba[i*4 + 3] = a
                img = Image.frombytes("RGBA", (gw, gh), bytes(rgba))
                fn = f"pf99_argb_{gw}x{gh}.png"
                img.save(fn)
                print(f"  wrote {fn}")
    except ImportError:
        print("  (PIL not installed)")
    except Exception as ex:
        print(f"  image save error: {ex}")

# Variant B: byte-swap 16-bit pairs first
print()
print("Variant B: byte-swap 16-bit pairs of compressed stream:")
swapped = bytearray(len(data))
for i in range(0, len(data) & ~1, 2):
    swapped[i]   = data[i+1]
    swapped[i+1] = data[i]
try:
    inflated = zlib.decompress(bytes(swapped))
    print(f"  OK: inflated to {len(inflated):,} bytes")
except Exception as e:
    print(f"  FAIL: {e}")

# Variant C: byte-swap 32-bit dwords
print()
print("Variant C: byte-swap 32-bit dwords:")
swapped4 = bytearray(len(data))
for i in range(0, len(data) & ~3, 4):
    swapped4[i+0] = data[i+3]
    swapped4[i+1] = data[i+2]
    swapped4[i+2] = data[i+1]
    swapped4[i+3] = data[i+0]
try:
    inflated = zlib.decompress(bytes(swapped4))
    print(f"  OK: inflated to {len(inflated):,} bytes")
except Exception as e:
    print(f"  FAIL: {e}")

# Variant D: Maybe data is segmented and uses multiple zlib streams
print()
print("Variant D: try multi-stream decompression:")
got = bytearray()
n = 0
streams = 0
while n < len(data):
    if data[n] != 0x78:
        print(f"  no zlib magic at +0x{n:x}, stopping")
        break
    try:
        d = zlib.decompressobj()
        out = d.decompress(data[n:])
        out += d.flush()
        got += out
        consumed = len(data[n:]) - len(d.unused_data)
        n += consumed
        streams += 1
        print(f"  stream {streams}: {consumed:,}B → {len(out):,}B  (total {len(got):,}B)")
        if streams > 50: break
    except Exception as e:
        print(f"  stream {streams + 1} failed at +0x{n:x}: {e}")
        break
print(f"  final total: {streams} streams, {len(got):,}B vs raw_size {raw_size:,}")
if got:
    Path("pf99_multi.bin").write_bytes(got)
    print("  saved pf99_multi.bin")
