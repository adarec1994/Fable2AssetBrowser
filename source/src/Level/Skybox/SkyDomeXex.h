#pragma once

#include <array>
#include <cstdint>

namespace SkyDomeXex {

inline constexpr double kBetaRayleighCommon =
    248.05023415027725;
inline constexpr double kBetaRayleighPowBase = 0.000580084099999878;
inline constexpr double kBetaRayleighPowExponent = 2.0;

inline constexpr double kBetaRayleighChannel[3] = {
    0.007337032921211229, 0.009459203185216139, 0.025727610716641394};

inline constexpr float kBetaMieChannel[3] = {
    0.0056148912f, 0.0063754139f, 0.010514311f};

inline constexpr float kRayleighMultiplierScale = 500.0f;

inline constexpr float kRayleighPhaseNorm = 0.059683103f;
inline constexpr float kMiePhaseNorm = 0.079577468f;

inline constexpr float kMieG_A0 = 0.10000000149011612f;
inline constexpr float kMieG_A1 = 0.03180000185966492f;
inline constexpr float kMieG_B0 = 0.4440000057220459f;
inline constexpr float kMieG_B1 = 0.05984000116586685f;

inline constexpr float kOpticalScaleRayleigh = 150.0f;
inline constexpr float kOpticalScaleMie = 100.0f;

inline constexpr float kHorizonDistance = 1900.0f;
inline constexpr float kRecipMaxFogDistance = 0.00050000002f;

inline constexpr float kDomeRadius = 2850.0f;

inline constexpr int kLutWidth = 64;

struct ThemeInputs {
    float sun_intensity = 1.0f;
    float beta_rayleigh_multiplier = 1.0f;
    float beta_mie_multiplier = 1.0f;
    float sky_colour[4] = {1, 1, 1, 0};
    float complementary_colour[4] = {1, 1, 1, 0};
    float complementary_bias = 0.0f;
    float sunset_colour[4] = {1, 1, 1, 0};

    float fogging_start = 0.0f;
    float close_fog_max_distance = 0.0f;

    float sun_direction[4] = {0, 0, 1, 0};
};

struct AtmosphereState {

    float beta_rayleigh[4] = {};

    float beta_mie[4] = {};

    float sun_direction[4] = {};

    float scattering_misc[4] = {};

    float lut_beta_rayleigh_phase[4] = {};
    float lut_beta_mie_phase[4] = {};
    float lut_cos_sun_elevation = 1.0f;
    float lut_hg[4] = {};

    float lut_scale[4] = {1, 1, 1, 1};
    float lut_colour_a[4] = {};
    float lut_colour_b[4] = {};
    float lut_bias_plus_half = 0.5f;

    float sun_element_colour[4] = {1, 1, 0, 0};
};

AtmosphereState ComputeAtmosphereState(const ThemeInputs& theme);

void BuildInScatterLut(const AtmosphereState& state,
                       std::array<std::uint16_t, kLutWidth * 4>& out_rgba16f);

void BuildInScatterLutFloat(const AtmosphereState& state,
                            std::array<float, kLutWidth * 4>& out_rgba);

extern const char* const kDomeVertexShaderHlsl;
extern const char* const kDomePixelShaderHlsl;

extern const char* const kStarsVertexShaderHlsl;
extern const char* const kStarsPixelShaderHlsl;
inline constexpr float kStarRadius = 2500.0f;
inline constexpr float kStarPointSize = 1.5f;
inline constexpr float kStarPhaseRate = 2.5f;

extern const char* const kDomeFullscreenVertexShaderHlsl;

struct DomeConstantBuffer {
    float view_projection[16] = {};
    float camera_position[4] = {};
    float camera_position_engine[4] = {};
    float beta_rayleigh[4] = {};
    float beta_mie[4] = {};
    float sun_direction[4] = {};
    float scattering_misc[4] = {};
    float overlay_blend[4] = {};

    float dome_misc[4] = {kOpticalScaleRayleigh, kOpticalScaleMie, 1.0f, 0.0f};
    float bgmap_c72[4] = {};
    float bgmap_c73[4] = {};
    float bgmap_c74[4] = {};
    float bgmap_c75[4] = {};
    float bgmap_c76[4] = {};
    float bgmap_c28[4] = {};

    float camera_right[4] = {};
    float camera_up[4] = {};
    float camera_forward[4] = {};
};

}
