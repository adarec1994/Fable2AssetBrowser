"""Dump what comes right after the 2 textures + 1 float in an .ehf
body — i.e. the mesh tile vector that the validated parser walks
through as sub_82A850A0.

The MDL precedent is "textures then positions", so look at the bytes
and see if anything in there looks like vertex positions / UVs.

Each tile starts with: f_a, f_b, w_sub (u32), h_sub (u32).
Then w_sub * h_sub cells of 160 bytes each:
  - 4 × vec4 (16B each)
  - 8 × vec3 (12B each)
Then 24-byte trailer.
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
print(f"# {ehf_path.name}  body={len(body):,}B")

# Skip tex[0..1]
p = 0
p = skip_tex(body, p)
p = skip_tex(body, p)
state_176 = be_f32(body, p); p += 4
print(f"state[+176] float = {state_176}")

# Mesh tile vector header
tile_count = be_u32(body, p); p += 4
print(f"sub_82A850A0 tile count = {tile_count}")

for tile_i in range(min(tile_count, 3)):
    print()
    print(f"=== tile[{tile_i}] @ body 0x{p:x} ===")
    tile_start = p
    f_a   = be_f32(body, p);     p += 4
    f_b   = be_f32(body, p);     p += 4
    w_sub = be_u32(body, p);     p += 4
    h_sub = be_u32(body, p);     p += 4
    print(f"  f_a={f_a}  f_b={f_b}  w_sub={w_sub}  h_sub={h_sub}")
    print(f"  cells = {w_sub}*{h_sub} = {w_sub*h_sub}  @ body 0x{p:x}")

    if w_sub * h_sub == 0:
        continue

    # First cell of this tile
    cell_start = p
    print(f"  --- first cell raw 160 bytes ---")
    raw = body[cell_start:cell_start + 160]
    for off in range(0, 160, 16):
        hex_p = " ".join(f"{b:02x}" for b in raw[off:off+16])
        floats = struct.unpack_from(">4f", raw, off)
        ints   = struct.unpack_from(">4I", raw, off)
        print(f"    +0x{off:02x}: {hex_p}")
        print(f"            floats = ({floats[0]:+.4f}, {floats[1]:+.4f}, "
              f"{floats[2]:+.4f}, {floats[3]:+.4f})")

    # Interpret as 4 vec4 + 8 vec3
    print(f"  --- interpreted ---")
    for vi in range(4):
        off = vi * 16
        a, b, c, d = struct.unpack_from(">4f", raw, off)
        print(f"    vec4[{vi}] = ({a:+9.3f}, {b:+9.3f}, {c:+9.3f}, {d:+9.3f})")
    voff = 64
    for vi in range(8):
        off = voff + vi * 12
        a, b, c = struct.unpack_from(">3f", raw, off)
        print(f"    vec3[{vi}] = ({a:+9.3f}, {b:+9.3f}, {c:+9.3f})")

    # Show last cell too
    last_cell = cell_start + (w_sub * h_sub - 1) * 160
    if last_cell != cell_start:
        print(f"  --- last cell (#{w_sub*h_sub-1}) interpreted ---")
        for vi in range(4):
            off = last_cell + vi * 16
            a, b, c, d = struct.unpack_from(">4f", body, off)
            print(f"    vec4[{vi}] = ({a:+9.3f}, {b:+9.3f}, {c:+9.3f}, {d:+9.3f})")
        for vi in range(8):
            off = last_cell + 64 + vi * 12
            a, b, c = struct.unpack_from(">3f", body, off)
            print(f"    vec3[{vi}] = ({a:+9.3f}, {b:+9.3f}, {c:+9.3f})")

    # Skip past cells + trailer
    p = cell_start + w_sub * h_sub * 160 + 24

    # Trailer
    trail_off = cell_start + w_sub * h_sub * 160
    print(f"  --- 24B trailer @ body 0x{trail_off:x} ---")
    trail = body[trail_off:trail_off + 24]
    print(f"    hex:    " + " ".join(f"{b:02x}" for b in trail))
    print(f"    floats: " + str(struct.unpack(">6f", trail)))
