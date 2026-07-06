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
    std::vector<uint8_t> data;   // raw pixel bytes (populated for pf=98 weight masks)
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

// One background ("vista") patch: world AABB + its streamed background-map
// texture pages. Pages are {file_offset, byte_size} windows into the SAME
// .ehf file (the data past the body), each a pf=35 BC1 tex blob; page 0 is
// the highest-resolution level (XEX: HFGF_ReadStage4Records18 pairs one map
// per patch, HFGF final section stores the page table, sub_82B24980 streams
// a page by wrapping the file stream at that offset/size).
struct EhfBgPatch {
    float aabb_min[3] = {0.0f, 0.0f, 0.0f};
    float aabb_max[3] = {0.0f, 0.0f, 0.0f};
    std::vector<std::pair<uint32_t, uint32_t>> pages;   // {offset, size}
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
    std::vector<EhfPaintResource> weight_masks;   // per-layer blend masks (pf=98)
    std::vector<EhfChunk>        chunks;
    std::vector<uint8_t>         splat_indices;
    std::vector<EhfBgPatch>      bg_patches;      // vista patches + page tables

    size_t                       bytes_consumed = 0;
    size_t                       bytes_remaining = 0;
};

bool ParseEhfBody(const std::vector<uint8_t>& ehf, EhfParsedBody& out);

}
