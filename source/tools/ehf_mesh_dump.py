"""Walk every mesh-tile in an .ehf body and dump the 8 vec3s per cell
to a Wavefront .obj as point cloud, so we can see whether the data
forms a coherent terrain shape.
"""
from __future__ import annotations
import struct, sys
from pathlib import Path

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
p = skip_tex(body, p)
p = skip_tex(body, p)
p += 4   # state[+176] float
tile_count = be_u32(body, p); p += 4

# Save 8 separate OBJs — one per "slot" of the 8 vec3s in each cell.
# That way if e.g. slot 0 is X-positions across the grid, slot 0's obj
# will show a coherent grid; the others won't.

# Also save one combined obj with all 8 slots merged.
slot_pts = [[] for _ in range(8)]
slot_with_tile_idx = [[] for _ in range(8)]

for tile_i in range(tile_count):
    p += 8  # f_a, f_b
    w_sub = be_u32(body, p); p += 4
    h_sub = be_u32(body, p); p += 4
    cells_start = p
    for cy in range(h_sub):
        for cx in range(w_sub):
            cell = cells_start + ((cy * w_sub) + cx) * 160
            # Skip 4 vec4s (64 bytes), then 8 vec3s
            for vi in range(8):
                voff = cell + 64 + vi * 12
                x = be_f32(body, voff + 0)
                y = be_f32(body, voff + 4)
                z = be_f32(body, voff + 8)
                # Skip insane sentinel values
                if abs(x) > 1e6 or abs(y) > 1e6 or abs(z) > 1e6:
                    continue
                slot_pts[vi].append((x, y, z))
                slot_with_tile_idx[vi].append((x, y, z, tile_i, cx, cy))
    p = cells_start + w_sub * h_sub * 160 + 24

print(f"tiles: {tile_count}")
for i in range(8):
    print(f"  slot[{i}]: {len(slot_pts[i])} points  "
          f"X range [{min(p[0] for p in slot_pts[i]) if slot_pts[i] else 0:.1f}, "
          f"{max(p[0] for p in slot_pts[i]) if slot_pts[i] else 0:.1f}]  "
          f"Y [{min(p[1] for p in slot_pts[i]) if slot_pts[i] else 0:.1f}, "
          f"{max(p[1] for p in slot_pts[i]) if slot_pts[i] else 0:.1f}]  "
          f"Z [{min(p[2] for p in slot_pts[i]) if slot_pts[i] else 0:.1f}, "
          f"{max(p[2] for p in slot_pts[i]) if slot_pts[i] else 0:.1f}]")

# Save one OBJ per slot
for si in range(8):
    fn = f"ehf_slot{si}.obj"
    with open(fn, "w") as f:
        f.write(f"# slot {si} from {ehf_path.name}\n")
        for x, y, z in slot_pts[si]:
            f.write(f"v {x} {z} {y}\n")  # swap Y/Z so terrain is "up" in viewer
    print(f"  wrote {fn} ({len(slot_pts[si])} verts)")

# Combined
with open("ehf_all_slots.obj", "w") as f:
    f.write(f"# all 8 slots from {ehf_path.name}\n")
    for si in range(8):
        for x, y, z in slot_pts[si]:
            f.write(f"v {x} {z} {y}\n")
print(f"  wrote ehf_all_slots.obj")
