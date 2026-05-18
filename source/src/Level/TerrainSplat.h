#pragma once

#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
struct ID3D11Device;
struct ID3D11ShaderResourceView;
struct ID3D11SamplerState;
#endif

namespace Level { struct EhfParsedBody; }
namespace EhfLodThumbnails { struct Entry; }

namespace TerrainSplat {

constexpr int kCommonLodSize = 1024;
constexpr int kMaxLayers     = 16;
constexpr int kMaxMaterials  = 32;

struct Resources {
#ifdef _WIN32
    ID3D11ShaderResourceView* lod_diffuse_array        = nullptr;
    ID3D11ShaderResourceView* lod_detail_array         = nullptr;
    ID3D11ShaderResourceView* lod_normal_array         = nullptr;
    ID3D11ShaderResourceView* lod_detail_normal_array  = nullptr;
    ID3D11ShaderResourceView* chunk_idx_array          = nullptr;
    ID3D11ShaderResourceView* chunk_blend_array        = nullptr;
    ID3D11ShaderResourceView* chunk_uv_array           = nullptr;
    ID3D11ShaderResourceView* splat_mask               = nullptr;
    ID3D11ShaderResourceView* lightmap                 = nullptr;
    ID3D11ShaderResourceView* material_weight_array    = nullptr;
#endif
    float world_origin_x = 0.f, world_origin_z = 0.f;
    float chunk_extent_x = 80.f, chunk_extent_z = 80.f;
    int   chunk_w = 0,  chunk_h = 0;
    int   lod_count = 0;

    float mesh_to_world_x = 0.f, mesh_to_world_z = 0.f;

    float tile_scale = 0.125f;
    int   splat_w = 0;
    int   splat_h = 0;
    int   weight_w = 0;
    int   weight_h = 0;

    float material_params[kMaxMaterials][4] = {};

    uint32_t generation = 0;

    bool  ok = false;
};

#ifdef _WIN32
bool Build(ID3D11Device*                                       device,
           const Level::EhfParsedBody&                          parsed,
           const std::vector<EhfLodThumbnails::Entry>&          lod_thumbs,
           const std::vector<uint8_t>&                          lightmap_rgba,
           int                                                  lightmap_w,
           int                                                  lightmap_h,
           float                                                mesh_to_world_x,
           float                                                mesh_to_world_z);
#endif

const Resources& Get();

void Clear();

}
