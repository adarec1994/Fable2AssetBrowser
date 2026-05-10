import struct
from collections import Counter, defaultdict

TOC = "C:/Users/pwd12/OneDrive/Documents/GitHub/Fable2AssetBrowser/source/cmake-build-debug/extracted/fable2_anims.animation_toc"
DAT = "C:/Users/pwd12/OneDrive/Documents/GitHub/Fable2AssetBrowser/source/cmake-build-debug/extracted/fable2_anims.animation_data"
with open(TOC, "rb") as f: toc = f.read()
with open(DAT, "rb") as f: dat = f.read()
def be(b, off, n=4): return int.from_bytes(b[off:off+n], "big")

nrec = be(toc, 16); nspec = be(toc, 20); nstr = be(toc, 24)
pos = 28
strs = []
for _ in range(nstr):
    end = toc.index(b"\0", pos); strs.append(toc[pos:end].decode("latin-1")); pos = end + 1
clips = []
for i in range(nrec):
    f0 = be(toc, pos);     f1 = be(toc, pos+4)
    off = be(toc, pos+8);  fld_c = be(toc, pos+12)
    nev = be(toc, pos+20)
    duration = struct.unpack(">f", toc[pos+16:pos+20])[0]
    evts = []
    for j in range(nev):
        ept = pos + 24 + j*12
        evts.append((struct.unpack(">f", toc[ept:ept+4])[0],
                     be(toc, ept+4), be(toc, ept+8)))
    clips.append({"i": i, "off": off, "fld12": fld_c, "n_events": nev, "duration": duration, "events": evts})
    pos += 24 + nev * 12

# Co-occurrence of (bone_count, encoding_kind)
co = defaultdict(Counter)
for ci, c in enumerate(clips):
    o = c["off"]
    if o + 24 > len(dat): continue
    bc = be(dat, o+16)
    ek = be(dat, o+20)
    co[bc][ek] += 1

print("(bone_count -> encoding_kind distribution) for first ~25 bone counts:")
for bc in sorted(co.keys())[:25]:
    items = ", ".join(f"k{k}:{n}" for k, n in sorted(co[bc].items()))
    print(f"  bones={bc:4d}  total={sum(co[bc].values()):4d}  {items}")

# Most common encoding kinds vs bone-count buckets
buckets = [(0,5),(5,15),(15,30),(30,50),(50,100),(100,300)]
print("\nencoding-kind by bone-count bucket:")
for lo, hi in buckets:
    c = Counter()
    for bc in co:
        if lo <= bc < hi:
            c.update(co[bc])
    items = ", ".join(f"k{k}:{n}" for k, n in c.most_common(5))
    print(f"  bones [{lo:3d},{hi:3d}): {items}")

# Look at a clip with bones=25 (most common): peek its bytes
sample = next(c for c in clips if be(dat, c["off"]+16) == 25 and be(dat, c["off"]+20) == 5)
o = sample["off"]
print(f"\nSample clip[{sample['i']}] off=0x{o:08X} bones=25 ek=5:")
print("  hdr+0x00:", " ".join(f"{b:02X}" for b in dat[o:o+32]))
print("  hdr+0x20:", " ".join(f"{b:02X}" for b in dat[o+32:o+64]))
print("  hdr+0x40:", " ".join(f"{b:02X}" for b in dat[o+64:o+96]))
print("  hdr+0x60:", " ".join(f"{b:02X}" for b in dat[o+96:o+128]))

# And a 1-bone clip with ek=1
sample1 = next(c for c in clips if be(dat, c["off"]+16) == 1 and be(dat, c["off"]+20) == 1)
o = sample1["off"]
print(f"\nSample clip[{sample1['i']}] off=0x{o:08X} bones=1 ek=1:")
print("  hdr+0x00:", " ".join(f"{b:02X}" for b in dat[o:o+32]))
print("  hdr+0x20:", " ".join(f"{b:02X}" for b in dat[o+32:o+64]))
print("  hdr+0x40:", " ".join(f"{b:02X}" for b in dat[o+64:o+96]))

# Special record: peek the strings the SpecialRecords reference. We don't
# parse SpecialRecord here, but let's see if any clip's events reference
# string indices that look like clip names.
print(f"\nTotal strings: {len(strs)}")
print("First 30 strings:")
for s in strs[:30]:
    print(f"  '{s}'")
print("\nA few mid-range strings:")
for s in strs[200:220]:
    print(f"  '{s}'")
