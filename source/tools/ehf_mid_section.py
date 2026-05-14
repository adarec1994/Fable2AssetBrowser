"""Detailed dump of the 530K mid-body region of bl_chapter3 .ehf:
[end of BC5 normal map blob ... start of palette).

This region MUST contain the per-cell material binding data the
terrain shader uses to pick a palette entry per heightfield cell.
The user (correctly) wants this found.

Strategy:
1. Walk forwards from the BC5 end with a structural dump in 16B rows.
2. Highlight regions that look like:
   - bbox headers (3 floats min + 3 floats max = 24 bytes, plus
     plausible-magnitude values around the level's world extent).
   - count u32s (small numbers <= 65536).
   - index streams (long runs of small-range bytes).
3. Find the boundary between "BVH-shaped data" and the actual
   per-cell index data.
"""

from __future__ import annotations
import struct
from pathlib import Path

EHF = Path("cmake-build-debug/extracted/bl_chapter3_heightfield_id_9501a1af.ehf")
blob = EHF.read_bytes()
BC5_END   = 0x16cfcb  # end of PF=40 BC5 normal map blob
PAL_START = 0x1ee4d7  # start of palette
W, H = 769, 769  # heightfield dims for chapter3

print(f"Region: 0x{BC5_END:x} .. 0x{PAL_START:x}  ({PAL_START - BC5_END:,} bytes)")
print(f"Heightfield: {W}x{H} = {W*H:,} cells")
print()

def be_u32(b, off):
    return struct.unpack_from(">I", b, off)[0]

def be_f32(b, off):
    return struct.unpack_from(">f", b, off)[0]

# 1) The first 32-byte node is what I thought was a "bbox":
#    +0..15: zero, +inf, +inf, +inf  (min vector with INF sentinel)
#    +16..31: zero, 1018, 1316, 1582 (max vector)
# These look like {pad:u32, min:vec3, pad:u32, max:vec3}.
# How many such 32-byte nodes are there?

# Read 32-byte records from BC5_END+24 (after the 24-byte header)
# until we no longer match the bbox pattern.
node_start = BC5_END + 0x1c  # skip first 28 bytes of header
print(f"=== walking 32-byte nodes starting at 0x{node_start:x} ===")
i = node_start
node_count = 0
max_nodes_to_show = 6
while i + 32 <= BC5_END + 1024:
    # Read as 8 floats
    vals = [be_f32(blob, i + j*4) for j in range(8)]
    min_vec = vals[1:4]
    max_vec = vals[5:8]
    if node_count < max_nodes_to_show:
        print(f"  node @0x{i:x}:")
        print(f"    pad={vals[0]:.4f}  min=({min_vec[0]:.2f}, {min_vec[1]:.2f}, {min_vec[2]:.2f})")
        print(f"    pad={vals[4]:.4f}  max=({max_vec[0]:.2f}, {max_vec[1]:.2f}, {max_vec[2]:.2f})")
    node_count += 1
    i += 32

# 2) Dump distinct sections of the gap with sample interpretations
sample_offsets = [
    BC5_END,
    BC5_END + 0x100,
    BC5_END + 0x1000,
    BC5_END + 0x10000,
    BC5_END + 0x40000,
    BC5_END + 0x60000,
    PAL_START - 0x200,
    PAL_START - 0x80,
]

for off in sample_offsets:
    if off < 0 or off + 64 > len(blob):
        continue
    print(f"\n=== sample @ 0x{off:x} (offset from BC5_END = 0x{off-BC5_END:x}) ===")
    for r in range(0, 64, 16):
        row = blob[off + r:off + r + 16]
        hexs = " ".join(f"{b:02x}" for b in row)
        asciis = "".join(chr(b) if 32 <= b < 127 else "." for b in row)
        # Also try interpreting as 4 BE f32s and 4 BE u32s.
        f0 = be_f32(blob, off + r)
        f1 = be_f32(blob, off + r + 4)
        u0 = be_u32(blob, off + r)
        u1 = be_u32(blob, off + r + 4)
        print(f"  +0x{off+r:08x}  {hexs:<48}  |{asciis}|"
              f"   f=[{f0:.2f}, {f1:.2f}]  u=[{u0}, {u1}]")

# 3) Look for the SIZE 591361 (or 295680 or 147840, etc.) encoded
#    anywhere as a u32 BE — that'd be a count of cells.
print(f"\n=== searching for plausible cell counts as u32 BE ===")
needles = {
    "W*H":          W * H,
    "W*H/2":        W * H // 2,
    "W*H/4":        W * H // 4,
    "(W-1)*(H-1)":  (W - 1) * (H - 1),
    "96*96":        96 * 96,    # the "96" count we saw in the bbox header
    "72*96":        72 * 96,
    "W":            W,
    "H":            H,
}
for label, val in needles.items():
    nb = val.to_bytes(4, "big")
    pos = blob.find(nb, BC5_END, PAL_START)
    if pos >= 0:
        print(f"  {label} = {val:,} (0x{val:x})  first found at 0x{pos:x}"
              f"  (offset from BC5_END = 0x{pos-BC5_END:x})")
    else:
        print(f"  {label} = {val:,} NOT FOUND in gap")

# 4) Look for ~~zero~~ entropy regions — runs of low-byte-value data
#    that'd indicate an index stream (palette indices are mostly 0..40).
print(f"\n=== scanning for low-entropy regions (values 0..63) ===")
window = 1024
stride = 4096
for s in range(BC5_END, PAL_START, stride):
    if s + window > PAL_START:
        break
    sub = blob[s:s+window]
    n_lo = sum(1 for b in sub if b < 64)
    pct = 100.0 * n_lo / window
    flag = "★" if pct > 90 else (" " if pct > 50 else " ")
    print(f"  +0x{s:08x}  pct_below_64 = {pct:5.1f}%  {flag}")
