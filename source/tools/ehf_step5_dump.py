"""Dump the bytes between sub_82A860E8 and the art\\ anchor for an EHF
to figure out what 'textures[2..3]' actually contain.

For chapter3 this region is huge (157KB), for autumn_1 it's tiny (1.8KB).
That suggests it's NOT always two .tex blobs — the structure has to be
something else.
"""
from __future__ import annotations
import struct, sys
from pathlib import Path

def be_u32(b, o): return struct.unpack_from(">I", b, o)[0]
def be_f32(b, o): return struct.unpack_from(">f", b, o)[0]

def inspect(path: Path):
    blob = path.read_bytes()
    body_off = be_u32(blob, 55)
    body_size = be_u32(blob, 59)
    body = blob[body_off:body_off + body_size]
    print(f"# {path.name}  body={len(body):,}B")

    # Skip tex[0..1]
    pos = 0
    for i in range(2):
        magic = be_u32(body, pos)
        assert magic == 0xFFFFFFFE
        mt = be_u32(body, pos + 0x20)
        PF = be_u32(body, pos + 0x18)
        raw = be_u32(body, pos + mt)
        if PF == 98:
            pos = pos + mt + 4 + raw
        else:
            comp = be_u32(body, pos + mt + 4)
            pos = pos + mt + 8 + comp

    # Step 2: float
    pos += 4
    # Step 3: 82A850A0 - count + N entries
    cnt = be_u32(body, pos); pos += 4
    for k in range(cnt):
        f_a = be_f32(body, pos); pos += 4
        f_b = be_f32(body, pos); pos += 4
        w_sub = be_u32(body, pos); pos += 4
        h_sub = be_u32(body, pos); pos += 4
        pos += w_sub * h_sub * 160 + 24
    # Step 4: 82A860E8 - float + count + N×18B
    pos += 4
    cnt2 = be_u32(body, pos); pos += 4
    pos += cnt2 * 18

    start = pos
    end = body.find(b"art\\") - 4
    region = body[start:end]
    print(f"  step5 region: 0x{start:x} .. 0x{end:x} = {end-start:,} bytes")
    print()

    # Print first 256 bytes hex+ascii
    n = min(len(region), 1024)
    for i in range(0, n, 32):
        hex_part = " ".join(f"{b:02x}" for b in region[i:i+32])
        ascii_part = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in region[i:i+32])
        print(f"  +0x{i:04x}: {hex_part:<96}  {ascii_part}")

    print()
    print("  last 256 bytes before LOD anchor:")
    n2 = min(len(region), 256)
    for i in range(len(region) - n2, len(region), 32):
        hex_part = " ".join(f"{b:02x}" for b in region[i:i+32])
        ascii_part = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in region[i:i+32])
        print(f"  +0x{i:04x}: {hex_part:<96}  {ascii_part}")

    # Try to interpret first 4 bytes as count and see if a tex-magic
    # follows it.
    if len(region) >= 4:
        first_u32 = be_u32(region, 0)
        print()
        print(f"  first u32 = {first_u32}  (0x{first_u32:08x})")
        if first_u32 > 0 and first_u32 < 1000 and len(region) >= 8:
            second = be_u32(region, 4)
            print(f"  second u32 = 0x{second:08x} ({'TEX MAGIC' if second == 0xFFFFFFFE else 'not tex magic'})")

    # If a .tex magic IS at offset 0, walk forward as if it's two
    # textures and see if we hit the LOD anchor exactly.
    print()
    print("  --- walking as 2 textures ---")
    p = 0
    for i in range(2):
        if p + 4 > len(region) or be_u32(region, p) != 0xFFFFFFFE:
            print(f"  tex[2+{i}] no magic at +0x{p:x}")
            break
        W   = be_u32(region, p + 0x10)
        H   = be_u32(region, p + 0x14)
        PF  = be_u32(region, p + 0x18)
        mt  = be_u32(region, p + 0x20)
        if p + mt + 4 > len(region):
            print(f"  tex[2+{i}] header overflows")
            break
        raw = be_u32(region, p + mt)
        if PF == 98:
            new_p = p + mt + 4 + raw
        else:
            if p + mt + 8 > len(region):
                print(f"  tex[2+{i}] needs comp_size past end")
                break
            comp = be_u32(region, p + mt + 4)
            new_p = p + mt + 8 + comp
        print(f"  tex[2+{i}] @ +0x{p:x}  W={W} H={H} PF={PF} raw={raw} "
              f"-> next @ +0x{new_p:x} (region={len(region)})")
        p = new_p
        if p == len(region):
            print(f"  ✓ reached end of step5 region exactly with {i+1} textures")
            break


if __name__ == "__main__":
    if len(sys.argv) > 1:
        inspect(Path(sys.argv[1]))
    else:
        for name in ("bl_chapter3_heightfield_id_9501a1af.ehf",
                     "autumn_1_id_930d14f6.ehf",
                     "autumn_2_id_00054f7b.ehf"):
            p = Path("cmake-build-debug/extracted") / name
            if p.exists():
                inspect(p)
                print()
