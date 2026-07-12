#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Level {

enum WaterParamIdx : int {
    WP_FRESNEL_BIAS         = 0,
    WP_REFLECTION_BIAS      = 1,
    WP_NM_SPEED0X           = 2,
    WP_NM_SPEED0Y           = 3,
    WP_NM_SPEED1X           = 4,
    WP_NM_SPEED1Y           = 5,
    WP_NM_SCALE0X           = 6,
    WP_NM_SCALE0Y           = 7,
    WP_NM_SCALE1X           = 8,
    WP_NM_SCALE1Y           = 9,
    WP_DETAIL_SPEED0X       = 10,
    WP_DETAIL_SPEED0Y       = 11,
    WP_DETAIL_SPEED1X       = 12,
    WP_DETAIL_SPEED1Y       = 13,
    WP_DETAIL_SCALE0X       = 14,
    WP_DETAIL_SCALE0Y       = 15,
    WP_DETAIL_SCALE1X       = 16,
    WP_DETAIL_SCALE1Y       = 17,
    WP_SURFACE_R            = 18,
    WP_SURFACE_G            = 19,
    WP_SURFACE_B            = 20,
    WP_DEEP_R               = 21,
    WP_DEEP_G               = 22,
    WP_DEEP_B               = 23,
    WP_NORMAL_SCALE         = 24,
    WP_REFLECTION_SCALE_X   = 25,
    WP_REFLECTION_SCALE_Y   = 26,
    WP_REFRACTION_SCALE_X   = 27,
    WP_REFRACTION_SCALE_Y   = 28,
    WP_REFLECTION_STRENGTH  = 29,
    WP_DIFFUSE_ABSORPTION   = 30,
    WP_SPEC_REFL_FACTOR     = 31,
    WP_SPEC_HIGHLIGHT       = 32,
    WP_SPEC_POWER           = 33,
    WP_GLITTER_BEND         = 34,
    WP_GLITTER_STRENGTH     = 35,
    WP_GLITTER_POWER        = 36,
};

struct WaterTile {
    float cx = 0.0f;
    float cz = 0.0f;
    float ex = 0.0f;
    float ez = 0.0f;
    int   cells_x = 0;
    int   cells_z = 0;
    std::vector<uint32_t> aux;
    std::vector<uint8_t>  mask;
};

struct WaterBody {
    float        param_a = 0.0f;
    float        base_height = 0.0f;
    std::array<float, 37> params{};
    std::string  normal_map_path;
    std::string  secondary_map_path;
    uint32_t     declared_tile_count = 0;
    std::vector<WaterTile> tiles;

    float p(int idx, float fallback) const {
        if (idx < 0 || idx >= int(params.size())) return fallback;
        const float v = params[size_t(idx)];
        return (v == v) ? v : fallback;
    }
};

struct WaterScene {
    uint32_t version    = 0;
    uint32_t body_count = 0;
    uint32_t tile_count = 0;
    std::vector<WaterBody> bodies;
};

bool ParseWaterFile(const std::vector<uint8_t>& bytes, WaterScene& out);

}
