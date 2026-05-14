"""Validate the IDA-derived 63-byte .ehf header layout against real
extracted .ehf files.

Layout under test (from sub_82A855A8 in IDA — see docs/level_format.md
section 9b.11):

  +0   23B  magic    "HeightFieldGraphicsFile"
  +23   4B  u32 BE   version       (= 18 in Bloodstone)
  +27   4B  f32 BE   f0
  +31   4B  f32 BE   f1
  +35   4B  u32 BE   u0
  +39   4B  u32 BE   u1
  +43   4B  f32 BE   f2
  +47   4B  f32 BE   f3
  +51   4B  f32 BE   f4
  +55   4B  u32 BE   body_offset
  +59   4B  u32 BE   body_size

This script reads every .ehf in cmake-build-debug/extracted/, validates
magic + version, checks body_offset+body_size fits in the file, and
dumps the first 96 bytes of the body so we can eyeball what comes
next (texture-pool index? a name? a count?).

Run with `python3 tools/ehf_dump.py` (or `py` on Windows).
"""

from __future__ import annotations
import os
import struct
import sys
from pathlib import Path

MAGIC = b"HeightFieldGraphicsFile"
HEADER_LEN = 63


def read_be_u32(b: bytes, off: int) -> int:
    return struct.unpack_from(">I", b, off)[0]


def read_be_f32(b: bytes, off: int) -> float:
    return struct.unpack_from(">f", b, off)[0]


def parse_header(blob: bytes) -> dict | None:
    if len(blob) < HEADER_LEN:
        return None
    if blob[:23] != MAGIC:
        return None
    return {
        "magic": blob[:23].decode("ascii"),
        "version":     read_be_u32(blob, 23),
        "f0":          read_be_f32(blob, 27),
        "f1":          read_be_f32(blob, 31),
        "u0":          read_be_u32(blob, 35),
        "u1":          read_be_u32(blob, 39),
        "f2":          read_be_f32(blob, 43),
        "f3":          read_be_f32(blob, 47),
        "f4":          read_be_f32(blob, 51),
        "body_offset": read_be_u32(blob, 55),
        "body_size":   read_be_u32(blob, 59),
    }


def fmt_hex_dump(b: bytes, start: int, length: int) -> str:
    end = min(start + length, len(b))
    out = []
    for row_start in range(start, end, 16):
        row = b[row_start:row_start + 16]
        hex_part = " ".join(f"{x:02x}" for x in row)
        ascii_part = "".join(chr(x) if 32 <= x < 127 else "." for x in row)
        out.append(f"    +0x{row_start - start:03x}  {hex_part:<48}  |{ascii_part}|")
    return "\n".join(out)


def main(extracted_dir: Path) -> int:
    ehfs = sorted(extracted_dir.glob("*.ehf"))
    if not ehfs:
        print(f"no .ehf files in {extracted_dir}", file=sys.stderr)
        return 1

    print(f"# Found {len(ehfs)} .ehf files in {extracted_dir}")
    print()

    bad = 0
    versions = {}
    body_offsets = {}

    for ehf in ehfs:
        blob = ehf.read_bytes()
        h = parse_header(blob)
        if h is None:
            print(f"## {ehf.name}: BAD HEADER ({len(blob)}B, first 4B = {blob[:4].hex()})")
            bad += 1
            continue

        body_end = h["body_offset"] + h["body_size"]
        body_fits = body_end <= len(blob)
        versions.setdefault(h["version"], []).append(ehf.name)
        body_offsets.setdefault(h["body_offset"], []).append(ehf.name)

        print(f"## {ehf.name}  ({len(blob):,}B)")
        print(f"   version = {h['version']}")
        print(f"   floats  : f0={h['f0']:>12.4f}  f1={h['f1']:>12.4f}  "
              f"f2={h['f2']:>12.4f}  f3={h['f3']:>12.4f}  f4={h['f4']:>12.4f}")
        print(f"   u32     : u0={h['u0']:>10}  u1={h['u1']:>10}")
        print(f"   body    : offset=0x{h['body_offset']:08x} ({h['body_offset']:,})  "
              f"size={h['body_size']:,}  end=0x{body_end:08x} "
              f"{'OK' if body_fits else '!! OVERFLOWS FILE'}")
        if body_fits and h["body_size"] >= 16:
            print("   body[0..95]:")
            print(fmt_hex_dump(blob, h["body_offset"], 96))
            # Also peek inside the .tex header
            tex_off = h["body_offset"]
            tex_magic = read_be_u32(blob, tex_off)
            tex_w     = read_be_u32(blob, tex_off + 0x10)
            tex_h     = read_be_u32(blob, tex_off + 0x14)
            tex_pf    = read_be_u32(blob, tex_off + 0x18)
            tex_mt    = read_be_u32(blob, tex_off + 0x20)
            tex_raw   = read_be_u32(blob, tex_off + tex_mt + 0)
            tex_comp  = read_be_u32(blob, tex_off + tex_mt + 4)
            print(f"   tex hdr : magic=0x{tex_magic:08x} W={tex_w} H={tex_h}"
                  f" PF={tex_pf} mt=0x{tex_mt:02x}"
                  f" raw={tex_raw:,} comp={tex_comp:,}")
            # Sanity: raw_size for various interpretations of the format
            n_pix = tex_w * tex_h
            print(f"   raw vs: W*H={n_pix:,}"
                  f"  W*H/2={n_pix//2:,}  W*H*2={n_pix*2:,}"
                  f"  W*H*4={n_pix*4:,}")
        # Show what comes AFTER body_end
        leftover_start = h["body_offset"] + h["body_size"]
        leftover_size  = len(blob) - leftover_start
        if leftover_size > 0:
            print(f"   leftover after body: {leftover_size:,}B"
                  f"  starting at 0x{leftover_start:08x}")
            print(fmt_hex_dump(blob, leftover_start, min(96, leftover_size)))
        print()

    print(f"# Summary: {len(ehfs)} files, {bad} bad headers")
    print(f"# Versions seen: {dict((v, len(names)) for v, names in versions.items())}")
    print(f"# Distinct body offsets: {len(body_offsets)}")
    for off, names in sorted(body_offsets.items()):
        print(f"   offset=0x{off:08x}  count={len(names)}  "
              f"example={names[0]}")
    return 0 if bad == 0 else 2


if __name__ == "__main__":
    here = Path(__file__).resolve().parent
    default = here.parent / "cmake-build-debug" / "extracted"
    extracted = Path(sys.argv[1]) if len(sys.argv) > 1 else default
    sys.exit(main(extracted))
