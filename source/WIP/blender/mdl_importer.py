#------------------------------------------------
#--- 010 Editor v2.0 Binary Template
#
#      File: MDL.bt
#   Authors: Converted from Hex Workshop format
#   Version: 1.2
#   Purpose: Parse MDL model files
#  Category: Game
# File Mask: *.mdl
#  ID Bytes:
#   History:
#------------------------------------------------
# (Reference template content above; Blender code below)

bl_info = {
    "name": "Fable MDL (auto strip/list) + Materials + UVs + Skeleton",
    "author": "ChatGPT",
    "version": (1, 5, 3),
    "blender": (3, 0, 0),
    "location": "File > Import > Fable MDL",
    "description": "Imports .mdl meshes with UVs, skeleton, and weights",
    "category": "Import-Export",
}

import bpy
from bpy.props import StringProperty, BoolProperty
from bpy_extras.io_utils import ImportHelper
import struct
import os
from mathutils import Matrix, Vector, Quaternion

class BER:
    def __init__(self, data: bytes):
        self.b = data
        self.i = 0
        self.n = len(data)
    def tell(self) -> int:
        return self.i
    def seek(self, off: int):
        if not (0 <= off <= self.n):
            raise ValueError("seek out of bounds")
        self.i = off
    def read(self, n: int) -> bytes:
        if self.i + n > self.n:
            raise EOFError("read beyond end")
        s = self.b[self.i:self.i + n]
        self.i += n
        return s
    def u8(self) -> int:
        return struct.unpack(">B", self.read(1))[0]
    def u16(self) -> int:
        return struct.unpack(">H", self.read(2))[0]
    def u32(self) -> int:
        return struct.unpack(">I", self.read(4))[0]
    def f32(self) -> float:
        return struct.unpack(">f", self.read(4))[0]
    def bytes_fixed(self, n: int) -> bytes:
        return self.read(n)
    def cstring(self, max_scan: int = 1_000_000) -> str:
        start = self.i
        limit = min(self.n, start + max_scan)
        j = start
        while j < limit and self.b[j] != 0:
            j += 1
        if j >= limit:
            raise ValueError("unterminated string")
        s = self.b[start:j]
        self.i = j + 1
        try:
            return s.decode("utf-8", errors="replace")
        except Exception:
            return s.decode("latin1", errors="replace")

def _bounded_count(x: int, lo: int = 0, hi: int = 65535) -> int:
    return x if (lo <= x < hi) else 0

def half_to_float(h: int) -> float:
    s = (h >> 15) & 0x0001
    e = (h >> 10) & 0x001F
    f = h & 0x03FF
    if e == 0:
        if f == 0:
            return -0.0 if s else 0.0
        while (f & 0x0400) == 0:
            f <<= 1
            e -= 1
        e += 1
        f &= ~0x0400
    elif e == 31:
        if f == 0:
            return float("-inf") if s else float("inf")
        return float("nan")
    e = e + (127 - 15)
    f = f << 13
    i = (s << 31) | (e << 23) | f
    return struct.unpack("!f", struct.pack("!I", i))[0]

class Vertex:
    __slots__ = ("PosX","PosY","PosZ","PosW","Normal","BoneID1","BoneID2","BoneID3","BoneID4","Weight1","Weight2","Weight3","Weight4","UV_U","UV_V","Color")
    def __init__(self, PosX, PosY, PosZ, PosW, Normal, BoneID1, BoneID2, BoneID3, BoneID4, Weight1, Weight2, Weight3, Weight4, UV_U, UV_V, Color):
        self.PosX=PosX; self.PosY=PosY; self.PosZ=PosZ; self.PosW=PosW
        self.Normal=Normal
        self.BoneID1=BoneID1; self.BoneID2=BoneID2; self.BoneID3=BoneID3; self.BoneID4=BoneID4
        self.Weight1=Weight1; self.Weight2=Weight2; self.Weight3=Weight3; self.Weight4=Weight4
        self.UV_U=UV_U; self.UV_V=UV_V
        self.Color=Color

def parse_bone_entry(r: BER):
    name = r.cstring()
    parent = r.u32()
    return (name, parent)

def parse_bone_transform(r: BER):
    qx = r.f32(); qy = r.f32(); qz = r.f32(); qw = r.f32()
    tx = r.f32(); ty = r.f32(); tz = r.f32()
    sx = r.f32(); sy = r.f32(); sz = r.f32()
    _ = r.f32()
    return {"quat": (qx, qy, qz, qw), "pos": (tx, ty, tz), "scale": (sx, sy, sz)}

def parse_material(r: BER):
    texture = r.cstring()
    spec = r.cstring()
    normal = r.cstring()
    unkname = r.cstring()
    tint = r.cstring()
    _unk1 = r.u32()
    _unk2a = r.u32()
    _unk2b = r.u32()
    check_pos = r.tell()
    check_byte = r.u8()
    if check_byte != 0x01:
        r.seek(check_pos)
    return (texture, spec, normal, unkname, tint)

def parse_vertex(r: BER) -> Vertex:
    posx = r.u16()
    posy = r.u16()
    posz = r.u16()
    posw = r.u16()
    normal = r.u32()
    b1 = r.u8()
    b2 = r.u8()
    b3 = r.u8()
    b4 = r.u8()
    w1 = r.u8()
    w2 = r.u8()
    w3 = r.u8()
    w4 = r.u8()
    u = r.u16()
    v = r.u16()
    color = r.u32()
    return Vertex(posx, posy, posz, posw, normal, b1, b2, b3, b4, w1, w2, w3, w4, u, v, color)

def parse_unk16(r: BER):
    return [r.f32() for _ in range(4)]

def parse_submeshdesc(r: BER):
    u1 = r.u32()
    u2 = r.u8()
    u3 = [r.u32(), r.u32(), r.u32()]
    u4 = [r.f32() for _ in range(6)]
    return (u1, u2, u3, u4)

def parse_mesh(r: BER):
    u1 = r.u32()
    name = r.cstring()
    u2 = [r.f32(), r.f32()]
    u3 = r.bytes_fixed(21)
    u4 = r.f32()
    u5 = [r.u32(), r.u32(), r.u32()]
    mcount = r.u32()
    mats = []
    for _ in range(_bounded_count(mcount)):
        mats.append(parse_material(r))
    return {"Unk1":u1,"MeshName":name,"Unk2":u2,"Unk3":u3,"Unk4":u4,"Unk5":u5,"MaterialCount":mcount,"Materials":mats}

def parse_meshbuffer(r: BER):
    u8pair = [r.u32(), r.u32()]
    some_count1 = r.u32()
    tri_len = r.u32()
    vcount = r.u32()
    smcount = r.u32()
    for _ in range(_bounded_count(smcount)):
        _ = parse_submeshdesc(r)
    vertices = [parse_vertex(r) for _ in range(_bounded_count(vcount))]
    faces_idx = [r.u16() for _ in range(_bounded_count(tri_len))]
    for _ in range(_bounded_count(vcount)):
        _ = parse_unk16(r)
    _ = r.bytes_fixed(9)
    return {
        "Unk8":u8pair,
        "SomeCount1":some_count1,
        "IndexCount":tri_len,
        "VertexCount":vcount,
        "SubMeshCount":smcount,
        "Vertices":vertices,
        "FacesIdx":faces_idx,
    }

def parse_mdl(data: bytes):
    r = BER(data)
    _ = r.bytes_fixed(8)
    _ = r.u32()
    _ = r.u32()
    _ = r.bytes_fixed(88)
    _ = [r.u32() for _ in range(8)]
    bone_count = r.u32()
    bones = []
    for i in range(_bounded_count(bone_count)):
        bones.append(parse_bone_entry(r) + (i,))
    bt_count = r.u32()
    transforms = []
    for _ in range(_bounded_count(bt_count)):
        transforms.append(parse_bone_transform(r))
    _ = [r.f32() for _ in range(10)]
    mesh_count = r.u32()
    _ = [r.u32(), r.u32()]
    _ = r.bytes_fixed(13)
    _ = [r.u32() for _ in range(5)]
    unk6_count = r.u32()
    for _ in range(_bounded_count(unk6_count)):
        _ = r.f32()
    _ = r.u32()
    meshes = [parse_mesh(r) for _ in range(_bounded_count(mesh_count))]
    mbs    = [parse_meshbuffer(r) for _ in range(_bounded_count(mesh_count))]
    return {"Meshes": meshes, "MeshBuffers": mbs, "Bones": bones, "Transforms": transforms}

def verts_uvs_weights_from_meshbuffer(mb, bone_names):
    FSCALE = 50.0
    verts = []
    uvs = []
    weight_data = []
    for v in mb["Vertices"]:
        x = half_to_float(v.PosX) * FSCALE * 2
        y = half_to_float(v.PosY) * FSCALE * 2
        z = half_to_float(v.PosZ) * FSCALE * 2
        verts.append((x, y, z))
        u = half_to_float(v.UV_U)
        v_coord = 1.0 - half_to_float(v.UV_V)
        uvs.append((u, v_coord))
        bone1 = v.BoneID1
        bone2 = v.BoneID2
        bone3 = v.BoneID3
        bone4 = v.BoneID4
        weight1 = v.Weight1
        weight2 = v.Weight2
        weight3 = v.Weight3
        weight4 = v.Weight4
        maxweight = 0
        if bone1 != 0: maxweight += weight1
        if bone2 != 0: maxweight += weight2
        if bone3 != 0: maxweight += weight3
        if bone4 != 0: maxweight += weight4
        w = {'boneids': [], 'weights': []}
        if maxweight != 0:
            mxw = 255.0
            if weight1 != 0: w['boneids'].append(bone1); w['weights'].append(weight1 / mxw)
            if weight2 != 0: w['boneids'].append(bone2); w['weights'].append(weight2 / mxw)
            if weight3 != 0: w['boneids'].append(bone3); w['weights'].append(weight3 / mxw)
            if weight4 != 0: w['boneids'].append(bone4); w['weights'].append(weight4 / mxw)
        weight_data.append(w)
    return verts, uvs, weight_data

def split_by_restart(indices):
    segs = []
    i = 0
    n = len(indices)
    while i < n:
        j = i
        while j < n and indices[j] != 0xFFFF:
            j += 1
        if j - i > 0:
            segs.append(indices[i:j])
        i = j + 1
    return segs

def build_faces_from_strip_segment(seg, vcount, flip):
    faces = []
    for t in range(2, len(seg)):
        a, b, c = int(seg[t-2]), int(seg[t-1]), int(seg[t])
        if a == b or b == c or a == c:
            continue
        tri = (a, b, c) if (t % 2 == 0) else (a, c, b)
        if 0 <= tri[0] < vcount and 0 <= tri[1] < vcount and 0 <= tri[2] < vcount:
            faces.append((tri[0], tri[2], tri[1]) if flip else tri)
    return faces

def build_faces_and_matmap(indices, vcount, flip, mat_count):
    if 0xFFFF in indices:
        segs = split_by_restart(indices)
        faces_all = []
        mats = []
        for s, seg in enumerate(segs):
            faces = build_faces_from_strip_segment(seg, vcount, flip)
            faces_all.extend(faces)
            mats.extend([min(s, max(0, mat_count-1))] * len(faces))
        return faces_all, mats
    else:
        faces = []
        usable = (len(indices) // 3) * 3
        for k in range(0, usable, 3):
            a, b, c = int(indices[k]), int(indices[k+1]), int(indices[k+2])
            if a == b or b == c or a == c:
                continue
            if 0 <= a < vcount and 0 <= b < vcount and 0 <= c < vcount:
                faces.append((a, c, b) if flip else (a, b, c))
        mats = [0] * len(faces)
        return faces, mats

def tex_basename(tex_path: str) -> str:
    if not tex_path:
        return ""
    base = tex_path.replace("\\", "/").split("/")[-1]
    if "." in base:
        base = base.rsplit(".", 1)[0]
    return base

def ensure_material(name_hint: str):
    name = name_hint or "Material"
    if name in bpy.data.materials:
        return bpy.data.materials[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    m["fable_tex"] = name
    return m

def create_armature(bones, transforms, collection):
    if not bones:
        return None, {}
    
    FSCALE = 50.0
    
    armature = bpy.data.armatures.new("Armature")
    armature_obj = bpy.data.objects.new("Armature", armature)
    collection.objects.link(armature_obj)
    bpy.context.view_layer.objects.active = armature_obj
    bpy.ops.object.mode_set(mode='EDIT')
    
    edit_bones = []
    for name, parent_id, original_idx in bones:
        eb = armature.edit_bones.new(name)
        eb.head = (0.0, 0.0, 0.0)
        eb.tail = (0.0, 0.01, 0.0)
        edit_bones.append(eb)
    
    for i, (name, parent_id, original_idx) in enumerate(bones):
        if parent_id != 0xFFFFFFFF and 0 <= parent_id < len(edit_bones):
            edit_bones[i].parent = edit_bones[parent_id]
    
    for i, eb in enumerate(edit_bones):
        if i < len(transforms):
            t = transforms[i]
            qx, qy, qz, qw = t["quat"]
            tx, ty, tz = t["pos"]
            
            q = Quaternion((qw, qx, qy, qz))
            q.invert()
            R = q.to_matrix().to_4x4()
            
            T = Matrix.Translation((tx * FSCALE, ty * FSCALE, tz * FSCALE))
            
            M = T @ R
            
            if eb.parent:
                M = eb.parent.matrix @ M
            
            eb.matrix = M
            
            head = eb.head
            tail_offset = (M.to_3x3() @ Vector((0, 0.01, 0)))
            eb.tail = head + tail_offset
    
    bpy.ops.object.mode_set(mode='OBJECT')
    
    armature_obj.matrix_world = Matrix((
        [-1, 0, 0, 0],
        [ 0,-1, 0, 0],
        [ 0, 0, 1, 0],
        [ 0, 0, 0, 1],
    ))
    
    bone_name_map = {i: name for i, (name, _, _) in enumerate(bones)}
    return armature_obj, bone_name_map

def make_mesh_object(name, verts, uvs, weight_data, faces, mat_indices, mat_names, collection, armature_obj, bone_name_map):
    mesh = bpy.data.meshes.new(name + "_mesh")
    mesh.from_pydata(verts, [], faces)
    mesh.validate()
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    mats = []
    for base in mat_names:
        if not base:
            continue
        mats.append(ensure_material(base))
    for m in mats:
        obj.data.materials.append(m)
    if mats and mat_indices and len(mat_indices) == len(obj.data.polygons):
        for p, mi in zip(obj.data.polygons, mat_indices):
            p.material_index = min(mi, len(mats)-1)
    elif mats:
        for p in obj.data.polygons:
            p.material_index = 0
    if uvs and len(uvs) == len(verts):
        uv_layer = mesh.uv_layers.new(name="UVMap")
        for poly in mesh.polygons:
            for loop_idx in poly.loop_indices:
                vert_idx = mesh.loops[loop_idx].vertex_index
                uv_layer.data[loop_idx].uv = uvs[vert_idx]
    if armature_obj and bone_name_map and weight_data:
        used_bones = set()
        for w in weight_data:
            for bone_id in w['boneids']:
                if bone_id in bone_name_map:
                    used_bones.add(bone_name_map[bone_id])
        for bone_name in used_bones:
            if bone_name not in obj.vertex_groups:
                obj.vertex_groups.new(name=bone_name)
        skin_mod = obj.modifiers.new(name="Armature", type='ARMATURE')
        skin_mod.object = armature_obj
        for bone_name in used_bones:
            try:
                skin_mod.object.data.bones[bone_name]
            except KeyError:
                continue
        for vert_idx, w in enumerate(weight_data):
            for bone_id, weight in zip(w['boneids'], w['weights']):
                if bone_id in bone_name_map:
                    bone_name = bone_name_map[bone_id]
                    if bone_name in obj.vertex_groups:
                        obj.vertex_groups[bone_name].add([vert_idx], weight, 'ADD')
        obj.parent = armature_obj
    obj.matrix_world = Matrix((
        [-1, 0, 0, 0],
        [0, -1, 0, 0],
        [0, 0, 1, 0],
        [0, 0, 0, 1]
    ))
    return obj

class IMPORT_OT_fable_mdl_mesh(bpy.types.Operator, ImportHelper):
    bl_idname = "import_scene.fable_mdl_mesh_auto_mats"
    bl_label = "Import Fable MDL (auto strip/list + materials + UVs + Skeleton)"
    bl_options = {'REGISTER', 'UNDO'}
    filename_ext = ".mdl"
    filter_glob: StringProperty(default="*.mdl", options={'HIDDEN'})
    flip_winding: BoolProperty(
        name="Flip Winding",
        description="Swap winding if mesh appears inside-out",
        default=True
    )
    def execute(self, context):
        path = self.filepath
        with open(path, "rb") as f:
            data = f.read()
        mdl = parse_mdl(data)
        base_name = os.path.splitext(os.path.basename(path))[0]
        col = bpy.data.collections.new(base_name + "_MDL")
        context.scene.collection.children.link(col)
        armature_obj = None
        bone_name_map = {}
        bone_names = []
        if mdl["Bones"] and mdl["Transforms"]:
            armature_obj, bone_name_map = create_armature(mdl["Bones"], mdl["Transforms"], col)
            bone_names = [b[0] for b in mdl["Bones"]]
        created = 0
        meshes = mdl["Meshes"]
        mbs = mdl["MeshBuffers"]
        n = min(len(meshes), len(mbs))
        for i in range(n):
            mesh_hdr = meshes[i]
            mb = mbs[i]
            name = mesh_hdr["MeshName"] or f"Mesh_{i}"
            verts, uvs, weight_data = verts_uvs_weights_from_meshbuffer(mb, bone_names)
            if not verts:
                continue
            idx = mb["FacesIdx"]
            faces, mat_map = build_faces_and_matmap(idx, len(verts), self.flip_winding, mesh_hdr["MaterialCount"])
            mat_names = []
            for mat in mesh_hdr["Materials"]:
                base = tex_basename(mat[0])
                if base and base not in mat_names:
                    mat_names.append(base)
            make_mesh_object(name, verts, uvs, weight_data, faces, mat_map, mat_names, col, armature_obj, bone_name_map)
            created += 1
        if created == 0:
            self.report({'WARNING'}, "No meshes created")
        return {'FINISHED'}

def menu_func_import(self, context):
    self.layout.operator(IMPORT_OT_fable_mdl_mesh.bl_idname, text="Fable MDL (.mdl) — auto strip/list + materials + UVs + Skeleton")

classes = (IMPORT_OT_fable_mdl_mesh,)

def register():
    for c in classes:
        bpy.utils.register_class(c)
    bpy.types.TOPBAR_MT_file_import.append(menu_func_import)

def unregister():
    bpy.types.TOPBAR_MT_file_import.remove(menu_func_import)
    for c in reversed(classes):
        bpy.utils.unregister_class(c)

if __name__ == "__main__":
    register()