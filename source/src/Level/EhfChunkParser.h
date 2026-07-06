#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>


namespace Level {

struct EhfLodEntry {
    std::string strs[6];
    uint32_t    material_flags = 0;
    float       params[2][3] = {{0.0f, 0.0f, 0.0f},
                                {0.0f, 1.0f, 0.0f}};
};

struct EhfPaintResource {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t pixel_format = 0;
    std::vector<uint8_t> data;
};

struct EhfChunkLayer {
    uint32_t material_idx = 0;
    uint32_t name_idx = 0;
    float    tile_uv[2] = {0.0f, 0.0f};
    float    mask_scale[2] = {0.0f, 0.0f};
    uint8_t  texture_idx[4] = {0, 0, 0, 0};
    uint8_t  blend[4] = {0, 0, 0, 0};
};

struct EhfChunk {
    float                       origin[3];
    float                       extent[3];
    std::vector<EhfChunkLayer>  layers;
};

struct EhfBgPatch {
    float aabb_min[3] = {0.0f, 0.0f, 0.0f};
    float aabb_max[3] = {0.0f, 0.0f, 0.0f};
    std::vector<std::pair<uint32_t, uint32_t>> pages;
};

struct EhfParsedBody {
    bool                         ok = false;
    std::string                  error;

    uint32_t                     chunk_w = 0;
    uint32_t                     chunk_h = 0;
    uint32_t                     splat_w = 0;
    uint32_t                     splat_h = 0;

    std::vector<EhfLodEntry>     lods;
    std::vector<EhfPaintResource> paint_resources;
    std::vector<EhfPaintResource> weight_masks;
    std::vector<EhfChunk>        chunks;
    std::vector<uint8_t>         splat_indices;
    std::vector<EhfBgPatch>      bg_patches;

    size_t                       bytes_consumed = 0;
    size_t                       bytes_remaining = 0;
};

bool ParseEhfBody(const std::vector<uint8_t>& ehf, EhfParsedBody& out);

}
