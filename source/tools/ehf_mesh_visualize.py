"""Render each of the 8 vec3 slots from ehf_mesh_dump as a top-down
heightmap PNG, so we can see which slot(s) hold real position data."""
from PIL import Image
import struct
from pathlib import Path
import sys

def be_u32(b, o): return struct.unpack_from(">I", b, o)[0]
def be_f32(b, o): return struct.unpack_from(">f", b, o)[0]

def skip_tex(body, p):
    PF  = be_u32(body, p + 0x18)
    mt  = be_u32(body, p + 0x20)
    raw = be_u32(body, p + mt)
    if PF == 98: return p + mt + 4 + raw
    return p + mt + 8 + be_u32(body, p + mt + 4)

ehf_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(
    "cmake-build-debug/extracted/bl_chapter3_heightfield_id_9501a1af.ehf")
blob = ehf_path.read_bytes()
body_off = be_u32(blob, 55)
body = blob[body_off:body_off + be_u32(blob, 59)]

p = 0
p = skip_tex(body, p); p = skip_tex(body, p)
p += 4
tile_count = be_u32(body, p); p += 4

slot_pts = [[] for _ in range(8)]

for tile_i in range(tile_count):
    p += 8
    w_sub = be_u32(body, p); p += 4
    h_sub = be_u32(body, p); p += 4
    cells_start = p
    for cy in range(h_sub):
        for cx in range(w_sub):
            cell = cells_start + ((cy * w_sub) + cx) * 160
            for vi in range(8):
                voff = cell + 64 + vi * 12
                x = be_f32(body, voff + 0)
                y = be_f32(body, voff + 4)
                z = be_f32(body, voff + 8)
                if abs(x) > 1e6 or abs(y) > 1e6 or abs(z) > 1e6:
                    continue
                slot_pts[vi].append((x, y, z))
    p = cells_start + w_sub * h_sub * 160 + 24

# Find global bounds
all_pts = [pt for slot in slot_pts for pt in slot]
gx_min = min(p[0] for p in all_pts)
gx_max = max(p[0] for p in all_pts)
gy_min = min(p[1] for p in all_pts)
gy_max = max(p[1] for p in all_pts)
gz_min = min(p[2] for p in all_pts)
gz_max = max(p[2] for p in all_pts)

print(f"Bounds: X={gx_min}..{gx_max}  Y={gy_min}..{gy_max}  Z={gz_min}..{gz_max}")

# Per slot, render an image where pixel = height at that XY
RES = 400
for si in range(8):
    pts = slot_pts[si]
    img = Image.new("RGB", (RES, RES), (20, 20, 30))
    pix = img.load()
    for x, y, z in pts:
        px = int((x - gx_min) / (gx_max - gx_min + 1) * (RES - 1))
        py = int((y - gy_min) / (gy_max - gy_min + 1) * (RES - 1))
        pz = int((z - gz_min) / (gz_max - gz_min + 1) * 255)
        py = RES - 1 - py
        for dx in (0,):
            for dy in (0,):
                ix, iy = px + dx, py + dy
                if 0 <= ix < RES and 0 <= iy < RES:
                    pix[ix, iy] = (pz, pz, pz)
    img.save(f"ehf_slot{si}_heatmap.png")
    print(f"  wrote ehf_slot{si}_heatmap.png  ({len(pts)} pts)")

# Combined
img = Image.new("RGB", (RES, RES), (20, 20, 30))
pix = img.load()
colors = [
    (255, 80, 80),  (80, 255, 80),  (80, 80, 255),  (255, 255, 80),
    (255, 80, 255), (80, 255, 255), (255, 200, 80), (200, 80, 255),
]
for si in range(8):
    for x, y, z in slot_pts[si]:
        px = int((x - gx_min) / (gx_max - gx_min + 1) * (RES - 1))
        py = int((y - gy_min) / (gy_max - gy_min + 1) * (RES - 1))
        py = RES - 1 - py
        if 0 <= px < RES and 0 <= py < RES:
            pix[px, py] = colors[si]
img.save("ehf_all_slots_colored.png")
print(f"  wrote ehf_all_slots_colored.png")
