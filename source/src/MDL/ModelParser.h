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

struct MDLClothInfo {
    uint32_t RecordIndex   = 0;
    uint32_t MeshIndex     = 0;
    size_t   BlockOffset   = 0;
    size_t   BlockSize     = 0;
    uint32_t SimVertexCount = 0;
    uint32_t TriangleCount = 0;
    uint32_t IndexCount    = 0;
    uint32_t EdgeCount     = 0;
    uint32_t BonesPerVertex = 0;
    uint32_t DistConstraints = 0;
    uint32_t BendConstraints = 0;
    uint32_t LinkConstraints = 0;
    float    SimParams[6]  = {0,0,0,0,0,0};
    size_t   RestPosOffset = 0;
    size_t   IndexListOffset = 0;
    size_t   BoneIdOffset  = 0;
    size_t   WeightOffset  = 0;
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
    std::vector<MDLClothInfo> ClothBlocks;
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
    bool is_cloth = false;
    bool cloth_sim = false;
    std::vector<uint8_t> cloth_pin;
    float cloth_damping = 0.05f;
    bool alpha_test = true;
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

bool parse_mdl_cloth_blocks(const std::vector<unsigned char>& data, MDLInfo& info);

bool build_mdl_cloth_geometry(const std::vector<unsigned char>& data, std::vector<MDLMeshGeom>& out);

bool build_mdl_engine_geometry(const std::vector<unsigned char>& data, std::vector<MDLMeshGeom>& out);

bool reparse_mdl_buffers_via_polymsh_scan(const std::vector<unsigned char>& data,
                                          MDLInfo& info);

bool reparse_mdl_missing_buffers_optstr(const std::vector<unsigned char>& data,
                                        MDLInfo& info);

bool reparse_mdl_as_foliage_48b(const std::vector<unsigned char>& data,
                                MDLInfo& info);

bool reparse_mdl_multi_instance_buffers(const std::vector<unsigned char>& data,
                                        MDLInfo& info);
