#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct MDLBoneInfo {
    std::string Name;
    int ParentID;
};

struct MDLMaterialInfo {
    std::string DiffuseTexName;
    std::string NormalTexName;
    std::string SpecularTexName;
    std::string MetallicTexName;
    std::string ExtraTexName;
    uint32_t Unk1 = 0;
    uint32_t Unk2[2] = {0, 0};
};

struct MDLMeshInfo {
    std::string MeshName;
    uint32_t MaterialCount = 0;
    std::vector<MDLMaterialInfo> Materials;
};

// Cloth / soft-body deformer extension block (engine reader sub_82B31CA0).
// Present on a skinned buffer record whose ext1 flag == 1 (typically meshes
// named "cloth*"). Decoded byte-exact across all 63 cloth-bearing globals
// models. Developer-mode only for now: populated by parse_mdl_cloth_blocks().
struct MDLClothInfo {
    uint32_t RecordIndex   = 0;   // buffer-record index (rigid+skinned order)
    uint32_t MeshIndex     = 0;   // owning mesh-header index (record idx1)
    size_t   BlockOffset   = 0;   // file offset of block (just after ext1 flag)
    size_t   BlockSize     = 0;   // total block size in bytes
    uint32_t SimVertexCount = 0;  // cloth particle count (<= render vtx; welded)
    uint32_t TriangleCount = 0;   // sim triangle count (== IndexCount/3)
    uint32_t IndexCount    = 0;   // sim triangle index list length (3*tris)
    uint32_t EdgeCount     = 0;   // n77 — per-edge/link element count
    uint32_t BonesPerVertex = 0;  // skin-bind weights per particle (usually 4)
    uint32_t DistConstraints = 0; // distance-constraint count
    uint32_t BendConstraints = 0; // bend-constraint count (the "bendB" list)
    uint32_t LinkConstraints = 0; // link-constraint count
    float    SimParams[6]  = {0,0,0,0,0,0}; // stiffness, ?, damping, scale xyz
    size_t   RestPosOffset = 0;   // file offset of rest-position vec3 array
    size_t   IndexListOffset = 0; // file offset of sim triangle index list
    size_t   BoneIdOffset  = 0;   // file offset of per-particle bone ids (B bytes each)
    size_t   WeightOffset  = 0;   // file offset of per-particle weights (vec4 f32 each)
};

struct MDLSubMeshInfo {
    uint32_t FaceCount = 0;
    uint32_t StartIndex = 0;
    uint8_t  MaterialIndex = 0;
};

struct MDLMeshBufferInfo {
    uint32_t VertexCount = 0;
    size_t   VertexOffset = 0;
    uint32_t FaceCount = 0;
    size_t   FaceOffset = 0;
    uint32_t SubMeshCount = 0;
    bool     IsAltPath = false;
    bool     IsFoliagePath = false;
    uint32_t FoliageVertexStride = 0;
    std::vector<MDLSubMeshInfo> SubMeshes;
    uint32_t MeshIndex = 0;

    size_t   UvBufferOffset = 0;
    uint32_t UvBufferStride = 0;
};

struct MDLInfo {
    std::string Magic;
    uint32_t HeaderSize = 0;
    uint32_t BoneCount = 0;
    uint32_t BoneTransformCount = 0;
    std::vector<MDLBoneInfo> Bones;
    std::vector<std::vector<float>> BoneTransforms;
    bool HasBoneTransforms = false;
    uint32_t Unk6Count = 0;
    uint32_t MeshCount = 0;
    std::vector<MDLMeshInfo> Meshes;
    std::vector<MDLMeshBufferInfo> MeshBuffers;
    std::vector<MDLClothInfo> ClothBlocks; // dev-mode only; see parse_mdl_cloth_blocks
};

struct MDLMeshGeom {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<uint32_t> indices;
    std::vector<uint16_t> bone_ids;
    std::vector<float> bone_weights;
    std::string diffuse_tex_name;
    std::string normal_tex_name;
    std::string specular_tex_name;
    std::string metallic_tex_name;
    std::string extra_tex_name;
    std::string name;
    bool is_terrain = false;
    bool is_water = false;
    bool is_cloth = false;   // dev-mode cloth/soft-body sim overlay mesh
    bool cloth_sim = false;  // ext1==1 cloth record → run the cloth solver on it
    std::vector<uint8_t> cloth_pin;  // per-vertex: 1 = anchored to skeleton (from
                                     // cloth-block flags mapped to render verts)
    float cloth_damping = 0.05f;     // sim param (from the cloth block)
    bool alpha_test = true;  // discard on diffuse.a<0.25; engine geom sets false
                             // (Fable packs gloss in diffuse-alpha, not opacity)
    float water_params[38] = {};
    bool has_water_theme = false;
    float water_opacity = 1.0f;
    float water_shallow_colour[3] = {0.155f, 0.285f, 0.235f};
    float water_deep_colour[3] = {0.010f, 0.075f, 0.085f};
    float water_theme_params[10] = {};
    uint32_t MeshIndex = 0;
    uint32_t SubMeshIndex = 0;

    struct PickRange {
        uint32_t selection_id = 0;
        uint32_t index_start = 0;
        uint32_t index_count = 0;
        float center[3] = {0.0f, 0.0f, 0.0f};
        float radius = 0.0f;
    };
    std::vector<PickRange> pick_ranges;
};

bool build_mdl_buffer_for_name(const std::string &mdl_name, std::vector<unsigned char> &out);
bool build_mdl_buffer_for_name_with_body(const std::string& mdl_name,
                                         const std::string& preferred_body_bnk,
                                         std::vector<unsigned char>& out);
bool parse_mdl_info(const std::vector<unsigned char>& data, MDLInfo& out);
bool parse_mdl_info(const std::vector<unsigned char>& data, MDLInfo& out, const std::string& file_path);
bool parse_mdl_geometry(const std::vector<unsigned char>& data, const MDLInfo& info, std::vector<MDLMeshGeom>& out);

// Developer-mode: walk the file engine-faithfully and extract every cloth /
// soft-body extension block into info.ClothBlocks. Self-contained and fully
// bounds-checked — returns false and clears ClothBlocks on any inconsistency
// (rigid/foliage/polymsh bodies simply yield none). Does not touch the other
// fields of `info`, so it is safe to call after parse_mdl_info().
bool parse_mdl_cloth_blocks(const std::vector<unsigned char>& data, MDLInfo& info);

// Developer-mode: build renderable geometry for every cloth/soft-body block and
// APPEND it to `out` (existing entries are preserved). Each cloth mesh carries
// the block's rest positions, sim triangle list, computed normals, and the
// block's own skin binding (bone ids + weights) so it deforms with the skeleton
// exactly like the render mesh. Returns true if any cloth mesh was appended.
bool build_mdl_cloth_geometry(const std::vector<unsigned char>& data, std::vector<MDLMeshGeom>& out);

// Developer-mode: fully engine-faithful geometry decode (rigid reader
// sub_82B2BCC8 = 20 B/vtx, skinned reader = 28 B/vtx) with the corrected
// LOD-section prologue (the 32 section-start bytes are 4 (f32,u32) LOD-range
// pairs, not a zero block — the fix that lets LOD'd / "polymsh" models parse).
// Walks the first LOD section, reads every record byte-exactly, captures
// per-mesh materials, and APPENDS fully TEXTURED renderable geometry to `out`.
// Self-contained and fully bounds-checked: returns false and appends nothing on
// any inconsistency (asserts it lands on each section boundary). Intended as the
// SOLE geometry source in developer mode (no heuristic fallback). NOT yet
// corpus-validated — dev-mode test surface.
bool build_mdl_engine_geometry(const std::vector<unsigned char>& data, std::vector<MDLMeshGeom>& out);

bool reparse_mdl_buffers_via_polymsh_scan(const std::vector<unsigned char>& data,
                                          MDLInfo& info);

bool reparse_mdl_missing_buffers_optstr(const std::vector<unsigned char>& data,
                                        MDLInfo& info);

bool reparse_mdl_as_foliage_48b(const std::vector<unsigned char>& data,
                                MDLInfo& info);

bool reparse_mdl_multi_instance_buffers(const std::vector<unsigned char>& data,
                                        MDLInfo& info);
