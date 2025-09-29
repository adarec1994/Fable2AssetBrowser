#!/usr/bin/env python3
"""
MDL → GLB per MeshBuffer with robust MeshBuffer re-sync, unlimited buffers,
and 13th-byte sequential marker verification.

Exports: <file>.buf0.glb, <file>.buf1.glb, ...

For each MeshBuffer prints:
- initial 13 bytes (hex)
- vertex start offset, vertex count, stride
- face start offset, face count, and whether strips or triangles

Usage:
  python mdl_parser_resync_glb_allbufs.py <file.mdl> [--little-endian] [--stride N] [--no-faces]
"""

from __future__ import annotations
import json
import math
import struct
import sys
from pathlib import Path
from typing import List, Tuple, Optional

# -------------------- tunables --------------------
DEFAULT_VERTEX_STRIDE = 28     # can be overridden by CLI --stride
SUBMESH_DESC_SIZE     = 41     # bytes per SubMeshDesc block
INDEX_STRIDE          = 2      # WORD indices in file
UNK16_STRIDE          = 16     # 4 floats (per vertex)
MAX_STR_LEN           = 8192   # cap for reading C-strings defensively

_DEF_EXTS = ('.tex', '.dds', '.tga')
_ALLOWED_START = set(range(48,58)) | set(range(65,91)) | set(range(97,123)) | {0x2F,0x5C,0x2E,0x5F,0x3A}

# -------------------- half-float --------------------

def _half_to_float(h: int) -> float:
    s = (h >> 15) & 0x1
    e = (h >> 10) & 0x1F
    f = h & 0x3FF
    if e == 0:
        if f == 0:
            return -0.0 if s else 0.0
        return ((-1.0) if s else 1.0) * (f / 1024.0) * (2.0 ** -14)
    if e == 0x1F:
        if f == 0:
            return float('-inf') if s else float('inf')
        return float('nan')
    return ((-1.0) if s else 1.0) * (1.0 + f/1024.0) * (2.0 ** (e - 15))

def read_half_be(b: bytes) -> float:
    return _half_to_float((b[0] << 8) | b[1])

def read_half_le(b: bytes) -> float:
    return _half_to_float((b[1] << 8) | b[0])

# -------------------- small helpers --------------------

def looks_like_ascii_start(b: int) -> bool:
    return b in _ALLOWED_START

def looks_like_path(s: str) -> bool:
    ls = s.lower()
    return any(ls.endswith(ext) or ls.endswith(ext + '.') for ext in _DEF_EXTS)

def _u32_be(b: bytes, i: int) -> int:
    return struct.unpack_from('>I', b, i)[0]

def _u32_le(b: bytes, i: int) -> int:
    return struct.unpack_from('<I', b, i)[0]

def pad4(n: int) -> int:
    return (n + 3) & ~3

# -------------------- Reader --------------------
class Reader:
    def __init__(self, data: bytes, big_endian: bool = True):
        self.data = data
        self.be = big_endian
        self.endian = '>' if big_endian else '<'
        self.off = 0

    def need(self, n: int):
        if self.off + n > len(self.data):
            raise ValueError(f"Read beyond EOF @0x{self.off:08X} need {n} have {len(self.data)-self.off}")

    def can(self, n: int) -> bool:
        return self.off + n <= len(self.data)

    def rbytes(self, n: int) -> bytes:
        self.need(n)
        b = self.data[self.off:self.off+n]
        self.off += n
        return b

    def ru32(self) -> int:
        return struct.unpack(self.endian+'I', self.rbytes(4))[0]

    def rf32(self) -> float:
        return struct.unpack(self.endian+'f', self.rbytes(4))[0]

    def rstr(self, maxlen: int = MAX_STR_LEN) -> str:
        start = self.off
        out = bytearray()
        limit = min(len(self.data), start + maxlen)
        while self.off < limit:
            c = self.rbytes(1)
            if c == b'\x00':
                break
            out.extend(c)
        return out.decode('utf-8', errors='ignore')

    def ru32_arr(self, k: int):
        return [self.ru32() for _ in range(k)]

    def rf32_arr(self, k: int):
        return [self.rf32() for _ in range(k)]

# -------------------- MeshBuffer header sniffing --------------------

def _plausible_meshbuffer_counts(some: int, idx: int, vtx: int, sm: int,
                                 file_len: int, at: int, preamble: int,
                                 vertex_stride: int) -> bool:
    if not (0 <= sm <= 4096): return False
    if not (1 <= vtx <= 300_000): return False
    if not (0 <= idx <= 5_000_000): return False
    sub_sz = sm * SUBMESH_DESC_SIZE
    vtx_sz = vtx * vertex_stride
    need = preamble + 16 + sub_sz + vtx_sz
    return (at + need) <= file_len

def looks_like_meshbuffer0(data: bytes, at: int, be: bool, vertex_stride: int) -> bool:
    if at + 24 > len(data): return False
    U = _u32_be if be else _u32_le
    some = U(data, at + 8)
    idx  = U(data, at + 12)
    vtx  = U(data, at + 16)
    sm   = U(data, at + 20)
    return _plausible_meshbuffer_counts(some, idx, vtx, sm, len(data), at, 8, vertex_stride)

def looks_like_meshbuffer_special(data: bytes, at: int, be: bool, vertex_stride: int) -> bool:
    if at + 33 > len(data): return False
    U = _u32_be if be else _u32_le
    some = U(data, at + 17)
    idx  = U(data, at + 21)
    vtx  = U(data, at + 25)
    sm   = U(data, at + 29)
    return _plausible_meshbuffer_counts(some, idx, vtx, sm, len(data), at, 17, vertex_stride)

def _header_preface_len(data: bytes, at: int, be: bool, vertex_stride: int) -> Optional[int]:
    if looks_like_meshbuffer0(data, at, be, vertex_stride): return 8
    if looks_like_meshbuffer_special(data, at, be, vertex_stride): return 17
    return None

def _marker_matches(data: bytes, at: int, expected_marker: int) -> bool:
    if at + 13 > len(data): return False
    return data[at + 12] == (expected_marker & 0xFF)

def find_meshbuffer_with_marker(data: bytes, start_at: int, be: bool, vertex_stride: int,
                                expected_marker: int) -> Optional[int]:
    n = len(data)
    for i in range(max(0, start_at), n - 64):
        if _header_preface_len(data, i, be, vertex_stride) is not None and _marker_matches(data, i, expected_marker):
            return i
    for i in range(0, n - 64):
        if _header_preface_len(data, i, be, vertex_stride) is not None and _marker_matches(data, i, expected_marker):
            return i
    return None

def find_any_meshbuffer(data: bytes, start_at: int, be: bool, vertex_stride: int) -> Optional[int]:
    n = len(data)
    for i in range(max(0, start_at), n - 64):
        if _header_preface_len(data, i, be, vertex_stride) is not None:
            return i
    for i in range(0, n - 64):
        if _header_preface_len(data, i, be, vertex_stride) is not None:
            return i
    return None

# -------------------- header skip (materials kept, debug silent) --------------------

def skip_to_mesh_headers(r: Reader, vertex_stride: int, quiet_mats: bool) -> int:
    # Preamble
    r.ru32_arr(8)
    bone_count = r.ru32()
    for _ in range(min(bone_count, 65535)):
        _ = r.rstr(); _ = r.ru32()
    bt_count = r.ru32()
    for _ in range(min(bt_count, 65535)):
        r.ru32_arr(11)
    r.rf32_arr(10)
    mesh_count = r.ru32()
    r.ru32_arr(2); r.rbytes(13); r.ru32_arr(5)
    unk6c = r.ru32()
    if 0 < unk6c < 65535:
        r.rf32_arr(unk6c)
    _ = r.ru32()  # Unk7

    data = r.data
    for _mi in range(mesh_count):
        _ = r.ru32()           # Unk1
        _ = r.rstr()           # MeshName
        r.rf32_arr(2)
        r.rbytes(21)
        _ = r.rf32()
        r.ru32_arr(3)
        matc = r.ru32()

        for _mj in range(min(matc, 200000)):
            hdr = find_any_meshbuffer(data, r.off, r.be, vertex_stride)
            if hdr is not None and hdr == r.off:
                return mesh_count

            _ = r.rstr(); _ = r.rstr(); _ = r.rstr()

            def read_3b_and_floats() -> bool:
                if r.can(3 + 12):
                    r.rbytes(3); r.rf32_arr(3); return True
                if r.can(16 + 3 + 12):
                    r.rbytes(16); r.rbytes(3); r.rf32_arr(3); return True
                return False

            if not read_3b_and_floats():
                hdr = find_any_meshbuffer(data, r.off, r.be, vertex_stride)
                if hdr is not None and hdr >= r.off:
                    r.off = hdr
                    return mesh_count
                break

            if r.can(16) and (r.off + 16 < len(data)):
                b0 = data[r.off]
                b16 = data[r.off + 16]
                if (not looks_like_ascii_start(b0)) and looks_like_ascii_start(b16):
                    r.rbytes(16)

    hdr = find_any_meshbuffer(data, r.off, r.be, vertex_stride)
    if hdr is None:
        raise ValueError("Could not find a MeshBuffer header after meshes")
    if hdr != r.off:
        print(f"[sync] advancing to first MeshBuffer header @0x{hdr:08X}")
        r.off = hdr
    return mesh_count

# -------------------- faces reconstruction --------------------

def build_faces_from_indices(indices: List[int], vcount: int) -> List[Tuple[int,int,int]]:
    faces: List[Tuple[int,int,int]] = []
    has_restart = any(ix == 0xFFFF for ix in indices)
    if not has_restart:
        limit = (len(indices) // 3) * 3
        for i in range(0, limit, 3):
            a, b, c = indices[i], indices[i+1], indices[i+2]
            if a >= vcount or b >= vcount or c >= vcount or a == b or b == c or a == c:
                continue
            faces.append((a+1, b+1, c+1))
        return faces
    si = 0
    n = len(indices)
    while si < n:
        ei = si
        while ei < n and indices[ei] != 0xFFFF:
            ei += 1
        seg = indices[si:ei]
        for t in range(2, len(seg)):
            a, b, c = seg[t-2], seg[t-1], seg[t]
            if a == b or b == c or a == c:
                continue
            tri = (a, b, c) if (t % 2) == 0 else (a, c, b)
            if tri[0] < vcount and tri[1] < vcount and tri[2] < vcount:
                faces.append((tri[0]+1, tri[1]+1, tri[2]+1))
        si = ei + 1
    return faces

# -------------------- buffer parsing --------------------

def parse_buffer(r: Reader, which: int, preface_len: int, vertex_stride: int, write_faces: bool):
    start = r.off
    head13 = r.data[start:start+13]
    head13_hex = ' '.join(f"{b:02X}" for b in head13)

    if preface_len == 8:
        _ = r.ru32_arr(2)
    elif preface_len == 17:
        r.rbytes(17)
    else:
        raise ValueError(f"Unsupported preface length: {preface_len}")

    some = r.ru32()
    idx  = r.ru32()
    vtx  = r.ru32()
    sm   = r.ru32()

    print(f"\nBuffer {which} @0x{start:08X} (preface {preface_len})")
    print(f"  initial 13 bytes: {head13_hex}")

    r.off += sm * SUBMESH_DESC_SIZE
    vstart = r.off

    max_v = min(vtx, max(0, (len(r.data) - vstart) // vertex_stride))
    if max_v != vtx:
        vtx = max_v

    positions: List[Tuple[float,float,float]] = []
    if vtx > 0:
        read_half = read_half_be if r.be else read_half_le
        raw = r.data[vstart:vstart + vtx * vertex_stride]
        for i in range(vtx):
            base = i * vertex_stride
            x = read_half(raw[base     : base+2])
            y = read_half(raw[base+2   : base+4])
            z = read_half(raw[base+4   : base+6])
            # write as X Z Y later; keep XYZ here
            positions.append((x, y, z))
    r.off = vstart + vtx * vertex_stride

    face_start = r.off
    faces: List[Tuple[int,int,int]] = []
    indices: List[int] = []
    if write_faces and idx > 0:
        face_bytes = idx * INDEX_STRIDE
        avail = len(r.data) - r.off
        read_n = min(face_bytes, max(0, avail))
        face_blob = r.rbytes(read_n)
        u16fmt = '>H' if r.be else '<H'
        indices = [struct.unpack(u16fmt, face_blob[i:i+2])[0]
                   for i in range(0, len(face_blob), 2) if i+2 <= len(face_blob)]
        faces = build_faces_from_indices(indices, vtx)
    else:
        r.off += idx * INDEX_STRIDE

    unk16_bytes = vtx * UNK16_STRIDE
    if r.off + unk16_bytes <= len(r.data):
        r.off += unk16_bytes

    mode = "triangle strips" if (write_faces and any(ix == 0xFFFF for ix in indices)) else "triangles"
    print(f"  vertex start offset = 0x{vstart:08X}")
    print(f"  vertex count        = {vtx}")
    print(f"  stride              = {vertex_stride}")
    print(f"  face start offset   = 0x{face_start:08X}")
    print(f"  face count          = {idx}")
    if write_faces and idx > 0:
        print(f"  mode                = {mode} (built {len(faces)} tris)")
    else:
        print(f"  mode                = {mode} (faces skipped)")

    return vstart, vtx, positions, faces

# -------------------- GLB writer --------------------

def write_glb(out_path: Path,
              positions: List[Tuple[float,float,float]],
              faces: List[Tuple[int,int,int]],
              src_name: str, which: int):
    """
    Minimal GLB:
      - scene(0) -> node(0) -> mesh(0) -> primitive(0)
      - POSITION (float32 VEC3), indices optional
      - If faces empty, export as POINTS (mode 0) without indices.
    """
    # Reorder to X Z Y as you wanted for OBJ: here we permanently store XZY
    pos_f32 = []
    minx = miny = minz = float('inf')
    maxx = maxy = maxz = float('-inf')
    for (x, y, z) in positions:
        px, py, pz = x, z, y  # X Z Y
        pos_f32.extend([px, py, pz])
        if px < minx: minx = px
        if py < miny: miny = py
        if pz < minz: minz = pz
        if px > maxx: maxx = px
        if py > maxy: maxy = py
        if pz > maxz: maxz = pz

    # Build flat index array from faces (convert 1-based -> 0-based)
    flat_idx: List[int] = []
    for a, b, c in faces:
        flat_idx.extend([a-1, b-1, c-1])

    # Decide index component type
    have_indices = len(flat_idx) > 0
    if have_indices:
        max_index = max(flat_idx) if flat_idx else 0
        if max_index < 65536:
            comp_type = 5123  # UNSIGNED_SHORT
            idx_bytes = bytearray()
            for i in flat_idx:
                idx_bytes += struct.pack('<H', int(i))
        else:
            comp_type = 5125  # UNSIGNED_INT
            idx_bytes = bytearray()
            for i in flat_idx:
                idx_bytes += struct.pack('<I', int(i))
    else:
        comp_type = None
        idx_bytes = b''

    # Build binary buffer: positions then (optional) indices
    pos_bytes = struct.pack('<' + 'f'*len(pos_f32), *pos_f32)
    pos_len   = len(pos_bytes)
    pos_off   = 0

    cur_off   = pad4(pos_len)
    if cur_off != pos_len:
        pos_bytes += b'\x00' * (cur_off - pos_len)

    if have_indices:
        idx_off = cur_off
        bin_data = pos_bytes + idx_bytes
    else:
        idx_off = None
        bin_data = pos_bytes

    # Pad BIN to 4B
    if len(bin_data) % 4 != 0:
        bin_data += b'\x00' * (4 - (len(bin_data) % 4))

    # Build glTF JSON
    buffers = [{
        "byteLength": len(bin_data)
    }]

    bufferViews = [{
        "buffer": 0,
        "byteOffset": pos_off,
        "byteLength": len(pos_bytes),
        "target": 34962  # ARRAY_BUFFER
    }]

    accessors = [{
        "bufferView": 0,
        "byteOffset": 0,
        "componentType": 5126,  # FLOAT
        "count": len(positions),
        "type": "VEC3",
        "min": [minx if math.isfinite(minx) else 0.0,
                miny if math.isfinite(miny) else 0.0,
                minz if math.isfinite(minz) else 0.0],
        "max": [maxx if math.isfinite(maxx) else 0.0,
                maxy if math.isfinite(maxy) else 0.0,
                maxz if math.isfinite(maxz) else 0.0],
    }]

    prim = {
        "attributes": {"POSITION": 0}
    }

    if have_indices:
        bufferViews.append({
            "buffer": 0,
            "byteOffset": idx_off,
            "byteLength": len(idx_bytes),
            "target": 34963  # ELEMENT_ARRAY_BUFFER
        })
        accessors.append({
            "bufferView": 1,
            "byteOffset": 0,
            "componentType": comp_type,
            "count": len(flat_idx),
            "type": "SCALAR"
        })
        prim["indices"] = 1
        prim["mode"] = 4  # TRIANGLES
    else:
        prim["mode"] = 0  # POINTS, so it still previews

    meshes = [{
        "name": f"{src_name}#buf{which}",
        "primitives": [prim]
    }]
    nodes = [{
        "mesh": 0,
        "name": f"node_buf{which}"
    }]
    scenes = [{
        "nodes": [0]
    }]

    gltf = {
        "asset": {"version": "2.0", "generator": "mdl_parser_resync_glb_allbufs"},
        "scene": 0,
        "scenes": scenes,
        "nodes": nodes,
        "meshes": meshes,
        "buffers": buffers,
        "bufferViews": bufferViews,
        "accessors": accessors
    }

    json_txt = json.dumps(gltf, separators=(',', ':'))
    json_bytes = json_txt.encode('utf-8')
    if len(json_bytes) % 4 != 0:
        json_bytes += b' ' * (4 - (len(json_bytes) % 4))

    # GLB header + chunks
    total_len = 12 + (8 + len(json_bytes)) + (8 + len(bin_data))
    with open(out_path, 'wb') as f:
        # header
        f.write(struct.pack('<I', 0x46546C67))  # magic 'glTF'
        f.write(struct.pack('<I', 2))           # version
        f.write(struct.pack('<I', total_len))
        # JSON chunk
        f.write(struct.pack('<I', len(json_bytes)))
        f.write(b'JSON')
        f.write(json_bytes)
        # BIN chunk
        f.write(struct.pack('<I', len(bin_data)))
        f.write(b'BIN\x00')
        f.write(bin_data)

    print(f"  ✓ Wrote GLB: {out_path}")

# -------------------- main --------------------

def main():
    if len(sys.argv) < 2:
        print("Usage: python mdl_parser_resync_glb_allbufs.py <file.mdl> [--little-endian] [--stride N] [--no-faces]")
        sys.exit(1)

    file_path = Path(sys.argv[1])
    big_endian = ('--little-endian' not in sys.argv)
    write_faces = ('--no-faces' not in sys.argv)

    vstride = DEFAULT_VERTEX_STRIDE
    if '--stride' in sys.argv:
        try:
            vstride = int(sys.argv[sys.argv.index('--stride')+1])
        except Exception:
            print("--stride requires an integer argument")
            sys.exit(2)

    data = file_path.read_bytes()
    r = Reader(data, big_endian)

    print(f"File: {file_path.name} | Size: {len(data)} bytes | Endian: {'BE' if big_endian else 'LE'} | Stride: {vstride}")

    mesh_count = skip_to_mesh_headers(r, vstride, quiet_mats=True)
    if mesh_count <= 0:
        print('No meshes found')
        return

    # Align first buffer to marker 0 if available
    hdr0 = find_meshbuffer_with_marker(r.data, r.off, r.be, vstride, expected_marker=0)
    if hdr0 is not None and hdr0 != r.off:
        print(f"[sync] marker-align to buffer0 @0x{hdr0:08X}")
        r.off = hdr0

    which = 0
    while True:
        here = r.off
        want = which & 0xFF
        if not _marker_matches(r.data, here, want):
            next_hdr = find_meshbuffer_with_marker(r.data, here, r.be, vstride, expected_marker=want)
            if next_hdr is None:
                print(f"No further MeshBuffer with marker {want:02X} after 0x{here:08X}. Stopping.")
                break
            if next_hdr != here:
                print(f"[sync] skipping to marker {want:02X} header @0x{next_hdr:08X}")
                r.off = next_hdr

        pre = _header_preface_len(r.data, r.off, r.be, vstride)
        if pre is None:
            next_hdr = find_meshbuffer_with_marker(r.data, r.off+1, r.be, vstride, expected_marker=want)
            if next_hdr is None:
                print(f"No further plausible MeshBuffer at 0x{r.off:08X}. Stopping.")
                break
            print(f"[sync] re-align to header @0x{next_hdr:08X}")
            r.off = next_hdr
            pre = _header_preface_len(r.data, r.off, r.be, vstride)
            if pre is None:
                print(f"No valid header even after re-align @0x{r.off:08X}. Stopping.")
                break

        try:
            vstart, vtx, positions, faces = parse_buffer(r, which, pre, vstride, write_faces)
            out = file_path.with_suffix(f'.buf{which}.glb')
            write_glb(out, positions, faces, file_path.name, which)
            which += 1
        except Exception as e:
            print(f"Stop at buffer {which}: {e} (offset 0x{here:08X})")
            break

if __name__ == '__main__':
    main()
