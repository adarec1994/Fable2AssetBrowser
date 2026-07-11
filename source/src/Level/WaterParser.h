#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Level {

// Byte-exact model of the engine's water file (XEX WaterFile_DeserializeRecord
// @0x82B28398). All values big-endian. Layout:
//   u32 version (must be 2)
//   u32 recordCount
//   recordCount x u32 absolute record offsets
// Each record ("water body"):
//   u32 marker (0x0FEC)
//   f32 param_a            (WaterParams+0)
//   f32 height_z           (WaterParams+4, the flat water plane height)
//   f32 params[37]         (WaterParams+8..+152, indices below)
//   strz normal_map_path
//   strz secondary_map_path (usually empty)
//   u32 patchCount
//   patches, then a trailing u32 marker.
// Each patch:
//   u32 marker (0x0FEC)
//   f32 centre_x, centre_y (patch CENTRE in engine world XY)
//   f32 width, height      (FULL extents; PostLoadBuild does centre +/- 0.5*e)
//   f32 cells_x, cells_y   (grid resolution as floats)
//   u32 n, n x u32         (small aux list, e.g. {1,3})
//   u32 maskCount
//   u8  mask[maskCount]    (row-major cells_x * cells_y, 1 = water present)
//   u32 marker (0x0FEC)

// Indices into WaterBody::params (37 floats at WaterParams+8).
// Byte-exact per the XEX g_WaterConstants packer (sub_82B2A008 in the IDB,
// dst stride 16 bytes = one shader register per row, block = Shaders.sbk
// param #23, PS c116..c141). Defaults per WaterParams_SetDefaults @0x82B29B80.
enum WaterParamIdx : int {
    WP_FRESNEL_BIAS         = 0,   // m_FresnelBias        (default 0.2)
    WP_REFLECTION_BIAS      = 1,   // m_ReflectionBias     (default 0.0)
    WP_NM_SPEED0X           = 2,   // normal map 0 scroll speed (default 0.052)
    WP_NM_SPEED0Y           = 3,   //                            (default 0.011)
    WP_NM_SPEED1X           = 4,   // normal map 1 scroll speed (default -0.019)
    WP_NM_SPEED1Y           = 5,   //                            (default 0.019)
    WP_NM_SCALE0X           = 6,   // m_BumpmapScales[0] (uv = world * scale)
    WP_NM_SCALE0Y           = 7,
    WP_NM_SCALE1X           = 8,   // m_BumpmapScales[1]
    WP_NM_SCALE1Y           = 9,
    WP_DETAIL_SPEED0X       = 10,
    WP_DETAIL_SPEED0Y       = 11,
    WP_DETAIL_SPEED1X       = 12,
    WP_DETAIL_SPEED1Y       = 13,
    WP_DETAIL_SCALE0X       = 14,
    WP_DETAIL_SCALE0Y       = 15,
    WP_DETAIL_SCALE1X       = 16,
    WP_DETAIL_SCALE1Y       = 17,
    WP_SURFACE_R            = 18,  // m_SurfaceWaterColour
    WP_SURFACE_G            = 19,
    WP_SURFACE_B            = 20,
    WP_DEEP_R               = 21,  // m_DeepWaterColour
    WP_DEEP_G               = 22,
    WP_DEEP_B               = 23,
    WP_NORMAL_SCALE         = 24,  // default 0.05
    WP_REFLECTION_SCALE_X   = 25,  // default 2.0
    WP_REFLECTION_SCALE_Y   = 26,  // default 2.0
    WP_REFRACTION_SCALE_X   = 27,  // default 2.0
    WP_REFRACTION_SCALE_Y   = 28,  // default 2.0
    WP_REFLECTION_STRENGTH  = 29,  // default 0.75
    WP_DIFFUSE_ABSORPTION   = 30,  // default 0.1
    WP_SPEC_REFL_FACTOR     = 31,  // default 0.15
    WP_SPEC_HIGHLIGHT       = 32,  // default 0.1
    WP_SPEC_POWER           = 33,  // default 2.0
    WP_GLITTER_BEND         = 34,  // default 0.3
    WP_GLITTER_STRENGTH     = 35,  // default 5.0
    WP_GLITTER_POWER        = 36,  // default 128.0
};

struct WaterTile {
    float cx = 0.0f;      // patch centre, engine world X (app x)
    float cz = 0.0f;      // patch centre, engine world Y (app z)
    float ex = 0.0f;      // full extent along X
    float ez = 0.0f;      // full extent along Y
    int   cells_x = 0;    // mask/grid columns
    int   cells_z = 0;    // mask/grid rows
    std::vector<uint32_t> aux;   // the small u32 list between rect and mask
    std::vector<uint8_t>  mask;  // cells_x * cells_z bytes, 1 = water
};

struct WaterBody {
    float        param_a = 0.0f;      // WaterParams+0
    float        base_height = 0.0f;  // WaterParams+4 (water plane Z)
    std::array<float, 37> params{};   // WaterParams+8.., see WaterParamIdx
    std::string  normal_map_path;
    std::string  secondary_map_path;
    uint32_t     declared_tile_count = 0;
    std::vector<WaterTile> tiles;

    float p(int idx, float fallback) const {
        if (idx < 0 || idx >= int(params.size())) return fallback;
        const float v = params[size_t(idx)];
        return (v == v) ? v : fallback;  // NaN guard
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
