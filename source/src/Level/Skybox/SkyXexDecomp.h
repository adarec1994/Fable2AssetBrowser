#pragma once

#include <array>
#include <cstdint>

// 1:1 translation of the Fable 2 default.xex SKY primitive (type 24) render
// path, recovered July 2026.  Every function in this namespace mirrors one XEX
// function; the original address is given next to each declaration and the
// bodies in SkyXexDecomp.cpp preserve the original control flow, constants and
// theme-record accesses.  Device-level work (shader selection, constant
// upload, texture binding, draws, occlusion queries) goes through RenderApi so
// the same code can drive the D3D11 preview backend.
//
// Big picture (traced from SkyPrimitive_RenderCallback @ 0x8228EBA8):
//   - The "skybox" is a unit cube [0,1]^3 (8 verts, 6 quad faces) drawn with
//     face mask 63; VS 132 / PS 133 evaluate the procedural Rayleigh/Mie
//     atmosphere per pixel.  There is no sky cubemap texture.
//   - Moon, sun disc, sun beams (godrays), sun glare+streaks and moon glare
//     are camera-anchored world-space billboards fed by EnvironmentTheme
//     records; visibility for the glare passes comes from HW occlusion
//     queries issued while drawing the discs into a half-res offscreen pass
//     (Sky_RenderOcclusionSourcePass @ 0x82277240).
//   - Stars are 512 point sprites (VS 150 / PS 151) with positions generated
//     in the vertex shader; brightness gated by theme STARS_BRIGHTNESS.
namespace SkyXex {

// ---------------------------------------------------------------------------
// EnvironmentTheme runtime records.
//
// The theme runtime keeps an array of 16-byte records at theme+16, one per
// registered parameter, in registration order.  Float parameters are records
// 0..189 (order recovered from Theme_RegisterLightingAndSkyParams
// @ 0x832817F8); texture parameters live at record index 194 + slot (base
// baked into ThemeRecord_ReadPairs @ 0x821D08C0).  A record holds the blend
// stack for cross-fading themes: pairs of (weight, value); weights normalised
// to sum 1 (ThemeRecord_ReadPairs).  The interpolated result vector for
// record N is read directly at theme + 16 + 16*N by the render code.
// ---------------------------------------------------------------------------
enum ThemeParam : std::uint16_t {
    MAIN_LIGHT_COLOUR = 0,
    MAIN_LIGHT_SHADOWED = 1,
    AMBIENT_COLOUR = 2,
    LIGHTMAP_COLOUR = 3,
    AMBIENT_NORMAL_MAP_DARKENING_COLOUR = 4,
    DISTANT_SHADOW_FACTOR = 5,
    ENABLE_LIGHT_ZONE_SHADOW_CULLING = 6,
    SUN_INTENSITY = 7,
    SKY_BETA_RAYLEIGH_MULTIPLIER = 8,   // "SKY_BETA_RAYLEIGH_MULTIPLAYER" (sic)
    SKY_BETA_MIE_MULTIPLIER = 9,        // "SKY_BETA_MIE_MULTIPLAYER" (sic)
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
    // 103..132: WATER_* (see fable2-water memory / WaterParser)
    CLOUDS_LAYER1_TRANSPARENCY = 133,
    // 133..184: CLOUDS_LAYER1..4 blocks of 13 (see CloudRuntime)
    GROUND_MIST_STRENGTH = 185,
    LIGHTING_PREPROCESS_SKY_COLOUR_TOP = 186,
    LIGHTING_PREPROCESS_SKY_COLOUR_BOTTOM = 187,
    LIGHTING_PREPROCESS_SKY_COLOUR_TOP_FINAL_BOUNCE = 188,
    LIGHTING_PREPROCESS_SKY_COLOUR_BOTTOM_FINAL_BOUNCE = 189,

    THEME_FLOAT_PARAM_COUNT = 190,
    // Texture records follow at 194 + ThemeTextureSlot.
    THEME_TEXTURE_RECORD_BASE = 194,
};

// Registration order in Sky_SetupTexturesAndMaterials @ 0x832813E8.
enum ThemeTextureSlot : std::uint8_t {
    TEXSLOT_TONE_MAPPING_OVERLAY = 0,
    TEXSLOT_SKY_OVERLAY = 1,
    TEXSLOT_SUN_BEAMS = 2,
    TEXSLOT_SUN_DISC = 3,
    TEXSLOT_SUN_STREAKS = 4,
    TEXSLOT_SUN_GLARE = 5,
    TEXSLOT_MOON = 6,          // horizontal strip of 8 phases, u = phase/8
    TEXSLOT_MOON_GLARE = 7,
    TEXSLOT_CLOUDS_LAYER1_DENSITY = 8,
    TEXSLOT_CLOUDS_LAYER2_DENSITY = 9,
    TEXSLOT_CLOUDS_LAYER3_DENSITY = 10,
    TEXSLOT_CLOUDS_LAYER4_DENSITY = 11,
    TEXSLOT_WATER_NORMAL_MAP = 12,
    TEXSLOT_WATER_DETAIL_MAP = 13,
    TEXSLOT_WATER_HEIGHT_MAP = 14,
};

// Shader bank entry indices selected via Shader_SelectVertexEntry
// @ 0x8222C268 / Shader_SelectPixelEntry @ 0x82208C48.
enum ShaderEntry : std::uint16_t {
    VS_SKY_DOME = 132,
    PS_SKY_DOME = 133,
    VS_CELESTIAL_BILLBOARD = 139,  // shared by moon/sun disc/beams/glares
    PS_SUN_GLARE_STREAKS = 140,    // also moon glare
    PS_SUN_DISC = 141,
    PS_MOON_OCCLUSION = 144,       // moon disc drawn into occlusion pass
    PS_MOON = 143,                 // moon disc in main view
    PS_SUN_BEAMS = 145,
    VS_STARS = 150,
    PS_STARS = 151,
    VS_SKY_DOME_BGMAP = 153,       // background-map / lighting-gen dome pass
    PS_SKY_DOME_BGMAP = 154,
};

// Logical shader constant ids (remapped through the table @ 0x834BDBC4 by
// ShaderConstant_SetFloat4Logical @ 0x821EAF90 et al.).
enum LogicalConstant : std::uint16_t {
    LC_SKY_OVERLAY_BLEND = 225,     // lerp factor between the two overlay texs
    LC_SUN_BEAMS_COLOUR = 244,      // also glare/streaks colour
    LC_SUN_DISC_COLOUR = 245,
    LC_MOON_PARAMS = 147,           // moon transparency & intensity pack
    LC_DOME_MISC_A = 265,           // prim+80 float
    LC_DOME_MISC_B = 266,           // prim+76 float
    LC_STARS_PARAMS = 390,          // (1.5, 2.5, time, brightness)
    LC_BILLBOARD_TINT = 405,        // set to (0,1,0,1)-style default each quad
    LC_BGMAP_DOME_PARAM = 412,
};

// Fixed distances / scale factors (float literals in the XEX image).
inline constexpr float kMoonBillboardDistance = 4000.0f;   // 0x82000C24
inline constexpr float kMoonSizeScale = 300.0f;            // 0x8209BE64
inline constexpr float kMoonOcclusionSizeScale = 400.0f;   // 0x8209BE48
inline constexpr float kMoonOcclusionVisibilityDiv = 6000.0f; // 0x82000C20
inline constexpr float kMoonPhaseUStep = 0.125f;           // 8-phase strip
inline constexpr float kMoonGlareDistance = 1000.0f;       // 0x8209BAAC
inline constexpr float kMoonGlareSizeScale = 300.0f;
inline constexpr float kSunDiscDistance = 1900.0f;
inline constexpr float kSunDiscSizeScale = 300.0f;
inline constexpr float kSunDiscVisibilityDiv = 4000.0f;
inline constexpr float kSunBeamsDistance = 1900.0f;
inline constexpr float kSunBeamsSizeScale = 600.0f;
inline constexpr float kSunGlareDistance = 1999.0f;
inline constexpr float kSunGlareSizeScale = 2000.0f;
inline constexpr float kIntensityGate = 0.000099999997f;   // ">= 1e-4" gates
inline constexpr int kStarCount = 512;                     // point sprites
inline constexpr float kStarPointSizeMin = 0.1f;
inline constexpr float kStarPointSizeMax = 64.0f;

// ---------------------------------------------------------------------------
// Sky dome geometry (sub_821D71E8 @ 0x821D71E8).
//
// The dome is a unit cube: 8 float4 vertices at 0x83317EA0 and 8 groups of 6
// u16 indices (2 triangles per face) at 0x83317E38; groups 6 and 7 are zero
// padding.  A face-select bitmask picks which quads to emit:
//   main view (0x821A58C0)          -> mask 63  (all six faces)
//   bg-map/lighting pass (0x821D1450)-> mask 59  (skips face 2, the +Z pair)
// Vertices are expanded to view rays in VS 132/153; draw is
// DrawIndexedPrimitiveUP(TRIANGLELIST, 8 verts, stride 16).
// ---------------------------------------------------------------------------
struct Float4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
};

extern const std::array<Float4, 8> kDomeCubeVertices;     // 0x83317EA0
extern const std::array<std::uint16_t, 48> kDomeCubeIndices; // 0x83317E38

inline constexpr std::uint8_t kDomeFaceMaskMain = 63;
inline constexpr std::uint8_t kDomeFaceMaskBgMap = 59;

// ---------------------------------------------------------------------------
// Engine object views.  These mirror the XEX layouts; only the fields the sky
// path touches are named, with their byte offsets from the object base.
// ---------------------------------------------------------------------------

// Interpolated theme record vector: 4 floats (scalar params replicate .x).
struct ThemeRecordValue {
    float v[4] = {};
};

// Read-only view of the environment-theme runtime object.
struct ThemeView {
    // Returns pointer to record data at theme + 16 + 16*record.
    virtual const float* RecordValue(std::uint32_t record) const = 0;
    // ThemeRecord_ReadPairs (0x821D08C0): copy up to `count` (weight, value)
    // pairs of texture record `slot` (record base 194), weights normalised.
    virtual std::uint32_t ReadTexturePairs(std::uint32_t slot, float* pairs,
                                           std::uint32_t count) const = 0;
    // ThemeTexture_GetTop (0x821D0860): value (texture handle) of the top
    // blend entry of texture record `slot`.
    virtual std::uint32_t TopTextureHandle(std::uint32_t slot) const = 0;
    virtual ~ThemeView() = default;
};

// Per-frame render context passed to the render callbacks ("ctx" below).
// Recovered fields:
//   ctx+4    -> frame data:  +20 theme object, +336 view matrix (4x4)
//   ctx+1680 -> camera world position (float4)
//   ctx+1696 -> camera view-forward (float4)
//   ctx+1788 -> update mode (2 = full theme refresh this frame)
//   ctx+1798 -> byte: skip clouds
//   ctx+1802 -> byte: disable background-map binding
struct RenderContext {
    const ThemeView* theme = nullptr;
    float camera_pos[4] = {};
    float view_forward[4] = {};
    float view_matrix[16] = {};       // ctx+4 -> +336, row-major as stored
    std::int32_t update_mode = 0;     // ctx+1788
    bool skip_clouds = false;         // ctx+1798
    bool no_background_map = false;   // ctx+1802
    // Mist volume containing the camera (Mist_FindVolumeContainingPoint
    // @ 0x82295E50, engine+3412 list of type-27 primitives) or null.
    const void* mist_volume = nullptr;
};

// Sun element state, embedded in SkyPrimitive at +144 (sub-object).
//   +0   colour float4 (beam/glare tint, premultiplied elsewhere)
//   +16  sun direction float4 (unit, world space)
//   +36  visibility fraction [0,1] from the occlusion query
//   +40  occlusion query object
//   +44  beams texture ref (slot 2)
//   +48  disc texture ref (slot 3)
//   +52  glare texture ref (slot 5)
//   +56  streaks texture ref (slot 4)
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

// Moon element state, embedded in SkyPrimitive at +208.
//   +16  visibility fraction from the moon occlusion query
//   +20  moon phase int 0..7 (set via Sky_SetMoonPhase @ 0x82422840)
//   +24  occlusion query object
//   +28  moon texture ref (slot 6)      (used by both moon draws)
//   +?   glare texture ref (slot 7)
struct MoonElement {
    float direction[4] = {};
    float visibility = 0.0f;
    std::int32_t phase = 0;
    void* occlusion_query = nullptr;
    std::uint32_t moon_texture = 0;
    std::uint32_t glare_texture = 0;
};

// SkyPrimitive (type 24; ctor 0x82A5F7E8, 544 bytes).  Fields used here:
//   +20   engine/back-pointer (theme lookup + mist list)
//   +76   dome misc float -> logical constant 266
//   +80   dome misc float -> logical constant 265
//   +144  SunElement
//   +208  MoonElement
//   +256  CloudLayer*[4]
//   +272  sky overlay texture A ref  (blend pair 0 of texture record 1)
//   +276  sky overlay texture B ref  (blend pair 1 of texture record 1)
struct SkyPrimitive {
    float dome_misc_a = 0.0f;   // +76
    float dome_misc_b = 0.0f;   // +80
    SunElement sun;             // +144
    MoonElement moon;           // +208
    void* cloud_layers[4] = {}; // +256
    std::uint32_t overlay_texture_a = 0; // +272
    std::uint32_t overlay_texture_b = 0; // +276
    float overlay_blend = 0.0f; // -> logical constant 225
};

// ---------------------------------------------------------------------------
// Device abstraction.  Each member corresponds to one XEX helper; addresses
// in comments.  The 1:1 bodies call only these.
// ---------------------------------------------------------------------------
struct RenderApi {
    // Shader_SelectVertexEntry @ 0x8222C268 / Shader_SelectPixelEntry
    // @ 0x82208C48: select program from the global shader bank.
    void (*select_vertex_shader)(std::uint16_t entry) = nullptr;
    void (*select_pixel_shader)(std::uint16_t entry) = nullptr;

    // ShaderConstant_SetFloat4Logical @ 0x821EAF90 (also inline variant
    // sub_82210418 / sub_821C56C0): route through the logical->physical
    // constant remap table @ 0x834BDBC4.
    void (*set_float4)(std::uint16_t logical_id, const float value[4]) = nullptr;
    // ShaderConstant_SetFloatLogical @ 0x821E8AD8.
    void (*set_float)(std::uint16_t logical_id, float value) = nullptr;

    // Texture bind via the per-shader sampler remap (sub_821B7020 patterns);
    // `which` is the sampler role, 0 = primary, 1 = secondary.
    void (*bind_texture)(std::uint32_t texture_handle, int which) = nullptr;

    // TexturePtr_AssignFromHandle @ 0x8227FF40 + RenderResource_Prepare
    // @ 0x8221EC20: resolve a theme texture handle, kick streaming; returns
    // false while the mip chain is not resident (XEX tests +84 == 0x7FFFFFFF).
    bool (*prepare_texture)(std::uint32_t texture_handle) = nullptr;

    // sub_82A5EFD8: camera-facing quad, 4 verts stride 20 (xyz + uv),
    // indices {0,1,2, 1,3,2}, DrawIndexedPrimitiveUP(TRIANGLELIST).
    // centre/right/up in world space; uv_rect = (u0, v0, u1, v1).
    void (*draw_billboard)(const float centre[3], const float right_half[3],
                           const float up_half[3], const float uv_rect[4]) = nullptr;

    // sub_821D71E8: emit selected faces of the unit cube (see tables above).
    void (*draw_dome_cube)(std::uint8_t face_mask) = nullptr;

    // sub_8222C788(start, count) -> DrawPrimitive(POINTLIST, start, count);
    // the stars VS synthesises positions from the vertex index.
    void (*draw_points)(std::uint32_t start, std::uint32_t count) = nullptr;

    // Occlusion query wrappers (sub_822195E8: 2 = begin, 1 = end;
    // sub_8220BE78: poll result, returns pixel count when ready).
    void (*occlusion_begin)(void* query) = nullptr;
    void (*occlusion_end)(void* query) = nullptr;
    bool (*occlusion_result)(void* query, std::uint32_t* pixels) = nullptr;

    // Render-state dedup cache blocks (inlined throughout the XEX around
    // dword_834B8090 / dword_83490BF0).  Encapsulated: the bodies request
    // logical state changes and the host applies them.
    void (*set_render_state)(std::uint32_t state_id, std::uint32_t value) = nullptr;

    // BgMap_BindForRender @ 0x82A7A8A8 / BgMap_ClearBooleanBinding
    // @ 0x8226E048 (gated by g_BackgroundMapsEnabled @ 0x8335CA12).
    void (*bind_background_map)(const void* mist_volume,
                                const RenderContext* ctx) = nullptr;
    void (*clear_background_map)(void) = nullptr;

    // Global clock (dbl_83496C48) used by the stars twinkle and the bg-map
    // dome cloud scroll.
    double (*global_time)(void) = nullptr;
};

// Render-state ids used by the sky path (values are the XEX state-cache slots
// around 0x834BC0A0..0x834BC24C; see .cpp for the per-function sets).
enum SkyRenderState : std::uint32_t {
    RS_DEPTH_WRITE,       // 0x834BC0D0 block
    RS_DEPTH_TEST,        // 0x834BC0E8 block
    RS_ALPHA_BLEND_ENABLE,// 0x834BC148 block
    RS_ALPHA_TEST_ENABLE, // 0x834BC160 block
    RS_BLEND_OP,          // 0x834BC178 block
    RS_SRC_BLEND,         // 0x834BC190 block (values from 0x83316EB0/EBC)
    RS_DST_BLEND,         // 0x834BC1A8 block (values from 0x83316EC0)
    RS_CULL_MODE,         // 0x834BC118 block (values from 0x83316EF8/EFC)
    RS_POINT_SIZE_MIN,    // 0x834BC220 block (0.1f)
    RS_POINT_SIZE_MAX,    // 0x834BC238 block (64.0f)
    RS_FOG_RELATED,       // 0x834BC298 block
    RS_UNKNOWN_A,         // 0x834BC0A0 block (bg-map dome pass)
};

// ---------------------------------------------------------------------------
// 1:1 function ports.  Original XEX addresses in comments.
// ---------------------------------------------------------------------------

// SkyPrimitive_RenderCallback @ 0x8228EBA8 (vtbl render slot of prim type 24).
void SkyPrimitive_RenderCallback(RenderApi& api, SkyPrimitive& prim,
                                 RenderContext& ctx);

// sub_8229B1E0 @ 0x8229B1E0: procedural atmosphere dome (VS 132 / PS 133).
void Sky_DrawAtmosphereDome(RenderApi& api, SkyPrimitive& prim,
                            RenderContext& ctx);

// sub_821DB590 @ 0x821DB590: dome texture/constant state shared by both dome
// passes (overlay textures + fog constants + optional bg-map bind).
void Sky_SetupDomeState(RenderApi& api, SkyPrimitive& prim,
                        RenderContext& ctx);

// sub_821D3928 @ 0x821D3928: moon billboard, main view (PS 143).
void Sky_DrawMoon(RenderApi& api, MoonElement& moon, RenderContext& ctx);

// sub_821C4FB8 @ 0x821C4FB8: 512-point starfield (VS 150 / PS 151).
void Sky_DrawStars(RenderApi& api, SkyPrimitive& prim, RenderContext& ctx);

// sub_821D8310 @ 0x821D8310: sun disc + occlusion query (PS 141).
void Sky_DrawSunDisc(RenderApi& api, SunElement& sun, RenderContext& ctx);

// sub_821D4028 @ 0x821D4028: sun beams / godrays billboard (PS 145).
void Sky_DrawSunBeams(RenderApi& api, SunElement& sun, RenderContext& ctx);

// sub_8227DBA0 @ 0x8227DBA0: sun glare + streaks billboard (PS 140), scaled
// by dot(sunDir, viewFwd)^2 * visibility^2.
void Sky_DrawSunGlareStreaks(RenderApi& api, SunElement& sun,
                             RenderContext& ctx);

// sub_821D5478 @ 0x821D5478: moon glare billboard (PS 140).
void Sky_DrawMoonGlare(RenderApi& api, MoonElement& moon, RenderContext& ctx);

// sub_822792D0 @ 0x822792D0: moon disc into the occlusion pass (PS 144),
// issues the moon visibility query.
void Sky_DrawMoonOcclusion(RenderApi& api, MoonElement& moon,
                           RenderContext& ctx);

// sub_822A0AC0 @ 0x822A0AC0: post-cloud glare callback (streaks + moon glare).
void SkyGlare_RenderCallback(RenderApi& api, SkyPrimitive& prim,
                             RenderContext& ctx);

// sub_82277240 @ 0x82277240 (called from SkyCloud_DepthRepopulate
// @ 0x821CB3A0): half-resolution offscreen pass that renders the moon and sun
// discs with occlusion queries and resolves the buffer; only the body's sky
// calls are ported, the render-target plumbing is host-side.
void Sky_RenderOcclusionSourcePass(RenderApi& api, SkyPrimitive& prim,
                                   RenderContext& ctx);

// sub_821D1450 @ 0x821D1450: dome pass for background-map / lighting
// generation (VS 153 / PS 154, face mask 59).
void Sky_DrawBgMapDome(RenderApi& api, SkyPrimitive& prim, RenderContext& ctx);

}  // namespace SkyXex
