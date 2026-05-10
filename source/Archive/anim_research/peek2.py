import struct
from collections import Counter

TOC = "C:/Users/pwd12/OneDrive/Documents/GitHub/Fable2AssetBrowser/source/cmake-build-debug/extracted/fable2_anims.animation_toc"
DAT = "C:/Users/pwd12/OneDrive/Documents/GitHub/Fable2AssetBrowser/source/cmake-build-debug/extracted/fable2_anims.animation_data"
with open(TOC, "rb") as f: toc = f.read()
with open(DAT, "rb") as f: dat = f.read()

def be(b, off, n=4):
    return int.from_bytes(b[off:off+n], "big")

nrec = be(toc, 16); nspec = be(toc, 20); nstr = be(toc, 24)
pos = 28
for _ in range(nstr):
    end = toc.index(b"\0", pos); pos = end + 1

clips = []
for i in range(nrec):
    f0 = be(toc, pos);     f1 = be(toc, pos+4)
    off = be(toc, pos+8);  fld_c = be(toc, pos+12)
    nev = be(toc, pos+20)
    clips.append({"i": i, "off": off, "fld12": fld_c, "n_events": nev})
    pos += 24 + nev * 12

# For each clip, parse 6 u32 of header
ctrs = [Counter() for _ in range(6)]
sizes = []
for ci, c in enumerate(clips):
    o = c["off"]
    if o + 24 > len(dat): continue
    for j in range(6):
        ctrs[j][be(dat, o + j*4)] += 1
    if ci+1 < len(clips):
        sz = clips[ci+1]["off"] - o
        sizes.append((c["i"], sz, c["fld12"], be(dat, o+12), be(dat, o+16), be(dat, o+20)))

print("Header dword distributions (top 6 each):")
for j in range(6):
    print(f"  u32[{j}] (offset 0x{j*4:02X}): {len(ctrs[j])} unique values")
    for v, n in ctrs[j].most_common(5):
        print(f"    0x{v:08X} = {v}  ({n}x)")

# Look at sizes vs. fld12
print("\nFirst 20 clips: size, header fields...")
for s in sizes[:20]:
    i, sz, f12, h12, h16, h20 = s
    print(f"  clip[{i:4d}] size={sz:7d} TOC_fld12=0x{f12:08X} hdr12={h12} hdr16={h16} hdr20={h20}")

# Check size/fld12 correlations
print("\nfld12 (boneCount?) vs avg clip size:")
by_f12 = {}
for i, sz, f12, *_ in sizes:
    by_f12.setdefault(f12, []).append(sz)
for f12, szs in sorted(by_f12.items())[:15]:
    print(f"  fld12={f12:4d}  count={len(szs):4d}  avg_size={sum(szs)//len(szs):8d}  bytes/fld12={sum(szs)//len(szs)//max(1,f12):6d}")
