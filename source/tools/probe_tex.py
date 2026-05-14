"""Probe a .tex file for any kind of compression we can recognise.

We've now confirmed via TEX_FORMAT.md that the file in extracted/ is
the concatenation of three already-zlib-decompressed BNK extracts —
so the BODY of each mip record should already be RAW (decoded by
the BNK reader before write).

But the user is saying these files "are compressed" — so let's
exhaustively check:
  1. Is the WHOLE file zlib/gzip compressed?
  2. Does any mip body start with a zlib/gzip magic?
  3. Is there a raw-deflate stream embedded anywhere?
  4. What's the actual entropy profile across the file?
"""
from __future__ import annotations
import struct, sys, zlib
from pathlib import Path

def be_u32(b, o): return struct.unpack_from(">I", b, o)[0]

def try_inflate(data: bytes, wbits: int, label: str):
    try:
        d = zlib.decompressobj(wbits)
        out = d.decompress(data)
        out += d.flush()
        return True, out, len(data) - len(d.unused_data)
    except Exception as e:
        return False, str(e), 0

def probe(path: Path):
    raw = path.read_bytes()
    print(f"\n===== {path.name}  ({len(raw)} bytes) =====")
    print(f"First 16 bytes: {raw[:16].hex(' ')}")

    # Magic checks
    if raw[:4] == b"\xff\xff\xff\xfe":
        print("Has Fable 2 .tex magic FFFFFFFE — file is in raw (post-BNK-inflate) form")
    if raw[:2] == b"\x1f\x8b":
        print("** gzip magic at file start **")
        return
    if raw[0] == 0x78 and raw[1] in (0x01, 0x9C, 0xDA):
        print("** zlib magic at file start **")
        return

    # Whole-file inflate attempts
    for wbits, lbl in [(15, "zlib"), (-15, "raw deflate"), (31, "gzip")]:
        ok, out, used = try_inflate(raw, wbits, lbl)
        if ok and len(out) > 0:
            print(f"  WHOLE-FILE {lbl} inflate: OK, {used} → {len(out)} bytes")

    # Parse header + mip table assuming standard layout
    if len(raw) < 0x20:
        return
    W = be_u32(raw, 0x10)
    H = be_u32(raw, 0x14)
    PF = be_u32(raw, 0x18)
    nm = be_u32(raw, 0x1C)
    print(f"W={W} H={H} PF={PF} mipCount={nm}")
    if nm > 32:
        nm = 4
    offs = []
    for i in range(nm):
        p = 0x20 + i * 4
        if p + 4 > len(raw): break
        offs.append(be_u32(raw, p))
    print(f"Mip offsets: {[hex(o) for o in offs]}")

    # For each mip, examine the MipDef and body
    for mi, mo in enumerate(offs):
        if mo + 48 > len(raw): continue
        cf = be_u32(raw, mo)
        do = be_u32(raw, mo + 4)
        ds = be_u32(raw, mo + 8)
        body_start = mo + 48
        body_end = offs[mi + 1] if mi + 1 < len(offs) else len(raw)
        body_span = body_end - body_start
        print(f"  mip[{mi}] @ +0x{mo:X}  CF={cf}  DataOff={do}  DataSize={ds}  "
              f"body span [{body_start:#x}..{body_end:#x}) = {body_span} bytes")
        if body_start >= len(raw): continue
        body_first = raw[body_start:body_start + 16]
        print(f"    body[0..16] = {body_first.hex(' ')}")

        # zlib at body_start?
        if body_first[:1] == b"\x78":
            ok, out, used = try_inflate(raw[body_start:body_end], 15, "zlib")
            print(f"    zlib @ body: ok={ok} used={used} out_len={len(out) if ok else '?'}")

        # raw deflate at body_start?
        ok, out, used = try_inflate(raw[body_start:body_end], -15, "raw deflate")
        if ok and len(out) > 16:
            print(f"    RAW DEFLATE @ body works: consumed {used} → {len(out)} bytes")

        # scan for zlib magic 78 X within body
        b = raw[body_start:body_end]
        for ofs in range(0, min(2000, len(b) - 2)):
            if b[ofs] == 0x78 and b[ofs + 1] in (0x01, 0x9C, 0xDA):
                print(f"    zlib magic at body offset +{ofs}")
                ok, out, used = try_inflate(b[ofs:], 15, "zlib")
                if ok and len(out) > 16:
                    print(f"      inflate ok: {used} → {len(out)} bytes")
                break

        # entropy / byte distribution
        zeros = b.count(0)
        ffs = b.count(0xFF)
        print(f"    byte dist: zeros={zeros}/{len(b)} ({100*zeros/max(1,len(b)):.0f}%)  "
              f"FFs={ffs}/{len(b)} ({100*ffs/max(1,len(b)):.0f}%)")


if __name__ == "__main__":
    paths = sys.argv[1:] if len(sys.argv) > 1 else [
        "cmake-build-debug/extracted/dng_walls_detail_01_spec.tex",
        "cmake-build-debug/extracted/balverine_selfillum.tex",
        "cmake-build-debug/extracted/flithit selfillum.tex",
        "cmake-build-debug/extracted/flitmagic selfillum.tex",
        "cmake-build-debug/extracted/flitshot selfillum.tex",
    ]
    for p in paths:
        probe(Path(p))
