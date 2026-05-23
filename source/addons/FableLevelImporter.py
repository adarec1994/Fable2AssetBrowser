bl_info = {
    "name": "Fable 2 Level Importer",
    "author": "Fable2AssetBrowser",
    "version": (0, 1, 0),
    "blender": (3, 6, 0),
    "location": "File > Import > Fable 2 Level (.fable)",
    "category": "Import-Export",
}

import json
import os
import re
import struct
from array import array

import bpy
from bpy_extras.io_utils import ImportHelper
from mathutils import Matrix


GLTF_TO_BLENDER = Matrix((
    (1, 0, 0, 0),
    (0, 0, -1, 0),
    (0, 1, 0, 0),
    (0, 0, 0, 1),
))
FABLE_TO_BLENDER = Matrix((
    (1, 0, 0, 0),
    (0, 0, 1, 0),
    (0, 1, 0, 0),
    (0, 0, 0, 1),
))
RAW_MODEL_TO_FABLE_LOCAL = Matrix((
    (1, 0, 0, 0),
    (0, 0, 1, 0),
    (0, 1, 0, 0),
    (0, 0, 0, 1),
))
FBX_MODEL_TO_FABLE_LOCAL = Matrix((
    (1, 0, 0, 0),
    (0, -1, 0, 0),
    (0, 0, 1, 0),
    (0, 0, 0, 1),
))
TERRAIN_IMPORT_TO_EDITOR = FABLE_TO_BLENDER @ GLTF_TO_BLENDER.inverted()
FBX_TERRAIN_IMPORT_TO_EDITOR = FABLE_TO_BLENDER


def fable_matrix(values, model_local=RAW_MODEL_TO_FABLE_LOCAL):
    if not values or len(values) != 16:
        return Matrix.Identity(4)
    raw = Matrix((
        (values[0], values[1], values[2], values[3]),
        (values[4], values[5], values[6], values[7]),
        (values[8], values[9], values[10], values[11]),
        (values[12], values[13], values[14], values[15]),
    ))
    return FABLE_TO_BLENDER @ raw @ model_local


def raw_prop_matrix(inst, export_format):
    raw = inst.get("raw") or []
    model_local = FBX_MODEL_TO_FABLE_LOCAL if export_format == "FBX" else RAW_MODEL_TO_FABLE_LOCAL
    if len(raw) < 13:
        return fable_matrix(inst.get("matrix"), model_local)

    px = raw[0]
    py = raw[2]
    pz = raw[1]
    if inst.get("fullTransform"):
        scale = raw[12]
        if not isinstance(scale, (int, float)) or abs(scale) <= 0.000001:
            scale = 1.0
        r = raw[3:12]
        level = Matrix((
            (r[0] * scale, r[1] * scale, r[2] * scale, px),
            (r[3] * scale, r[4] * scale, r[5] * scale, py),
            (r[6] * scale, r[7] * scale, r[8] * scale, pz),
            (0, 0, 0, 1),
        ))
    else:
        s = raw[6]
        c = raw[7]
        sx = raw[9] if raw[9] != 0 else 1.0
        sy = raw[10] if raw[10] != 0 else sx
        sz = raw[11] if raw[11] != 0 else sx
        level = Matrix((
            (c * sx, 0, s * sy, px),
            (0, sz, 0, py),
            (-s * sx, 0, c * sy, pz),
            (0, 0, 0, 1),
        ))
    return FABLE_TO_BLENDER @ level @ model_local


def ensure_collection(name, parent=None):
    coll = bpy.data.collections.get(name)
    if coll is None:
        coll = bpy.data.collections.new(name)
        target = parent if parent is not None else bpy.context.scene.collection
        target.children.link(coll)
    return coll


def make_data_collection(name):
    existing = bpy.data.collections.get(name)
    if existing is None:
        return bpy.data.collections.new(name)
    index = 1
    while True:
        candidate = bpy.data.collections.get(name + "." + str(index).zfill(3))
        if candidate is None:
            return bpy.data.collections.new(name + "." + str(index).zfill(3))
        index += 1


def clean_name(value, fallback):
    text = value or fallback
    text = os.path.splitext(os.path.basename(text))[0] or fallback
    text = re.sub(r"[^A-Za-z0-9_. -]+", "_", text)
    return text[:63] if text else fallback


def import_scene_file(path):
    before = set(bpy.data.objects)
    ext = os.path.splitext(path)[1].lower()
    if ext in {".glb", ".gltf"}:
        bpy.ops.import_scene.gltf(filepath=path)
    elif ext == ".fbx":
        bpy.ops.import_scene.fbx(filepath=path)
    else:
        raise RuntimeError("Unsupported model format: " + ext)
    imported = [obj for obj in bpy.data.objects if obj not in before]
    imported_set = set(imported)
    roots = [obj for obj in imported if obj.parent not in imported_set]
    return imported, roots


def move_to_collection(objects, collection):
    for obj in objects:
        for coll in list(obj.users_collection):
            coll.objects.unlink(obj)
        collection.objects.link(obj)


def move_to_data_collection(objects, name):
    collection = make_data_collection(name)
    for obj in objects:
        for coll in list(obj.users_collection):
            coll.objects.unlink(obj)
        collection.objects.link(obj)
    return collection


def duplicate_tree(src, parent, collection):
    dup = src.copy()
    dup.data = src.data
    collection.objects.link(dup)
    dup.parent = parent
    dup.matrix_parent_inverse.identity()
    dup.matrix_local = src.matrix_local if src.parent else src.matrix_world
    for child in src.children:
        duplicate_tree(child, dup, collection)
    return dup


def find_terrain_uv_layers(obj):
    if obj.type != "MESH" or not obj.data.uv_layers:
        return "", ""
    names = [uv.name for uv in obj.data.uv_layers]
    tiled = names[0]
    weights = names[1] if len(names) > 1 else names[0]
    for name in names:
        lname = name.lower()
        if "weight" in lname or "terrain" in lname:
            weights = name
    return tiled, weights


def load_image(path):
    if not path or not os.path.exists(path):
        return None
    if os.path.splitext(path)[1].lower() == ".dds":
        image = load_rgba_dds(path)
        if image is not None:
            return image
    try:
        return bpy.data.images.load(path, check_existing=True)
    except RuntimeError:
        return None


def load_rgba_dds(path):
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return None
    if len(data) < 128 or data[:4] != b"DDS ":
        return None
    header_size = struct.unpack_from("<I", data, 4)[0]
    height = struct.unpack_from("<I", data, 12)[0]
    width = struct.unpack_from("<I", data, 16)[0]
    pf_size = struct.unpack_from("<I", data, 76)[0]
    bpp = struct.unpack_from("<I", data, 88)[0]
    rmask = struct.unpack_from("<I", data, 92)[0]
    gmask = struct.unpack_from("<I", data, 96)[0]
    bmask = struct.unpack_from("<I", data, 100)[0]
    amask = struct.unpack_from("<I", data, 104)[0]
    if header_size != 124 or pf_size != 32 or bpp != 32:
        return None
    size = int(width) * int(height) * 4
    if width <= 0 or height <= 0 or len(data) < 128 + size:
        return None

    def shift(mask):
        if mask == 0:
            return 0
        s = 0
        while mask & 1 == 0:
            mask >>= 1
            s += 1
        return s

    rs = shift(rmask)
    gs = shift(gmask)
    bs = shift(bmask)
    a_shift = shift(amask)
    pixels = array("f")
    pixels.extend([0.0] * size)
    src = memoryview(data)[128:128 + size]
    out = 0
    for i in range(0, size, 4):
        px = struct.unpack_from("<I", src, i)[0]
        pixels[out + 0] = ((px & rmask) >> rs) / 255.0
        pixels[out + 1] = ((px & gmask) >> gs) / 255.0
        pixels[out + 2] = ((px & bmask) >> bs) / 255.0
        pixels[out + 3] = ((px & amask) >> a_shift) / 255.0 if amask else 1.0
        out += 4
    image = bpy.data.images.new(os.path.basename(path),
                                width=int(width),
                                height=int(height),
                                alpha=True)
    image.pixels.foreach_set(pixels)
    image.filepath = path
    image.pack()
    return image


def make_image_node(nodes, image, uv_node, x, y):
    tex = nodes.new("ShaderNodeTexImage")
    tex.location = (x, y)
    tex.image = image
    tex.extension = "REPEAT"
    tex.interpolation = "Linear"
    return tex


def build_terrain_material(base_dir, terrain, terrain_obj):
    lods = terrain.get("lods") or []
    if terrain_obj.type != "MESH" or not lods:
        return None

    tiled_uv_name, weight_uv_name = find_terrain_uv_layers(terrain_obj)
    mat = bpy.data.materials.new("Fable_EHF_Terrain")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    output = nodes.new("ShaderNodeOutputMaterial")
    output.location = (900, 0)
    bsdf = nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.location = (650, 0)
    links.new(bsdf.outputs["BSDF"], output.inputs["Surface"])

    uv_tiled = nodes.new("ShaderNodeUVMap")
    uv_tiled.location = (-1200, 180)
    uv_tiled.uv_map = tiled_uv_name
    uv_weights = nodes.new("ShaderNodeUVMap")
    uv_weights.location = (-1200, -80)
    uv_weights.uv_map = weight_uv_name

    color_socket = None
    x = -950
    y = 220
    used = 0
    for lod in lods:
        files = lod.get("files") or []
        base_rel = files[0] if len(files) > 0 else ""
        weight_rel = lod.get("weight") or ""
        base_img = load_image(os.path.join(base_dir, base_rel))
        if base_img is None:
            continue

        base_node = make_image_node(nodes, base_img, uv_tiled, x, y)
        params = lod.get("params") or []
        scale = 0.125
        if params and params[0] and params[0][0]:
            scale = float(params[0][0])
        uv_scale = nodes.new("ShaderNodeVectorMath")
        uv_scale.location = (x - 230, y)
        uv_scale.operation = "MULTIPLY"
        uv_scale.inputs[1].default_value[0] = scale / 0.125
        uv_scale.inputs[1].default_value[1] = scale / 0.125
        uv_scale.inputs[1].default_value[2] = 1.0
        links.new(uv_tiled.outputs["UV"], uv_scale.inputs[0])
        links.new(uv_scale.outputs["Vector"], base_node.inputs["Vector"])

        weight_img = load_image(os.path.join(base_dir, weight_rel))
        if weight_img is None:
            if color_socket is None:
                color_socket = base_node.outputs["Color"]
            continue
        weight_node = make_image_node(nodes, weight_img, uv_weights,
                                      x, y - 260)
        weight_node.extension = "CLIP"
        weight_node.image.colorspace_settings.name = "Non-Color"
        links.new(uv_weights.outputs["UV"], weight_node.inputs["Vector"])

        weighted = nodes.new("ShaderNodeMixRGB")
        weighted.location = (x + 250, y - 40)
        weighted.blend_type = "MULTIPLY"
        weighted.inputs["Fac"].default_value = 1.0
        links.new(base_node.outputs["Color"], weighted.inputs["Color1"])
        links.new(weight_node.outputs["Color"], weighted.inputs["Color2"])

        if color_socket is None:
            color_socket = weighted.outputs["Color"]
        else:
            add = nodes.new("ShaderNodeMixRGB")
            add.location = (x + 500, y - 40)
            add.blend_type = "ADD"
            add.inputs["Fac"].default_value = 1.0
            links.new(color_socket, add.inputs["Color1"])
            links.new(weighted.outputs["Color"], add.inputs["Color2"])
            color_socket = add.outputs["Color"]

        x += 300
        y -= 120
        used += 1
        if used >= 24:
            break

    if color_socket is not None:
        links.new(color_socket, bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.9
    return mat


def apply_terrain_material(base_dir, manifest, terrain_objects):
    terrain = manifest.get("terrain") or {}
    for obj in terrain_objects:
        if obj.type != "MESH":
            continue
        mat = build_terrain_material(base_dir, terrain, obj)
        if mat is None:
            continue
        obj.data.materials.clear()
        obj.data.materials.append(mat)


def align_terrain_to_editor_space(manifest, objects):
    terrain = manifest.get("terrain") or {}
    mesh = terrain.get("mesh") or ""
    ext = os.path.splitext(mesh)[1].lower()
    if ext in {".glb", ".gltf"}:
        align = TERRAIN_IMPORT_TO_EDITOR
    elif ext == ".fbx":
        align = FBX_TERRAIN_IMPORT_TO_EDITOR
    else:
        return
    imported = set(objects)
    roots = [obj for obj in objects if obj.parent not in imported]
    for obj in roots:
        obj.matrix_world = align @ obj.matrix_world


def progress_set(window_manager, step, total):
    if total <= 0:
        return
    window_manager.progress_update(min(step, total))


def log_status(text):
    print("Fable importer: " + text)


class FABLE2_OT_import_level(bpy.types.Operator, ImportHelper):
    bl_idname = "import_scene.fable2_level"
    bl_label = "Import Fable 2 Level"
    bl_options = {"REGISTER", "UNDO"}

    filename_ext = ".fable"
    filter_glob: bpy.props.StringProperty(default="*.fable", options={"HIDDEN"})

    def execute(self, context):
        base_dir = os.path.dirname(self.filepath)
        with open(self.filepath, "r", encoding="utf-8") as f:
            manifest = json.load(f)

        level_name = manifest.get("level", {}).get("name") or "Fable Level"
        export_format = (manifest.get("format") or "").upper()
        root_coll = ensure_collection(level_name)
        terrain_coll = ensure_collection("Terrain", root_coll)
        instance_coll = ensure_collection("Instances", root_coll)
        library_prefix = clean_name(level_name, "FableLevel") + "_ModelLibrary"
        models = manifest.get("models", [])
        instances = manifest.get("instances", [])
        total_steps = 2 + len(models) + max(1, len(instances))
        wm = context.window_manager
        wm.progress_begin(0, total_steps)
        step = 0

        try:
            terrain = manifest.get("terrain") or {}
            terrain_mesh = terrain.get("mesh")
            if terrain_mesh:
                terrain_path = os.path.join(base_dir, terrain_mesh)
                if os.path.exists(terrain_path):
                    log_status("importing terrain")
                    imported, _ = import_scene_file(terrain_path)
                    move_to_collection(imported, terrain_coll)
                    align_terrain_to_editor_space(manifest, imported)
                    apply_terrain_material(base_dir, manifest, imported)
            step += 1
            progress_set(wm, step, total_steps)

            model_collections = {}
            for model in models:
                rel = model.get("file")
                src = model.get("source") or rel
                if not rel:
                    step += 1
                    progress_set(wm, step, total_steps)
                    continue
                path = os.path.join(base_dir, rel)
                if not os.path.exists(path):
                    step += 1
                    progress_set(wm, step, total_steps)
                    continue
                imported, _ = import_scene_file(path)
                coll_name = library_prefix + "_" + clean_name(rel, "model")
                model_coll = move_to_data_collection(imported, coll_name)
                model_collections[rel] = model_coll
                model_collections[src] = model_coll
                step += 1
                progress_set(wm, step, total_steps)

            log_status("creating " + str(len(instances)) + " collection instances")
            for index, inst in enumerate(instances):
                rel = inst.get("model")
                model_coll = model_collections.get(rel)
                if model_coll:
                    empty = bpy.data.objects.new(clean_name(rel, "model"), None)
                    empty.empty_display_type = "CUBE"
                    empty.empty_display_size = 0.35
                    empty.instance_type = "COLLECTION"
                    empty.instance_collection = model_coll
                    empty.matrix_world = raw_prop_matrix(inst, export_format)
                    instance_coll.objects.link(empty)
                if index % 128 == 0:
                    progress_set(wm, step + index + 1, total_steps)
            progress_set(wm, total_steps, total_steps)
            log_status("done")
        finally:
            wm.progress_end()

        return {"FINISHED"}


def menu_func_import(self, context):
    self.layout.operator(FABLE2_OT_import_level.bl_idname,
                         text="Fable 2 Level (.fable)")


classes = (FABLE2_OT_import_level,)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    bpy.types.TOPBAR_MT_file_import.append(menu_func_import)


def unregister():
    bpy.types.TOPBAR_MT_file_import.remove(menu_func_import)
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)


if __name__ == "__main__":
    register()
