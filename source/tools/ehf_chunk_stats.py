"""Inspect how chunk texture-indices vary across chapter3 — are the 4
per-layer indices really per-corner (and varying chunk-to-chunk) or
are we always seeing the same (0, 3, 0, 3) pattern?

Imports the working walker logic to do the parse, then summarises
the chunk-layer texture-index distribution.
"""
from __future__ import annotations
import struct
import sys
import zlib
from pathlib import Path

# Inline the validated parser logic from ehf_body_walker.py.
sys.path.insert(0, str(Path(__file__).parent))
from ehf_body_walker import walk_chapter3

ehf = Path(sys.argv[1]) if len(sys.argv) > 1 else \
      Path("cmake-build-debug/extracted/bl_chapter3_heightfield_id_9501a1af.ehf")

# The walker logs everything but doesn't return structured data.
# Re-parse here in a self-contained way to get the chunk records.

from ehf_body_walker import StreamWalker, be_u32, be_f32

blob = ehf.read_bytes()
assert blob[:23] == b"HeightFieldGraphicsFile"
body_off = be_u32(blob, 55)
body_size = be_u32(blob, 59)
body = blob[body_off:body_off + body_size]

# We'll do the parse manually using the same step list.  This is a
# direct copy of walk_chapter3's body parse, but collects per-chunk
# texture indices into lists.

import io
sw = StreamWalker(body)

# 1. 2 textures
for _ in range(2):
    sw.read_tex_blob()
# 2. float
sw.f32()
# 3. sub_82A850A0
cnt = sw.u32()
for _ in range(cnt):
    sw.f32(); sw.f32(); w_sub = sw.u32(); h_sub = sw.u32()
    sw.skip(w_sub * h_sub * 160 + 24)
# 4. sub_82A860E8
sw.f32()
cnt2 = sw.u32()
sw.skip(cnt2 * 18)
# 5. anchor jump
art_pos = body.find(b"art\\")
assert art_pos > 0
sw.pos = art_pos - 4
# 6. LOD vector
lc = sw.u32()
lod_strs = []
for _ in range(lc):
    strs = [sw.read_null_string() for _ in range(3)]
    sw.skip(12)
    strs += [sw.read_null_string() for _ in range(3)]
    sw.skip(12)
    lod_strs.append(strs)
# 7. sub_82A85DB0
db0 = sw.u32()
for _ in range(db0):
    pf = be_u32(body, sw.pos + 0x18)
    if pf == 98:
        # uncompressed
        magic = sw.u32(); sw.skip(8); _ = sw.u32(); sw.u32(); sw.u32(); pf2 = sw.u32(); sw.u32(); mt = sw.u32()
        # ... we already consumed 36 bytes of header
        sw.skip(mt - 36 + 4)
        raw = sw.u32()  # ??? Actually let me reuse parser
    else:
        sw.read_tex_blob()

W = sw.u32(); H = sw.u32()
print(f"chunk grid: {W} x {H}")

import collections
all_idx = collections.Counter()
all_blend = collections.Counter()
chunks_by_pattern = collections.Counter()
samples = []

for ci in range(W * H):
    ox = sw.f32(); oy = sw.f32(); oz = sw.f32()
    ex = sw.f32(); ey = sw.f32(); ez = sw.f32()
    lcount = sw.u32()
    layers = []
    for li in range(lcount):
        v0 = sw.u32()
        name_idx = sw.u32()
        f1 = sw.f32(); f2 = sw.f32()
        # READ THE 8 BYTES AS INTERLEAVED (idx, blend) PAIRS
        b = body[sw.pos: sw.pos + 8]
        sw.skip(8)
        pairs = [(b[0], b[1]), (b[2], b[3]), (b[4], b[5]), (b[6], b[7])]
        layers.append({"name_idx": name_idx, "f": (f1, f2), "pairs": pairs})
    if ci < 5 or ci % 100 == 0 or ci > W*H - 5:
        samples.append((ci, ox, oy, oz, ex, ey, ez, layers))
    # Count
    for lyr in layers:
        for idx, blend in lyr["pairs"]:
            all_idx[idx] += 1
            all_blend[blend] += 1
    # Pattern: tuple of L0's 4 idx values
    if layers:
        pat = tuple(p[0] for p in layers[0]["pairs"])
        chunks_by_pattern[pat] += 1

print()
print("Texture index distribution (across all layers, all chunks):")
for idx, count in all_idx.most_common():
    print(f"  idx={idx:3d}: {count:6d} occurrences")
print()
print("Blend value distribution:")
for blend, count in all_blend.most_common():
    print(f"  blend={blend:3d}: {count:6d} occurrences")
print()
print("Top L0-pattern distribution (chunks with same idx[0..3] in L0):")
for pat, count in chunks_by_pattern.most_common(10):
    print(f"  L0 idx={pat}: {count} chunks")
print()
print("Sample chunks:")
for samp in samples[:20]:
    ci, ox, oy, oz, ex, ey, ez, ls = samp
    print(f"  chunk[{ci}] origin=({ox:.0f},{oy:.0f},{oz:.0f}) extent=({ex:.0f},{ey:.0f},{ez:.0f}) layers={len(ls)}")
    for li, lyr in enumerate(ls):
        print(f"    L{li}: name={lyr['name_idx']}  f={lyr['f']}  pairs={lyr['pairs']}")
