#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Level {

struct WaterTile {
    float    cx = 0.0f;
    float    cz = 0.0f;
    float    ex = 0.0f;
    float    ez = 0.0f;
    float    h_min = 0.0f;
    float    h_max = 0.0f;
    std::vector<uint32_t> dims;
    std::vector<uint8_t> mask;
};

struct WaterBody {
    std::array<float, 38> wave_params{};
    float        base_height = 0.0f;
    uint32_t     declared_tile_count = 0;
    std::string  normal_map_path;
    std::string  secondary_map_path;
    std::vector<WaterTile> tiles;
};

struct WaterScene {
    uint32_t version    = 0;
    uint32_t body_count = 0;
    uint32_t tile_count = 0;
    std::vector<WaterBody> bodies;
};

bool ParseWaterFile(const std::vector<uint8_t>& bytes, WaterScene& out);

}
