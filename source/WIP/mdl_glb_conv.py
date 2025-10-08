#!/usr/bin/env python3
"""MDL to GLB converter with material extraction and mesh merging."""

from __future__ import annotations
import json
import math
import struct
import sys
from pathlib import Path
from typing import List, Tuple, Optional, Dict

VERTEX_STRIDE = 28
UV_OFFSET = 20
SUBMESH_SIZE = 41
INDEX_SIZE = 2
UNK16_SIZE = 16
MAX_STR = 8192

EXTS = ('.tex', '.dds', '.tga')
ASCII_START = set(range(48,58)) | set(range(65,91)) | set(range(97,123)) | {0x2F,0x5C,0x2E,0x5F,0x3A}

def half_to_float(h: int) -> float:
    s, e, f = (h >> 15) & 1, (h >> 10) & 0x1F, h & 0x3FF
    if e == 0:
        return 0.0 if f == 0 else ((-1.0) ** s) * (f / 1024.0) * (2.0 ** -14)
    if e == 0x1F:
        return float('-inf' if s else 'inf') if f == 0 else float('nan')
    return ((-1.0) ** s) * (1.0 + f/1024.0) * (2.0 ** (e - 15))

def read_half(b: bytes, be: bool) -> float:
    return half_to_float((b[0] << 8) | b[1] if be else (b[1] << 8) | b[0])

def u32(b: bytes, i: int, be: bool) -> int:
    return struct.unpack_from('>I' if be else '<I', b, i)[0]

def pad4(n: int) -> int:
    return (n + 3) & ~3

def name_to_color(name: str) -> List[float]:
    h = 2166136261
    for ch in name.encode('utf-8', 'ignore'):
        h = ((h ^ ch) * 16777619) & 0xFFFFFFFF
    return [0.35 + 0.55 * ((h >> (i*8)) & 0xFF) / 255.0 for i in range(3)] + [1.0]

class Reader:
    def __init__(self, data: bytes, be: bool = True):
        self.data, self.be, self.off = data, be, 0
        self.fmt = '>' if be else '<'

    def need(self, n: int):
        if self.off + n > len(self.data):
            raise ValueError(f"EOF @0x{self.off:08X}")

    def can(self, n: int) -> bool:
        return self.off + n <= len(self.data)

    def rbytes(self, n: int) -> bytes:
        self.need(n)
        b = self.data[self.off:self.off+n]
        self.off += n
        return b

    def ru32(self) -> int:
        return struct.unpack(self.fmt+'I', self.rbytes(4))[0]

    def rf32(self) -> float:
        return struct.unpack(self.fmt+'f', self.rbytes(4))[0]

    def rstr(self, maxlen: int = MAX_STR) -> str:
        out = bytearray()
        limit = min(len(self.data), self.off + maxlen)
        while self.off < limit:
            c = self.rbytes(1)
            if c == b'\x00': break
            out.extend(c)
        return out.decode('utf-8', errors='ignore')

    def ru32_arr(self, k: int):
        return [self.ru32() for _ in range(k)]

    def rf32_arr(self, k: int):
        return [self.rf32() for _ in range(k)]

    def ru32_as_rf32_arr(self, k: int):
        return [struct.unpack(self.fmt+'f', struct.pack(self.fmt+'I', self.ru32()))[0] for _ in range(k)]

def valid_counts(sm: int, idx: int, vtx: int, flen: int, at: int, pre: int, stride: int) -> bool:
    if not (0 <= sm <= 4096 and 1 <= vtx <= 300_000 and 0 <= idx <= 5_000_000):
        return False
    return (at + pre + 16 + sm * SUBMESH_SIZE + vtx * stride) <= flen

def is_meshbuffer(data: bytes, at: int, be: bool, stride: int, offsets: tuple) -> bool:
    pre, s_off, i_off, v_off, sm_off = offsets
    if at + sm_off + 4 > len(data): return False
    U = lambda i: u32(data, at + i, be)
    return valid_counts(U(sm_off), U(i_off), U(v_off), len(data), at, pre, stride)

def meshbuffer_type(data: bytes, at: int, be: bool, stride: int) -> Optional[int]:
    if is_meshbuffer(data, at, be, stride, (8, 8, 12, 16, 20)): return 8
    if is_meshbuffer(data, at, be, stride, (17, 17, 21, 25, 29)): return 17
    return None

def find_buffer(data: bytes, start: int, be: bool, stride: int, marker: Optional[int] = None) -> Optional[int]:
    check = lambda i: (meshbuffer_type(data, i, be, stride) is not None and 
                      (marker is None or (i + 12 < len(data) and data[i + 12] == (marker & 0xFF))))
    
    for i in range(max(0, start), len(data) - 64):
        if check(i): return i
    for i in range(0, len(data) - 64):
        if check(i): return i
    return None

def parse_headers(r: Reader, stride: int) -> Tuple[int, List[Dict], List[List[float]], List[List[Dict]]]:
    r.ru32_arr(8)
    
    bones = []
    bone_count = r.ru32()
    if 0 < bone_count < 65535:
        bones = [{"name": r.rstr(), "id": r.ru32(), "original_index": i} for i in range(bone_count)]
    
    transforms = []
    bt_count = r.ru32()
    if 0 < bt_count < 65535:
        if bt_count == bone_count:
            transforms = [r.ru32_as_rf32_arr(11) for _ in range(bt_count)]
        else:
            [r.rbytes(44) for _ in range(min(bt_count, 65535))]
    
    r.rf32_arr(10)
    mesh_count = r.ru32()
    r.ru32_arr(2); r.rbytes(13); r.ru32_arr(5)
    
    unk6c = r.ru32()
    if 0 < unk6c < 65535:
        r.rf32_arr(unk6c)
    r.ru32()
    
    materials = [[] for _ in range(max(mesh_count, 0))]
    for mi in range(mesh_count):
        r.ru32(); mesh_name = r.rstr(); r.rf32_arr(2); r.rbytes(21); r.rf32(); r.ru32_arr(3)
        matc = r.ru32()
        
        mats = []
        for _ in range(min(matc, 200000)):
            tex, spec, norm = r.rstr(), r.rstr(), r.rstr()
            
            if r.can(15):
                r.rbytes(3); r.rf32_arr(3)
            elif r.can(31):
                r.rbytes(19); r.rf32_arr(3)
            else:
                break
            
            mats.append({
                "display_name": tex or spec or norm or f"Mesh{mi}_Mat{len(mats)}",
                "albedo": tex, "spec": spec, "normal": norm
            })
            
            if r.can(16) and not (r.data[r.off] in ASCII_START) and r.data[r.off+16] in ASCII_START:
                r.rbytes(16)
        
        materials[mi] = mats
    
    hdr = find_buffer(r.data, r.off, r.be, stride)
    if hdr is None:
        raise ValueError("No MeshBuffer found")
    if hdr != r.off:
        print(f"[sync] advancing to MeshBuffer @0x{hdr:08X}")
        r.off = hdr
    
    return mesh_count, bones, transforms, materials

def build_faces(indices: List[int], vcount: int) -> List[Tuple[int,int,int]]:
    faces = []
    has_restart = 0xFFFF in indices
    
    if not has_restart:
        for i in range(0, (len(indices) // 3) * 3, 3):
            a, b, c = indices[i:i+3]
            if a < vcount and b < vcount and c < vcount and len({a,b,c}) == 3:
                faces.append((a+1, b+1, c+1))
        return faces
    
    si = 0
    while si < len(indices):
        ei = si
        while ei < len(indices) and indices[ei] != 0xFFFF:
            ei += 1
        
        seg = indices[si:ei]
        for t in range(2, len(seg)):
            a, b, c = seg[t-2:t+1]
            if len({a,b,c}) < 3: continue
            tri = (a, b, c) if (t % 2) == 0 else (a, c, b)
            if all(x < vcount for x in tri):
                faces.append(tuple(x+1 for x in tri))
        si = ei + 1
    
    return faces

def parse_buffer(r: Reader, which: int, pre: int, stride: int, write_faces: bool):
    start = r.off
    print(f"\nBuffer {which} @0x{start:08X} (preface {pre})")
    print(f"  initial 13 bytes: {' '.join(f'{b:02X}' for b in r.data[start:start+13])}")
    
    r.rbytes(pre)
    some, idx, vtx, sm = r.ru32(), r.ru32(), r.ru32(), r.ru32()
    r.off += sm * SUBMESH_SIZE
    
    vstart = r.off
    max_v = min(vtx, max(0, (len(r.data) - vstart) // stride))
    vtx = min(vtx, max_v)
    
    positions, texcoords = [], []
    if vtx > 0:
        raw = r.data[vstart:vstart + vtx * stride]
        for i in range(vtx):
            base = i * stride
            positions.append(tuple(read_half(raw[base+j:base+j+2], r.be) for j in range(0, 6, 2)))
            if base + UV_OFFSET + 4 <= len(raw):
                u = read_half(raw[base + UV_OFFSET:base + UV_OFFSET + 2], r.be)
                v = 1.0 - read_half(raw[base + UV_OFFSET + 2:base + UV_OFFSET + 4], r.be)
                texcoords.append((u, v))
    
    r.off = vstart + vtx * stride
    face_start = r.off
    
    faces, indices = [], []
    if write_faces and idx > 0:
        avail = min(idx * INDEX_SIZE, len(r.data) - r.off)
        blob = r.rbytes(max(0, avail))
        fmt = '>H' if r.be else '<H'
        indices = [struct.unpack(fmt, blob[i:i+2])[0] for i in range(0, len(blob)-1, 2)]
        faces = build_faces(indices, vtx)
    else:
        r.off += idx * INDEX_SIZE
    
    if r.off + vtx * UNK16_SIZE <= len(r.data):
        r.off += vtx * UNK16_SIZE
    
    mode = "triangle strips" if (write_faces and 0xFFFF in indices) else "triangles"
    print(f"  vertex: offset=0x{vstart:08X} count={vtx} stride={stride}")
    print(f"  faces: offset=0x{face_start:08X} count={idx} mode={mode} {'(built '+str(len(faces))+' tris)' if write_faces and idx else '(skipped)'}")
    
    return vstart, vtx, positions, faces, texcoords

def write_glb(path: Path, geoms: List[dict], src: str, bones: List[Dict], 
             transforms: List[List[float]], mats_per_mesh: List[List[Dict]]):
    
    gltf = {
        "asset": {"version": "2.0", "generator": "mdl_glb_converter"},
        "scene": 0, "scenes": [{"nodes": []}], "nodes": [], "meshes": [],
        "materials": [], "buffers": [{}], "bufferViews": [], "accessors": []
    }
    
    bin_data = bytearray()
    
    gltf["materials"].append({
        "name": "Fallback",
        "pbrMetallicRoughness": {"baseColorFactor": [0.7, 0.7, 0.7, 1.0], 
                                "metallicFactor": 0.0, "roughnessFactor": 1.0}
    })
    
    mat_map = {}
    for mi, mats in enumerate(mats_per_mesh):
        if not mats: continue
        key = (mi, 0)
        if key in mat_map: continue
        m = mats[0]
        gltf["materials"].append({
            "name": m.get("display_name", f"Mesh{mi}_Mat0"),
            "pbrMetallicRoughness": {"baseColorFactor": name_to_color(m.get("display_name", "")),
                                    "metallicFactor": 0.1, "roughnessFactor": 0.9}
        })
        mat_map[key] = len(gltf["materials"]) - 1
    
    processed = []
    for g in geoms:
        if not g['positions']: continue
        
        pos_data = struct.pack('<' + 'f' * (len(g['positions']) * 3),
                              *[c for p in g['positions'] for c in p])
        pos_off = len(bin_data)
        bin_data.extend(pos_data)
        bin_data.extend(b'\x00' * ((4 - len(bin_data) % 4) % 4))
        
        gltf["bufferViews"].append({"buffer": 0, "byteOffset": pos_off, 
                                   "byteLength": len(pos_data), "target": 34962})
        
        bounds = [[min(p[i] for p in g['positions']) if math.isfinite(min(p[i] for p in g['positions'])) else 0.0,
                  max(p[i] for p in g['positions']) if math.isfinite(max(p[i] for p in g['positions'])) else 0.0] 
                 for i in range(3)]
        
        gltf["accessors"].append({
            "bufferView": len(gltf["bufferViews"]) - 1, "componentType": 5126,
            "count": len(g['positions']), "type": "VEC3",
            "min": [b[0] for b in bounds], "max": [b[1] for b in bounds]
        })
        
        prim = {"attributes": {"POSITION": len(gltf["accessors"]) - 1}}
        
        if g['texcoords']:
            uv_data = struct.pack('<' + 'f' * (len(g['texcoords']) * 2),
                                 *[c for uv in g['texcoords'] for c in uv])
            uv_off = len(bin_data)
            bin_data.extend(uv_data)
            bin_data.extend(b'\x00' * ((4 - len(bin_data) % 4) % 4))
            
            gltf["bufferViews"].append({"buffer": 0, "byteOffset": uv_off,
                                       "byteLength": len(uv_data), "target": 34962})
            gltf["accessors"].append({"bufferView": len(gltf["bufferViews"]) - 1,
                                     "componentType": 5126, "count": len(g['texcoords']), "type": "VEC2"})
            prim["attributes"]["TEXCOORD_0"] = len(gltf["accessors"]) - 1
        
        if g['faces']:
            flat = [i - 1 for f in g['faces'] for i in f]
            comp = 5123 if max(flat) < 65536 else 5125
            fmt = 'H' if comp == 5123 else 'I'
            idx_data = struct.pack('<' + fmt * len(flat), *flat)
            idx_off = len(bin_data)
            bin_data.extend(idx_data)
            bin_data.extend(b'\x00' * ((4 - len(bin_data) % 4) % 4))
            
            gltf["bufferViews"].append({"buffer": 0, "byteOffset": idx_off,
                                       "byteLength": len(idx_data), "target": 34963})
            gltf["accessors"].append({"bufferView": len(gltf["bufferViews"]) - 1,
                                     "componentType": comp, "count": len(flat), "type": "SCALAR"})
            prim.update({"indices": len(gltf["accessors"]) - 1, "mode": 4})
        else:
            prim["mode"] = 0
        
        mat_idx = mat_map.get((g['which'], 0), 0)
        prim["material"] = mat_idx
        processed.append({'prim': prim, 'which': g['which']})
    
    skin_idx = None
    roots = []
    
    if bones:
        ignore = {b['original_index'] for b in bones if "Rig_Asset" in b['name']}
        parent_map = {b['original_index']: b['id'] for b in bones}
        
        for i in range(len(bones)):
            if i in ignore: continue
            pid = parent_map.get(i)
            while pid is not None and pid in ignore:
                pid = parent_map.get(pid)
            parent_map[i] = pid
        
        idx_to_node = {}
        joints = []
        
        for b in bones:
            oi = b['original_index']
            if oi in ignore: continue
            
            ni = len(gltf["nodes"])
            idx_to_node[oi] = ni
            joints.append(ni)
            
            node = {"name": b['name']}
            if transforms and oi < len(transforms) and len(transforms[oi]) >= 10:
                t = transforms[oi]
                node.update({"rotation": t[0:4], "translation": t[4:7], "scale": t[7:10]})
            gltf["nodes"].append(node)
        
        for b in bones:
            oi = b['original_index']
            if oi in ignore: continue
            
            pid = parent_map.get(oi)
            cni = idx_to_node[oi]
            
            if pid is None or pid == oi or pid not in idx_to_node:
                roots.append(cni)
            else:
                pni = idx_to_node[pid]
                if "children" not in gltf["nodes"][pni]:
                    gltf["nodes"][pni]["children"] = []
                gltf["nodes"][pni]["children"].append(cni)
        
        ibm_data = struct.pack('<' + 'f' * (len(joints) * 16),
                              *([1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1] * len(joints)))
        ibm_off = len(bin_data)
        bin_data.extend(ibm_data)
        bin_data.extend(b'\x00' * ((4 - len(bin_data) % 4) % 4))
        
        gltf["bufferViews"].append({"buffer": 0, "byteOffset": ibm_off, "byteLength": len(ibm_data)})
        gltf["accessors"].append({"bufferView": len(gltf["bufferViews"]) - 1,
                                 "componentType": 5126, "count": len(joints), "type": "MAT4"})
        gltf["skins"] = [{"inverseBindMatrices": len(gltf["accessors"]) - 1, "joints": joints}]
        skin_idx = 0
    
    for item in processed:
        mesh_idx = len(gltf["meshes"])
        gltf["meshes"].append({"name": f"{src}#buf{item['which']}", "primitives": [item['prim']]})
        
        node = {"mesh": mesh_idx, "name": f"node_buf{item['which']}"}
        if skin_idx is not None and item['which'] == 0:
            node["skin"] = skin_idx
        
        node_idx = len(gltf["nodes"])
        gltf["nodes"].append(node)
        roots.append(node_idx)
    
    root_idx = len(gltf["nodes"])
    gltf["nodes"].append({
        "name": "Root",
        "rotation": [-math.sin(math.pi/4), 0, 0, math.cos(math.pi/4)],
        "children": roots
    })
    gltf["scenes"][0]["nodes"] = [root_idx]
    
    if not gltf["meshes"]:
        print("No geometry to export")
        return
    
    gltf["buffers"][0]["byteLength"] = len(bin_data)
    
    json_bytes = json.dumps(gltf, separators=(',', ':')).encode('utf-8')
    json_bytes += b' ' * ((4 - len(json_bytes) % 4) % 4)
    
    total = 12 + 8 + len(json_bytes) + 8 + len(bin_data)
    
    with open(path, 'wb') as f:
        f.write(struct.pack('<IIII', 0x46546C67, 2, total, len(json_bytes)) + b'JSON' + json_bytes)
        f.write(struct.pack('<I', len(bin_data)) + b'BIN\x00' + bin_data)
    
    print(f"\n✓ Exported: {len(gltf['meshes'])} meshes, {len(gltf['materials'])} materials, {len(bones)} bones → {path}")

def main():
    if len(sys.argv) < 2:
        print("Usage: python mdl_glb.py <file.mdl> [--little-endian] [--stride N] [--no-faces]")
        sys.exit(1)
    
    file_path = Path(sys.argv[1])
    be = '--little-endian' not in sys.argv
    faces = '--no-faces' not in sys.argv
    stride = VERTEX_STRIDE
    
    if '--stride' in sys.argv:
        try:
            stride = int(sys.argv[sys.argv.index('--stride') + 1])
        except:
            print("--stride needs integer")
            sys.exit(2)
    
    data = file_path.read_bytes()
    r = Reader(data, be)
    
    print(f"File: {file_path.name} | {len(data)} bytes | {'BE' if be else 'LE'} | Stride: {stride}")
    
    mesh_count, bones, transforms, materials = parse_headers(r, stride)
    if mesh_count <= 0:
        print('No meshes found')
        return
    
    if bones:
        print(f"Found {len(bones)} bones, {len(transforms)} transforms")
    
    print(f"MeshCount: {mesh_count}")
    for i, mats in enumerate(materials):
        if mats:
            print(f"  Mesh {i}: {', '.join(m.get('display_name', f'Mat{j}') for j, m in enumerate(mats))}")
    
    hdr0 = find_buffer(r.data, r.off, r.be, stride, 0)
    if hdr0 and hdr0 != r.off:
        print(f"[sync] align to buffer0 @0x{hdr0:08X}")
        r.off = hdr0
    
    geoms = []
    which = 0
    
    while which < mesh_count:
        here = r.off
        want = which & 0xFF
        
        if not (r.off + 12 < len(r.data) and r.data[r.off + 12] == want):
            next_hdr = find_buffer(r.data, here, r.be, stride, want)
            if not next_hdr:
                print(f"No buffer with marker {want:02X}. Stopping.")
                break
            if next_hdr != here:
                print(f"[sync] skip to marker {want:02X} @0x{next_hdr:08X}")
                r.off = next_hdr
        
        pre = meshbuffer_type(r.data, r.off, r.be, stride)
        if not pre:
            next_hdr = find_buffer(r.data, r.off + 1, r.be, stride, want)
            if not next_hdr:
                print(f"No valid buffer. Stopping.")
                break
            print(f"[sync] realign @0x{next_hdr:08X}")
            r.off = next_hdr
            pre = meshbuffer_type(r.data, r.off, r.be, stride)
            if not pre:
                print(f"Invalid header. Stopping.")
                break
        
        try:
            _, _, positions, faces_data, texcoords = parse_buffer(r, which, pre, stride, faces)
            if positions:
                geoms.append({'positions': positions, 'faces': faces_data, 
                            'texcoords': texcoords, 'which': which, 'mat_slot': 0})
            which += 1
        except Exception as e:
            print(f"Error at buffer {which}: {e}")
            break
    
    if which >= mesh_count:
        print(f"\nReached MeshCount ({mesh_count})")
    
    if geoms:
        write_glb(file_path.with_suffix('.glb'), geoms, file_path.name, bones, transforms, materials)
    else:
        print("\nNo valid buffers to export")

if __name__ == '__main__':
    main()