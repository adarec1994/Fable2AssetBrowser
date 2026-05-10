import struct, sys, os

TOC = "C:/Users/pwd12/OneDrive/Documents/GitHub/Fable2AssetBrowser/source/cmake-build-debug/extracted/fable2_anims.animation_toc"
DAT = "C:/Users/pwd12/OneDrive/Documents/GitHub/Fable2AssetBrowser/source/cmake-build-debug/extracted/fable2_anims.animation_data"

def be(b, off, n=4):
    return int.from_bytes(b[off:off+n], "big")

with open(TOC, "rb") as f: toc = f.read()
with open(DAT, "rb") as f: dat = f.read()

assert toc[:8] == b"AnimBank"
v1 = be(toc, 8); v2 = be(toc, 12)
nrec = be(toc, 16); nspec = be(toc, 20); nstr = be(toc, 24)
print(f"TOC: v={v1}/{v2} records={nrec} special={nspec} strings={nstr}")

# Read string table
pos = 28
strings = []
for _ in range(nstr):
    end = toc.index(b"\0", pos)
    strings.append(toc[pos:end].decode("latin-1"))
    pos = end + 1
print(f"strings end at offset 0x{pos:X}")

# Read AnimRecords. Each: 6 u32 (one of them f32) + events*12 bytes.
# Layout (from sub_82B82248): +0,+4 = hashes, +8 = data_offset,
# +12 = ??? (length or type), +16 = duration f32, +20 = event_count
clips = []
for i in range(nrec):
    f0 = be(toc, pos);     f1 = be(toc, pos+4)
    off = be(toc, pos+8);  fld_c = be(toc, pos+12)
    duration_bits = be(toc, pos+16)
    duration = struct.unpack(">f", toc[pos+16:pos+20])[0]
    nev = be(toc, pos+20)
    pos += 24 + nev * 12
    clips.append({
        "i": i, "key0": f0, "key1": f1,
        "offset": off, "field12": fld_c,
        "duration": duration, "n_events": nev
    })

print(f"AnimRecords end at offset 0x{pos:X}")
print(f"\nFirst 8 clips:")
for c in clips[:8]:
    sl = dat[c["offset"]:c["offset"]+32]
    sl_hex = " ".join(f"{b:02X}" for b in sl[:24])
    print(f"  clip[{c['i']:4d}] off=0x{c['offset']:08X} fld12=0x{c['field12']:08X} dur={c['duration']:7.3f} ev={c['n_events']:3d}  bytes: {sl_hex}")

# Distribution of fld12 — could be the type tag
from collections import Counter
ctr = Counter(c["field12"] for c in clips)
print(f"\nfield12 distribution (top 10):")
for v, n in ctr.most_common(10):
    print(f"  0x{v:08X} = {v:10d}  count={n}")

# Distribution of first byte of clip blob
ctr2 = Counter()
for c in clips:
    if c["offset"] + 4 <= len(dat):
        ctr2[dat[c["offset"]]] += 1
print(f"\nFirst-byte distribution (top 10):")
for v, n in ctr2.most_common(10):
    print(f"  0x{v:02X}  count={n}")
