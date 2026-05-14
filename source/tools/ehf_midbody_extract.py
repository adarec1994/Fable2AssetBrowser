"""Extract the mid-body section (between the PF=40 BC5 normal map end
and the palette start) for each .ehf.  I earlier dismissed this region
as Havok physics, but the user wants to verify — for chapter3 it's
~530 KB which is a lot of data we haven't accounted for.
"""
import struct
from pathlib import Path

ART = b"art\\"
MAGIC = b"HeightFieldGraphicsFile"


def u32(b, o): return struct.unpack_from(">I", b, o)[0]


for ehf in sorted(Path("cmake-build-debug/extracted").glob("*.ehf")):
    blob = ehf.read_bytes()
    if blob[:23] != MAGIC:
        continue

    body_off = u32(blob, 55)
    body_size = u32(blob, 59)
    body_end = body_off + body_size

    # Find PF=40 BC5 normal map.
    bc5_end = None
    i = body_off
    while i + 0x60 < body_end:
        if (blob[i] == 0xFF and blob[i+1] == 0xFF and
            blob[i+2] == 0xFF and blob[i+3] == 0xFE and
            u32(blob, i + 0x18) == 40):
            mt = u32(blob, i + 0x20)
            cs = u32(blob, i + mt + 4)
            bc5_end = i + mt + 8 + cs
            break
        i += 1
    if bc5_end is None:
        print(f"{ehf.name}: no PF=40 found")
        continue

    # Find palette start.
    pal_start = None
    for j in range(bc5_end, body_end - 4):
        if blob[j:j+4] == ART:
            cnt = u32(blob, j - 4)
            if 1 < cnt < 200:
                pal_start = j
                break
    if pal_start is None:
        print(f"{ehf.name}: no palette found after BC5")
        continue

    mid = blob[bc5_end:pal_start]
    out = ehf.parent / f"{ehf.stem}_midbody_BC5end_to_palette.bin"
    out.write_bytes(mid)
    print(f"{ehf.name}:")
    print(f"  bc5_end    = 0x{bc5_end:x}")
    print(f"  pal_start  = 0x{pal_start:x}")
    print(f"  midbody    = {len(mid):,} bytes")
    print(f"  out        = {out.name}")
