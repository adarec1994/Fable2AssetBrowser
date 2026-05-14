"""Dump the actual chunk grid origins and extents from chapter3's .ehf
plus the .ghf-derived terrain bounds, so we can see whether the
chunk grid even covers the same area as the heightfield mesh."""
from __future__ import annotations
import struct, sys, gzip
from pathlib import Path

def be_u32(b, o): return struct.unpack_from(">I", b, o)[0]
def be_f32(b, o): return struct.unpack_from(">f", b, o)[0]

base = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(
    "cmake-build-debug/extracted")

ehf_path = base / "bl_chapter3_heightfield_id_9501a1af.ehf"
ghf_path = base / "bl_chapter3_heightfield_id_9501a1af.ghf"

# .ghf bounds
if ghf_path.exists():
    raw = ghf_path.read_bytes()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    tile = be_f32(raw, 0x00)
    W    = be_u32(raw, 0x0C)
    H    = be_u32(raw, 0x10)
    print(f"# .ghf: {W} × {H} cells, tile_size={tile}")
    print(f"#       world extents: {W*tile:.0f} × {H*tile:.0f} wu")
    # Scan heights
    cells = W * H
    hs = struct.unpack_from(f">{cells}f" if False else ">f", raw, 0x14)
    # That's just one — let me sample
    h_min, h_max = 1e30, -1e30
    for i in range(0, cells, max(1, cells // 1000)):
        h = be_f32(raw, 0x14 + i * 14)
        h_min = min(h_min, h)
        h_max = max(h_max, h)
    print(f"#       height range (sampled): [{h_min:.1f}, {h_max:.1f}]")
    print()
else:
    print(f"# (no .ghf found at {ghf_path})")

# .ehf chunk grid
blob = ehf_path.read_bytes()
body_off = be_u32(blob, 55)
body = blob[body_off:body_off + be_u32(blob, 59)]

def skip_tex(p):
    PF  = be_u32(body, p + 0x18)
    mt  = be_u32(body, p + 0x20)
    raw = be_u32(body, p + mt)
    if PF == 98: return p + mt + 4 + raw
    return p + mt + 8 + be_u32(body, p + mt + 4)

p = 0
p = skip_tex(p); p = skip_tex(p); p += 4
cnt = be_u32(body, p); p += 4
for k in range(cnt):
    p += 8
    w_sub = be_u32(body, p); p += 4
    h_sub = be_u32(body, p); p += 4
    p += w_sub * h_sub * 160 + 24
p += 4
cnt2 = be_u32(body, p); p += 4
p += cnt2 * 18

art = body.find(b"art\\")
p = art - 4
lc = be_u32(body, p); p += 4
for k in range(lc):
    for _ in range(3): p = body.index(b"\x00", p) + 1
    p += 12
    for _ in range(3): p = body.index(b"\x00", p) + 1
    p += 12

db0_cnt = be_u32(body, p); p += 4
for k in range(db0_cnt):
    p = skip_tex(p)

W = be_u32(body, p); p += 4
H = be_u32(body, p); p += 4
print(f"# .ehf: chunk grid {W} × {H}")

# Walk chunks and collect origins/extents/layer counts
ox_min = oz_min = 1e30; ox_max = oz_max = -1e30
ex_min = ez_min = 1e30; ex_max = ez_max = -1e30
layer_count_hist = {}
for ci in range(W * H):
    ox = be_f32(body, p); p += 4
    oy = be_f32(body, p); p += 4
    oz = be_f32(body, p); p += 4
    ex = be_f32(body, p); p += 4
    ey = be_f32(body, p); p += 4
    ez = be_f32(body, p); p += 4
    lc = be_u32(body, p); p += 4
    p += lc * 24    # skip layer bytes
    ox_min = min(ox_min, ox); ox_max = max(ox_max, ox)
    oz_min = min(oz_min, oz); oz_max = max(oz_max, oz)
    ex_min = min(ex_min, ex); ex_max = max(ex_max, ex)
    ez_min = min(ez_min, ez); ez_max = max(ez_max, ez)
    layer_count_hist[lc] = layer_count_hist.get(lc, 0) + 1
    if ci < 3 or ci == W * H - 1 or (ci % (W*H // 4) == 0):
        print(f"  chunk[{ci}] origin=({ox:+8.1f},{oy:+8.1f},{oz:+8.1f}) "
              f"extent=({ex:+8.1f},{ey:+8.1f},{ez:+8.1f}) layers={lc}")

print()
print(f"# chunk origin   X range: [{ox_min:.1f}, {ox_max:.1f}]")
print(f"# chunk origin   Z range: [{oz_min:.1f}, {oz_max:.1f}]")
print(f"# chunk extent   X range: [{ex_min:.1f}, {ex_max:.1f}]")
print(f"# chunk extent   Z range: [{ez_min:.1f}, {ez_max:.1f}]")
print(f"# chunk grid total area: "
      f"{ox_max - ox_min + ex_max:.0f} × {oz_max - oz_min + ez_max:.0f} wu")
print(f"# layer count histogram: {layer_count_hist}")
