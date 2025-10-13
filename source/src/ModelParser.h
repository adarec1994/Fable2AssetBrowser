#pragma once
#include <string>
#include <vector>
#include <cstdint>

bool build_mdl_buffer_for_name(const std::string &mdl_name, std::vector<unsigned char> &out);

struct MDLBoneInfo {
    std::string Name;
    int ParentID;
};

struct MDLMaterialInfo {
    std::string TextureName;
    std::string SpecularMapName;
    std::string NormalMapName;
    std::string UnkName;
    std::string TintName;
    uint32_t Unk1 = 0;
    uint32_t Unk2[2] = {0, 0};
};

struct MDLMeshInfo {
    std::string MeshName;
    uint32_t MaterialCount = 0;
    std::vector<MDLMaterialInfo> Materials;
};

struct MDLMeshBufferInfo {
    uint32_t VertexCount = 0;
    size_t   VertexOffset = 0;
    uint32_t FaceCount = 0;
    size_t   FaceOffset = 0;
    uint32_t SubMeshCount = 0;
    bool     IsAltPath = false;
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
};

struct MDLMeshGeom {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<uint32_t> indices;
    std::vector<uint16_t> bone_ids;
    std::vector<float> bone_weights;
    std::string diffuse_tex_name;
};

bool parse_mdl_info(const std::vector<unsigned char>& data, MDLInfo& out);
bool parse_mdl_geometry(const std::vector<unsigned char>& data, const MDLInfo& info, std::vector<MDLMeshGeom>& out);
