#include "SkyDomeXex.h"

#include <cmath>
#include <cstring>

namespace SkyDomeXex {
namespace {

inline float Dot3(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

float VectorAngle(const float* a, const float* b) {
    const float length_product =
        std::sqrt(Dot3(a, a)) * std::sqrt(Dot3(b, b));
    if (length_product <= 0.000099999997f) return 0.0f;
    const float cosine = Dot3(a, b) / length_product;
    if (std::fabs(cosine - 1.0f) < 0.000099999997f) return 0.0f;
    if (std::fabs(cosine + 1.0f) >= 0.000099999997f) {
        return static_cast<float>(std::acos(cosine));
    }
    return 3.1415927f;
}

float SunElevationAngle(const float sun_direction[4]) {
    const float negated[3] = {-sun_direction[0], -sun_direction[1],
                              -sun_direction[2]};
    const float horizontal[3] = {negated[0], negated[1], 0.0f};
    return VectorAngle(negated, horizontal);
}

std::uint16_t FloatToHalf(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    std::uint32_t abs_bits = bits & 0x7fffffffu;
    if (abs_bits >= 0x7f800000u) {
        return static_cast<std::uint16_t>(
            sign | 0x7c00u | ((abs_bits > 0x7f800000u) ? 0x200u : 0u));
    }
    if (abs_bits >= 0x47800000u) {
        return static_cast<std::uint16_t>(sign | 0x7c00u);
    }
    if (abs_bits < 0x38800000u) {
        const std::uint32_t shift = 126u - (abs_bits >> 23);
        std::uint32_t mantissa = (abs_bits & 0x7fffffu) | 0x800000u;
        if (shift > 24u) return static_cast<std::uint16_t>(sign);
        const std::uint32_t rounded =
            (mantissa >> shift) +
            (((mantissa >> (shift - 1)) & 1u) &
             (((mantissa & ((1u << (shift - 1)) - 1u)) != 0u) |
              ((mantissa >> shift) & 1u)));
        return static_cast<std::uint16_t>(sign | (rounded >> 13));
    }
    const std::uint32_t half =
        ((abs_bits >> 13) - 0x1c000u) & 0x7fffu;
    const std::uint32_t remainder = abs_bits & 0x1fffu;
    std::uint32_t rounded = half;
    if (remainder > 0x1000u ||
        (remainder == 0x1000u && (half & 1u))) {
        ++rounded;
    }
    return static_cast<std::uint16_t>(sign | rounded);
}

}

AtmosphereState ComputeAtmosphereState(const ThemeInputs& theme) {
    AtmosphereState state{};

    const double rayleigh_common =
        std::pow(kBetaRayleighPowBase, kBetaRayleighPowExponent) *
        kBetaRayleighCommon;
    float beta_rayleigh_base[3];
    for (int channel = 0; channel < 3; ++channel) {
        beta_rayleigh_base[channel] = static_cast<float>(
            rayleigh_common * kBetaRayleighChannel[channel]);
    }

    const float rayleigh_scale =
        theme.beta_rayleigh_multiplier * kRayleighMultiplierScale;
    const float mie_scale = theme.beta_mie_multiplier;
    for (int channel = 0; channel < 3; ++channel) {
        state.beta_rayleigh[channel] =
            beta_rayleigh_base[channel] * rayleigh_scale;
        state.beta_mie[channel] = kBetaMieChannel[channel] * mie_scale;
    }
    state.beta_rayleigh[3] = 0.0f;
    state.beta_mie[3] = 0.0f;

    for (int i = 0; i < 4; ++i) {
        state.sun_direction[i] = theme.sun_direction[i];
    }

    state.scattering_misc[0] = theme.fogging_start;
    state.scattering_misc[1] = kHorizonDistance;
    state.scattering_misc[2] =
        theme.close_fog_max_distance > 0.0f
            ? 1.0f / theme.close_fog_max_distance
            : 1.0f;
    state.scattering_misc[3] = kRecipMaxFogDistance;

    for (int channel = 0; channel < 3; ++channel) {
        state.lut_beta_rayleigh_phase[channel] =
            state.beta_rayleigh[channel] * kRayleighPhaseNorm;
        state.lut_beta_mie_phase[channel] =
            state.beta_mie[channel] * kMiePhaseNorm;
    }
    state.lut_beta_rayleigh_phase[3] = 0.0f;
    state.lut_beta_mie_phase[3] = 0.0f;

    const float elevation = SunElevationAngle(theme.sun_direction);
    state.lut_cos_sun_elevation =
        static_cast<float>(std::cos(elevation));

    const float f10 = elevation * kMieG_A1;
    const float f8 = -f10;
    const float f7 = f8 + kMieG_A0;
    const float f9 = kMieG_B0 - elevation * kMieG_B1;
    const float f6 = f9 - f7;
    const float g = static_cast<float>(
        static_cast<double>(f6) * 0.5 + static_cast<double>(f7));

    const float g_squared = g * g;
    state.lut_hg[0] =
        static_cast<float>(1.0 - static_cast<double>(g_squared));
    state.lut_hg[1] = g_squared + 1.0f;
    state.lut_hg[2] = g * 2.0f;
    state.lut_hg[3] = 0.0f;

    state.lut_scale[0] = 1.0f;
    state.lut_scale[1] = 1.0f;
    state.lut_scale[2] = 1.0f;
    state.lut_scale[3] = theme.sun_intensity;

    const float cos_elev = state.lut_cos_sun_elevation;
    state.sun_element_colour[0] =
        (theme.sunset_colour[0] - 1.0f) * cos_elev + 1.0f;
    state.sun_element_colour[1] =
        (theme.sunset_colour[1] - 1.0f) * cos_elev + 1.0f;
    state.sun_element_colour[2] = theme.sunset_colour[2] * cos_elev;
    state.sun_element_colour[3] = 0.0f;

    for (int i = 0; i < 3; ++i) {
        state.lut_colour_a[i] = theme.sky_colour[i];
        state.lut_colour_b[i] = theme.complementary_colour[i];
    }
    state.lut_colour_a[3] = 0.0f;
    state.lut_colour_b[3] = 0.0f;

    state.lut_bias_plus_half = theme.complementary_bias + 0.5f;
    return state;
}

void BuildInScatterLutFloat(const AtmosphereState& state,
                            std::array<float, kLutWidth * 4>& out_rgba) {

    float recip[3];
    for (int channel = 0; channel < 3; ++channel) {
        recip[channel] = 1.0f / (state.beta_rayleigh[channel] +
                                 state.beta_mie[channel]);
    }

    float colour_delta[3];
    for (int channel = 0; channel < 3; ++channel) {
        colour_delta[channel] =
            state.lut_colour_a[channel] - state.lut_colour_b[channel];
    }

    const float ramp_delta[3] = {1.0f - 1.0f, 0.3f - 1.0f, 0.1f - 1.0f};

    for (int i = 0; i < kLutWidth; ++i) {
        const float t =
            static_cast<float>(i) / static_cast<float>(kLutWidth);

        const float one_plus_t2 = t * t + 1.0f;

        const float hg_denominator = std::pow(
            state.lut_hg[1] - state.lut_hg[2] * t, 1.5f);

        const float t_cos = t * state.lut_cos_sun_elevation;

        const float hg_ratio = state.lut_hg[0] / hg_denominator;

        const float gated =
            (t * 0.5f + state.lut_bias_plus_half < 0.0f) ? 0.0f : hg_ratio;
        const float colour_factor = gated < 1.0f ? gated : 1.0f;

        for (int channel = 0; channel < 3; ++channel) {

            const float phase =
                state.lut_beta_mie_phase[channel] * hg_ratio +
                state.lut_beta_rayleigh_phase[channel] * one_plus_t2;

            const float ramp = ramp_delta[channel] * t_cos + 1.0f;

            const float colour =
                colour_delta[channel] * colour_factor +
                state.lut_colour_b[channel];

            const float scaled =
                ramp * state.lut_scale[channel] * state.lut_scale[3];
            out_rgba[std::size_t(i) * 4 + channel] =
                scaled * (phase * recip[channel]) * colour;
        }
        out_rgba[std::size_t(i) * 4 + 3] = 0.0f;
    }
}

void BuildInScatterLut(const AtmosphereState& state,
                       std::array<std::uint16_t, kLutWidth * 4>& out) {
    std::array<float, kLutWidth * 4> texels{};
    BuildInScatterLutFloat(state, texels);
    for (std::size_t i = 0; i < texels.size(); ++i) {
        out[i] = FloatToHalf(texels[i]);
    }
}

const char* const kDomeVertexShaderHlsl = R"(
cbuffer SkyDomeXexCB : register(b6) {
    row_major float4x4 view_projection;
    float4 camera_position;         // preview space
    float4 camera_position_engine;  // engine space (PS c19)
    float4 beta_rayleigh;           // c64
    float4 beta_mie;                // c65
    float4 sun_direction;           // c66, engine space
    float4 scattering_misc;         // c67
    float4 overlay_blend;           // x = c47.x
    float4 dome_misc;               // x=c31.x  y=c46.x  z=c20.w  w=b129
    float4 bgmap_c72;
    float4 bgmap_c73;
    float4 bgmap_c74;
    float4 bgmap_c75;
    float4 bgmap_c76;
    float4 bgmap_c28;
    float4 camera_right;    // engine space, w = tan(fov_x/2)
    float4 camera_up;       // engine space, w = tan(fov_y/2)
    float4 camera_forward;  // engine space
}

struct VSOUT {
    float4 position : SV_Position;
    float3 ray : TEXCOORD0;  // engine space, unnormalised (o0)
};

// Packed program 110 (VSHADER entry 132, hash 0x7255B944).
VSOUT VSMain(float3 position : POSITION) {
    // packet 3: vfetch r0.xyz (unit-cube corner, [0,1]^3)
    float3 r0 = position;
    // packet 4: mad r0.xyz, r0.xyz, c255.y, c255.z   (c255 = 2850, 2, -1, 0)
    r0 = r0 * 2.0 - 1.0;
    // packet 5: mul r0.xyz, r0.xyz, c255.x
    r0 = r0 * 2850.0;
    // packet 6: add r1.xyz, r0.xyz, c9.xyz; scalar sges r1.w -> 1.0
    // (camera-centred dome; engine ray -> preview axes via .xzy)
    float3 world = camera_position.xyz + r0.xzy;
    VSOUT output;
    // packets 7-10: dp4 oPos with rows c0..c3
    output.position = mul(float4(world, 1.0), view_projection);
    // packet 11: max o0.xyz, r0, r0
    output.ray = r0;
    return output;
}

// Host fullscreen variant: same ray field as the retail cube (see header).
VSOUT VSMainFullscreen(uint id : SV_VertexID) {
    float2 corner;
    if (id == 0)      corner = float2(-1.0, -1.0);
    else if (id == 1) corner = float2(-1.0,  3.0);
    else              corner = float2( 3.0, -1.0);

    VSOUT output;
    output.position = float4(corner, 0.999, 1.0);
    float3 ray = camera_forward.xyz +
                 camera_right.xyz * (corner.x * camera_right.w) +
                 camera_up.xyz * (corner.y * camera_up.w);
    // Magnitude parity with the retail interpolant; PSMain normalises.
    output.ray = ray * 2850.0;
    return output;
}
)";

const char* const kDomePixelShaderHlsl = R"(
Texture2D in_scatter_lut : register(t4);   // fetch f4, 64x1 RGBA16F
Texture2D overlay_a : register(t13);       // fetch f13 (SKY_OVERLAY pair A)
Texture2D overlay_b : register(t14);       // fetch f14 (SKY_OVERLAY pair B)
Texture2D bgmap_primary : register(t11);   // fetch f11 (background map)
Texture2D bgmap_lookup : register(t12);    // fetch f12
SamplerState wrap_sampler : register(s0);
SamplerState clamp_sampler : register(s1);

cbuffer SkyDomeXexCB : register(b6) {
    row_major float4x4 view_projection;
    float4 camera_position;
    float4 camera_position_engine;  // c19
    float4 beta_rayleigh;           // c64
    float4 beta_mie;                // c65
    float4 sun_direction;           // c66
    float4 scattering_misc;         // c67 (y = 1900 horizon distance)
    float4 overlay_blend;           // x = c47.x
    float4 dome_misc;               // x=150 (c31.x) y=100 (c46.x) z=c20.w w=b129
    float4 bgmap_c72;
    float4 bgmap_c73;
    float4 bgmap_c74;
    float4 bgmap_c75;
    float4 bgmap_c76;
    float4 bgmap_c28;
    float4 camera_right;
    float4 camera_up;
    float4 camera_forward;
}

struct PSIN {
    float4 position : SV_Position;
    float3 ray : TEXCOORD0;
};

// Literal constant file of packed program 111 (embedded ucode prefix,
// upload record c248..c255):
//   c250 = (-0.0187293,  0.180141,   0.5,       1.05)
//   c251 = (-pi,        -0.085133,   pi,       -2.0)
//   c252 = (pi/2,        1.57073,   -0.212114,  0.999866)
//   c253 = (1.0,         0.0,        0.074261, -0.330299)
//   c254 = (0.2,         0.0208351,  2/pi,      1/(2pi))
//   c255 = (1.4427,      0, 0, 0)   [1/ln2: exp(x) via exp2]

// Packed program 111 (PSHADER entry 133, hash 0xD5610E4C).
float4 PSMain(PSIN input) : SV_Target {
    float4 r0 = float4(input.ray, 0.0);
    float4 r1 = 0.0, r2 = 0.0, r3 = 0.0, r4 = 0.0, r5 = 0.0, r6 = 0.0;

    // cf[0] packets 10-15
    r0.w = dot(r0.zxy, r0.zxy);                       // p10 dp3
    r1.x = rsqrt(abs(r0.w));                          // p11 rsq
    r6.yzw = r1.x * r0.xyz;                           // p12 (n = normalize)
    r3.x = abs(r6.w) * -0.0187293;                    // p13 c250.x
    r2.xyz = r1.x * sun_direction.xyz;                // p14
    r1.y = min(abs(r6.y), abs(r6.z));                 // p14 scalar mins
    r2.w = saturate(dot(r2.zxy, r0.zxy));             // p15 dp3_sat (LUT u)
    r1.w = max(r6.y, r6.z);                           // p15 scalar maxs
    r1.z = -abs(r6.w) + 1.0;                          // p16 c253.x
    r0.w = max(abs(r6.y), abs(r6.z));                 // p16 scalar maxs
    r2.z = (abs(r6.z) > abs(r6.y)) ? 1.0 : 0.0;       // p17 sgt
    r0.w = 1.0 / r0.w;                                // p17 scalar rcp

    // cf[1] packets 16-21 (exec boundary is at p16 in hardware; the
    // straight-line translation is equivalent)
    r5.z = (r1.w >= -r1.w) ? 1.0 : 0.0;               // p18 sge
    r2.x = sqrt(abs(r1.z));                           // p18 scalar sqrt
    r4.x = r1.y * r0.w;                               // p19 (atan ratio)
    r6.x = min(r6.y, r6.z);                           // p19 scalar mins
    r3.w = r4.x * r4.x;                               // p20
    r3.z = log2(abs(r6.w));                           // p20 scalar log
    r1.z = r3.z * 0.2;                                // p21 c254.x
    r1.w = r3.w * 0.0208351;                          // p21 c254.y
    r1.y = -2.0 * r2.x;                               // p21 scalar c251.w

    // cf[2] packets 22-27
    r0.w = r1.w + -0.085133;                          // p22 c251.y
    r2.y = exp2(r1.z);                                // p22 scalar (|nz|^0.2)
    r0.w = r3.w * r0.w + 0.180141;                    // p23 c250.y
    r3.y = r3.w * r0.w;                               // p24
    r1.z = 1.05 + r2.y;                               // p24 scalar subsc
    r3.xy = r3.xy + float2(0.074261, -0.330299);      // p25 c253.zw
    r1.w = scattering_misc.y * r1.x;                  // p25 scalar (1900/len)
    r4.y = (-r6.x > r6.x) ? 1.0 : 0.0;                // p26 sgt
    r4.z = (-r6.y > r6.y) ? 1.0 : 0.0;
    r4.w = (-r6.w > r6.w) ? 1.0 : 0.0;
    float prev = abs(r6.w);                           // p26 scalar maxs -.
    r3.y = r3.w * r3.y;                               // p27
    r3.x = prev * r3.x;                               // p27 muls_prev

    // cf[3] packets 28-33
    r5.xy = r3.xy + float2(-0.212114, 0.999866);      // p28 c252.zw
    r0.w = scattering_misc.y * r1.z;                  // p28 scalar mulsc
    r3.y = r4.x * r5.y;                               // p29
    r3.w = r4.y * r5.z;
    prev = abs(r6.w);                                 // p29 scalar maxs -.
    r1.x = r3.y * -2.0;                               // p30 c251.w
    r1.z = prev * r5.x;                               // p30 muls_prev
    r5.z = r1.x + 1.5707964;                          // p31 c252.x
    r5.w = r1.z + 1.57073;                            // p31 c252.y
    r5.x = dome_misc.x + r0.w;                        // p31 scalar (c31.x=150)
    r1.x = r1.y * r5.w + 3.1415927;                   // p32 c251.z
    r3.z = r1.x * r4.w;                               // p33
    r5.y = dome_misc.y + r0.w;                        // p33 scalar (c46.x=100)

    // cf[4] packets 34-39
    r1.x = r5.z * r2.z + r3.y;                        // p34
    r1.y = r5.w * r2.x + r3.z;
    r1.z = r4.z * -3.1415927 + r1.x;                  // p35 c251.x
    r3.x = r1.z + r1.z;                               // p36
    r1.x = 1.5707964 + r1.y;                          // p36 scalar subsc c252.x
    r1.y = r3.w * -r3.x + r1.z;                       // p37
    float2 uv_tmp = float2(r1.y * 0.15915494,         // p38 c254.w (1/2pi)
                           r1.x * 0.63663858);        // p38 c254.z (2/pi)
    r1.x = uv_tmp.x;
    r1.z = uv_tmp.y;
    r1.y = 0.5 + r1.x;                                // p39 scalar addsc c250.z

    // cf[5] packets 40-42: texture fetches
    r4.xyz = in_scatter_lut.Sample(
        clamp_sampler, float2(r2.w, 0.5)).xyz;        // p40 f4
    float4 tex_b = overlay_b.Sample(wrap_sampler, r1.yz);  // p41 f14
    r6 = tex_b.wxyz;                                  // dest swizzle wxyz
    r3 = overlay_a.Sample(wrap_sampler, r1.yz);       // p42 f13

    // cf[7] packets 43-48: overlay cross-fade in squared space
    r6.x = r6.x - r3.w;                               // p43 (Ba - Aa)
    r1.xyz = r3.xyz * r3.xyz;                         // p44 (A rgb^2)
    r6.y = r6.y * r6.y - r1.x;                        // p45 (Br^2-Ar^2)
    r6.z = r6.w * r6.w - r1.z;                        //     (Bb^2-Ab^2)
    r6.w = r6.z;  // NOTE: hardware computes lanes in parallel; see below
    // p45 exact parallel semantics: y=B.r^2-A.r^2, z=B.b^2-A.b^2,
    // w=B.g^2-A.g^2 from pre-packet values:
    {
        float pre_y = tex_b.x, pre_z = tex_b.z, pre_w = tex_b.y;
        r6.y = pre_y * pre_y - r1.x;
        r6.z = pre_z * pre_z - r1.z;
        r6.w = pre_w * pre_w - r1.y;
    }
    r3.x = r6.x * overlay_blend.x + r3.w;             // p46 (alpha mix)
    {
        float3 mixed;                                 // p47
        mixed.x = r6.y * overlay_blend.x + r1.x;      // R^2 mix
        mixed.y = r6.z * overlay_blend.x + r1.z;      // B^2 mix
        mixed.z = r6.w * overlay_blend.x + r1.y;      // G^2 mix
        r3.y = mixed.x;
        r3.z = mixed.y;
        r3.w = mixed.z;
    }
    r2.x = r5.x * r2.y + r0.w;                        // p48 (optical depths)
    r2.y = r5.y * r2.y + r0.w;

    // cf[8] packets 49-54: transmittance
    r1.xyz = r2.y * beta_mie.xyz;                     // p49
    r1.xyz = r2.x * beta_rayleigh.xyz + r1.xyz;       // p50
    r1.xyz = -r1.xyz * 1.4427;                        // p51 c255.x (1/ln2)
    r1.x = exp2(r1.x);                                // p52 scalar
    r2.x = r1.w * r0.x;                               // p53 (1900 * n)
    r2.z = r1.w * r0.z;
    r2.w = r1.w * r0.y;
    r1.y = exp2(r1.y);                                // p53 scalar
    float3 horizon_point;                             // p54 (+ c19 cam pos)
    horizon_point.x = r2.x + camera_position_engine.x;
    horizon_point.y = r2.w + camera_position_engine.y;
    horizon_point.z = r2.z + camera_position_engine.z;
    r0.x = horizon_point.x;
    r0.z = horizon_point.y;
    r0.w = horizon_point.z;
    r1.z = exp2(r1.z);                                // p54 scalar

    // cf[9] packets 55-58: in-scatter + overlay composite
    r1.xyz = -r1.xyz + 1.0;                           // p55 (1 - T)
    r1.xyz = r1.xyz * r4.xyz;                         // p56 (* LUT colour)
    r3.y = r3.y - r1.x;                               // p57
    r3.z = r3.z - r1.z;
    r3.w = r3.w - r1.y;
    r1.x = r3.y * r3.x + r1.x;                        // p58 (lerp by alpha)
    r1.y = r3.w * r3.x + r1.y;
    r1.z = r3.z * r3.x + r1.z;

    // cf[10]: cjmp !b129 -> exec_end.  Background-map reprojection branch
    // (packets 59-99), active only inside mist volumes with a bound bg map.
    [branch]
    if (dome_misc.w > 0.5) {
        // p59 cndgt: select horizon world point or camera position
        r3.x = (-r2.z > 0.0) ? r0.x : camera_position_engine.x;
        r3.y = (-r2.z > 0.0) ? r0.z : camera_position_engine.y;
        // p60: bg-map UV transform
        r3.xy = r3.xy * bgmap_c74.xy + bgmap_c74.zw;
        // p61 tfetch f12 -> r0.y
        r0.y = bgmap_lookup.Sample(wrap_sampler, r3.xy).x;
        // p62 cndge
        r3.x = (r2.z >= 0.0) ? 0.0 : 1.0;
        r3.w = (r2.z >= 0.0) ? 1.0 : 0.0;
        // p63
        r1.w = -camera_position_engine.z + bgmap_c76.y;
        r0.y = bgmap_c76.x * -r0.y;                   // p63 scalar
        r2.y = r1.w + r0.y;                           // p64
        r1.w = 1.0 / r2.z;                            // p64 scalar rcp
        r4.x = r2.y * r1.w;                           // p65
        r4.y = r2.w * r1.w;
        r3.y = (1.0 > r4.x) ? 1.0 : 0.0;              // p66 sgt
        r3.z = 1.0;                                   // p66 scalar sges(a,a)
        r4.z = abs(r3.x) + abs(r3.z);                 // p67
        r4.w = abs(r3.y) + abs(r3.w);
        r4.z = (r4.z != 0.0) ? 1.0 : 0.0;             // p68 sne
        r4.w = (r4.w != 0.0) ? 1.0 : 0.0;
        r1.w = r4.w * r4.w;                           // p69 scalar muls
        r0.y = r0.y + bgmap_c76.y;                    // p70
        bool p0 = (r1.w == 0.0);                      // p70 scalar setp_eq
        [branch]
        if (!p0) {
            r3.xy = r3.xy * r3.zw;                    // p71
            r2.xyz = r4.xyx * r2.xyz +
                     camera_position_engine.xyz;      // p72
            r4.xyz = (r3.y == 0.0)
                ? float3(r0.x, r0.z, r0.w) : r2.xyz;  // p73 cndeq
            r5.xyz = (r3.x == 0.0)
                ? camera_position_engine.xyz : r2.xyz;  // p74 cndeq
            r2.xy = r5.xy * bgmap_c73.xy;             // p75
            r2.z = r4.y * bgmap_c73.y;                // p76
            r2.w = r4.x * bgmap_c73.x;
            r2 = r2 + bgmap_c73.zwwz;                 // p77
            r0.x = r2.y + r2.z;                       // p78
            r0.z = r2.x + r2.w;
            float2 swapped = float2(r0.z * 0.5, r0.x * 0.5);  // p79 c250.z
            r0.x = swapped.x;
            r0.z = swapped.y;
            r3.z = bgmap_primary.Sample(wrap_sampler, r2.xy).x;  // p80 f11
            r3.w = bgmap_primary.Sample(wrap_sampler,
                                        float2(r0.x, r0.z)).x;   // p81 f11
            r0.w = bgmap_c75.x * 0.5;                 // p82
            r2.w = -bgmap_c75.y + 1.0;                // p83
            r2.x = r0.y - r5.z;                       // p84
            r5.xyz = r4.xyz - r5.xyz;                 // p85
            prev = r0.y;                              // p85 scalar maxs -.
            r0.x = dot(r5.zxy, r5.zxy);               // p86 dp3
            r2.y = prev + -r4.z;                      // p86 adds_prev
            r3.xy = saturate(r2.xy * bgmap_c76.w);    // p87 mul_sat
            r2.y = r3.x + r3.y;                       // p88
            r2.z = r3.z + r3.w;
            r2.x = sqrt(abs(r0.x));                   // p88 scalar sqrt
            float2 halves = float2(r2.x * 0.5, r2.z * 0.5);  // p89
            r0.x = halves.x;
            r0.z = halves.y;
            float3 prod = float3(r0.z * r2.w,         // p90
                                 r0.x * r2.y,
                                 r0.w * r2.z);
            r0.xyz = prod;
            r0.x = r0.x + bgmap_c75.y;                // p91
            r0.y = r0.z - r0.z;                       // p91 scalar subs (z,z)
            r0.x = r0.y * r0.x;                       // p92
            r0.x = max(r0.x, 0.0);                    // p93
            r0.x = bgmap_c76.z * r0.x;                // p94 scalar
            r0.y = exp2(r0.x);                        // p95 scalar
            r0.x = 1.0 + r0.y;                        // p96 scalar subsc
            float3 tint = r0.x * bgmap_c72.zyx;       // p97 (lanes x,z,w)
            r0.x = tint.x;
            r0.z = tint.y;
            r0.w = tint.z;
            float3 scaled2 = float3(r0.w, r0.z, r0.x) * bgmap_c28.y;  // p98
            r0.x = scaled2.x;
            r0.z = scaled2.y;
            r0.w = scaled2.z;
            r1.xyz = r1.xyz * r0.y +
                     float3(r0.x, r0.z, r0.w);        // p99 mad
        }
    }

    // cf[18] packet 100: mul o0.xyz1, r1.xyz, c20.w
    float3 hdr = r1.xyz * dome_misc.z;
    // HOST STAND-IN (not retail): the XEX renders this into an HDR target
    // and the TONE_MAPPING pass (theme records 21..52) compresses it.  Until
    // that pass is ported, bound the output with Reinhard x/(1+x).
    return float4(hdr / (1.0 + hdr), 1.0);
}
)";

const char* const kDomeFullscreenVertexShaderHlsl = kDomeVertexShaderHlsl;

const char* const kStarsVertexShaderHlsl = R"(
cbuffer SkyDomeXexCB : register(b6) {
    row_major float4x4 view_projection;
    float4 camera_position;
    float4 camera_position_engine;
    float4 beta_rayleigh;
    float4 beta_mie;
    float4 sun_direction;
    float4 scattering_misc;
    float4 overlay_blend;
    float4 dome_misc;
    float4 bgmap_c72;
    float4 bgmap_c73;
    float4 bgmap_c74;
    float4 bgmap_c75;
    float4 bgmap_c76;
    float4 bgmap_c28;
    float4 camera_right;    // engine space, w = tan(fov_x/2)
    float4 camera_up;       // engine space, w = tan(fov_y/2)
    float4 camera_forward;  // engine space
}
cbuffer SkyStarsCB : register(b7) {
    // x = engine clock (dbl_83496C48), y = STARS_BRIGHTNESS (record 70),
    // z = half point width in NDC, w = half point height in NDC.
    float4 star_params;
}

struct VSOUT {
    float4 position : SV_Position;
    float colour : TEXCOORD0;
};

VSOUT VSMain(uint vertex_id : SV_VertexID) {
    const uint star = vertex_id / 6u;
    const uint corner = vertex_id % 6u;
    const float index = (float)star;

    // c390 = (time, brightness, 1.5, 2.5); c254 = (2500, 2, 1, -1);
    // c255 = (732.051, 236.068, 645.751, 141.421).
    // packet 3: r0 = index * c255
    float4 r0 = index * float4(732.051, 236.068, 645.751, 141.421);
    // packet 5: r2 = frac(r0); scalar r0.w = c390.w * c390.x (phase rate)
    float4 r2 = frac(r0);
    float phase_rate = 2.5 * star_params.x;
    // packet 6: r0.xy = r2.xy * 2 - 1
    float2 dir_xy = r2.xy * 2.0 - 1.0;
    // packet 7: r3 = r2.xywz + 1; scalar r0.z = r2.z * r2.z
    float4 r3 = r2.xywz + 1.0;
    float r0z = r2.z * r2.z;
    // packet 8: r3 = phase_rate * r3; scalar r0.w = r2.w * r2.w
    r3 = phase_rate * r3;
    float hash_sq = r2.w * r2.w;
    // packet 9: r1.x = dp2add(r0.xy, r0.xy, r0.z)
    float len_sq = dot(dir_xy, dir_xy) + r0z;
    // packet 10: r3 = frac(r3); scalar r1.x = rsq|len|
    r3 = frac(r3);
    float inv_len = rsqrt(abs(len_sq));
    // packet 11: r1.y = max4(r3); scalar prev = r2.z
    float twinkle = max(max(r3.x, r3.y), max(r3.z, r3.w));
    // packet 12: r0.xy *= inv_len; r0.w *= twinkle; scalar r0.z = r2.z*inv_len
    float3 direction = float3(dir_xy * inv_len, r2.z * inv_len);
    float value = hash_sq * twinkle;
    // packet 13: r1.xyz = direction * 2500 + c46 (anchor; camera-relative
    // in the preview, so the offset cancels)
    float3 world = direction * 2500.0;
    // packets 14-17: oPos = VP * r1 -> host camera-basis projection
    float depth = dot(world, camera_forward.xyz);
    VSOUT output;
    if (depth <= 0.01) {
        output.position = float4(0.0, 0.0, -10.0, 1.0);  // clipped away
        output.colour = 0.0;
        return output;
    }
    float2 ndc = float2(
        dot(world, camera_right.xyz) / (depth * camera_right.w),
        dot(world, camera_up.xyz) / (depth * camera_up.w));
    // oPts = 1.5 (c390.z): expand the point sprite corner
    const float2 corner_offsets[6] = {
        float2(-1, 1), float2(1, 1), float2(-1, -1),
        float2(-1, -1), float2(1, 1), float2(1, -1)
    };
    ndc += corner_offsets[corner] * star_params.zw;
    output.position = float4(ndc, 0.9985, 1.0);
    // packet 18: o0.x = r0.w * c390.y (brightness)
    output.colour = value * star_params.y;
    return output;
}
)";

const char* const kStarsPixelShaderHlsl = R"(
struct PSIN {
    float4 position : SV_Position;
    float colour : TEXCOORD0;
};
// Packed program 127: max o0.xyz0, r0.x, r0.x (alpha 0; additive ONE/ONE).
float4 PSMain(PSIN input) : SV_Target {
    return float4(input.colour.xxx, 0.0);
}
)";

}
