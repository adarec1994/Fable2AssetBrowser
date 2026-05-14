"""Dump bytes right after the palette in chapter3 .ehf to identify the
per-cell material index layout."""
from pathlib import Path

blob = Path("cmake-build-debug/extracted/bl_chapter3_heightfield_id_9501a1af.ehf").read_bytes()

pal_start = 0x1ee4d7
body_end  = 0x235f1b
ART = b"art\\"

# Walk palette entries.
i = pal_start
entries = 0
while i + 4 < body_end:
    if blob[i:i+4] != ART:
        break
    end_d = blob.index(b"\x00", i)
    if blob[end_d+1:end_d+5] != ART:
        break
    end_n = blob.index(b"\x00", end_d + 1)
    i = end_n + 1 + 13
    entries += 1

print(f"palette entries parsed: {entries}, palette ends at 0x{i:x}")
print(f"gap 2 (post-palette to body_end): {body_end - i:,} bytes")
print()

def dump(label, start, end):
    print(f"=== {label} @ 0x{start:x} ===")
    for off in range(start, end, 16):
        row = blob[off:off+16]
        hexs = " ".join(f"{b:02x}" for b in row)
        asciis = "".join(chr(b) if 32<=b<127 else "." for b in row)
        print(f"  +0x{off:08x}  {hexs:<48}  |{asciis}|")

dump("POST-PALETTE start", i, min(i + 256, body_end))
print()
dump("NEAR BODY END", body_end - 128, body_end)

W, H = 769, 769
print(f"\nExpected sizes for {W}x{H} = {W*H} cells:")
print(f"  1 byte/cell:     {W*H}")
print(f"  0.5 byte/cell:   {W*H//2}")
print(f"  (W-1)*(H-1):     {(W-1)*(H-1)}")
print(f"  (W-1)*(H-1)/2:   {(W-1)*(H-1)//2}")
print(f"  per-chunk 8x8:   {((W+7)//8)*((H+7)//8)}  ({(W+7)//8}x{(H+7)//8})")
print(f"  per-chunk 16x16: {((W+15)//16)*((H+15)//16)}  ({(W+15)//16}x{(H+15)//16})")
print(f"  per-chunk 32x32: {((W+31)//32)*((H+31)//32)}  ({(W+31)//32}x{(H+31)//32})")
