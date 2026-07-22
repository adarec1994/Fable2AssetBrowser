#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <filesystem>
#include <optional>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <sstream>
#include <limits>
#include <mutex>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <chrono>
#include "ModelPreview.h"
#include "../Level/Terrain/TerrainPaint.h"
#include "../Level/Terrain/TerrainSplat.h"
#include "../Level/Skybox/SkyboxRenderer.h"
extern std::vector<Fx::Placement> g_pending_level_fx;
extern Fx::Bank                   g_particle_bank;
extern bool                       g_particle_bank_loaded;
#include "../Utilities/Files.h"
#include "../Utilities/Utils.h"
#include "../Utilities/State.h"
#include "../BNKCore.cpp"
#include "../textures/TexParser.h"
#include "../textures/LhTexCodec.h"
#include "OutputLog.h"
#include <zlib.h>
#ifdef _WIN32
#include <initguid.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
using namespace DirectX;
#else
#include <GL/glew.h>
#endif
FlyCam g_flycam;
#include "ModelPreview/Common/Helpers.inl"
#include "ModelPreview/Textures/BlockCompression.inl"
#include "ModelPreview/Textures/XboxTiling.inl"
#include "ModelPreview/Textures/Decoder.inl"
#include "ModelPreview/Textures/Lookup.inl"
#include "ModelPreview/Camera/FlyCam.inl"
#ifdef _WIN32
#include "ModelPreview/DirectX/Resources/Release.inl"
#include "ModelPreview/DirectX/Shaders/Shaders.inl"
#include "ModelPreview/DirectX/Resources/Textures.inl"
#include "ModelPreview/DirectX/Resources/RenderTarget.inl"
#include "ModelPreview/DirectX/Pipeline/Pipeline.inl"
#include "ModelPreview/DirectX/Lifecycle.inl"
#include "ModelPreview/DirectX/Particles.inl"
#include "ModelPreview/DirectX/Resources/TextureCache.inl"
#include "ModelPreview/DirectX/Animation/Skeleton.inl"
#include "ModelPreview/DirectX/Animation/Cloth.inl"
#include "ModelPreview/DirectX/Model/Build.inl"
#include "ModelPreview/DirectX/Rendering/Frame.inl"
#include "ModelPreview/DirectX/Animation/WorldPose.inl"

#else
#include "ModelPreview/OpenGL/Resources.inl"
#include "ModelPreview/OpenGL/Lifecycle.inl"
#include "ModelPreview/OpenGL/Textures.inl"
#include "ModelPreview/OpenGL/Model/Build.inl"
#include "ModelPreview/OpenGL/Math/Matrices.inl"
#include "ModelPreview/OpenGL/Rendering/Frame.inl"
#endif

static bool mp_geometry_vertices(const MDLMeshGeom& geometry,
                                 std::vector<MPVertex>& vertices) {
    const size_t count = geometry.positions.size() / 3;
    if (count == 0 || geometry.positions.size() != count * 3) return false;
    vertices.assign(count, MPVertex{});
    const bool have_normals = geometry.normals.size() == count * 3;
    const bool have_uvs = geometry.uvs.size() == count * 2;
    const bool have_bones = geometry.bone_ids.size() == count * 4;
    const bool have_weights = geometry.bone_weights.size() == count * 4;
    auto capped_bone = [](uint16_t value) -> uint8_t {
        return static_cast<uint8_t>(value < MP_MAX_BONES ? value : 0);
    };
    for (size_t i = 0; i < count; ++i) {
        MPVertex& vertex = vertices[i];
        vertex.px = geometry.positions[i * 3];
        vertex.py = geometry.positions[i * 3 + 1];
        vertex.pz = geometry.positions[i * 3 + 2];
        vertex.nx = have_normals ? geometry.normals[i * 3] : 0.0f;
        vertex.ny = have_normals ? geometry.normals[i * 3 + 1] : 1.0f;
        vertex.nz = have_normals ? geometry.normals[i * 3 + 2] : 0.0f;
        vertex.u = have_uvs ? geometry.uvs[i * 2] : 0.0f;
        vertex.v = have_uvs ? geometry.uvs[i * 2 + 1] : 0.0f;
        if (have_bones) {
            vertex.b0 = capped_bone(geometry.bone_ids[i * 4]);
            vertex.b1 = capped_bone(geometry.bone_ids[i * 4 + 1]);
            vertex.b2 = capped_bone(geometry.bone_ids[i * 4 + 2]);
            vertex.b3 = capped_bone(geometry.bone_ids[i * 4 + 3]);
        }
        if (have_weights) {
            vertex.w0 = geometry.bone_weights[i * 4];
            vertex.w1 = geometry.bone_weights[i * 4 + 1];
            vertex.w2 = geometry.bone_weights[i * 4 + 2];
            vertex.w3 = geometry.bone_weights[i * 4 + 3];
        } else {
            vertex.w0 = 1.0f;
        }
    }
    return true;
}

static void mp_update_mesh_bounds(const MDLMeshGeom& geometry,
                                  MPPerMesh& mesh) {
    if (geometry.positions.size() < 3) return;
    float minimum[3] = {1.0e30f, 1.0e30f, 1.0e30f};
    float maximum[3] = {-1.0e30f, -1.0e30f, -1.0e30f};
    for (size_t i = 0; i + 2 < geometry.positions.size(); i += 3) {
        for (int axis = 0; axis < 3; ++axis) {
            minimum[axis] = std::min(minimum[axis], geometry.positions[i + axis]);
            maximum[axis] = std::max(maximum[axis], geometry.positions[i + axis]);
        }
    }
    for (int axis = 0; axis < 3; ++axis) {
        mesh.center[axis] = (minimum[axis] + maximum[axis]) * 0.5f;
    }
    float radius_squared = 0.0f;
    for (size_t i = 0; i + 2 < geometry.positions.size(); i += 3) {
        const float x = geometry.positions[i] - mesh.center[0];
        const float y = geometry.positions[i + 1] - mesh.center[1];
        const float z = geometry.positions[i + 2] - mesh.center[2];
        radius_squared = std::max(radius_squared, x * x + y * y + z * z);
    }
    mesh.radius = std::max(0.0001f, std::sqrt(radius_squared));
    if (!mesh.pick_ranges.empty()) mesh.pick_positions = geometry.positions;
}

bool MP_UpdateGeometry(const std::vector<MDLMeshGeom>& geoms,
                       const MDLInfo& info,
                       ModelPreview& mp) {
    if (!mp.has_model || mp.meshes.empty()) return false;
    std::vector<std::vector<MPVertex>> vertices(mp.meshes.size());
    for (size_t i = 0; i < mp.meshes.size(); ++i) {
        const MPPerMesh& mesh = mp.meshes[i];
        if (mesh.source_mesh_idx >= geoms.size() ||
            !mp_geometry_vertices(geoms[mesh.source_mesh_idx], vertices[i])) {
            return false;
        }
    }

#ifdef _WIN32
    ID3D11Device* device = nullptr;
    for (const MPPerMesh& mesh : mp.meshes) {
        if (mesh.cloth_sim || !mesh.vb) return false;
        if (!device) mesh.vb->GetDevice(&device);
    }
    if (!device) return false;
    for (size_t i = 0; i < mp.meshes.size(); ++i) {
        D3D11_BUFFER_DESC description{};
        mp.meshes[i].vb->GetDesc(&description);
        const uint64_t bytes = vertices[i].size() * sizeof(MPVertex);
        if (bytes == 0 || bytes != description.ByteWidth ||
            bytes > std::numeric_limits<UINT>::max() ||
            description.Usage != D3D11_USAGE_DEFAULT) {
            device->Release();
            return false;
        }
    }
    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    device->Release();
    if (!context) return false;
    for (size_t i = 0; i < mp.meshes.size(); ++i) {
        context->UpdateSubresource(mp.meshes[i].vb, 0, nullptr,
                                   vertices[i].data(), 0, 0);
        mp_update_mesh_bounds(geoms[mp.meshes[i].source_mesh_idx],
                              mp.meshes[i]);
    }
    context->Release();



    mp_set_skeleton_bind(info, mp, false);
#else
    for (size_t i = 0; i < mp.meshes.size(); ++i) {
        MPPerMesh& mesh = mp.meshes[i];
        if (!mesh.vbo) return false;
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        GLint bytes = 0;
        glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bytes);
        const size_t wanted = vertices[i].size() * sizeof(MPVertex);
        if (bytes < 0 || static_cast<size_t>(bytes) != wanted) {
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            return false;
        }
    }
    for (size_t i = 0; i < mp.meshes.size(); ++i) {
        MPPerMesh& mesh = mp.meshes[i];
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        vertices[i].size() * sizeof(MPVertex),
                        vertices[i].data());
        mp_update_mesh_bounds(geoms[mesh.source_mesh_idx], mesh);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    (void)info;
#endif
    return true;
}

size_t MP_RemoveMeshesByNamePrefix(ModelPreview& mp,
                                   const std::string& prefix)
{
    size_t removed = 0;
    for (size_t i = mp.meshes.size(); i-- > 0;) {
        if (mp.meshes[i].name.rfind(prefix, 0) != 0) continue;
#ifdef _WIN32
        mp_release_mesh(mp.meshes[i]);
#else
        mp_release_mesh_gl(mp.meshes[i]);
#endif
        mp.meshes.erase(mp.meshes.begin() + (ptrdiff_t)i);
        ++removed;
    }
    return removed;
}

size_t MP_RemoveMeshesByInstHash(ModelPreview& mp, uint64_t inst_hash)
{
    size_t removed = 0;
    for (size_t i = mp.meshes.size(); i-- > 0;) {
        const auto& ranges = mp.meshes[i].pick_ranges;
        if (ranges.empty()) continue;
        bool all_match = true;
        for (const auto& pr : ranges) {
            if (pr.inst_hash != inst_hash) {
                all_match = false;
                break;
            }
        }
        if (!all_match) continue;
#ifdef _WIN32
        mp_release_mesh(mp.meshes[i]);
#else
        mp_release_mesh_gl(mp.meshes[i]);
#endif
        mp.meshes.erase(mp.meshes.begin() + (ptrdiff_t)i);
        ++removed;
    }
    return removed;
}
