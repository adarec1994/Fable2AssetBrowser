#!/usr/bin/env python3
"""
convert.py
----------
Conversion logic for Fable 2 .mdl files to .glb format.
Adapted from the original f2export.py script to be used as a library.
"""
from __future__ import annotations
import json
import math
import struct
import tempfile
from pathlib import Path
from typing import List, Tuple, Optional, Dict
import bnk_core as core
import os
import shutil

DEFAULT_VERTEX_STRIDE = 28
UV_OFFSET_IN_STRIDE = 20
SUBMESH_DESC_SIZE = 41
INDEX_STRIDE = 2
UNK16_STRIDE = 16
MAX_STR_LEN = 8192

_DEF_EXTS = ('.tex', '.dds', '.tga')
_ALLOWED_START = set(range(48, 58)) | set(range(65, 91)) | set(range(97, 123)) | {0x2F, 0x5C, 0x2E, 0x5F, 0x3A}


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
    return ((-1.0) if s else 1.0) * (1.0 + f / 1024.0) * (2.0 ** (e - 15))


def read_half_be(b: bytes) -> float:
    return _half_to_float((b[0] << 8) | b[1])


def read_half_le(b: bytes) -> float:
    return _half_to_float((b[1] << 8) | b[0])


def _u32_be(b: bytes, i: int) -> int:
    return struct.unpack_from('>I', b, i)[0]


def _u32_le(b: bytes, i: int) -> int:
    return struct.unpack_from('<I', b, i)[0]


class Reader:
    def __init__(self, data: bytes, big_endian: bool = True):
        self.data = data
        self.be = big_endian
        self.endian = '>' if big_endian else '<'
        self.off = 0

    def need(self, n: int):
        if self.off + n > len(self.data):
            raise ValueError(f"Read beyond EOF @0x{self.off:08X} need {n} have {len(self.data) - self.off}")

    def can(self, n: int) -> bool:
        return self.off + n <= len(self.data)

    def rbytes(self, n: int) -> bytes:
        self.need(n)
        b = self.data[self.off:self.off + n]
        self.off += n
        return b

    def ru32(self) -> int:
        return struct.unpack(self.endian + 'I', self.rbytes(4))[0]

    def rf32(self) -> float:
        return struct.unpack(self.endian + 'f', self.rbytes(4))[0]

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

    def ru32_as_rf32_arr(self, k: int):
        ints = self.ru32_arr(k)
        pack_fmt = self.endian + 'I'
        unpack_fmt = self.endian + 'f'
        return [struct.unpack(unpack_fmt, struct.pack(pack_fmt, v))[0] for v in ints]


def _plausible_meshbuffer_counts(idx: int, vtx: int, sm: int, file_len: int, at: int, preamble: int,
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
    idx = U(data, at + 12)
    vtx = U(data, at + 16)
    sm = U(data, at + 20)
    return _plausible_meshbuffer_counts(idx, vtx, sm, len(data), at, 8, vertex_stride)


def looks_like_meshbuffer_special(data: bytes, at: int, be: bool, vertex_stride: int) -> bool:
    if at + 33 > len(data): return False
    U = _u32_be if be else _u32_le
    idx = U(data, at + 21)
    vtx = U(data, at + 25)
    sm = U(data, at + 29)
    return _plausible_meshbuffer_counts(idx, vtx, sm, len(data), at, 17, vertex_stride)


def _header_preface_len(data: bytes, at: int, be: bool, vertex_stride: int) -> Optional[int]:
    if looks_like_meshbuffer0(data, at, be, vertex_stride): return 8
    if looks_like_meshbuffer_special(data, at, be, vertex_stride): return 17
    return None


def _marker_matches(data: bytes, at: int, expected_marker: int) -> bool:
    if at + 13 > len(data): return False
    return data[at + 12] == (expected_marker & 0xFF)


def find_meshbuffer_with_marker(data: bytes, start_at: int, be: bool, vertex_stride: int, expected_marker: int) -> \
Optional[int]:
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


def skip_to_mesh_headers(r: Reader, vertex_stride: int) -> Tuple[int, List[Dict], List[List[float]]]:
    r.ru32_arr(8)
    bones: List[Dict] = []
    bone_count = r.ru32()
    if 0 < bone_count < 65535:
        for i in range(bone_count):
            name = r.rstr()
            bone_id = r.ru32()
            bones.append({"name": name, "id": bone_id, "original_index": i})
    bone_transforms: List[List[float]] = []
    bt_count = r.ru32()
    if 0 < bt_count < 65535 and bt_count == bone_count:
        for _ in range(bt_count):
            bone_transforms.append(r.ru32_as_rf32_arr(11))
    else:
        if bt_count != bone_count and bt_count > 0:
            pass
        for _ in range(min(bt_count, 65535)):
            r.rbytes(44)
    r.rf32_arr(10)
    mesh_count = r.ru32()
    r.ru32_arr(2);
    r.rbytes(13);
    r.ru32_arr(5)
    unk6c = r.ru32()
    if 0 < unk6c < 65535:
        r.rf32_arr(unk6c)
    r.ru32()
    data = r.data
    for _ in range(mesh_count):
        r.ru32()
        r.rstr()
        r.rf32_arr(2)
        r.rbytes(21)
        r.rf32()
        r.ru32_arr(3)
        matc = r.ru32()
        for _ in range(min(matc, 200000)):
            hdr = find_any_meshbuffer(data, r.off, r.be, vertex_stride)
            if hdr is not None and hdr == r.off:
                return mesh_count, bones, bone_transforms
            r.rstr();
            r.rstr();
            r.rstr()
            if r.can(3 + 12):
                r.rbytes(3);
                r.rf32_arr(3)
            elif r.can(16 + 3 + 12):
                r.rbytes(16);
                r.rbytes(3);
                r.rf32_arr(3)
            else:
                hdr = find_any_meshbuffer(data, r.off, r.be, vertex_stride)
                if hdr is not None and hdr >= r.off:
                    r.off = hdr
                    return mesh_count, bones, bone_transforms
                break
            if r.can(16) and (r.off + 16 < len(data)):
                b0 = data[r.off]
                b16 = data[r.off + 16]
                if (b0 not in _ALLOWED_START) and (b16 in _ALLOWED_START):
                    r.rbytes(16)
    hdr = find_any_meshbuffer(data, r.off, r.be, vertex_stride)
    if hdr is None:
        raise ValueError("Could not find a MeshBuffer header after meshes")
    r.off = hdr
    return mesh_count, bones, bone_transforms


def build_faces_from_indices(indices: List[int], vcount: int) -> List[Tuple[int, int, int]]:
    faces: List[Tuple[int, int, int]] = []
    if 0xFFFF not in indices:
        for i in range(0, len(indices) - 2, 3):
            a, b, c = indices[i], indices[i + 1], indices[i + 2]
            if not (a >= vcount or b >= vcount or c >= vcount or a == b or b == c or a == c):
                faces.append((a + 1, b + 1, c + 1))
        return faces
    si = 0
    n = len(indices)
    while si < n:
        try:
            ei = indices.index(0xFFFF, si)
        except ValueError:
            ei = n
        seg = indices[si:ei]
        for t in range(2, len(seg)):
            a, b, c = seg[t - 2], seg[t - 1], seg[t]
            if a == b or b == c or a == c:
                continue
            tri = (a, b, c) if (t % 2) == 0 else (a, c, b)
            if all(v < vcount for v in tri):
                faces.append((tri[0] + 1, tri[1] + 1, tri[2] + 1))
        si = ei + 1
    return faces


def parse_buffer(r: Reader, preface_len: int, vertex_stride: int):
    if preface_len == 8:
        r.ru32_arr(2)
    elif preface_len == 17:
        r.rbytes(17)
    else:
        raise ValueError(f"Unsupported preface length: {preface_len}")
    r.ru32()
    idx = r.ru32()
    vtx = r.ru32()
    sm = r.ru32()
    r.off += sm * SUBMESH_DESC_SIZE
    vstart = r.off
    max_v = min(vtx, max(0, (len(r.data) - vstart) // vertex_stride))
    vtx = max_v if max_v != vtx else vtx
    positions, texcoords = [], []
    if vtx > 0:
        read_half = read_half_be if r.be else read_half_le
        raw = r.data[vstart:vstart + vtx * vertex_stride]
        for i in range(vtx):
            base = i * vertex_stride
            x, y, z = (read_half(raw[base:base + 2]),
                       read_half(raw[base + 2:base + 4]),
                       read_half(raw[base + 4:base + 6]))
            positions.append((x, y, z))
            if base + UV_OFFSET_IN_STRIDE + 4 <= len(raw):
                u = read_half(raw[base + UV_OFFSET_IN_STRIDE:base + UV_OFFSET_IN_STRIDE + 2])
                v = 1.0 - read_half(raw[base + UV_OFFSET_IN_STRIDE + 2:base + UV_OFFSET_IN_STRIDE + 4])
                texcoords.append((u, v))
    r.off = vstart + vtx * vertex_stride
    faces, indices = [], []
    if idx > 0:
        face_bytes = r.rbytes(min(idx * INDEX_STRIDE, len(r.data) - r.off))
        u16fmt = '>H' if r.be else '<H'
        indices = [struct.unpack(u16fmt, face_bytes[i:i + 2])[0] for i in range(0, len(face_bytes), 2)]
        faces = build_faces_from_indices(indices, vtx)
    else:
        r.off += idx * INDEX_STRIDE
    unk16_bytes = vtx * UNK16_STRIDE
    if r.off + unk16_bytes <= len(r.data):
        r.off += unk16_bytes
    return positions, faces, texcoords


def write_merged_glb(out_path: Path, all_geometries: List[dict], src_name: str, bones: List[Dict],
                     bone_transforms: List[List[float]]):
    gltf = {"asset": {"version": "2.0", "generator": "fable2-bnk-converter"}, "scene": 0, "scenes": [{"nodes": []}],
            "nodes": [], "meshes": [], "buffers": [], "bufferViews": [], "accessors": []}
    bin_data = bytearray()
    processed_primitives = []

    for geom in all_geometries:
        if not geom['positions']: continue
        pos_f32 = [c for p in geom['positions'] for c in p]
        min_pos = [min(pos_f32[i::3]) for i in range(3)]
        max_pos = [max(pos_f32[i::3]) for i in range(3)]
        pos_bytes = struct.pack(f"<{len(pos_f32)}f", *pos_f32)
        bv_idx = len(gltf['bufferViews'])
        gltf['bufferViews'].append(
            {"buffer": 0, "byteOffset": len(bin_data), "byteLength": len(pos_bytes), "target": 34962})
        bin_data.extend(pos_bytes)
        acc_idx = len(gltf['accessors'])
        gltf['accessors'].append(
            {"bufferView": bv_idx, "componentType": 5126, "count": len(geom['positions']), "type": "VEC3",
             "min": min_pos, "max": max_pos})
        prim = {"attributes": {"POSITION": acc_idx}}
        if geom['texcoords']:
            uv_f32 = [c for uv in geom['texcoords'] for c in uv]
            uv_bytes = struct.pack(f"<{len(uv_f32)}f", *uv_f32)
            bv_idx = len(gltf['bufferViews'])
            gltf['bufferViews'].append(
                {"buffer": 0, "byteOffset": len(bin_data), "byteLength": len(uv_bytes), "target": 34962})
            bin_data.extend(uv_bytes)
            acc_idx = len(gltf['accessors'])
            gltf['accessors'].append(
                {"bufferView": bv_idx, "componentType": 5126, "count": len(geom['texcoords']), "type": "VEC2"})
            prim["attributes"]["TEXCOORD_0"] = acc_idx
        flat_idx = [i - 1 for face in geom['faces'] for i in face]
        if flat_idx:
            max_index = max(flat_idx)
            packer, comp_type = ('H', 5123) if max_index < 65536 else ('I', 5125)
            idx_bytes = struct.pack(f"<{len(flat_idx)}{packer}", *flat_idx)
            bv_idx = len(gltf['bufferViews'])
            gltf['bufferViews'].append(
                {"buffer": 0, "byteOffset": len(bin_data), "byteLength": len(idx_bytes), "target": 34963})
            bin_data.extend(idx_bytes)
            acc_idx = len(gltf['accessors'])
            gltf['accessors'].append(
                {"bufferView": bv_idx, "componentType": comp_type, "count": len(flat_idx), "type": "SCALAR"})
            prim.update({"indices": acc_idx, "mode": 4})
        processed_primitives.append({'primitive': prim, 'which': geom['which']})

    scene_root_nodes = []
    if bones:
        valid_bones = [b for b in bones if "Rig_Asset" not in b['name']]
        original_to_node = {b['original_index']: i for i, b in enumerate(valid_bones)}
        joint_indices = list(original_to_node.values())
        for bone in valid_bones:
            node = {"name": bone['name']}
            if bone_transforms and bone['original_index'] < len(bone_transforms):
                tf = bone_transforms[bone['original_index']]
                if len(tf) >= 10:
                    node.update({"rotation": tf[0:4], "translation": tf[4:7], "scale": tf[7:10]})
            gltf['nodes'].append(node)
        for i, bone in enumerate(valid_bones):
            parent_idx = bone['id']
            if parent_idx in original_to_node and parent_idx != bone['original_index']:
                parent_node_idx = original_to_node[parent_idx]
                if "children" not in gltf['nodes'][parent_node_idx]:
                    gltf['nodes'][parent_node_idx]["children"] = []
                gltf['nodes'][parent_node_idx]["children"].append(i)
            else:
                scene_root_nodes.append(i)

        ibm_floats = [c for _ in joint_indices for c in [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]]
        ibm_bytes = struct.pack(f'<{len(ibm_floats)}f', *ibm_floats)
        bv_idx = len(gltf['bufferViews'])
        gltf['bufferViews'].append({"buffer": 0, "byteOffset": len(bin_data), "byteLength": len(ibm_bytes)})
        bin_data.extend(ibm_bytes)
        acc_idx = len(gltf['accessors'])
        gltf['accessors'].append(
            {"bufferView": bv_idx, "componentType": 5126, "count": len(joint_indices), "type": "MAT4"})
        gltf.setdefault('skins', []).append({"inverseBindMatrices": acc_idx, "joints": joint_indices})

    for item in processed_primitives:
        mesh_idx = len(gltf['meshes'])
        gltf['meshes'].append({"name": f"{src_name}_mesh_{item['which']}", "primitives": [item['primitive']]})
        node_idx = len(gltf['nodes'])
        node = {"mesh": mesh_idx, "name": f"{src_name}_node_{item['which']}"}
        if bones: node["skin"] = 0
        gltf['nodes'].append(node)
        scene_root_nodes.append(node_idx)

    root_node_idx = len(gltf['nodes'])
    gltf['nodes'].append({"name": "root", "children": scene_root_nodes,
                          "rotation": [-math.sin(math.pi / 4), 0, 0, math.cos(math.pi / 4)]})
    gltf['scenes'][0]['nodes'].append(root_node_idx)

    gltf['buffers'].append({"byteLength": len(bin_data)})
    json_txt = json.dumps(gltf, separators=(',', ':'))
    json_bytes = json_txt.encode('utf-8')
    json_bytes += b' ' * ((4 - len(json_bytes) % 4) % 4)
    total_len = 12 + 8 + len(json_bytes) + 8 + len(bin_data)
    with open(out_path, 'wb') as f:
        f.write(struct.pack('<IIII', 0x46546C67, 2, total_len, len(json_bytes)))
        f.write(b'JSON')
        f.write(json_bytes)
        f.write(struct.pack('<I', len(bin_data)))
        f.write(b'BIN\x00')
        f.write(bin_data)


def convert_single_mdl(mdl_path: str, out_dir: str):
    mdl_path = Path(mdl_path)
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    data = mdl_path.read_bytes()
    r = Reader(data, big_endian=True)

    _, bones, bone_transforms = skip_to_mesh_headers(r, DEFAULT_VERTEX_STRIDE)

    all_geometries = []
    which = 0
    while r.off < len(r.data) - 64:
        pre = _header_preface_len(data, r.off, r.be, DEFAULT_VERTEX_STRIDE)
        if pre is None:
            next_hdr = find_any_meshbuffer(data, r.off + 1, r.be, DEFAULT_VERTEX_STRIDE)
            if next_hdr is None: break
            r.off = next_hdr
            pre = _header_preface_len(data, r.off, r.be, DEFAULT_VERTEX_STRIDE)
            if pre is None: break

        try:
            positions, faces, texcoords = parse_buffer(r, pre, DEFAULT_VERTEX_STRIDE)
            if positions:
                all_geometries.append({'positions': positions, 'faces': faces, 'texcoords': texcoords, 'which': which})
            which += 1
        except Exception:
            break

    if all_geometries:
        out_path = out_dir / mdl_path.with_suffix('.glb').name
        write_merged_glb(out_path, all_geometries, mdl_path.stem, bones, bone_transforms)

