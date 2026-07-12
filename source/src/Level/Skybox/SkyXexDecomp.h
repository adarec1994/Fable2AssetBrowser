#pragma once

#include <array>
#include <cstdint>

namespace SkyXex {

enum ThemeParam : std::uint16_t {
    MAIN_LIGHT_COLOUR = 0,
    MAIN_LIGHT_SHADOWED = 1,
    AMBIENT_COLOUR = 2,
    LIGHTMAP_COLOUR = 3,
    AMBIENT_NORMAL_MAP_DARKENING_COLOUR = 4,
    DISTANT_SHADOW_FACTOR = 5,
    ENABLE_LIGHT_ZONE_SHADOW_CULLING = 6,
    SUN_INTENSITY = 7,
    SKY_BETA_RAYLEIGH_MULTIPLIER = 8,
    SKY_BETA_MIE_MULTIPLIER = 9,
    SKY_COLOUR = 10,
    SKY_COMPLEMENTARY_COLOUR = 11,
    SKY_COMPLEMENTARY_COLOUR_BIAS = 12,
    SKY_SUNSET_COLOUR = 13,
    SKY_FOGGING_START = 14,
    SKY_FOGGING_NEAR_DISTANCE = 15,
    SKY_FOGGING_NEAR_DENSITY = 16,
    SKY_FOGGING_FAR_DISTANCE = 17,
    SKY_FOGGING_FAR_DENSITY = 18,
    SKY_FOGGING_CLOSE_FOG_COLOUR = 19,
    SKY_FOGGING_CLOSE_FOG_MAX_DISTANCE = 20,
    TONE_MAPPING_EXPOSURE_SPEED = 21,
    TONE_MAPPING_MIDDLE_GRAY = 22,
    TONE_MAPPING_CONTRAST = 23,
    TONE_MAPPING_MIN_LUMINANCE_PERCENTAGE = 24,
    TONE_MAPPING_AVG_LUMINANCE_PERCENTAGE = 25,
    TONE_MAPPING_MAX_LUMINANCE_PERCENTAGE = 26,
    TONE_MAPPING_CONTRAST_ENHANCEMENT = 27,
    TONE_MAPPING_CONTRAST_ENHANCEMENT_EXPONENT = 28,
    TONE_MAPPING_CONTRAST_MAX_DEVIATION = 29,
    TONE_MAPPING_CONTRAST_CLAMPING = 30,
    TONE_MAPPING_MIN_LUMINANCE_UPPER_BOUND = 31,
    TONE_MAPPING_AVG_LUMINANCE_LOWER_BOUND = 32,
    TONE_MAPPING_MAX_LUMINANCE_LOWER_BOUND = 33,
    TONE_MAPPING_BLOOM_THRESHOLD_AVG_FACTOR = 34,
    TONE_MAPPING_BLOOM_FACTOR = 35,
    TONE_MAPPING_BLOOM_BLUR_SIZE = 36,
    TONE_MAPPING_INVERSE_BLOOM_THRESHOLD = 37,
    TONE_MAPPING_INVERSE_BLOOM_DARKENING_FACTOR = 38,
    TONE_MAPPING_INVERSE_BLOOM_STRENGTH = 39,
    TONE_MAPPING_COLOUR_SENSITIVITY_THRESHOLD = 40,
    TONE_MAPPING_OVERLAY_TEXTURE_SCALE_U = 41,
    TONE_MAPPING_OVERLAY_TEXTURE_SCALE_V = 42,
    TONE_MAPPING_OVERLAY_TEXTURE_STRENGTH = 43,
    TONE_MAPPING_SATURATION = 44,
    TONE_MAPPING_SATURATION_MASKED_OFFSET = 45,
    TONE_MAPPING_BRIGHTNESS = 46,
    TONE_MAPPING_BRIGHTNESS_MASKED_OFFSET = 47,
    TONE_MAPPING_RADIALBLUR_FACTOR = 48,
    TONE_MAPPING_RADIALBLUR_FALLOFF_MULTIPLIER = 49,
    TONE_MAPPING_RADIALBLUR_MAGNIFICATION_FACTOR = 50,
    TONE_MAPPING_RADIALBLUR_BLUR_SIZE = 51,
    TONE_MAPPING_RADIALBLUR_BLUR_INTENSITY = 52,
    MAIN_LIGHT_ELEVATION_0 = 53,
    MAIN_LIGHT_ELEVATION_6 = 54,
    MAIN_LIGHT_ELEVATION_12 = 55,
    MAIN_LIGHT_ELEVATION_18 = 56,
    LIGHT_RIG_XY_ROTATION = 57,
    MAIN_LIGHT_TIME_FACTOR = 58,
    SUN_AXIS_ELEVATION = 59,
    SUN_AXIS_Z_OFFSET = 60,
    SUN_AXIS_XY_ROTATION = 61,
    MOON_AXIS_ELEVATION = 62,
    MOON_AXIS_Z_OFFSET = 63,
    MOON_AXIS_XY_ROTATION = 64,
    MOON_INTENSITY = 65,
    MOON_SIZE = 66,
    MOON_GLARE_INTENSITY = 67,
    MOON_GLARE_SIZE = 68,
    MOON_TRANSPARENCY = 69,
    STARS_BRIGHTNESS = 70,
    SNOW_SIZE = 71,
    SNOW_FALLSPEED = 72,
    RAIN_SIZE = 73,
    RAIN_DENSITY = 74,
    HIDE_OCCLUSION_ERRORS = 75,
    WIND_STRENGTH_MIN = 76,
    WIND_STRENGTH_MAX = 77,
    WIND_STRENGTH_VARIATION = 78,
    WIND_XY_ROTATION_MIN = 79,
    WIND_ELEVATION_MIN = 80,
    WIND_XY_ROTATION_MAX = 81,
    WIND_ELEVATION_MAX = 82,
    WIND_DIRECTION_VARIATION = 83,
    WIND_CHANGE_FREQUENCY = 84,
    WIND_CHANGE_DURATION = 85,
    DOF_NEAR_PLANE = 86,
    DOF_NEAR_FOCAL_PLANE = 87,
    DOF_FAR_FOCAL_PLANE = 88,
    DOF_FAR_PLANE = 89,
    DOF_NEAR_BLUR_AMOUNT = 90,
    DOF_FAR_BLUR_AMOUNT = 91,
    SUN_DISC_SIZE = 92,
    SUN_DISC_COLOUR = 93,
    SUN_DISC_INTENSITY = 94,
    SUN_BEAMS_WIDTH = 95,
    SUN_BEAMS_HEIGHT = 96,
    SUN_BEAMS_INTENSITY = 97,
    SUN_STREAKS_INTENSITY = 98,
    SUN_STREAKS_SIZE = 99,
    SUN_GLARE_INTENSITY = 100,
    SUN_GLARE_SIZE = 101,
    WATER_VISTA_DISABLED = 102,

    CLOUDS_LAYER1_TRANSPARENCY = 133,

    GROUND_MIST_STRENGTH = 185,
    LIGHTING_PREPROCESS_SKY_COLOUR_TOP = 186,
    LIGHTING_PREPROCESS_SKY_COLOUR_BOTTOM = 187,
    LIGHTING_PREPROCESS_SKY_COLOUR_TOP_FINAL_BOUNCE = 188,
    LIGHTING_PREPROCESS_SKY_COLOUR_BOTTOM_FINAL_BOUNCE = 189,

    THEME_FLOAT_PARAM_COUNT = 190,

    THEME_TEXTURE_RECORD_BASE = 194,
};

enum ThemeTextureSlot : std::uint8_t {
    TEXSLOT_TONE_MAPPING_OVERLAY = 0,
    TEXSLOT_SKY_OVERLAY = 1,
    TEXSLOT_SUN_BEAMS = 2,
    TEXSLOT_SUN_DISC = 3,
    TEXSLOT_SUN_STREAKS = 4,
    TEXSLOT_SUN_GLARE = 5,
    TEXSLOT_MOON = 6,
    TEXSLOT_MOON_GLARE = 7,
    TEXSLOT_CLOUDS_LAYER1_DENSITY = 8,
    TEXSLOT_CLOUDS_LAYER2_DENSITY = 9,
    TEXSLOT_CLOUDS_LAYER3_DENSITY = 10,
    TEXSLOT_CLOUDS_LAYER4_DENSITY = 11,
    TEXSLOT_WATER_NORMAL_MAP = 12,
    TEXSLOT_WATER_DETAIL_MAP = 13,
    TEXSLOT_WATER_HEIGHT_MAP = 14,
};

enum ShaderEntry : std::uint16_t {
    VS_SKY_DOME = 132,
    PS_SKY_DOME = 133,
    VS_CELESTIAL_BILLBOARD = 139,
    PS_SUN_GLARE_STREAKS = 140,
    PS_SUN_DISC = 141,
    PS_MOON_OCCLUSION = 144,
    PS_MOON = 143,
    PS_SUN_BEAMS = 145,
    VS_STARS = 150,
    PS_STARS = 151,
    VS_SKY_DOME_BGMAP = 153,
    PS_SKY_DOME_BGMAP = 154,
};

enum LogicalConstant : std::uint16_t {
    LC_SKY_OVERLAY_BLEND = 225,
    LC_SUN_BEAMS_COLOUR = 244,
    LC_SUN_DISC_COLOUR = 245,
    LC_MOON_PARAMS = 147,
    LC_DOME_MISC_A = 265,
    LC_DOME_MISC_B = 266,
    LC_STARS_PARAMS = 390,
    LC_BILLBOARD_TINT = 405,
    LC_BGMAP_DOME_PARAM = 412,
};

inline constexpr float kMoonBillboardDistance = 4000.0f;
inline constexpr float kMoonSizeScale = 300.0f;
inline constexpr float kMoonOcclusionSizeScale = 400.0f;
inline constexpr float kMoonOcclusionVisibilityDiv = 6000.0f;
inline constexpr float kMoonPhaseUStep = 0.125f;
inline constexpr float kMoonGlareDistance = 1000.0f;
inline constexpr float kMoonGlareSizeScale = 300.0f;
inline constexpr float kSunDiscDistance = 1900.0f;
inline constexpr float kSunDiscSizeScale = 300.0f;
inline constexpr float kSunDiscVisibilityDiv = 4000.0f;
inline constexpr float kSunBeamsDistance = 1900.0f;
inline constexpr float kSunBeamsSizeScale = 600.0f;
inline constexpr float kSunGlareDistance = 1999.0f;
inline constexpr float kSunGlareSizeScale = 2000.0f;
inline constexpr float kIntensityGate = 0.000099999997f;
inline constexpr int kStarCount = 512;
inline constexpr float kStarPointSizeMin = 0.1f;
inline constexpr float kStarPointSizeMax = 64.0f;

struct Float4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
};

extern const std::array<Float4, 8> kDomeCubeVertices;
extern const std::array<std::uint16_t, 48> kDomeCubeIndices;

inline constexpr std::uint8_t kDomeFaceMaskMain = 63;
inline constexpr std::uint8_t kDomeFaceMaskBgMap = 59;

struct ThemeRecordValue {
    float v[4] = {};
};

struct ThemeView {

    virtual const float* RecordValue(std::uint32_t record) const = 0;

    virtual std::uint32_t ReadTexturePairs(std::uint32_t slot, float* pairs,
                                           std::uint32_t count) const = 0;

    virtual std::uint32_t TopTextureHandle(std::uint32_t slot) const = 0;
    virtual ~ThemeView() = default;
};

struct RenderContext {
    const ThemeView* theme = nullptr;
    float camera_pos[4] = {};
    float view_forward[4] = {};
    float view_matrix[16] = {};
    std::int32_t update_mode = 0;
    bool skip_clouds = false;
    bool no_background_map = false;

    const void* mist_volume = nullptr;
};

struct SunElement {
    float colour[4] = {};
    float direction[4] = {};
    float visibility = 0.0f;
    void* occlusion_query = nullptr;
    std::uint32_t beams_texture = 0;
    std::uint32_t disc_texture = 0;
    std::uint32_t glare_texture = 0;
    std::uint32_t streaks_texture = 0;
};

struct MoonElement {
    float direction[4] = {};
    float visibility = 0.0f;
    std::int32_t phase = 0;
    void* occlusion_query = nullptr;
    std::uint32_t moon_texture = 0;
    std::uint32_t glare_texture = 0;
};

struct SkyPrimitive {
    float dome_misc_a = 0.0f;
    float dome_misc_b = 0.0f;
    SunElement sun;
    MoonElement moon;
    void* cloud_layers[4] = {};
    std::uint32_t overlay_texture_a = 0;
    std::uint32_t overlay_texture_b = 0;
    float overlay_blend = 0.0f;
};

struct RenderApi {

    void (*select_vertex_shader)(std::uint16_t entry) = nullptr;
    void (*select_pixel_shader)(std::uint16_t entry) = nullptr;

    void (*set_float4)(std::uint16_t logical_id, const float value[4]) = nullptr;

    void (*set_float)(std::uint16_t logical_id, float value) = nullptr;

    void (*bind_texture)(std::uint32_t texture_handle, int which) = nullptr;

    bool (*prepare_texture)(std::uint32_t texture_handle) = nullptr;

    void (*draw_billboard)(const float centre[3], const float right_half[3],
                           const float up_half[3], const float uv_rect[4]) = nullptr;

    void (*draw_dome_cube)(std::uint8_t face_mask) = nullptr;

    void (*draw_points)(std::uint32_t start, std::uint32_t count) = nullptr;

    void (*occlusion_begin)(void* query) = nullptr;
    void (*occlusion_end)(void* query) = nullptr;
    bool (*occlusion_result)(void* query, std::uint32_t* pixels) = nullptr;

    void (*set_render_state)(std::uint32_t state_id, std::uint32_t value) = nullptr;

    void (*bind_background_map)(const void* mist_volume,
                                const RenderContext* ctx) = nullptr;
    void (*clear_background_map)(void) = nullptr;

    double (*global_time)(void) = nullptr;
};

enum SkyRenderState : std::uint32_t {
    RS_DEPTH_WRITE,
    RS_DEPTH_TEST,
    RS_ALPHA_BLEND_ENABLE,
    RS_ALPHA_TEST_ENABLE,
    RS_BLEND_OP,
    RS_SRC_BLEND,
    RS_DST_BLEND,
    RS_CULL_MODE,
    RS_POINT_SIZE_MIN,
    RS_POINT_SIZE_MAX,
    RS_FOG_RELATED,
    RS_UNKNOWN_A,
};

void SkyPrimitive_RenderCallback(RenderApi& api, SkyPrimitive& prim,
                                 RenderContext& ctx);

void Sky_DrawAtmosphereDome(RenderApi& api, SkyPrimitive& prim,
                            RenderContext& ctx);

void Sky_SetupDomeState(RenderApi& api, SkyPrimitive& prim,
                        RenderContext& ctx);

void Sky_DrawMoon(RenderApi& api, MoonElement& moon, RenderContext& ctx);

void Sky_DrawStars(RenderApi& api, SkyPrimitive& prim, RenderContext& ctx);

void Sky_DrawSunDisc(RenderApi& api, SunElement& sun, RenderContext& ctx);

void Sky_DrawSunBeams(RenderApi& api, SunElement& sun, RenderContext& ctx);

void Sky_DrawSunGlareStreaks(RenderApi& api, SunElement& sun,
                             RenderContext& ctx);

void Sky_DrawMoonGlare(RenderApi& api, MoonElement& moon, RenderContext& ctx);

void Sky_DrawMoonOcclusion(RenderApi& api, MoonElement& moon,
                           RenderContext& ctx);

void SkyGlare_RenderCallback(RenderApi& api, SkyPrimitive& prim,
                             RenderContext& ctx);

void Sky_RenderOcclusionSourcePass(RenderApi& api, SkyPrimitive& prim,
                                   RenderContext& ctx);

void Sky_DrawBgMapDome(RenderApi& api, SkyPrimitive& prim, RenderContext& ctx);

}
