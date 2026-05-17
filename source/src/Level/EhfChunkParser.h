#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Level {

struct EhfLodEntry {
    std::string strs[6];
    float       params[2][3] = {{0.0f, 1.0f, 0.0f},
                                {0.0f, 1.0f, 0.0f}};
};

struct EhfChunkLayer {
    uint32_t material_idx;
    uint32_t name_idx;
    float    tile_uv[2];
    uint8_t  texture_idx[4];
    uint8_t  blend[4];
};

struct EhfChunk {
    float                       origin[3];
    float                       extent[3];
    std::vector<EhfChunkLayer>  layers;
};

struct EhfParsedBody {
    bool                         ok = false;
    std::string                  error;

    uint32_t                     chunk_w = 0;
    uint32_t                     chunk_h = 0;
    uint32_t                     splat_w = 0;
    uint32_t                     splat_h = 0;

    std::vector<EhfLodEntry>     lods;
    std::vector<EhfChunk>        chunks;
    std::vector<uint8_t>         splat_indices;

    size_t                       bytes_consumed = 0;
    size_t                       bytes_remaining = 0;
};

bool ParseEhfBody(const std::vector<uint8_t>& ehf, EhfParsedBody& out);

}
