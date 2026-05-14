"""Find layer-name string anchors in a chapter3 .ehf so we know where
the layer-name list sits in the body parse order."""
import re
from pathlib import Path

blob = Path("cmake-build-debug/extracted/bl_chapter3_heightfield_id_9501a1af.ehf").read_bytes()

# Look for known layer-tag candidates.
patterns = [
    b"BaseLayer",
    b"DetailLayer",
    b"TopLayer",
    b"BlendLayer",
    b"OverLayer",
    b"BaseColorLayer",
    b"NormalLayer",
    b"Layer",
    b"BaseTile",
    b"Detail",
]
for p in patterns:
    found = []
    pos = 0
    while True:
        idx = blob.find(p, pos)
        if idx < 0:
            break
        found.append(idx)
        pos = idx + 1
    if found:
        print(f"{p!r}: {len(found)} occurrences, first 5: {[hex(x) for x in found[:5]]}")

# Find first art\ (palette start).
art_pos = blob.find(b"art\\")
print(f"first art\\\\: 0x{art_pos:x}")

# Pull any ASCII strings >= 4 chars in the 4KB window just before the palette.
print("\nStrings just before palette (ASCII >= 4 chars, ending NUL or quote):")
window_start = max(0, art_pos - 4096)
window = blob[window_start:art_pos]
for m in re.finditer(rb"[A-Za-z_][A-Za-z0-9_\\.]{3,40}\x00", window):
    print(f"  +0x{window_start + m.start():x}  {m.group(0).rstrip(bytes([0]))!r}")
