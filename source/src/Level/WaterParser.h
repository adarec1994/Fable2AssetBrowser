#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Level {

// One masked grid tile inside a water body. (cx, cz) is the tile centre in
// world XZ space; (ex, ez) are the half-extents; h_min/h_max bound the
// water surface in Y. `mask` is a row-major occupancy grid: 0x01 = water,
// 0x00 = no water. The grid dimensions are implied by `mask.size()` and
// the flags `dim[0..2]`, which appear to encode (layer_count, idx_x, idx_z)
// — we expose them verbatim for the renderer to interpret.
struct WaterTile {
    float    cx = 0.0f;
    float    cz = 0.0f;
    float    ex = 0.0f;
    float    ez = 0.0f;
    float    h_min = 0.0f;
    float    h_max = 0.0f;
    uint32_t flags[4] = {0, 0, 0, 0};  // flags[0..2] + mask_count
    std::vector<uint8_t> mask;
};

// A water body groups a wave-parameter block, a base surface height,
// a normal-map texture path, and the list of masked tiles that make up
// the body's footprint.
struct WaterBody {
    std::array<float, 40> wave_params{};
    float        base_height = 0.0f;
    std::string  normal_map_path;
    std::vector<WaterTile> tiles;
};

struct WaterScene {
    uint32_t version    = 0;
    uint32_t body_count = 0;
    uint32_t tile_count = 0;
    std::vector<WaterBody> bodies;
};

// Parse a Fable 2 .water file. Returns true if at least one body and one
// tile were parsed. The parser is tolerant of trailing/garbage data and
// will return whatever it managed to read before encountering anomalies.
bool ParseWaterFile(const std::vector<uint8_t>& bytes, WaterScene& out);

}  // namespace Level
