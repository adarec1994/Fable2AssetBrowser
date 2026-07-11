#pragma once

#include <array>
#include <cstdint>

// Byte-derived recovery of the retail Fable 2 sky dome ("skybox") pipeline.
//
// Sources, all recovered July 2026 and cross-checked against each other:
//   - ShadersRelease.sbk (653060 bytes) shader-table entries 132 (vertex,
//     hash 0x7255B944, packed program 110) and 133 (pixel, hash 0xD5610E4C,
//     packed program 111), decoded with the exact XenosShaderBinary port.
//   - default.xex CPU builders: Sky_UpdateAtmosphereConstantsAndLut
//     @ 0x82263530 (per-frame constants + in-scatter LUT inputs) and
//     Sky_BuildInScatterLutSlice @ 0x821F71F0 (64x1 RGBA16F LUT texels),
//     LUT texture creation @ 0x82A5FC20 (width 64, pixel format 104).
//   - Constant plumbing: atmosphere block uploaded to PS c64..c68 by
//     0x822823D8 (sbk param[6]: m_BetaRayleigh / m_BetaMie / m_SunDirection /
//     m_MeshAtmosphericScatteringRange_HorizonDistance_
//     RecipCloseFogMaxDistance_RecipMaxFogDistance / m_CloseFogColour);
//     background-map block to PS c72..c76 by BgMap_BindForRender
//     @ 0x82A7A8A8; logical 225 -> c47 overlay cross-fade, 265 -> c46,
//     266 -> c31 (both constant: 100.0f and 150.0f, exp(-0.0) * 100/150);
//     c19 = camera world position, c20.w = 1.0 during the sky pass
//     (GlobalPixelConstant20_SetW call in SkyCloud_DepthRepopulate).
//
// The pixel shader consumes only the *direction* of the interpolated ray, so
// any dome geometry producing the same ray field is pixel-exact; the retail
// unit cube and constants are kept regardless (SkyXexDecomp.h).
namespace SkyDomeXex {

// ---------------------------------------------------------------------------
// Exact scalar constants from the XEX image.
// ---------------------------------------------------------------------------

// pow(0.000580084099999878, 2.0) * 248.05023415027725 evaluated at runtime by
// 0x82263530 (three identical powf calls @ 0x821FE378); the doubles are the
// image constants dbl_82000EF0 / dbl_82000DE0 / dbl_82100A00.
inline constexpr double kBetaRayleighCommon =
    248.05023415027725;  // multiplied by pow(5.80084099999878e-4, 2.0)
inline constexpr double kBetaRayleighPowBase = 0.000580084099999878;
inline constexpr double kBetaRayleighPowExponent = 2.0;
// dbl_821009F8 / dbl_821009F0 / dbl_821009C8.
inline constexpr double kBetaRayleighChannel[3] = {
    0.007337032921211229, 0.009459203185216139, 0.025727610716641394};
// dbl_821009E0 / dbl_821009D8 / dbl_821009E8 (stored to dbl_833214A0/A8/B0);
// the float copies flt_821009D0/D4/C0 feed the vector build.
inline constexpr float kBetaMieChannel[3] = {
    0.0056148912f, 0.0063754139f, 0.010514311f};

// The SKY_BETA_RAYLEIGH_MULTIPLIER theme record is scaled by 500
// (flt_8209BF0C) before multiplying the Rayleigh base; the Mie multiplier is
// used unscaled (0x822636E4 / 0x82263738).
inline constexpr float kRayleighMultiplierScale = 500.0f;

// Phase normalisation constants applied to the LUT input betas
// (0x822638B4..D0): 3/16pi and 1/4pi.
inline constexpr float kRayleighPhaseNorm = 0.059683103f;  // flt_821009B8
inline constexpr float kMiePhaseNorm = 0.079577468f;       // flt_821009BC

// Mie asymmetry g is linear in the sun elevation angle (radians), computed
// at 0x8226391C..0x82263974:
//   a = 0.1 - 0.0318 * elev              (flt_8209B650, flt_821009B4)
//   b = 0.444 - 0.05984 * elev           (flt_833214C8, flt_821009B0)
//   g = (b - a) * 0.5 + a = (a + b) / 2  (dbl_82000D28 = 0.5)
inline constexpr float kMieG_A0 = 0.10000000149011612f;
inline constexpr float kMieG_A1 = 0.03180000185966492f;
inline constexpr float kMieG_B0 = 0.4440000057220459f;
inline constexpr float kMieG_B1 = 0.05984000116586685f;

// PS c31.x / c46.x: exp(-0.0) * 150.0 and * 100.0 (dbl_833214B8/C0); the
// exp argument is the literal dbl_82000E98 = -0.0, so these are constant.
inline constexpr float kOpticalScaleRayleigh = 150.0f;  // logical 266 -> c31
inline constexpr float kOpticalScaleMie = 100.0f;       // logical 265 -> c46

// c67 (m_...Range_HorizonDistance_RecipCloseFogMaxDistance_RecipMaxFogDist):
//   x = SKY_FOGGING_START (theme record 14)
//   y = 1900.0            (flt_82000EA8, the celestial horizon distance)
//   z = record 14 > 0 ? 1 / record 14 : 1
//   w = 0.0005            (flt_82000A88)
inline constexpr float kHorizonDistance = 1900.0f;
inline constexpr float kRecipMaxFogDistance = 0.00050000002f;

// Dome geometry (vertex shader literal c255): [0,1] cube -> *2-1 -> *2850.
inline constexpr float kDomeRadius = 2850.0f;

inline constexpr int kLutWidth = 64;  // 0x82A5FC20 creates 64x1 RGBA16F

// ---------------------------------------------------------------------------
// Inputs: interpolated theme records (already cross-faded by the theme
// runtime; the preview's EnvironmentTheme evaluation supplies these).
// ---------------------------------------------------------------------------
struct ThemeInputs {
    float sun_intensity = 1.0f;              // record 7 (gate only)
    float beta_rayleigh_multiplier = 1.0f;   // record 8
    float beta_mie_multiplier = 1.0f;        // record 9
    float sky_colour[4] = {1, 1, 1, 0};      // record 10 (rgb)
    float complementary_colour[4] = {1, 1, 1, 0};  // record 11 (rgb)
    float complementary_bias = 0.0f;         // record 12 (x)
    float sunset_colour[4] = {1, 1, 1, 0};   // record 13 (rgb; feeds the
                                             // cloud-lighting ramp, kept for
                                             // completeness)
    float fogging_start = 0.0f;              // record 14 (x)
    float close_fog_max_distance = 0.0f;     // record 20 (x)
    // Sun direction, unit length, ENGINE space (Z = up).  prim+96 in the XEX.
    float sun_direction[4] = {0, 0, 1, 0};
};

// ---------------------------------------------------------------------------
// CPU port of the per-frame constant builder (0x82263530, LUT-relevant part).
// ---------------------------------------------------------------------------
struct AtmosphereState {
    // PS c64 = beta_rayleigh_rgb * (record8 * 500).
    float beta_rayleigh[4] = {};
    // PS c65 = beta_mie_rgb * record9.
    float beta_mie[4] = {};
    // PS c66 = sun direction (engine space).
    float sun_direction[4] = {};
    // PS c67, see kHorizonDistance block above.
    float scattering_misc[4] = {};

    // LUT builder input block (mirrors the stack block handed to
    // 0x821F71F0 at a4; offsets are the XEX block offsets):
    float lut_beta_rayleigh_phase[4] = {};  // +0   betaR * mult*500 * 3/16pi
    float lut_beta_mie_phase[4] = {};       // +16  betaM * mult * 1/4pi
    float lut_cos_sun_elevation = 1.0f;     // +32  cos(sun elevation angle)
    float lut_hg[4] = {};                   // +48  (1-g^2, 1+g^2, 2g, 0)
    // +64: (1, 1, 1, SUN_INTENSITY) — the .w lane scales every LUT texel
    // (symbolically re-derived from the 0x82263530 instruction stream).
    float lut_scale[4] = {1, 1, 1, 1};
    float lut_colour_a[4] = {};             // +80  SKY_COLOUR (rgb, 0)
    float lut_colour_b[4] = {};             // +96  SKY_COMPLEMENTARY (rgb, 0)
    float lut_bias_plus_half = 0.5f;        // +112 record12.x + 0.5

    // SunElement colour (prim+128/prim+144, tail of 0x82263530):
    // lerp((1, 1, 0), SUNSET_COLOUR, cos(sun elevation)) with the blue lane
    // starting at 0 — white-yellow at noon, sunset-coloured at the horizon.
    // Feeds logical constant 244 (sun beams / glare tint).
    float sun_element_colour[4] = {1, 1, 0, 0};
};

AtmosphereState ComputeAtmosphereState(const ThemeInputs& theme);

// CPU port of the LUT slice builder (0x821F71F0).  Writes kLutWidth RGBA16F
// texels (IEEE half, round-to-nearest-even like VMX vpkd3d FLOAT16_4).
void BuildInScatterLut(const AtmosphereState& state,
                       std::array<std::uint16_t, kLutWidth * 4>& out_rgba16f);

// Float32 variant of the same math for hosts that prefer a float LUT
// texture; values are identical before half quantisation.
void BuildInScatterLutFloat(const AtmosphereState& state,
                            std::array<float, kLutWidth * 4>& out_rgba);

// ---------------------------------------------------------------------------
// HLSL translations of the retail programs.
//
// Instruction-per-instruction ports of packed programs 110/111; each
// statement is tagged with its Xenos packet index.  Register names r0..r6
// and constant names mirror the hardware registers; the cbuffer maps the
// original constant file:
//   c0..c3   view-projection rows            (vertex)
//   c9/c19   camera world position           (vertex/pixel copies)
//   c20.w    final output scale (1.0 in the retail sky pass)
//   c31.x    150.0     c46.x  100.0          (optical depth scales)
//   c47.x    overlay cross-fade weight       (logical 225)
//   c64..c67 atmosphere block                (see AtmosphereState)
//   c72..c76, c28.y, boolean b129: background-map reprojection branch
//   f4  in-scatter LUT (64x1)   f13/f14 sky overlay textures A/B
// ---------------------------------------------------------------------------
extern const char* const kDomeVertexShaderHlsl;  // entry VSMain (retail cube)
extern const char* const kDomePixelShaderHlsl;   // entry PSMain

// Starfield, packed programs 126 (VS entry 150, hash 0x0424F50C) and 127
// (PS entry 151, hash 0xA3104204).  512 hardware point sprites; the vertex
// shader hashes the vertex index into a unit direction (literal multipliers
// c255 = 732.051, 236.068, 645.751, 141.421), places the star at radius 2500
// (c254.x) around the anchor (logical 291), twinkles with max4(frac(phase))
// where phase advances at 2.5 * time (c390 = time, brightness, 1.5, 2.5),
// and outputs colour = hash^2 * twinkle * brightness.  Point size 1.5,
// additive ONE/ONE blending (state table 0x83316EB0 = D3DBLEND_ONE), point
// size clamp 0.1..64 from the draw function.  The host expands each point
// to a screen-space quad (entry VSStars / PSStars; cbuffers b6 dome + b7
// star params = time, brightness, half-pixel extents).
extern const char* const kStarsVertexShaderHlsl;
extern const char* const kStarsPixelShaderHlsl;
inline constexpr float kStarRadius = 2500.0f;       // c254.x
inline constexpr float kStarPointSize = 1.5f;       // c390.z -> oPts
inline constexpr float kStarPhaseRate = 2.5f;       // c390.w

// Host fullscreen-triangle vertex shader (entry VSMainFullscreen).  The
// retail dome is a camera-centred cube whose interpolant o0 equals the
// world-space camera ray at every covered pixel; a fullscreen triangle whose
// rays are built from the camera basis produces the identical direction
// field, and PSMain consumes only the ray direction.  Pixel output is
// therefore exact while sidestepping far-plane clipping of the 2850-unit
// cube in the preview.
extern const char* const kDomeFullscreenVertexShaderHlsl;

// Byte layout of the cbuffer both shaders bind at register(b6).
struct DomeConstantBuffer {
    float view_projection[16] = {};   // row_major, preview space
    float camera_position[4] = {};    // preview space (c9); .w unused
    float camera_position_engine[4] = {};  // engine space (c19)
    float beta_rayleigh[4] = {};      // c64
    float beta_mie[4] = {};           // c65
    float sun_direction[4] = {};      // c66, engine space, unit
    float scattering_misc[4] = {};    // c67
    float overlay_blend[4] = {};      // x = c47.x
    // x = c31.x (150), y = c46.x (100), z = c20.w (1.0), w = b129 flag
    float dome_misc[4] = {kOpticalScaleRayleigh, kOpticalScaleMie, 1.0f, 0.0f};
    float bgmap_c72[4] = {};
    float bgmap_c73[4] = {};
    float bgmap_c74[4] = {};
    float bgmap_c75[4] = {};
    float bgmap_c76[4] = {};
    float bgmap_c28[4] = {};          // y = c28.y
    // Host-only fields for the fullscreen variant: camera basis in ENGINE
    // space; .w of right/up carries tan(fov/2) horizontal/vertical.
    float camera_right[4] = {};
    float camera_up[4] = {};
    float camera_forward[4] = {};
};

}  // namespace SkyDomeXex
