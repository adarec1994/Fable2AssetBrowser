bl_info = {
    "name": "Import Fable 2 MDL (Improved)",
    "author": "Matthew, W (modified)",
    "version": (1, 3, 0),
    "blender": (3, 0, 0),
    "location": "File > Import > Fable 2 MDL (.mdl)",
    "description": "MDL import with improved bone orientation",
    "category": "Import-Export",
}

import bpy
from bpy.types import Operator
from bpy.props import StringProperty, BoolProperty, IntProperty, EnumProperty
from bpy_extras.io_utils import ImportHelper

from pathlib import Path
from typing import List, Dict, Optional, Tuple
import struct, math
import mathutils

VERTEX_STRIDE = 28
UV_OFFSET = 20
SUBMESH_SIZE = 41
INDEX_SIZE = 2
UNK16_SIZE = 16
MAX_STR = 8192
ASCII_START = set(range(48,58)) | set(range(65,91)) | set(range(97,123)) | {0x2F,0x5C,0x2E,0x5F,0x3A}

def half_to_float(h: int) -> float:
    s, e, f = (h >> 15) & 1, (h >> 10) & 0x1F, h & 0x3FF
    if e == 0:  return 0.0 if f == 0 else ((-1.0) ** s) * (f / 1024.0) * (2.0 ** -14)
    if e == 0x1F:  return float('-inf') if (f==0 and s) else (float('inf') if f==0 else float('nan'))
    return ((-1.0) ** s) * (1.0 + f/1024.0) * (2.0 ** (e - 15))

def read_half(b: bytes, be: bool) -> float:
    return half_to_float((b[0] << 8) | b[1] if be else (b[1] << 8) | b[0])

def u32(data: bytes, i: int, be: bool) -> int:
    return struct.unpack_from('>I' if be else '<I', data, i)[0]

# Reader (same behaviour as your script)
class Reader:
    def __init__(self, data: bytes, be: bool = True):
        self.data, self.be, self.off = data, be, 0
        self.fmt = '>' if be else '<'
    def need(self, n: int):
        if self.off + n > len(self.data): raise ValueError(f"EOF @0x{self.off:08X}")
    def can(self, n: int) -> bool: return self.off + n <= len(self.data)
    def rbytes(self, n: int) -> bytes:
        self.need(n); b = self.data[self.off:self.off+n]; self.off += n; return b
    def ru32(self) -> int: return struct.unpack(self.fmt+'I', self.rbytes(4))[0]
    def rf32(self) -> float: return struct.unpack(self.fmt+'f', self.rbytes(4))[0]
    def rstr(self, maxlen: int = MAX_STR) -> str:
        out = bytearray(); limit = min(len(self.data), self.off + maxlen)
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
    if not (0 <= sm <= 4096 and 1 <= vtx <= 300_000 and 0 <= idx <= 5_000_000): return False
    return (at + pre + 16 + sm * SUBMESH_SIZE + vtx * stride) <= flen

def is_meshbuffer(data: bytes, at: int, be: bool, stride: int, offsets: tuple) -> bool:
    pre, s_off, i_off, v_off, sm_off = offsets
    if at + sm_off + 4 > len(data): return False
    U = lambda i: u32(data, at + i, be)
    return valid_counts(U(sm_off), U(i_off), U(v_off), len(data), at, pre, stride)

def meshbuffer_type(data: bytes, at: int, be: bool, stride: int) -> Optional[int]:
    if is_meshbuffer(data, at, be, stride, (8, 8, 12, 16, 20)):  return 8
    if is_meshbuffer(data, at, be, stride, (17, 17, 21, 25, 29)): return 17
    return None

def find_buffer(data: bytes, start: int, be: bool, stride: int, marker: Optional[int] = None) -> Optional[int]:
    def check(i):
        ok = meshbuffer_type(data, i, be, stride) is not None
        if not ok: return False
        return True if marker is None else (i + 12 < len(data) and data[i + 12] == (marker & 0xFF))
    for i in range(max(0, start), len(data) - 64):
        if check(i): return i
    for i in range(0, len(data) - 64):
        if check(i): return i
    return None

def _looks_like_str_start(b: bytes, off: int) -> bool:
    if off >= len(b): return False
    return b[off] in ASCII_START

def parse_headers(r: Reader, stride: int):
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
    if 0 < unk6c < 65535: r.rf32_arr(unk6c)
    r.ru32()
    # skip/collect materials (not used for armature)
    for _ in range(max(mesh_count, 0)):
        r.ru32(); _ = r.rstr(); r.rf32_arr(2); r.rbytes(21); r.rf32(); r.ru32_arr(3)
        matc = r.ru32()
        for _m in range(min(matc, 256)):
            if not r.can(1) or not _looks_like_str_start(r.data, r.off): break
            _ = r.rstr(); _ = r.rstr(); _ = r.rstr()
            if r.can(15): r.rbytes(3); r.rf32_arr(3)
            elif r.can(31): r.rbytes(19); r.rf32_arr(3)
            else: break
        if r.can(16) and not (r.data[r.off] in ASCII_START) and r.data[r.off+16] in ASCII_START:
            r.rbytes(16)
    # align to first MeshBuffer
    hdr = find_buffer(r.data, r.off, r.be, stride)
    if hdr is None: raise ValueError("No MeshBuffer found")
    if hdr != r.off: r.off = hdr
    return mesh_count, bones, transforms

def build_faces(indices: List[int], vcount: int):
    faces = []
    if 0xFFFF not in indices:
        for i in range(0, (len(indices)//3)*3, 3):
            a,b,c = indices[i:i+3]
            if a<vcount and b<vcount and c<vcount and len({a,b,c})==3:
                faces.append((a,b,c))
        return faces
    si = 0
    while si < len(indices):
        ei = si
        while ei < len(indices) and indices[ei] != 0xFFFF: ei += 1
        seg = indices[si:ei]
        for t in range(2, len(seg)):
            a,b,c = seg[t-2:t+1]
            if len({a,b,c}) < 3: continue
            tri = (a,b,c) if (t%2)==0 else (a,c,b)
            if all(x<vcount for x in tri): faces.append(tri)
        si = ei + 1
    return faces

def parse_buffer(r: Reader, which: int, pre: int, stride: int, write_faces: bool):
    r.rbytes(pre)
    some, idx, vtx, sm = r.ru32(), r.ru32(), r.ru32(), r.ru32()
    r.off += sm * SUBMESH_SIZE
    vstart = r.off
    max_v = min(vtx, max(0, (len(r.data) - vstart) // stride))
    vtx = min(vtx, max_v)
    positions, texcoords, weights, joints = [], [], [], []
    if vtx > 0:
        raw = r.data[vstart:vstart + vtx * stride]
        for i in range(vtx):
            base = i * stride
            positions.append(tuple(read_half(raw[base+j:base+j+2], r.be) for j in range(0,6,2)))
            if base + 20 <= len(raw):
                bone_idx = raw[base + 15]
                weight_val = raw[base + 19]
                if bone_idx < 255:
                    joints.append([bone_idx,0,0,0])
                    weights.append([weight_val/255.0 if weight_val>0 else 1.0, 0.0,0.0,0.0])
                else:
                    joints.append([0,0,0,0]); weights.append([1.0,0.0,0.0,0.0])
            if base + UV_OFFSET + 4 <= len(raw):
                u = read_half(raw[base + UV_OFFSET:base + UV_OFFSET + 2], r.be)
                v = 1.0 - read_half(raw[base + UV_OFFSET + 2:base + UV_OFFSET + 4], r.be)
                texcoords.append((u,v))
    r.off = vstart + vtx * stride
    faces = []
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
    return vstart, vtx, positions, faces, texcoords, weights, joints

def local_trs_matrix(rec: List[float], quat_w_first: bool, invert_quat: bool = True) -> mathutils.Matrix:
    if quat_w_first:
        qw,qx,qy,qz = rec[0],rec[1],rec[2],rec[3]
    else:
        qx,qy,qz,qw = rec[0],rec[1],rec[2],rec[3]
    
    q = mathutils.Quaternion((qw,qx,qy,qz))
    
    # MaxScript uses inverse quaternion
    if invert_quat:
        q = q.inverted()
    
    R = q.to_matrix().to_4x4()
    T = mathutils.Matrix.Translation((rec[4],rec[5],rec[6]))
    
    # In MaxScript, scale is stored but often not used in final transform
    # Using identity scale for now
    S = mathutils.Matrix.Identity(4)
    
    return T @ R @ S

def compose_global(bones: List[Dict], transforms: List[List[float]], parent_map: Dict[int, Optional[int]],
                   quat_w_first: bool, apply_root_fix: bool, invert_quat: bool = True) -> Dict[int, mathutils.Matrix]:
    local = {}
    for b in bones:
        oi = b['original_index']
        if oi < len(transforms) and len(transforms[oi])>=10:
            local[oi] = local_trs_matrix(transforms[oi], quat_w_first, invert_quat)
    global_m = {}
    visiting=set()
    def get_g(oi: int) -> mathutils.Matrix:
        if oi in global_m: return global_m[oi]
        if oi in visiting: return mathutils.Matrix.Identity(4)
        visiting.add(oi)
        p = parent_map.get(oi)
        if p is None or p not in local:
            M = local.get(oi, mathutils.Matrix.Identity(4))
        else:
            M = get_g(p) @ local.get(oi, mathutils.Matrix.Identity(4))
        visiting.discard(oi)
        global_m[oi] = M
        return M
    for b in bones:
        oi = b['original_index']
        if oi in local: get_g(oi)
    
    # Apply coordinate system conversion (from MaxScript)
    if apply_root_fix:
        # MaxScript uses: matrix3 [-1,0,0] [0,-1,0] [0,0,1] [0,0,0]
        conversion = mathutils.Matrix([
            [-1, 0, 0, 0],
            [0, -1, 0, 0],
            [0, 0, 1, 0],
            [0, 0, 0, 1]
        ])
        for k in list(global_m.keys()):
            # Only apply to root bones (no parent)
            if parent_map.get(k) is None:
                global_m[k] = conversion @ global_m[k]
    
    return global_m

def build_armature(name: str, bones: List[Dict], transforms: List[List[float]],
                   quat_w_first: bool, no_root_rot: bool, bone_axis: str = 'Y',
                   invert_quat: bool = True) -> bpy.types.Object:
    ignore = {b['original_index'] for b in bones if "Rig_Asset" in b['name']}
    parent_map = {b['original_index']: b['id'] for b in bones}
    for i in range(len(bones)):
        pid = parent_map.get(i)
        while pid is not None and pid in ignore:
            pid = parent_map.get(pid)
        parent_map[i] = pid

    G = compose_global(bones, transforms, parent_map, quat_w_first, 
                      apply_root_fix=not no_root_rot, invert_quat=invert_quat)

    arm_data = bpy.data.armatures.new(name + "_Armature")
    arm_obj  = bpy.data.objects.new(name + "_Armature", object_data=arm_data)
    bpy.context.scene.collection.objects.link(arm_obj)
    bpy.context.view_layer.objects.active = arm_obj
    
    # Set display options
    arm_data.display_type = 'STICK'
    arm_data.show_names = True
    arm_data.show_axes = True
    arm_obj.show_in_front = True
    
    bpy.ops.object.mode_set(mode='EDIT')

    eb_map: Dict[int, bpy.types.EditBone] = {}
    for b in bones:
        oi = b['original_index']
        if oi in ignore or oi not in G: continue
        M = G[oi]
        head = M.to_translation()
        
        # Choose axis based on parameter
        if bone_axis == 'X':
            axis = M.col[0].to_3d()  # X-axis
        elif bone_axis == 'Y':
            axis = M.col[1].to_3d()  # Y-axis
        elif bone_axis == 'Z':
            axis = M.col[2].to_3d()  # Z-axis
        else:
            axis = mathutils.Vector((0,1,0))  # Default Y
            
        if axis.length < 1e-12: 
            axis = mathutils.Vector((0,1,0))  # Fallback to Y
        axis.normalize()
        
        eb = arm_data.edit_bones.new(b['name'])
        eb.head = head
        eb.tail = head + axis * 0.02  # Temporary tail
        eb.use_connect = False
        eb_map[oi] = eb

    # Set up parent relationships and collect children
    children: Dict[int, List[int]] = {oi: [] for oi in eb_map}
    for b in bones:
        oi = b['original_index']
        if oi not in eb_map: continue
        pid = parent_map.get(oi)
        if pid is not None and pid in eb_map:
            eb_map[oi].parent = eb_map[pid]
            children[pid].append(oi)

    # Improved bone tail positioning - point toward children
    for oi, eb in eb_map.items():
        M = G[oi]
        head = eb.head
        kids = children.get(oi, [])
        
        if kids:
            # Has children - point toward them
            if len(kids) == 1:
                # Single child - point directly at it
                child_head = eb_map[kids[0]].head
                direction = (child_head - head)
                if direction.length > 0.001:
                    eb.tail = child_head
                else:
                    # Too close, use partial distance
                    eb.tail = head + direction.normalized() * 0.05
            else:
                # Multiple children - point toward average position
                avg_pos = mathutils.Vector((0, 0, 0))
                for kid in kids:
                    avg_pos += eb_map[kid].head
                avg_pos /= len(kids)
                direction = (avg_pos - head)
                if direction.length > 0.001:
                    # Use fraction of distance to avoid overlapping
                    eb.tail = head + direction * 0.8
                else:
                    eb.tail = head + direction.normalized() * 0.05
        else:
            # No children - use transform's primary axis
            if bone_axis == 'X':
                axis = M.col[0].to_3d()
            elif bone_axis == 'Y':
                axis = M.col[1].to_3d()
            elif bone_axis == 'Z':
                axis = M.col[2].to_3d()
            else:
                axis = mathutils.Vector((0,1,0))
                
            if axis.length < 1e-12:
                axis = mathutils.Vector((0,1,0))
            axis.normalize()
            
            # Make leaf bones longer for visibility
            eb.tail = head + axis * 0.1
        
        # Ensure tail is not same as head
        if (eb.tail - eb.head).length < 0.001:
            eb.tail = eb.head + mathutils.Vector((0, 0.05, 0))
        
        # Set roll based on Z-axis
        z_axis = M.col[2].to_3d()
        if z_axis.length > 1e-12:
            z_axis.normalize()
            eb.align_roll(z_axis)

    bpy.ops.object.mode_set(mode='OBJECT')
    return arm_obj

def build_mesh_object(name: str, positions: List[Tuple[float,float,float]],
                      faces: List[Tuple[int,int,int]],
                      uvs: Optional[List[Tuple[float,float]]] = None,
                      root_rotate_fix: bool = True) -> bpy.types.Object:
    mesh = bpy.data.meshes.new(name + "_Mesh")
    verts = [mathutils.Vector(p) for p in positions]
    if root_rotate_fix:
        # Use same coordinate conversion as bones
        conversion = mathutils.Matrix([
            [-1, 0, 0],
            [0, -1, 0],
            [0, 0, 1]
        ])
        verts = [conversion @ v for v in verts]
    mesh.from_pydata([tuple(v) for v in verts], [], faces)
    mesh.update(calc_edges=True)
    if uvs and len(uvs) == len(verts):
        uv_layer = mesh.uv_layers.new(name="UVMap")
        uv_layer.active = True
        li = 0
        for poly in mesh.polygons:
            for loop_index in poly.loop_indices:
                vi = mesh.loops[loop_index].vertex_index
                uv_layer.data[li].uv = uvs[vi]
                li += 1
    obj = bpy.data.objects.new(name + "_Obj", mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj

def apply_skin_weights_single(mesh_obj: bpy.types.Object, arm_obj: bpy.types.Object,
                              joints: List[List[int]], weights: List[List[float]], bones: List[Dict]):
    idx_to_name = {b['original_index']: b['name'] for b in bones if "Rig_Asset" not in b['name']}
    vg = {oi: mesh_obj.vertex_groups.new(name=n) for oi,n in idx_to_name.items()}
    mesh_obj.parent = arm_obj
    mod = mesh_obj.modifiers.new(name="Armature", type='ARMATURE'); mod.object = arm_obj; mod.use_vertex_groups = True
    me = mesh_obj.data
    n = min(len(me.vertices), len(joints))
    for i in range(n):
        j = joints[i][0] if joints[i] else 0
        w = weights[i][0] if weights[i] else 1.0
        g = vg.get(j)
        if g: g.add([i], float(w), 'REPLACE')

class IMPORT_OT_fable2_mdl_native(Operator, ImportHelper):
    bl_idname = "import_scene.fable2_mdl_improved"
    bl_label = "Import Fable 2 MDL (Improved Bones)"
    bl_options = {'REGISTER', 'UNDO'}

    filename_ext = ".mdl"
    filter_glob: StringProperty(default="*.mdl", options={'HIDDEN'})

    little_endian: BoolProperty(name="Little Endian", default=False)
    stride: IntProperty(name="Vertex Stride", default=VERTEX_STRIDE, min=12, max=64)
    build_faces: BoolProperty(name="Build Faces", default=True)
    quat_w_first: BoolProperty(name="Quaternion W First", default=True, 
                               description="MaxScript format uses W first")
    no_root_rot: BoolProperty(name="No Root Conversion", default=False,
                             description="Skip coordinate system conversion")
    bone_axis: EnumProperty(
        name="Bone Primary Axis",
        description="Which axis bones point along",
        items=[
            ('X', "X-Axis", "Bones point along local X (MaxScript default)"),
            ('Y', "Y-Axis", "Bones point along local Y (Blender default)"),
            ('Z', "Z-Axis", "Bones point along local Z"),
        ],
        default='Y'
    )
    invert_quat: BoolProperty(name="Invert Quaternion", default=True,
                             description="MaxScript uses inverse quaternion")

    def execute(self, context):
        mdl_path = Path(self.filepath)
        data = mdl_path.read_bytes()
        r = Reader(data, be=not self.little_endian)
        try:
            mesh_count, bones, transforms = parse_headers(r, self.stride)
        except Exception as e:
            self.report({'ERROR'}, f"Header parse failed: {e}")
            return {'CANCELLED'}

        try:
            arm_obj = build_armature(mdl_path.stem, bones, transforms, 
                                   self.quat_w_first, self.no_root_rot, 
                                   self.bone_axis, self.invert_quat)
        except Exception as e:
            self.report({'ERROR'}, f"Armature build failed: {e}")
            return {'CANCELLED'}

        created = False
        which = 0
        hdr0 = find_buffer(r.data, r.off, r.be, self.stride, 0)
        if hdr0 and hdr0 != r.off: r.off = hdr0

        def seek_next(cur_off: int, want_marker: int) -> Optional[int]:
            nh = find_buffer(r.data, cur_off, r.be, self.stride, want_marker)
            if nh is not None: return nh
            return find_buffer(r.data, cur_off, r.be, self.stride, None)

        while which < mesh_count:
            here = r.off
            want = which & 0xFF
            if not (r.off + 12 < len(r.data) and r.data[r.off + 12] == want):
                nh = seek_next(here, want)
                if nh is None: break
                r.off = nh
            pre = meshbuffer_type(r.data, r.off, r.be, self.stride)
            if not pre:
                nh = seek_next(r.off + 1, want)
                if nh is None: break
                r.off = nh
                pre = meshbuffer_type(r.data, r.off, r.be, self.stride)
                if not pre: break

            try:
                _, vtx, positions, faces, uvs, weights, joints = parse_buffer(r, which, pre, self.stride, self.build_faces)
            except Exception as e:
                self.report({'WARNING'}, f"Mesh {which} parse failed: {e}")
                which += 1; continue

            if positions:
                mesh_obj = build_mesh_object(f"{mdl_path.stem}_buf{which}", positions, faces,
                                             uvs if uvs and len(uvs)==len(positions) else None,
                                             root_rotate_fix=(not self.no_root_rot))
                try:
                    apply_skin_weights_single(mesh_obj, arm_obj, joints, weights, bones)
                except Exception as e:
                    self.report({'WARNING'}, f"Skin assign failed on buf{which}: {e}")
                created = True
            which += 1

        if not created:
            self.report({'WARNING'}, "No geometry buffers produced any vertices")
        self.report({'INFO'}, f"Imported {mdl_path.name} with improved bone orientation")
        return {'FINISHED'}

def menu_func_import(self, context):
    self.layout.operator(IMPORT_OT_fable2_mdl_native.bl_idname, text="Fable 2 MDL (.mdl) - Improved")

def register():
    bpy.utils.register_class(IMPORT_OT_fable2_mdl_native)
    bpy.types.TOPBAR_MT_file_import.append(menu_func_import)

def unregister():
    bpy.types.TOPBAR_MT_file_import.remove(menu_func_import)
    bpy.utils.unregister_class(IMPORT_OT_fable2_mdl_native)

if __name__ == "__main__":
    register()