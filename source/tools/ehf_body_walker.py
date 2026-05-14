"""Walk the body of a .ehf step-by-step using the IDA-derived
structure, reporting stream offset and key values at each stage.

If the final offset matches body_end we've decoded the structure
correctly.  When it doesn't, the divergence point tells us which
step's byte budget is wrong.

Structure (from sub_82A855A8 in IDA):

  1. 4 × sub_82B86778 (read TEXTURE from stream — each reads a
     full .tex blob: header + zlib_stream)
  2. 1 float                                  → state[+176]
  3. sub_82A850A0: count + count × 96B entries
     each 96B entry has nested W'×H' × 192B mesh-vertex data
  4. sub_82A860E8: 1 float + count + count × 40B entries
     each 40B entry: 3 floats + 6 bytes (~18 bytes)
  5. sub_82A85F20: count + count × 240B LOD entries
     each LOD: 6 strings + 6 floats (variable)
  6. sub_82A85DB0: count + count × textures (full .tex blobs)
  7. u32 W                                    → state[+92]
  8. u32 H                                    → state[+96]
  9. W*H × chunks (each: vec4+vec4+layer_count+N×24B per-layer)
 10. sub_82A854D8: u8 flag + iterate the 40B vector for second-pass
"""
from __future__ import annotations
import struct
import sys
from pathlib import Path

MAGIC = b"HeightFieldGraphicsFile"


def be_u32(b, o): return struct.unpack_from(">I", b, o)[0]
def be_f32(b, o): return struct.unpack_from(">f", b, o)[0]


class StreamWalker:
    def __init__(self, body: bytes, log_prefix: str = ""):
        self.body = body
        self.pos = 0
        self.log = []
        self.prefix = log_prefix

    def remaining(self): return len(self.body) - self.pos
    def at(self, label, val=""):
        self.log.append(f"  @0x{self.pos:08x}  {label:<40} {val}")
    def peek(self, n=16):
        return " ".join(f"{b:02x}" for b in self.body[self.pos:self.pos+n])

    def u32(self):
        v = be_u32(self.body, self.pos); self.pos += 4; return v
    def f32(self):
        v = be_f32(self.body, self.pos); self.pos += 4; return v
    def u8(self):
        v = self.body[self.pos]; self.pos += 1; return v
    def skip(self, n): self.pos += n
    def vec4(self):
        x = self.f32(); y = self.f32(); z = self.f32(); w = self.f32()
        return (x, y, z, w)

    def read_tex_blob_pf98_style(self):
        """For PF=98 (and possibly others) the texture is stored
        uncompressed — raw_size bytes follow the header, not a zlib
        stream.  Header is the same 92 bytes."""
        magic = self.u32()
        if magic != 0xFFFFFFFE:
            raise ValueError(f"bad .tex magic at 0x{self.pos-4:x}: 0x{magic:x}")
        raw_hint = self.u32()
        unk0 = self.u32()
        header_count = self.u32()
        W = self.u32()
        H = self.u32()
        PF = self.u32()
        unk1 = self.u32()
        mt = self.u32()
        tex_start = self.pos - 36
        gap = (tex_start + mt) - self.pos
        if gap < 0:
            raise ValueError("bad mt")
        self.skip(gap)
        raw_size = self.u32()
        # For PF=98 the next 4 bytes are NOT comp_size — the raw data
        # starts immediately after raw_size.  Skip raw_size bytes.
        self.skip(raw_size)
        return {
            "W": W, "H": H, "PF": PF, "header_count": header_count,
            "raw_size": raw_size, "comp_size": 0,
            "tex_total_bytes": (self.pos - tex_start),
            "is_uncompressed": True,
        }

    def read_tex_blob(self):
        """Skip past a .tex blob in the stream.  Returns dict with key
        fields.  A .tex blob is: 92B header + raw_size_u32 + comp_size_u32
        ... wait that's already part of the header.  Let me re-check.

        .tex layout:
          +0x00  u32 magic (0xFFFFFFFE)
          +0x04  u32 raw_data_size_hint
          +0x08  u32 unknown_0
          +0x0C  u32 header_count
          +0x10  u32 TextureWidth
          +0x14  u32 TextureHeight
          +0x18  u32 PixelFormat
          +0x1C  u32 unknown_1
          +0x20  u32 mip_table_offset = 0x54
          +0x24..+0x53  zero padding
          +0x54  u32 unknown_2 = 13
          +0x58  u32 raw_size
          +0x5C  u32 comp_size
          +0x60  ... zlib stream of comp_size bytes
        Wait that's WRONG per existing docs.  Per TextureAtlasDecoder.h:
          +0x54  u32 raw_size
          +0x58  u32 comp_size
          +0x5C  zlib stream
        Mip table offset 0x54 includes the 4 bytes of unknown_2 right
        BEFORE raw_size.  Re-reading more carefully:
          +0x50  u32 unknown_2
          +0x54  u32 raw_size
          +0x58  u32 comp_size
          +0x5C  zlib stream of comp_size bytes
        So header = 0x5C bytes = 92, then zlib of comp_size = total
        92 + comp_size bytes.
        """
        magic = self.u32()
        if magic != 0xFFFFFFFE:
            raise ValueError(f"bad .tex magic at 0x{self.pos-4:x}: 0x{magic:x}")
        raw_hint = self.u32()
        unk0 = self.u32()
        header_count = self.u32()
        W = self.u32()
        H = self.u32()
        PF = self.u32()
        unk1 = self.u32()
        mt = self.u32()
        # Skip the zero padding from current pos to (.tex start) + mt
        # We're 9 u32 into the .tex so at offset mt - 36 from start of pad.
        # Actually: .tex start = self.pos - 36 (we've consumed 9 u32 = 36B).
        tex_start = self.pos - 36
        # Skip ahead to tex_start + mt
        gap = (tex_start + mt) - self.pos
        if gap < 0:
            raise ValueError("bad mt")
        self.skip(gap)
        # Read raw_size + comp_size at mt
        # But that depends on what mt points to.  Per docs, mt = 0x54 means
        # raw_size at +0x54, comp_size at +0x58, zlib at +0x5C.
        # But also unknown_2 might be at +0x50.  We already skipped to mt
        # (0x54), so raw is here.
        raw_size = self.u32()
        comp_size = self.u32()
        # Skip the zlib stream of comp_size bytes.
        self.skip(comp_size)
        return {
            "W": W, "H": H, "PF": PF, "header_count": header_count,
            "raw_size": raw_size, "comp_size": comp_size,
            "tex_total_bytes": (self.pos - tex_start),
        }

    def read_null_string(self, max_len=256):
        start = self.pos
        end = self.pos
        while end < len(self.body) and end < start + max_len:
            if self.body[end] == 0:
                break
            end += 1
        s = self.body[start:end].decode("ascii", errors="replace")
        self.pos = end + 1
        return s


def walk_chapter3(ehf_path: Path):
    blob = ehf_path.read_bytes()
    assert blob[:23] == MAGIC, "not an .ehf"
    body_off = be_u32(blob, 55)
    body_size = be_u32(blob, 59)
    body_end = body_off + body_size
    body = blob[body_off:body_end]
    print(f"# {ehf_path.name}")
    print(f"  body: {len(body):,} bytes")
    sw = StreamWalker(body)

    # Step 1: 2 textures (lightmap, normal)
    for i in range(2):
        sw.at(f"texture[{i}] start", sw.peek(16))
        info = sw.read_tex_blob()
        sw.at(f"texture[{i}] end",
              f"W={info['W']} H={info['H']} PF={info['PF']}"
              f" raw={info['raw_size']:,} comp={info['comp_size']:,}"
              f" total={info['tex_total_bytes']}")

    # Step 2: 1 float → state[+176]
    sw.at("float (state+176)", f"{sw.f32():.4f}    [next: {sw.peek(16)}]")

    # Step 3: sub_82A850A0 - count + N × 96B-entries-with-internal-mesh
    cnt = sw.u32()
    sw.at("sub_82A850A0 count", str(cnt))
    if cnt > 1000:
        sw.at(f"  ABORT — implausible count {cnt}")
        return sw
    for k in range(cnt):
        # Each 96B parent entry calls sub_82B250E8:
        #   4 stream reads (4×4B = 16B): float(entry+88), float(entry+92),
        #     u32(entry+80), float(entry+84)
        #   Then a w_sub × h_sub × 192B vertex grid (sub_82A1C110 reads
        #     4 vec4s, sub_82A1BEA8 reads 8 more vec4s per cell).
        sw.at(f"  82A850A0[{k}] start", sw.peek(32))
        f_a = sw.f32()  # entry+88
        f_b = sw.f32()  # entry+92
        w_sub = sw.u32()  # entry+80
        h_sub = sw.u32()  # entry+84 — note: it's read as float but stored as u32
        # Re-read h_sub as float and as u32 — IDA shows it's stored as float
        # at entry+84 but the loop uses *int* count.
        # Actually I'm not sure which u32 is "w_sub" — let me just guess.
        sw.at(f"    f_a={f_a:.3f}  f_b={f_b:.3f}  w_sub={w_sub}  h_sub={h_sub}")
        # Per-cell stream consumption (per sub_82B250E8 inner loop):
        #   4 × sub_82A1C110 reads (vec4 each = 16B) = 64B
        #   8 × sub_82A1BEA8 reads (3 floats each = 12B) = 96B
        # Total per cell = 160B
        # Per entry: 16B header reads + 160B × w_sub × h_sub + 24B trailer
        # (trailer = 2 × sub_82A1BEA8 = 24B at entry+16/+32)
        per_cell = 4 * 16 + 8 * 12  # = 160
        stream_for_grid = w_sub * h_sub * per_cell
        sw.at(f"    grid stream = {w_sub}*{h_sub}*{per_cell} = {stream_for_grid}B")
        if sw.pos + stream_for_grid + 24 > len(sw.body):
            sw.at(f"    ABORT — grid would overflow body")
            return sw
        sw.skip(stream_for_grid)
        sw.skip(24)  # 2 × sub_82A1BEA8 trailer

    sw.at("after sub_82A850A0", sw.peek(32))

    # Step 4: sub_82A860E8 — 1 float + count + N × 40B entries
    sw.at(f"sub_82A860E8 float (state+40)", f"{sw.f32():.4f}")
    cnt2 = sw.u32()
    sw.at("sub_82A860E8 count", str(cnt2))
    if cnt2 > 10000:
        sw.at(f"  ABORT — implausible count {cnt2}")
        return sw
    for k in range(cnt2):
        # sub_82B24200 reads: 3 floats + 6 bytes = 18 bytes from stream
        sw.skip(18)
    sw.at(f"after sub_82A860E8 ({cnt2} entries × 18B)", sw.peek(16))

    # ----- ANCHOR-BASED SHORTCUT -----
    # textures[2..3] consume an unknown number of bytes (PF=98 with weird
    # comp_size and possibly extra encoded data after).  Jump directly
    # to the LOD vector by scanning for the first 'art\' and backing up
    # 4 bytes (the count u32 right before).
    first_art = sw.body.find(b"art\\")
    if first_art > 0:
        candidate_count = be_u32(sw.body, first_art - 4)
        sw.at(f"anchor: first art\\ at body 0x{first_art:x}  candidate count = {candidate_count}")
        if 0 < candidate_count < 100:
            jump_to = first_art - 4
            sw.at(f"JUMP from 0x{sw.pos:x} to 0x{jump_to:x} (delta {jump_to - sw.pos:+d})")
            sw.pos = jump_to
        else:
            sw.at(f"anchor count implausible — bailing")
            return sw

    # Step 6: sub_82A85F20 — LOD vector (count + count × 240B entries).
    # Each entry calls sub_82A81748 which reads 6 strings (null-term) +
    # 6 u32/floats.  Total stream consumption = 6 × (string_len+1) + 24.
    cnt3 = sw.u32()
    sw.at("sub_82A85F20 LOD count", str(cnt3))
    if cnt3 > 100:
        sw.at(f"  ABORT — implausible LOD count {cnt3}")
        return sw
    for k in range(cnt3):
        # Per sub_82A81748's actual order:
        #   3 strings, 12 bytes, 3 strings, 12 bytes
        strs = []
        for _ in range(3):
            strs.append(sw.read_null_string())
        sw.skip(12)  # float v182 + u32 v193 + float v183
        for _ in range(3):
            strs.append(sw.read_null_string())
        sw.skip(12)  # float v184 + float v185 + float v186
        if k < 5 or k == cnt3 - 1:
            sw.at(f"  LOD[{k}]", str(strs))

    sw.at(f"after sub_82A85F20 ({cnt3} LODs)", sw.peek(32))

    # Step 7: sub_82A85DB0 — count + count × textures
    cnt4 = sw.u32()
    sw.at("sub_82A85DB0 texture count", str(cnt4))
    if cnt4 > 100:
        sw.at(f"  ABORT — implausible texture count {cnt4}")
        return sw
    for k in range(cnt4):
        sw.at(f"  85DB0_tex[{k}] start", sw.peek(16))
        pf = be_u32(sw.body, sw.pos + 0x18)
        try:
            if pf == 98:
                info = sw.read_tex_blob_pf98_style()
            else:
                info = sw.read_tex_blob()
            sw.at(f"  85DB0_tex[{k}] end",
                  f"W={info['W']} H={info['H']} PF={info['PF']}"
                  f" raw={info['raw_size']:,} comp={info['comp_size']:,}")
        except Exception as e:
            sw.at(f"  85DB0_tex[{k}] FAILED: {e}")
            return sw

    sw.at(f"after sub_82A85DB0 ({cnt4} tex)", sw.peek(32))

    # Step 8: u32 W, u32 H
    W = sw.u32()
    H = sw.u32()
    sw.at("CHUNK GRID W, H", f"W={W}, H={H}")
    if W == 0 or H == 0 or W > 1024 or H > 1024:
        sw.at(f"  ABORT — implausible W,H")
        return sw

    # Step 9: W*H × chunks via sub_82B25728
    # Each chunk: 2 × sub_82A1BEA8 (12B each, gives vec4 origin/extent)
    #           + u32 layer_count
    #           + count × sub_82B25930 per-layer
    # Per layer (sub_82B25930): sub_82B25850 (4B) + u32 name_idx (4B)
    #   + sub_82A1BD30 (2 floats = 8B) + 8B of 4×u8 pairs = 24 bytes
    chunks_dumped = []
    total_chunks = W * H
    for ci in range(total_chunks):
        if sw.remaining() < 28:
            sw.at(f"  chunk[{ci}] OUT OF BUFFER (pos=0x{sw.pos:x})")
            return sw
        # vec4 origin (3 floats = 12B from stream)
        ox = sw.f32(); oy = sw.f32(); oz = sw.f32()
        # vec4 extent (3 floats = 12B from stream)
        ex = sw.f32(); ey = sw.f32(); ez = sw.f32()
        layer_count = sw.u32()
        if layer_count > 32:
            sw.at(f"  chunk[{ci}] origin=({ox:.2f},{oy:.2f},{oz:.2f})"
                  f" extent=({ex:.2f},{ey:.2f},{ez:.2f})"
                  f" layers={layer_count}  ← ABORT")
            return sw
        layers = []
        for li in range(layer_count):
            if sw.remaining() < 24:
                sw.at(f"  chunk[{ci}].layer[{li}] OUT OF BUFFER")
                return sw
            v0 = sw.u32()  # sub_82B25850
            name_idx = sw.u32()
            f1 = sw.f32(); f2 = sw.f32()
            idx = struct.unpack_from(">4B", sw.body, sw.pos); sw.skip(4)
            blend = struct.unpack_from(">4B", sw.body, sw.pos); sw.skip(4)
            layers.append((v0, name_idx, f1, f2, list(idx), list(blend)))
        chunks_dumped.append((ci, ox, oy, oz, ex, ey, ez, layers))

    # Print first few and last few chunks
    for entry in chunks_dumped[:5]:
        ci, ox, oy, oz, ex, ey, ez, ls = entry
        sw.at(f"  chunk[{ci}] origin=({ox:.1f},{oy:.1f},{oz:.1f})"
              f" extent=({ex:.1f},{ey:.1f},{ez:.1f}) layers={len(ls)}")
        for li, lyr in enumerate(ls[:2]):
            v0, ni, f1, f2, idx, blend = lyr
            sw.at(f"    L{li}: name={ni} f=({f1:.3g},{f2:.3g})"
                  f" idx={idx} blend={blend}")
    if total_chunks > 10:
        sw.at(f"  ... ({total_chunks-5} more chunks) ...")
        for entry in chunks_dumped[-2:]:
            ci, ox, oy, oz, ex, ey, ez, ls = entry
            sw.at(f"  chunk[{ci}] origin=({ox:.1f},{oy:.1f},{oz:.1f})"
                  f" extent=({ex:.1f},{ey:.1f},{ez:.1f}) layers={len(ls)}")

    sw.at(f"after {total_chunks} chunks", sw.peek(32))

    # Step 10: sub_82A854D8 final pass
    # Reads u8 flag, then iterates state[+124..+128] for second-pass
    # reads via sub_82B24750.  Each sub_82B24750 reads u32 count + count × 8B.
    final_flag = sw.u8()
    sw.at(f"final pass u8 flag = {final_flag}")
    # The 40B entry vector from sub_82A860E8 had cnt2 = 72 entries.
    # For each, sub_82B24750 reads u32 sub_count + sub_count × 8B.
    for k in range(cnt2):
        if sw.remaining() < 4:
            sw.at(f"  final[{k}] OUT OF BUFFER")
            return sw
        sub_count = sw.u32()
        if sub_count > 1000:
            sw.at(f"  final[{k}] sub_count={sub_count} — implausible")
            return sw
        sw.skip(sub_count * 8)

    sw.at(f"END pos=0x{sw.pos:x} (body_end=0x{len(sw.body):x})")
    return sw

    return sw


if __name__ == "__main__":
    import io, sys as _sys
    _sys.stdout = io.TextIOWrapper(_sys.stdout.buffer, encoding="utf-8", errors="replace")
    ehf = Path(sys.argv[1]) if len(sys.argv) > 1 else \
          Path("cmake-build-debug/extracted/bl_chapter3_heightfield_id_9501a1af.ehf")
    sw = walk_chapter3(ehf)
    print()
    print("\n".join(sw.log))
    print()
    print(f"final pos = 0x{sw.pos:x}")
    print(f"body length = 0x{len(sw.body):x}")
    print(f"remaining = {sw.remaining()}")
