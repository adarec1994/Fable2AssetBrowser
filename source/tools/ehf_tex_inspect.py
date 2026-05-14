"""Walk every .tex blob in an .ehf body without using the anchor skip,
so we can see what textures[2..3] actually are.

Goal: figure out if textures[2..3] is the baked terrain albedo / color
texture we've been ignoring.
"""
from __future__ import annotations
import struct, sys
from pathlib import Path

def be_u32(b, o): return struct.unpack_from(">I", b, o)[0]
def be_f32(b, o): return struct.unpack_from(">f", b, o)[0]

PF_NAMES = {
    24: "BC1?/lightmap-shaped (1B/pix)",
    35: "BC3-ish",
    36: "BC2",
    39: "BC3",
    40: "BC5 (normal)",
    98: "uncompressed",
    99: "blob (splat?)",
}

def walk_texture(body, pos):
    """Read .tex header at pos.  Return (info_dict, new_pos)."""
    magic = be_u32(body, pos)
    if magic != 0xFFFFFFFE:
        raise ValueError(f"bad .tex magic at 0x{pos:x}: 0x{magic:08x}")
    W   = be_u32(body, pos + 0x10)
    H   = be_u32(body, pos + 0x14)
    PF  = be_u32(body, pos + 0x18)
    mt  = be_u32(body, pos + 0x20)
    raw = be_u32(body, pos + mt)
    if PF == 98:
        # Uncompressed: raw bytes follow raw_size
        new_pos = pos + mt + 4 + raw
        comp = 0
    else:
        comp = be_u32(body, pos + mt + 4)
        new_pos = pos + mt + 8 + comp
    return ({"W": W, "H": H, "PF": PF, "raw_size": raw, "comp_size": comp,
             "tex_start": pos, "tex_end": new_pos,
             "tex_total": new_pos - pos}, new_pos)


def inspect(path: Path):
    blob = path.read_bytes()
    assert blob[:23] == b"HeightFieldGraphicsFile", "not an .ehf"
    body_off = be_u32(blob, 55)
    body_size = be_u32(blob, 59)
    body = blob[body_off:body_off + body_size]

    print(f"# {path.name}")
    print(f"  body: {len(body):,} bytes (off=0x{body_off:x})")
    print()

    # Walk each step but report every texture we see.
    pos = 0

    print("=== Step 1: tex[0..1] ===")
    for i in range(2):
        info, pos = walk_texture(body, pos)
        nm = PF_NAMES.get(info['PF'], "?")
        print(f"  tex[{i}] @0x{info['tex_start']:08x}  "
              f"W={info['W']:4d} H={info['H']:4d}  PF={info['PF']:3d} ({nm}) "
              f"raw={info['raw_size']:9,} comp={info['comp_size']:9,} "
              f"size={info['tex_total']:9,}B")

    print()
    print("=== Step 2-4: float + 2 vectors ===")
    pos += 4  # state[+176] float
    cnt = be_u32(body, pos); pos += 4
    print(f"  82A850A0 count = {cnt}")
    for k in range(cnt):
        f_a = be_f32(body, pos);     pos += 4
        f_b = be_f32(body, pos);     pos += 4
        w_sub = be_u32(body, pos);   pos += 4
        h_sub = be_u32(body, pos);   pos += 4
        pos += w_sub * h_sub * 160 + 24
    pos += 4  # 82A860E8 float
    cnt2 = be_u32(body, pos); pos += 4
    print(f"  82A860E8 count = {cnt2}")
    pos += cnt2 * 18
    print(f"  after step 4 @0x{pos:x}")

    print()
    print("=== Step 5: tex[2..3] (the ones we were skipping!) ===")
    # Try reading as many .tex blobs as look valid before we hit the LOD
    # vector (look for 'art\').
    art_anchor = body.find(b"art\\")
    print(f"  art\\ anchor at body 0x{art_anchor:x}")
    tex_idx = 2
    while pos < art_anchor - 4:
        magic = be_u32(body, pos)
        if magic != 0xFFFFFFFE:
            print(f"  tex[{tex_idx}] STOP: no magic at 0x{pos:x} "
                  f"(saw 0x{magic:08x}); {art_anchor - 4 - pos} bytes left to anchor")
            break
        info, pos = walk_texture(body, pos)
        nm = PF_NAMES.get(info['PF'], "?")
        print(f"  tex[{tex_idx}] @0x{info['tex_start']:08x}  "
              f"W={info['W']:4d} H={info['H']:4d}  PF={info['PF']:3d} ({nm}) "
              f"raw={info['raw_size']:9,} comp={info['comp_size']:9,} "
              f"size={info['tex_total']:9,}B  -> next @0x{pos:x}")
        tex_idx += 1

    # If we got stuck before the LOD anchor, jump.
    if pos < art_anchor - 4:
        gap = art_anchor - 4 - pos
        print(f"  ... jumping over {gap}B of unknown data before LOD anchor")
        pos = art_anchor - 4

    print()
    print("=== Step 6: LOD palette ===")
    lc = be_u32(body, pos); pos += 4
    print(f"  LOD count = {lc}")
    for k in range(lc):
        strs = []
        for _ in range(3):
            end = body.index(b"\x00", pos)
            strs.append(body[pos:end].decode("ascii", errors="replace"))
            pos = end + 1
        pos += 12
        for _ in range(3):
            end = body.index(b"\x00", pos)
            strs.append(body[pos:end].decode("ascii", errors="replace"))
            pos = end + 1
        pos += 12
        if k < 3 or k == lc - 1:
            print(f"  LOD[{k}] = {strs}")

    print()
    print("=== Step 7: sub_82A85DB0 textures ===")
    db0_cnt = be_u32(body, pos); pos += 4
    print(f"  count = {db0_cnt}")
    for k in range(db0_cnt):
        info, pos = walk_texture(body, pos)
        nm = PF_NAMES.get(info['PF'], "?")
        print(f"  DB0[{k}] @0x{info['tex_start']:08x}  "
              f"W={info['W']:4d} H={info['H']:4d}  PF={info['PF']:3d} ({nm}) "
              f"raw={info['raw_size']:9,} comp={info['comp_size']:9,} "
              f"size={info['tex_total']:9,}B")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        ehf = Path(sys.argv[1])
    else:
        ehf = Path("cmake-build-debug/extracted/"
                   "bl_chapter3_heightfield_id_9501a1af.ehf")
    inspect(ehf)
