#include "SkyboxRenderer.h"

#ifdef _WIN32

#include "CloudRuntime.h"
#include "SkyDomeXex.h"
#include "SkyXexDecomp.h"
#include "../../UI/ModelPreview.h"
#include "../../UI/OutputLog.h"
#include "../../Utilities/State.h"
#include "../../textures/TexParser.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

using namespace DirectX;

namespace Skybox {
namespace {

void SkyDomeDebug(const std::string&) {}

}

void DebugLog(const char*) {}

namespace {

const char* kSkyElementVertexShader = R"(
struct VSIN {
    float4 position : POSITION;
    float2 uv : TEXCOORD0;
};
struct VSOUT {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
VSOUT VS(VSIN input) {
    VSOUT output;
    output.position = input.position;
    output.uv = input.uv;
    return output;
}
)";

const char* kSkyElementPixelShader = R"(
Texture2D element_tex : register(t0);
SamplerState element_sampler : register(s0);
cbuffer SkyElementCB : register(b6) {
    float4 element_colour;
}
struct PSIN {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
float4 PS(PSIN input) : SV_Target {
    float4 texel = element_tex.Sample(element_sampler, input.uv);
    return float4(texel.rgb * element_colour.rgb,
                  texel.a * element_colour.a);
}
)";

#if FABLE_ENABLE_RECONSTRUCTED_SKY_PREVIEW
const char* kSkyVertexShader = R"(
struct VSOUT{
    float4 p  : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOUT VS(uint id : SV_VertexID) {
    float2 p;
    if (id == 0) {
        p = float2(-1.0, -1.0);
    } else if (id == 1) {
        p = float2(-1.0,  3.0);
    } else {
        p = float2( 3.0, -1.0);
    }

    VSOUT o;
    o.p = float4(p, 0.999, 1.0);
    o.uv = float2((p.x + 1.0) * 0.5, 1.0 - (p.y + 1.0) * 0.5);
    return o;
}
)";

const char* kSkyPixelShader = R"(
Texture2D sky_moon_tex : register(t12);
Texture2D sky_moon_glare_tex : register(t13);
Texture2D sky_sun_disc_tex : register(t14);
SamplerState smp_cloud : register(s0);

cbuffer SkyCB : register(b4){
    float4 sky_top;
    float4 sky_bottom;
    float4 sky_sunset;
    float4 sky_params;
    float4 cloud_layer[4];
    float4 cloud_shape[4];
    float4 cloud_motion[4];
    float4 cloud_light[4];
    float4 cloud_global;
    float4 cloud_density_flags;
    float4 sky_right;
    float4 sky_up;
    float4 sky_forward;
    float4 sky_sun;
    float4 sky_texture_flags;
}

struct PSIN{
    float4 p  : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 PS(PSIN i) : SV_Target {
    float2 screen = float2(i.uv.x * 2.0 - 1.0,
                           (1.0 - i.uv.y) * 2.0 - 1.0);
    float3 ray = normalize(
        sky_forward.xyz +
        sky_right.xyz * screen.x * sky_right.w +
        sky_up.xyz * screen.y * sky_up.w);
    float3 sun_dir = normalize(sky_sun.xyz);
    float h = saturate(ray.y * 0.5 + 0.5);






    float rayleigh = max(sky_params.z, 0.05);
    float mie = max(sky_params.w, 0.05);
    float3 betaR = float3(0.007337, 0.009459, 0.0257276) * rayleigh;
    float3 betaM = float3(0.0056149, 0.0063754, 0.0105143) * mie;
    float cosT = dot(ray, sun_dir);
    float phaseR = 0.059683103 * (1.0 + cosT * cosT);
    float gm = 0.80;
    float hg = 1.0 + gm * gm - 2.0 * gm * cosT;
    float phaseM = 0.079577468 * (1.0 - gm * gm) /
                   max(pow(abs(hg), 1.5), 0.0001);
    float elev = max(ray.y, 0.004);
    float path = 1.0 / (elev + 0.09);
    float3 od = (betaR + betaM) * path * 26.0;
    float3 extinct = exp(-od);
    float3 beta_sum = max(betaR + betaM, 0.00001);
    float3 inscatter =
        (betaR * phaseR + betaM * phaseM) / beta_sum * (1.0 - extinct);



    float sun_h = saturate(sun_dir.y * 2.2 + 0.12);
    float3 col = inscatter * (7.2 * sun_h) * sky_top.rgb;
    float horizon = 1.0 - saturate(elev * 3.2);
    float bias = saturate(sky_params.y);
    col = lerp(col, sky_bottom.rgb * (0.35 + 0.65 * sun_h),
               horizon * (0.55 + 0.30 * bias));


    float sunset_w = saturate(1.0 - abs(sun_dir.y) * 5.0) *
                     saturate(cosT * 0.5 + 0.5);
    col = lerp(col, sky_sunset.rgb * (0.4 + 0.8 * phaseM),
               sunset_w * 0.45);

    float night = saturate((-sun_dir.y + 0.05) / 0.45);
    col = lerp(col, col * 0.22 + float3(0.010, 0.018, 0.050), night);

    float sun_align = saturate(cosT);
    if (sky_texture_flags.z > 0.5 && night < 0.95) {
        float3 sun_right = cross(float3(0.0, 1.0, 0.0), sun_dir);
        float right_len = dot(sun_right, sun_right);
        sun_right = normalize(lerp(float3(1.0, 0.0, 0.0), sun_right,
                                   step(0.0001, right_len)));
        float3 sun_up = normalize(cross(sun_dir, sun_right));
        float2 sun_xy = float2(dot(ray, sun_right), dot(ray, sun_up));
        float sun_radius = 0.055;
        float2 sun_uv = sun_xy / sun_radius * 0.5 + 0.5;
        float in_rect =
            step(0.0, sun_uv.x) * step(sun_uv.x, 1.0) *
            step(0.0, sun_uv.y) * step(sun_uv.y, 1.0);
        if (in_rect > 0.5) {
            float4 sun_sample = sky_sun_disc_tex.SampleLevel(
                smp_cloud, sun_uv, 0.0);
            float sun_alpha = max(max(sun_sample.a, sun_sample.r),
                                  max(sun_sample.g, sun_sample.b));
            col = lerp(col, sun_sample.rgb * saturate(sky_params.x),
                       saturate(sun_alpha * (1.0 - night)));
        }
    }
    col += sky_top.rgb * (1.0 - h) * 0.05;

    float3 moon_dir = normalize(-sun_dir);
    float moon_visible = saturate((moon_dir.y + 0.02) / 0.28) * night;
    if (moon_visible > 0.001) {
        float3 moon_right = cross(float3(0.0, 1.0, 0.0), moon_dir);
        float right_len = dot(moon_right, moon_right);
        moon_right = normalize(lerp(float3(1.0, 0.0, 0.0), moon_right,
                                    step(0.0001, right_len)));
        float3 moon_up = normalize(cross(moon_dir, moon_right));
        float2 moon_xy = float2(dot(ray, moon_right), dot(ray, moon_up));
        float moon_align = saturate(dot(ray, moon_dir));
        float moon_radius = 0.045;
        float2 moon_uv = moon_xy / moon_radius * 0.5 + 0.5;
        float in_rect =
            step(0.0, moon_uv.x) * step(moon_uv.x, 1.0) *
            step(0.0, moon_uv.y) * step(moon_uv.y, 1.0);
        float moon_edge = 1.0 - smoothstep(moon_radius * 0.45,
                                           moon_radius,
                                           length(moon_xy));
        float3 moon_col = float3(0.78, 0.82, 0.92);
        float moon_alpha = moon_edge;
        if (sky_texture_flags.x > 0.5 && in_rect > 0.5) {
            float2 moon_tiles = float2(max(cloud_global.w, 1.0),
                                       max(sky_texture_flags.w, 1.0));
            float4 moon_sample = sky_moon_tex.SampleLevel(
                smp_cloud, moon_uv / moon_tiles, 0.0);
            moon_col = max(moon_sample.rgb, moon_col * moon_sample.r);
            moon_alpha *= max(max(moon_sample.a, moon_sample.r),
                              max(moon_sample.g, moon_sample.b));
        } else {
            moon_alpha = 0.0;
        }
        float glare = pow(moon_align, 220.0) * 0.45;
        if (sky_texture_flags.y > 0.5 && in_rect > 0.5) {
            float4 glare_sample = sky_moon_glare_tex.SampleLevel(
                smp_cloud, moon_uv, 0.0);
            glare += max(max(glare_sample.r, glare_sample.g),
                         glare_sample.b) * 0.35;
        }
        col = lerp(col, moon_col, saturate(moon_alpha * moon_visible));
        col += float3(0.34, 0.40, 0.58) * glare * moon_visible;
    }

    return float4(saturate(col), 1.0);
}
)";
#endif

#if FABLE_ENABLE_RECONSTRUCTED_CLOUD_PREVIEW
const char* kCloudVertexShader = R"(
cbuffer CloudCB : register(b5) {
    float4x4 cloud_view_projection;
    float4 viewer_position;
    float4 viewer_direction;
    float4 light_position;
    float4 light_colour;
    float4 layer_params;
    float4 uv_scale_offset;
    float4 cloud_globals;
}

struct VSIN {
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct VSOUT {
    float4 position : SV_Position;
    float3 world_position : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

VSOUT VS(VSIN input) {
    VSOUT output;

    const float3 preview_position = input.position.xzy;
    output.position = mul(float4(preview_position, 1.0),
                          cloud_view_projection);
    output.world_position = preview_position;
    output.uv = input.uv;
    return output;
}
)";

const char* kCloudPixelShader = R"(
Texture2D cloud_density : register(t8);
SamplerState cloud_sampler : register(s0);

cbuffer CloudCB : register(b5) {
    float4x4 cloud_view_projection;
    float4 viewer_position;
    float4 viewer_direction;
    float4 light_position;
    float4 light_colour;
    float4 layer_params;
    float4 uv_scale_offset;
    float4 cloud_globals;
}

struct PSIN {
    float4 position : SV_Position;
    float3 world_position : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

float density_channel(float4 value) {

    return value.a;
}

float4 PS(PSIN input) : SV_Target {
    const float2 uv = input.uv * uv_scale_offset.xy +
                      uv_scale_offset.zw;
    const float4 centre = cloud_density.Sample(cloud_sampler, uv);
    const float density = density_channel(centre);

    const float distance_xy =
        length(input.world_position.xz - viewer_position.xz);
    const float distance_fade =
        1.0 - saturate((distance_xy - 1000.0) * 0.001);
    const float alpha =
        density * layer_params.x * distance_fade;

    float3 rgb = 0.0;
    [branch]
    if (alpha > 0.001) {
        const float plus_x = density_channel(
            cloud_density.Sample(cloud_sampler, uv, int2(1, 0)));
        const float plus_y = density_channel(
            cloud_density.Sample(cloud_sampler, uv, int2(0, 1)));
        const float minus_x = density_channel(
            cloud_density.Sample(cloud_sampler, uv, int2(-1, 0)));
        const float minus_y = density_channel(
            cloud_density.Sample(cloud_sampler, uv, int2(0, -1)));

        const float gradient_x = plus_x - minus_x;
        const float gradient_y = plus_y - minus_y;
        const float3 normal = normalize(float3(
            gradient_x, layer_params.w, gradient_y));
        const float3 light_direction = normalize(
            input.world_position - light_position.xyz);

        const float grazing = 1.0 - saturate(
            dot(normal, -viewer_direction.xyz));
        const float back = saturate(dot(light_direction, -normal));
        const float front = saturate(dot(light_direction, normal));
        const float lighting =
            front * (1.0 + pow(grazing, 32.0)) +
            back * back * (1.0 - density);

        rgb = ((lighting * light_colour.rgb + layer_params.y.xxx) *
               layer_params.z.xxx * centre.rgb) * cloud_globals.x;
    }

    if (alpha <= cloud_globals.z) discard;
    return float4(rgb, alpha);
}
)";
#endif

bool CompileShader(const char* source,
                   const char* entry,
                   const char* profile,
                   ID3DBlob** blob)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ID3DBlob* errors = nullptr;
    const HRESULT result = D3DCompile(
        source, std::strlen(source), nullptr, nullptr, nullptr,
        entry, profile, flags, 0, blob, &errors);
    if (FAILED(result) && errors) {
        const char* message =
            static_cast<const char*>(errors->GetBufferPointer());
        OutputLog::error(std::string("shader compile failed ") +
            profile + "/" + entry + ": " +
            (message ? message : "unknown error"));
    }
    if (errors) errors->Release();
    return SUCCEEDED(result);
}

struct AuthoredRgbaMip {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

ID3D11ShaderResourceView* CreateSrvFromAuthoredMips(
    ID3D11Device* device,
    const std::vector<AuthoredRgbaMip>& mips)
{
    if (!device || mips.empty() || mips.size() > 15) return nullptr;
    for (std::size_t i = 0; i < mips.size(); ++i) {
        const AuthoredRgbaMip& mip = mips[i];
        if (mip.width <= 0 || mip.height <= 0 ||
            mip.width > 8192 || mip.height > 8192 ||
            mip.pixels.size() <
                std::size_t(mip.width) * std::size_t(mip.height) * 4) {
            return nullptr;
        }
        if (i != 0 &&
            (mip.width != std::max(1, mips[i - 1].width / 2) ||
             mip.height != std::max(1, mips[i - 1].height / 2))) {
            return nullptr;
        }
    }

    D3D11_TEXTURE2D_DESC description{};
    description.Width = static_cast<UINT>(mips[0].width);
    description.Height = static_cast<UINT>(mips[0].height);
    description.MipLevels = static_cast<UINT>(mips.size());
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    std::vector<D3D11_SUBRESOURCE_DATA> initial(mips.size());
    for (std::size_t i = 0; i < mips.size(); ++i) {
        initial[i].pSysMem = mips[i].pixels.data();
        initial[i].SysMemPitch = static_cast<UINT>(mips[i].width * 4);
        initial[i].SysMemSlicePitch = static_cast<UINT>(
            mips[i].width * mips[i].height * 4);
    }

    ID3D11Texture2D* texture = nullptr;
    if (FAILED(device->CreateTexture2D(
            &description, initial.data(), &texture)) || !texture) {
        return nullptr;
    }
    ID3D11ShaderResourceView* view = nullptr;
    if (FAILED(device->CreateShaderResourceView(texture, nullptr, &view))) {
        texture->Release();
        return nullptr;
    }
    texture->Release();
    return view;
}

bool CreateCloudSrv(ID3D11Device* device,
                    const std::vector<unsigned char>& blob,
                    ID3D11ShaderResourceView** out_view)
{
    *out_view = nullptr;
    TexInfo info{};
    if (!parse_tex_info(blob, info) || info.Mips.empty()) return false;

    std::vector<AuthoredRgbaMip> decoded;
    decoded.reserve(std::min<std::size_t>(info.Mips.size(), 15));
    for (std::size_t i = 0; i < info.Mips.size() && i < 64; ++i) {
        AuthoredRgbaMip mip;
        bool ignored_alpha = false;
        if (!decode_tex_to_rgba(blob, mip.pixels, mip.width, mip.height,
                                &ignored_alpha, static_cast<int>(i))) {
            continue;
        }
        const std::size_t expected =
            std::size_t(mip.width) * std::size_t(mip.height) * 4;
        if (mip.width <= 0 || mip.height <= 0 ||
            mip.pixels.size() < expected) {
            continue;
        }
        decoded.push_back(std::move(mip));
    }
    if (decoded.empty()) return false;

    auto area = [](const AuthoredRgbaMip& mip) {
        return std::uint64_t(mip.width) * std::uint64_t(mip.height);
    };
    const auto base = std::max_element(
        decoded.begin(), decoded.end(),
        [&](const AuthoredRgbaMip& lhs, const AuthoredRgbaMip& rhs) {
            return area(lhs) < area(rhs);
        });

    std::vector<AuthoredRgbaMip> chain;
    chain.push_back(std::move(*base));
    while (chain.size() < 15 &&
           (chain.back().width > 1 || chain.back().height > 1)) {
        const int wanted_width = std::max(1, chain.back().width / 2);
        const int wanted_height = std::max(1, chain.back().height / 2);
        auto next = std::find_if(
            decoded.begin(), decoded.end(),
            [&](const AuthoredRgbaMip& candidate) {
                return !candidate.pixels.empty() &&
                       candidate.width == wanted_width &&
                       candidate.height == wanted_height;
            });
        if (next == decoded.end()) break;
        chain.push_back(std::move(*next));
    }

    *out_view = CreateSrvFromAuthoredMips(device, chain);
    return *out_view != nullptr;
}

bool CreateOrdinarySrv(ID3D11Device* device,
                       const std::vector<unsigned char>& blob,
                       ID3D11ShaderResourceView** out_view,
                       int* out_width = nullptr,
                       int* out_height = nullptr)
{
    *out_view = nullptr;
    std::vector<std::uint8_t> rgba;
    int width = 0;
    int height = 0;
    bool has_alpha = false;
    if (!decode_tex_to_rgba(
            blob, rgba, width, height, &has_alpha)) {
        return false;
    }
    *out_view = create_srv_from_rgba(device, width, height, rgba);
    if (out_width) *out_width = width;
    if (out_height) *out_height = height;
    return *out_view != nullptr;
}

void ReleaseCloudTextures(ModelPreview& preview)
{
    for (int i = 0; i < 4; ++i) {
        if (preview.cloud_density_srv[i]) {
            preview.cloud_density_srv[i]->Release();
            preview.cloud_density_srv[i] = nullptr;
        }
        preview.cloud_density_tried[i] = false;
        preview.cloud_density_tex_name[i].clear();
    }
}

void ReleaseSkyTextures(ModelPreview& preview)
{
    if (preview.sky_overlay_srv) {
        preview.sky_overlay_srv->Release();
        preview.sky_overlay_srv = nullptr;
    }
    if (preview.sky_sun_disc_srv) {
        preview.sky_sun_disc_srv->Release();
        preview.sky_sun_disc_srv = nullptr;
    }
    if (preview.sky_moon_srv) {
        preview.sky_moon_srv->Release();
        preview.sky_moon_srv = nullptr;
    }
    if (preview.sky_moon_glare_srv) {
        preview.sky_moon_glare_srv->Release();
        preview.sky_moon_glare_srv = nullptr;
    }
    if (preview.sky_sun_beams_srv) {
        preview.sky_sun_beams_srv->Release();
        preview.sky_sun_beams_srv = nullptr;
    }
    if (preview.sky_sun_glare_srv) {
        preview.sky_sun_glare_srv->Release();
        preview.sky_sun_glare_srv = nullptr;
    }
    preview.sky_overlay_tried = false;
    preview.sky_sun_disc_tried = false;
    preview.sky_moon_tried = false;
    preview.sky_moon_glare_tried = false;
    preview.sky_sun_beams_tried = false;
    preview.sky_sun_glare_tried = false;
    preview.sky_overlay_tex_name.clear();
    preview.sky_sun_disc_tex_name.clear();
    preview.sky_moon_tex_name.clear();
    preview.sky_moon_glare_tex_name.clear();
    preview.sky_sun_beams_tex_name.clear();
    preview.sky_sun_glare_tex_name.clear();
}

std::string PreferredTextureBank()
{
    return (S.selected_nested_index != -1 &&
            !S.selected_nested_temp_path.empty())
        ? S.selected_nested_temp_path
        : S.selected_bnk;
}

}

CameraFrame BuildCameraFrame(const FlyCam& camera, int width, int height)
{
    CameraFrame frame{};
    const float cy = std::cos(camera.yaw);
    const float sy = std::sin(camera.yaw);
    const float cp = std::cos(camera.pitch);
    const float sp = std::sin(camera.pitch);
    frame.view_forward[0] = sy * cp;
    frame.view_forward[1] = sp;
    frame.view_forward[2] = cy * cp;

    const XMVECTOR forward = XMVector3Normalize(XMVectorSet(
        frame.view_forward[0], frame.view_forward[1],
        frame.view_forward[2], 0.0f));
    const XMVECTOR world_up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const XMVECTOR right = XMVector3Normalize(
        XMVector3Cross(world_up, forward));
    const XMVECTOR up = XMVector3Normalize(
        XMVector3Cross(forward, right));
    XMFLOAT3 right_f{};
    XMFLOAT3 up_f{};
    XMFLOAT3 forward_f{};
    XMStoreFloat3(&right_f, right);
    XMStoreFloat3(&up_f, up);
    XMStoreFloat3(&forward_f, forward);
    frame.right[0] = right_f.x;
    frame.right[1] = right_f.y;
    frame.right[2] = right_f.z;
    frame.up[0] = up_f.x;
    frame.up[1] = up_f.y;
    frame.up[2] = up_f.z;
    frame.forward[0] = forward_f.x;
    frame.forward[1] = forward_f.y;
    frame.forward[2] = forward_f.z;
    frame.fov_radians = XMConvertToRadians(60.0f);
    frame.aspect = static_cast<float>(width) / static_cast<float>(height);
    frame.tan_half_y = std::tan(frame.fov_radians * 0.5f);
    frame.tan_half_x = frame.tan_half_y * frame.aspect;
    return frame;
}

FrameState EvaluateFrame(ModelPreview& preview)
{
    FrameState frame{};
    static const auto sky_start_time =
        std::chrono::steady_clock::now();
    frame.elapsed_time = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - sky_start_time).count();

    std::copy(std::begin(preview.sky_top_colour),
              std::end(preview.sky_top_colour),
              std::begin(frame.sky_top));
    std::copy(std::begin(preview.sky_bottom_colour),
              std::end(preview.sky_bottom_colour),
              std::begin(frame.sky_bottom));
    std::copy(std::begin(preview.sky_sunset_colour),
              std::end(preview.sky_sunset_colour),
              std::begin(frame.sky_sunset));
    std::copy(std::begin(preview.sky_params),
              std::end(preview.sky_params),
              std::begin(frame.sky_params));
    std::copy(std::begin(preview.main_light_colour),
              std::end(preview.main_light_colour),
              std::begin(frame.main_light_colour));
    std::copy(std::begin(preview.sky_element_params),
              std::end(preview.sky_element_params),
              std::begin(frame.element_params));
    for (int layer = 0; layer < 4; ++layer) {
        for (int field = 0; field < 4; ++field) {
            frame.cloud_layer[layer][field] =
                preview.cloud_layer[layer][field];
            frame.cloud_shape[layer][field] =
                preview.cloud_shape[layer][field];
            frame.cloud_motion[layer][field] =
                preview.cloud_motion[layer][field];
            frame.cloud_light[layer][field] =
                preview.cloud_light[layer][field];
        }
        frame.cloud_density_token[layer] =
            preview.cloud_density_token[layer];
    }
    frame.has_cloud_theme = preview.has_cloud_theme;
    frame.mist = preview.weather_mist_strength;
    frame.has_fog = preview.has_fog_theme;
    std::copy(std::begin(preview.weather_precip),
              std::end(preview.weather_precip),
              std::begin(frame.weather));
    std::copy(std::begin(preview.fog_range),
              std::end(preview.fog_range),
              std::begin(frame.fog_range));
    std::copy(std::begin(preview.fog_density),
              std::end(preview.fog_density),
              std::begin(frame.fog_density));

    frame.time_of_day = std::isfinite(preview.sky_time_of_day)
        ? preview.sky_time_of_day : 0.5f;
    frame.time_of_day -= std::floor(frame.time_of_day);
    const bool has_time_override =
        preview.time_of_day_override &&
        std::isfinite(preview.time_of_day_override_value);
    if (has_time_override) {
        frame.time_of_day = preview.time_of_day_override_value;
        frame.time_of_day -= std::floor(frame.time_of_day);
        if (frame.time_of_day < 0.0f) frame.time_of_day += 1.0f;
    }

    if (preview.has_day_night_cycle &&
        preview.day_night_keyframes.size() >= 2) {
        const float cycle_seconds =
            std::max(preview.day_night_cycle_seconds, 1.0f);
        if (!has_time_override) {
            frame.time_of_day = static_cast<float>(
                frame.elapsed_time / static_cast<double>(cycle_seconds));
            frame.time_of_day -= std::floor(frame.time_of_day);
        }

        const auto& keys = preview.day_night_keyframes;
        std::size_t a = keys.size() - 1;
        std::size_t b = 0;
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (frame.time_of_day < keys[i].time_of_day) {
                b = i;
                a = (i == 0) ? keys.size() - 1 : i - 1;
                break;
            }
        }

        float span = keys[b].time_of_day - keys[a].time_of_day;
        if (span <= 0.0f) span += 1.0f;
        float position = frame.time_of_day - keys[a].time_of_day;
        if (position < 0.0f) position += 1.0f;
        const float key_t = span > 0.0001f
            ? std::clamp(position / span, 0.0f, 1.0f)
            : 0.0f;
        auto lerp = [key_t](float value_a, float value_b) {
            return value_a + (value_b - value_a) * key_t;
        };

        for (int i = 0; i < 3; ++i) {
            frame.sky_top[i] = lerp(
                keys[a].sky_top_colour[i], keys[b].sky_top_colour[i]);
            frame.sky_bottom[i] = lerp(
                keys[a].sky_bottom_colour[i], keys[b].sky_bottom_colour[i]);
            frame.sky_sunset[i] = lerp(
                keys[a].sky_sunset_colour[i], keys[b].sky_sunset_colour[i]);
            frame.main_light_colour[i] = lerp(
                keys[a].main_light_colour[i],
                keys[b].main_light_colour[i]);
        }
        for (int i = 0; i < 4; ++i) {
            frame.sky_params[i] = lerp(
                keys[a].sky_params[i], keys[b].sky_params[i]);
            frame.weather[i] = lerp(
                keys[a].weather_precip[i], keys[b].weather_precip[i]);
            frame.fog_range[i] = lerp(
                keys[a].fog_range[i], keys[b].fog_range[i]);
        }
        for (int i = 0; i < 16; ++i) {
            frame.element_params[i] = lerp(
                keys[a].element_params[i], keys[b].element_params[i]);
        }
        frame.has_cloud_theme =
            keys[a].has_cloud_theme || keys[b].has_cloud_theme;
        frame.mist = lerp(keys[a].weather_mist_strength,
                          keys[b].weather_mist_strength);
        frame.fog_density[0] = lerp(
            keys[a].fog_density[0], keys[b].fog_density[0]);
        frame.fog_density[1] = lerp(
            keys[a].fog_density[1], keys[b].fog_density[1]);
        frame.has_fog =
            keys[a].has_fog_theme || keys[b].has_fog_theme;

        for (int layer = 0; layer < 4; ++layer) {
            for (int field = 0; field < 4; ++field) {
                frame.cloud_layer[layer][field] = lerp(
                    keys[a].cloud_layer[layer][field],
                    keys[b].cloud_layer[layer][field]);
                frame.cloud_shape[layer][field] = lerp(
                    keys[a].cloud_shape[layer][field],
                    keys[b].cloud_shape[layer][field]);
                frame.cloud_motion[layer][field] = lerp(
                    keys[a].cloud_motion[layer][field],
                    keys[b].cloud_motion[layer][field]);
                frame.cloud_light[layer][field] = lerp(
                    keys[a].cloud_light[layer][field],
                    keys[b].cloud_light[layer][field]);
            }

            const std::uint32_t token_a =
                keys[a].cloud_density_token[layer];
            const std::uint32_t token_b =
                keys[b].cloud_density_token[layer];
            const std::uint32_t selected_token =
                CloudRuntime::SelectDensityToken({
                    {1.0f - key_t, token_a}, {key_t, token_b}});
            const bool select_b = selected_token == token_b &&
                                  selected_token != token_a;
            const MPSkyCloudKeyframe& resource_key =
                select_b ? keys[b] : keys[a];
            const std::string& selected_name =
                resource_key.cloud_density_tex_name[layer];
            frame.cloud_density_token[layer] = selected_token;
            if (preview.cloud_density_tex_name[layer] != selected_name) {
                if (preview.cloud_density_srv[layer]) {
                    preview.cloud_density_srv[layer]->Release();
                    preview.cloud_density_srv[layer] = nullptr;
                }
                preview.cloud_density_tex_name[layer] = selected_name;
                preview.cloud_density_tried[layer] = false;
            }
            preview.cloud_density_token[layer] = selected_token;
        }
    }
    preview.current_time_of_day = frame.time_of_day;

    const float degrees_to_radians = XM_PI / 180.0f;
    const float time_factor =
        (preview.has_sun_axis && std::isfinite(preview.sun_axis[3]) &&
         preview.sun_axis[3] > 0.0f)
        ? preview.sun_axis[3] : 1.0f;
    const float theta =
        (frame.time_of_day * time_factor - 0.5f) * XM_2PI;
    const float elevation =
        (preview.has_sun_axis ? preview.sun_axis[0] : 26.0f) *
        degrees_to_radians;
    const float phi =
        (preview.has_sun_axis
            ? preview.sun_axis[1] + preview.sun_axis[2]
            : 0.0f) * degrees_to_radians;
    const float cos_theta = std::cos(theta);
    const float sin_theta = std::sin(theta);
    const float ground_x = cos_theta * std::cos(elevation);
    const float up = cos_theta * std::sin(elevation);
    const float ground_z = sin_theta;
    const float cos_phi = std::cos(phi);
    const float sin_phi = std::sin(phi);
    const XMVECTOR sun = XMVector3Normalize(XMVectorSet(
        ground_x * cos_phi - ground_z * sin_phi,
        up,
        ground_x * sin_phi + ground_z * cos_phi,
        0.0f));
    XMFLOAT3 sun_f{};
    XMStoreFloat3(&sun_f, sun);
    frame.sun_direction[0] = sun_f.x;
    frame.sun_direction[1] = sun_f.y;
    frame.sun_direction[2] = sun_f.z;

    if (preview.has_moon_axis) {
        const float moon_elevation =
            preview.moon_axis[0] * degrees_to_radians;
        const float moon_phi =
            (preview.moon_axis[1] + preview.moon_axis[2]) *
            degrees_to_radians;
        const float moon_ground_x = cos_theta * std::cos(moon_elevation);
        const float moon_up = cos_theta * std::sin(moon_elevation);
        const float cos_moon_phi = std::cos(moon_phi);
        const float sin_moon_phi = std::sin(moon_phi);
        const XMVECTOR moon = XMVector3Normalize(XMVectorSet(
            -(moon_ground_x * cos_moon_phi - ground_z * sin_moon_phi),
            -moon_up,
            -(moon_ground_x * sin_moon_phi + ground_z * cos_moon_phi),
            0.0f));
        XMFLOAT3 moon_f{};
        XMStoreFloat3(&moon_f, moon);
        frame.moon_direction[0] = moon_f.x;
        frame.moon_direction[1] = moon_f.y;
        frame.moon_direction[2] = moon_f.z;
    } else {
        frame.moon_direction[0] = -sun_f.x;
        frame.moon_direction[1] = -sun_f.y;
        frame.moon_direction[2] = -sun_f.z;
    }
    return frame;
}

void GetClearColour(const ModelPreview& preview,
                    const FrameState& frame,
                    float out_rgba[4])
{
    out_rgba[0] = 0.22f;
    out_rgba[1] = 0.22f;
    out_rgba[2] = 0.22f;
    out_rgba[3] = 1.0f;
#if FABLE_ENABLE_RECONSTRUCTED_SKY_PREVIEW
    if (preview.has_sky_theme && preview.show_sky) {
        out_rgba[0] = std::clamp(frame.sky_bottom[0], 0.0f, 1.0f);
        out_rgba[1] = std::clamp(frame.sky_bottom[1], 0.0f, 1.0f);
        out_rgba[2] = std::clamp(frame.sky_bottom[2], 0.0f, 1.0f);
    }
#else
    (void)preview;
    (void)frame;
#endif
}

void CreateD3D11Resources(ID3D11Device* device, ModelPreview& preview)
{
    {
        ID3DBlob* vertex_blob = nullptr;
        ID3DBlob* pixel_blob = nullptr;
#if FABLE_ENABLE_RECONSTRUCTED_SKY_PREVIEW
        if (CompileShader(kSkyVertexShader, "VS", "vs_5_0", &vertex_blob) &&
            CompileShader(kSkyPixelShader, "PS", "ps_5_0", &pixel_blob)) {
            device->CreateVertexShader(
                vertex_blob->GetBufferPointer(),
                vertex_blob->GetBufferSize(), nullptr, &preview.vs_sky);
            device->CreatePixelShader(
                pixel_blob->GetBufferPointer(),
                pixel_blob->GetBufferSize(), nullptr, &preview.ps_sky);
        }
#else
        OutputLog::info(
            "sky preview disabled: exact Xenos execution required");
#endif
        if (vertex_blob) vertex_blob->Release();
        if (pixel_blob) pixel_blob->Release();

        D3D11_BUFFER_DESC constant_buffer{};
        constant_buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constant_buffer.ByteWidth = 27 * 16;
        constant_buffer.Usage = D3D11_USAGE_DYNAMIC;
        constant_buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(
            &constant_buffer, nullptr, &preview.cbuffer_sky);
    }

    {

        ID3DBlob* vertex_blob = nullptr;
        ID3DBlob* pixel_blob = nullptr;
        if (CompileShader(SkyDomeXex::kDomeFullscreenVertexShaderHlsl,
                          "VSMainFullscreen", "vs_5_0", &vertex_blob) &&
            CompileShader(SkyDomeXex::kDomePixelShaderHlsl,
                          "PSMain", "ps_5_0", &pixel_blob)) {
            device->CreateVertexShader(
                vertex_blob->GetBufferPointer(),
                vertex_blob->GetBufferSize(), nullptr,
                &preview.vs_sky_dome);
            device->CreatePixelShader(
                pixel_blob->GetBufferPointer(),
                pixel_blob->GetBufferSize(), nullptr,
                &preview.ps_sky_dome);
            OutputLog::info("sky dome: retail xenos translation active");
        }
        SkyDomeDebug(std::string("create: vs_dome=") +
                     (preview.vs_sky_dome ? "ok" : "null") +
                     " ps_dome=" + (preview.ps_sky_dome ? "ok" : "null"));
        if (vertex_blob) vertex_blob->Release();
        if (pixel_blob) pixel_blob->Release();

        D3D11_BUFFER_DESC constant_buffer{};
        constant_buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constant_buffer.ByteWidth =
            static_cast<UINT>(sizeof(SkyDomeXex::DomeConstantBuffer));
        constant_buffer.Usage = D3D11_USAGE_DYNAMIC;
        constant_buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(
            &constant_buffer, nullptr, &preview.cbuffer_sky_dome);

        D3D11_TEXTURE2D_DESC lut_desc{};
        lut_desc.Width = SkyDomeXex::kLutWidth;
        lut_desc.Height = 1;
        lut_desc.MipLevels = 1;
        lut_desc.ArraySize = 1;
        lut_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        lut_desc.SampleDesc.Count = 1;
        lut_desc.Usage = D3D11_USAGE_DYNAMIC;
        lut_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        lut_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (SUCCEEDED(device->CreateTexture2D(
                &lut_desc, nullptr, &preview.sky_lut_tex)) &&
            preview.sky_lut_tex) {
            device->CreateShaderResourceView(
                preview.sky_lut_tex, nullptr, &preview.sky_lut_srv);
        }

        D3D11_SAMPLER_DESC clamp_sampler{};
        clamp_sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        clamp_sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        clamp_sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        clamp_sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        clamp_sampler.MaxLOD = D3D11_FLOAT32_MAX;
        device->CreateSamplerState(&clamp_sampler,
                                   &preview.sampler_sky_clamp);
    }

    {

        ID3DBlob* vertex_blob = nullptr;
        ID3DBlob* pixel_blob = nullptr;
        if (CompileShader(kSkyElementVertexShader, "VS", "vs_5_0",
                          &vertex_blob) &&
            CompileShader(kSkyElementPixelShader, "PS", "ps_5_0",
                          &pixel_blob)) {
            device->CreateVertexShader(
                vertex_blob->GetBufferPointer(),
                vertex_blob->GetBufferSize(), nullptr,
                &preview.vs_sky_element);
            device->CreatePixelShader(
                pixel_blob->GetBufferPointer(),
                pixel_blob->GetBufferSize(), nullptr,
                &preview.ps_sky_element);
            D3D11_INPUT_ELEMENT_DESC element_layout[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
                 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
                 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
            device->CreateInputLayout(
                element_layout, 2,
                vertex_blob->GetBufferPointer(),
                vertex_blob->GetBufferSize(),
                &preview.layout_sky_element);
        }
        if (vertex_blob) vertex_blob->Release();
        if (pixel_blob) pixel_blob->Release();

        D3D11_BUFFER_DESC constant_buffer{};
        constant_buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constant_buffer.ByteWidth = 16;
        constant_buffer.Usage = D3D11_USAGE_DYNAMIC;
        constant_buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(
            &constant_buffer, nullptr, &preview.cbuffer_sky_element);

        D3D11_BUFFER_DESC vertex_buffer{};
        vertex_buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertex_buffer.ByteWidth = 6 * 24;
        vertex_buffer.Usage = D3D11_USAGE_DYNAMIC;
        vertex_buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(
            &vertex_buffer, nullptr, &preview.sky_element_vb);

        ID3DBlob* stars_vs_blob = nullptr;
        ID3DBlob* stars_ps_blob = nullptr;
        if (CompileShader(SkyDomeXex::kStarsVertexShaderHlsl, "VSMain",
                          "vs_5_0", &stars_vs_blob) &&
            CompileShader(SkyDomeXex::kStarsPixelShaderHlsl, "PSMain",
                          "ps_5_0", &stars_ps_blob)) {
            device->CreateVertexShader(
                stars_vs_blob->GetBufferPointer(),
                stars_vs_blob->GetBufferSize(), nullptr,
                &preview.vs_sky_stars);
            device->CreatePixelShader(
                stars_ps_blob->GetBufferPointer(),
                stars_ps_blob->GetBufferSize(), nullptr,
                &preview.ps_sky_stars);
        }
        if (stars_vs_blob) stars_vs_blob->Release();
        if (stars_ps_blob) stars_ps_blob->Release();

        SkyDomeDebug(std::string("create: vs_element=") +
                     (preview.vs_sky_element ? "ok" : "null") +
                     " ps_element=" +
                     (preview.ps_sky_element ? "ok" : "null") +
                     " vs_stars=" +
                     (preview.vs_sky_stars ? "ok" : "null"));
    }

    {
        ID3DBlob* vertex_blob = nullptr;
        ID3DBlob* pixel_blob = nullptr;
#if FABLE_ENABLE_RECONSTRUCTED_CLOUD_PREVIEW
        if (CompileShader(
                kCloudVertexShader, "VS", "vs_5_0", &vertex_blob) &&
            CompileShader(
                kCloudPixelShader, "PS", "ps_5_0", &pixel_blob)) {
            device->CreateVertexShader(
                vertex_blob->GetBufferPointer(),
                vertex_blob->GetBufferSize(), nullptr, &preview.vs_cloud);
            device->CreatePixelShader(
                pixel_blob->GetBufferPointer(),
                pixel_blob->GetBufferSize(), nullptr, &preview.ps_cloud);
            D3D11_INPUT_ELEMENT_DESC cloud_layout[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
                 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
                 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
            device->CreateInputLayout(
                cloud_layout, 2,
                vertex_blob->GetBufferPointer(),
                vertex_blob->GetBufferSize(),
                &preview.layout_cloud);
        }
#else
        OutputLog::info(
            "cloud preview disabled: exact Xenos execution required");
#endif
        if (vertex_blob) vertex_blob->Release();
        if (pixel_blob) pixel_blob->Release();

        D3D11_BUFFER_DESC constant_buffer{};
        constant_buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constant_buffer.ByteWidth = 11 * 16;
        constant_buffer.Usage = D3D11_USAGE_DYNAMIC;
        constant_buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(
            &constant_buffer, nullptr, &preview.cbuffer_cloud);

        D3D11_BUFFER_DESC vertex_buffer{};
        vertex_buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertex_buffer.ByteWidth =
            4 * static_cast<UINT>(sizeof(CloudRuntime::Vertex));
        vertex_buffer.Usage = D3D11_USAGE_DYNAMIC;
        vertex_buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(
            &vertex_buffer, nullptr, &preview.cloud_vb);

        const auto& indices = CloudRuntime::Indices();
        D3D11_SUBRESOURCE_DATA index_data{};
        index_data.pSysMem = indices.data();
        D3D11_BUFFER_DESC index_buffer{};
        index_buffer.BindFlags = D3D11_BIND_INDEX_BUFFER;
        index_buffer.ByteWidth = static_cast<UINT>(
            indices.size() * sizeof(indices[0]));
        index_buffer.Usage = D3D11_USAGE_IMMUTABLE;
        device->CreateBuffer(
            &index_buffer, &index_data, &preview.cloud_ib);

        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        device->CreateSamplerState(&sampler, &preview.sampler_cloud);
    }
}

void ReleaseD3D11Resources(ModelPreview& preview)
{
    ReleaseCloudTextures(preview);
    ReleaseSkyTextures(preview);
    if (preview.vs_sky_element) {
        preview.vs_sky_element->Release();
        preview.vs_sky_element = nullptr;
    }
    if (preview.ps_sky_element) {
        preview.ps_sky_element->Release();
        preview.ps_sky_element = nullptr;
    }
    if (preview.vs_sky_stars) {
        preview.vs_sky_stars->Release();
        preview.vs_sky_stars = nullptr;
    }
    if (preview.ps_sky_stars) {
        preview.ps_sky_stars->Release();
        preview.ps_sky_stars = nullptr;
    }
    if (preview.layout_sky_element) {
        preview.layout_sky_element->Release();
        preview.layout_sky_element = nullptr;
    }
    if (preview.cbuffer_sky_element) {
        preview.cbuffer_sky_element->Release();
        preview.cbuffer_sky_element = nullptr;
    }
    if (preview.sky_element_vb) {
        preview.sky_element_vb->Release();
        preview.sky_element_vb = nullptr;
    }
    if (preview.vs_sky_dome) {
        preview.vs_sky_dome->Release();
        preview.vs_sky_dome = nullptr;
    }
    if (preview.ps_sky_dome) {
        preview.ps_sky_dome->Release();
        preview.ps_sky_dome = nullptr;
    }
    if (preview.cbuffer_sky_dome) {
        preview.cbuffer_sky_dome->Release();
        preview.cbuffer_sky_dome = nullptr;
    }
    if (preview.sky_lut_srv) {
        preview.sky_lut_srv->Release();
        preview.sky_lut_srv = nullptr;
    }
    if (preview.sky_lut_tex) {
        preview.sky_lut_tex->Release();
        preview.sky_lut_tex = nullptr;
    }
    if (preview.sampler_sky_clamp) {
        preview.sampler_sky_clamp->Release();
        preview.sampler_sky_clamp = nullptr;
    }
    if (preview.vs_sky) {
        preview.vs_sky->Release();
        preview.vs_sky = nullptr;
    }
    if (preview.ps_sky) {
        preview.ps_sky->Release();
        preview.ps_sky = nullptr;
    }
    if (preview.cbuffer_sky) {
        preview.cbuffer_sky->Release();
        preview.cbuffer_sky = nullptr;
    }
    if (preview.vs_cloud) {
        preview.vs_cloud->Release();
        preview.vs_cloud = nullptr;
    }
    if (preview.ps_cloud) {
        preview.ps_cloud->Release();
        preview.ps_cloud = nullptr;
    }
    if (preview.layout_cloud) {
        preview.layout_cloud->Release();
        preview.layout_cloud = nullptr;
    }
    if (preview.cbuffer_cloud) {
        preview.cbuffer_cloud->Release();
        preview.cbuffer_cloud = nullptr;
    }
    if (preview.cloud_vb) {
        preview.cloud_vb->Release();
        preview.cloud_vb = nullptr;
    }
    if (preview.cloud_ib) {
        preview.cloud_ib->Release();
        preview.cloud_ib = nullptr;
    }
    if (preview.sampler_cloud) {
        preview.sampler_cloud->Release();
        preview.sampler_cloud = nullptr;
    }
}

void ResetForBuild(ModelPreview& preview)
{
    preview.has_sky_theme = false;
    preview.main_light_colour[0] = 1.0f;
    preview.main_light_colour[1] = 1.0f;
    preview.main_light_colour[2] = 1.0f;
    preview.has_cloud_theme = false;
    preview.cloud_layer_count = 0;
    preview.cloud_runtime_initialised = false;
    for (int i = 0; i < 4; ++i) {
        preview.cloud_layer[i][0] = 0.0f;
        preview.cloud_layer[i][1] = 0.0f;
        preview.cloud_layer[i][2] = 0.001f;
        preview.cloud_layer[i][3] = 0.001f;
        std::fill(std::begin(preview.cloud_shape[i]),
                  std::end(preview.cloud_shape[i]), 0.0f);
        std::fill(std::begin(preview.cloud_motion[i]),
                  std::end(preview.cloud_motion[i]), 0.0f);
        std::fill(std::begin(preview.cloud_light[i]),
                  std::end(preview.cloud_light[i]), 0.0f);
        preview.cloud_density_token[i] = 0;
        CloudRuntime::InitialiseLayer(preview.cloud_runtime[i]);
    }
    preview.sky_time_of_day = -1.0f;
    preview.has_day_night_cycle = false;
    preview.day_night_keyframes.clear();
    preview.time_of_day_override = false;
    preview.time_of_day_override_value = 0.5f;
    preview.current_time_of_day = 0.5f;
    ReleaseCloudTextures(preview);
    ReleaseSkyTextures(preview);
}

void DrawSky(ID3D11Device* device,
             ID3D11DeviceContext* context,
             ModelPreview& preview,
             const CameraFrame& camera,
             const FrameState& frame)
{
    auto load_cloud_density = [&](int index) {
        if (index < 0 || index >= 4) return;
        if (preview.cloud_density_srv[index] ||
            preview.cloud_density_tried[index]) {
            return;
        }
        preview.cloud_density_tried[index] = true;
        const std::string& texture_name =
            preview.cloud_density_tex_name[index];
        if (texture_name.empty()) {
            SkyDomeDebug("texload skip(empty name) cloud_density " +
                         std::to_string(index));
            return;
        }

        std::vector<unsigned char> texture_data;
        if (!build_any_tex_buffer_for_name(
                texture_name, texture_data, PreferredTextureBank())) {
            OutputLog::warn(
                "cloud density texture not found: " + texture_name);
            SkyDomeDebug("texload FAIL(not found) cloud_density name=" +
                         texture_name + " bank=" + PreferredTextureBank());
            return;
        }
        ID3D11ShaderResourceView* view = nullptr;
        CreateCloudSrv(device, texture_data, &view);
        if (!view) {
            OutputLog::error(
                "cloud density texture failed to decode: " + texture_name);
            return;
        }
        preview.cloud_density_srv[index] = view;
        OutputLog::info(
            "cloud density texture bound: " + texture_name);
    };

    if (frame.has_cloud_theme && preview.show_sky &&
        preview.vs_cloud && preview.ps_cloud) {
        for (int i = 0; i < 4; ++i) load_cloud_density(i);
    }

    const bool exact_dome_ready =
        preview.vs_sky_dome && preview.ps_sky_dome &&
        preview.cbuffer_sky_dome && preview.sky_lut_tex &&
        preview.sky_lut_srv;
    static int debug_frame = 0;
    const bool debug_now = (debug_frame < 5) || (debug_frame % 300 == 0);
    ++debug_frame;
    if (debug_now) {
        std::ostringstream log;
        log << "frame " << debug_frame
            << ": has_sky_theme=" << preview.has_sky_theme
            << " show_sky=" << preview.show_sky
            << " exact_ready=" << exact_dome_ready
            << " old_ready="
            << (preview.vs_sky && preview.ps_sky && preview.cbuffer_sky);
        SkyDomeDebug(log.str());
    }
    if (!preview.has_sky_theme || !preview.show_sky) {
        return;
    }
    if (!exact_dome_ready &&
        (!preview.vs_sky || !preview.ps_sky || !preview.cbuffer_sky)) {
        return;
    }

    auto load_sky_texture =
        [&](const std::string& texture_name,
            bool& tried,
            ID3D11ShaderResourceView*& target,
            const char* label,
            int* out_width = nullptr,
            int* out_height = nullptr) {
            if (target || tried) return;
            tried = true;
            if (texture_name.empty()) {
                SkyDomeDebug(std::string("texload skip(empty name) ") +
                             label);
                return;
            }

            std::vector<unsigned char> texture_data;
            if (!build_any_tex_buffer_for_name(
                    texture_name, texture_data, PreferredTextureBank())) {
                OutputLog::warn(
                    std::string(label) + " texture not found: " +
                    texture_name);
                SkyDomeDebug(std::string("texload FAIL(not found) ") +
                             label + " name=" + texture_name + " bank=" +
                             PreferredTextureBank());
                return;
            }
            ID3D11ShaderResourceView* view = nullptr;
            CreateOrdinarySrv(device, texture_data, &view,
                              out_width, out_height);
            if (!view) {
                OutputLog::error(
                    std::string(label) + " texture failed to decode: " +
                    texture_name);
                SkyDomeDebug(std::string("texload FAIL(decode) ") + label +
                             " name=" + texture_name);
                return;
            }
            target = view;
            OutputLog::info(
                std::string(label) + " texture bound: " + texture_name);
        };

    {
        int moon_width = 0;
        int moon_height = 0;
        const bool was_loaded = preview.sky_moon_srv != nullptr;
        load_sky_texture(preview.sky_moon_tex_name,
                         preview.sky_moon_tried,
                         preview.sky_moon_srv,
                         "sky moon", &moon_width, &moon_height);
        if (!was_loaded && preview.sky_moon_srv &&
            moon_width > 0 && moon_height > 0) {
            const float texture_aspect =
                static_cast<float>(moon_width) /
                static_cast<float>(moon_height);
            float tiles_x = 1.0f;
            float tiles_y = 1.0f;
            if (texture_aspect >= 7.0f) {
                tiles_x = 8.0f;
                tiles_y = 1.0f;
            } else if (texture_aspect >= 3.5f) {
                tiles_x = 4.0f;
                tiles_y = 1.0f;
            } else if (texture_aspect >= 1.8f) {
                tiles_x = 4.0f;
                tiles_y = 2.0f;
            } else if (texture_aspect <= 0.15f) {
                tiles_x = 1.0f;
                tiles_y = 8.0f;
            } else if (texture_aspect <= 0.6f) {
                tiles_x = 2.0f;
                tiles_y = 4.0f;
            }
            preview.sky_moon_tiles[0] = tiles_x;
            preview.sky_moon_tiles[1] = tiles_y;
        }
    }
    load_sky_texture(preview.sky_moon_glare_tex_name,
                     preview.sky_moon_glare_tried,
                     preview.sky_moon_glare_srv,
                     "sky moon glare");
    load_sky_texture(preview.sky_sun_disc_tex_name,
                     preview.sky_sun_disc_tried,
                     preview.sky_sun_disc_srv,
                     "sky sun disc");
    load_sky_texture(preview.sky_overlay_tex_name,
                     preview.sky_overlay_tried,
                     preview.sky_overlay_srv,
                     "sky overlay");
    load_sky_texture(preview.sky_sun_beams_tex_name,
                     preview.sky_sun_beams_tried,
                     preview.sky_sun_beams_srv,
                     "sky sun beams");
    load_sky_texture(preview.sky_sun_glare_tex_name,
                     preview.sky_sun_glare_tried,
                     preview.sky_sun_glare_srv,
                     "sky sun glare");

    if (exact_dome_ready) {

        SkyDomeXex::ThemeInputs theme{};
        theme.sun_intensity = frame.sky_params[0];
        theme.complementary_bias = frame.sky_params[1];
        theme.beta_rayleigh_multiplier = frame.sky_params[2];
        theme.beta_mie_multiplier = frame.sky_params[3];
        for (int i = 0; i < 3; ++i) {
            theme.sky_colour[i] = frame.sky_top[i];
            theme.complementary_colour[i] = frame.sky_bottom[i];
            theme.sunset_colour[i] = frame.sky_sunset[i];
        }
        theme.fogging_start = frame.fog_range[0];
        theme.close_fog_max_distance = frame.fog_range[3];

        theme.sun_direction[0] = frame.sun_direction[0];
        theme.sun_direction[1] = frame.sun_direction[2];
        theme.sun_direction[2] = frame.sun_direction[1];
        theme.sun_direction[3] = 0.0f;

        const SkyDomeXex::AtmosphereState state =
            SkyDomeXex::ComputeAtmosphereState(theme);

        std::array<std::uint16_t, SkyDomeXex::kLutWidth * 4> lut{};
        SkyDomeXex::BuildInScatterLut(state, lut);
        D3D11_MAPPED_SUBRESOURCE lut_mapped{};
        if (SUCCEEDED(context->Map(preview.sky_lut_tex, 0,
                                   D3D11_MAP_WRITE_DISCARD, 0,
                                   &lut_mapped))) {
            std::memcpy(lut_mapped.pData, lut.data(),
                        lut.size() * sizeof(lut[0]));
            context->Unmap(preview.sky_lut_tex, 0);
        }

        SkyDomeXex::DomeConstantBuffer constants{};
        std::memcpy(constants.beta_rayleigh, state.beta_rayleigh,
                    sizeof(constants.beta_rayleigh));
        std::memcpy(constants.beta_mie, state.beta_mie,
                    sizeof(constants.beta_mie));
        std::memcpy(constants.sun_direction, state.sun_direction,
                    sizeof(constants.sun_direction));
        std::memcpy(constants.scattering_misc, state.scattering_misc,
                    sizeof(constants.scattering_misc));

        constants.overlay_blend[0] = 0.0f;

        constants.dome_misc[2] = 10.0f;
        auto to_engine = [](const float v[3], float w, float out[4]) {
            out[0] = v[0];
            out[1] = v[2];
            out[2] = v[1];
            out[3] = w;
        };
        to_engine(camera.right, camera.tan_half_x, constants.camera_right);
        to_engine(camera.up, camera.tan_half_y, constants.camera_up);
        to_engine(camera.forward, 0.0f, constants.camera_forward);

        D3D11_MAPPED_SUBRESOURCE cb_mapped{};
        if (SUCCEEDED(context->Map(preview.cbuffer_sky_dome, 0,
                                   D3D11_MAP_WRITE_DISCARD, 0,
                                   &cb_mapped))) {
            std::memcpy(cb_mapped.pData, &constants, sizeof(constants));
            context->Unmap(preview.cbuffer_sky_dome, 0);
        }

        if (debug_now) {
            std::ostringstream log;
            log << "  theme: sun_int=" << theme.sun_intensity
                << " bias=" << theme.complementary_bias
                << " rayM=" << theme.beta_rayleigh_multiplier
                << " mieM=" << theme.beta_mie_multiplier
                << " fogstart=" << theme.fogging_start
                << "\n  sky=(" << theme.sky_colour[0] << ','
                << theme.sky_colour[1] << ',' << theme.sky_colour[2]
                << ") comp=(" << theme.complementary_colour[0] << ','
                << theme.complementary_colour[1] << ','
                << theme.complementary_colour[2]
                << ") sun_engine=(" << theme.sun_direction[0] << ','
                << theme.sun_direction[1] << ',' << theme.sun_direction[2]
                << ")\n  betaR=(" << state.beta_rayleigh[0] << ','
                << state.beta_rayleigh[1] << ',' << state.beta_rayleigh[2]
                << ") betaM=(" << state.beta_mie[0] << ','
                << state.beta_mie[1] << ',' << state.beta_mie[2]
                << ") hg=(" << state.lut_hg[0] << ',' << state.lut_hg[1]
                << ',' << state.lut_hg[2] << ") cosElev="
                << state.lut_cos_sun_elevation;
            std::array<float, SkyDomeXex::kLutWidth * 4> lut_debug{};
            SkyDomeXex::BuildInScatterLutFloat(state, lut_debug);
            log << "\n  lut[0]=(" << lut_debug[0] << ',' << lut_debug[1]
                << ',' << lut_debug[2] << ") lut[63]=("
                << lut_debug[63 * 4 + 0] << ',' << lut_debug[63 * 4 + 1]
                << ',' << lut_debug[63 * 4 + 2] << ')'
                << "\n  cam fwd_engine=(" << constants.camera_forward[0]
                << ',' << constants.camera_forward[1] << ','
                << constants.camera_forward[2] << ") tan=("
                << constants.camera_right[3] << ','
                << constants.camera_up[3] << ") exposure="
                << constants.dome_misc[2]
                << " overlay_srv=" << (preview.sky_overlay_srv != nullptr)
                << " -> draw";
            SkyDomeDebug(log.str());
        }

        float blend_factor[4] = {0, 0, 0, 0};
        context->RSSetState(preview.rs);
        context->OMSetDepthStencilState(preview.dssNoWrite, 0);
        context->OMSetBlendState(preview.bs, blend_factor, 0xFFFFFFFF);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(preview.vs_sky_dome, nullptr, 0);
        context->PSSetShader(preview.ps_sky_dome, nullptr, 0);
        context->VSSetConstantBuffers(6, 1, &preview.cbuffer_sky_dome);
        context->PSSetConstantBuffers(6, 1, &preview.cbuffer_sky_dome);
        ID3D11ShaderResourceView* lut_srv = preview.sky_lut_srv;
        context->PSSetShaderResources(4, 1, &lut_srv);
        ID3D11ShaderResourceView* overlay_srv = preview.sky_overlay_srv;
        context->PSSetShaderResources(13, 1, &overlay_srv);
        ID3D11SamplerState* samplers[2] = {
            preview.sampler_cloud ? preview.sampler_cloud
                                  : preview.sampler,
            preview.sampler_sky_clamp,
        };
        context->PSSetSamplers(0, 2, samplers);
        context->Draw(3, 0);

        ID3D11Buffer* null_buffer = nullptr;
        context->VSSetConstantBuffers(6, 1, &null_buffer);
        context->PSSetConstantBuffers(6, 1, &null_buffer);
        ID3D11ShaderResourceView* null_srv = nullptr;
        context->PSSetShaderResources(4, 1, &null_srv);
        context->PSSetShaderResources(13, 1, &null_srv);

        if (preview.vs_sky_element && preview.ps_sky_element &&
            preview.layout_sky_element && preview.sky_element_vb &&
            preview.cbuffer_sky_element) {
            struct ElementVertex {
                float position[4];
                float uv[2];
            };
            auto dot3 = [](const float* a, const float* b) {
                return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
            };
            auto draw_billboard = [&](const float direction[3],
                                      float distance,
                                      float half_width,
                                      float half_height,
                                      ID3D11ShaderResourceView* texture,
                                      const float colour[4],
                                      bool additive,
                                      float u0, float u1) {
                if (!texture) return;
                const float centre[3] = {direction[0] * distance,
                                         direction[1] * distance,
                                         direction[2] * distance};
                if (dot3(centre, camera.forward) <= 0.0f) return;

                ElementVertex quad[4]{};
                const float corner_x[4] = {-1.0f, 1.0f, -1.0f, 1.0f};
                const float corner_y[4] = {1.0f, 1.0f, -1.0f, -1.0f};
                for (int corner = 0; corner < 4; ++corner) {
                    float world[3];
                    for (int axis = 0; axis < 3; ++axis) {
                        world[axis] = centre[axis] +
                            camera.right[axis] * corner_x[corner] *
                                half_width +
                            camera.up[axis] * corner_y[corner] *
                                half_height;
                    }
                    float depth = dot3(world, camera.forward);
                    if (depth < 0.01f) depth = 0.01f;
                    quad[corner].position[0] =
                        dot3(world, camera.right) /
                        (depth * camera.tan_half_x);
                    quad[corner].position[1] =
                        dot3(world, camera.up) /
                        (depth * camera.tan_half_y);
                    quad[corner].position[2] = 0.9985f;
                    quad[corner].position[3] = 1.0f;
                    quad[corner].uv[0] =
                        corner_x[corner] > 0.0f ? u1 : u0;
                    quad[corner].uv[1] =
                        corner_y[corner] > 0.0f ? 0.0f : 1.0f;
                }
                const ElementVertex vertices[6] = {
                    quad[0], quad[1], quad[2],
                    quad[2], quad[1], quad[3]};

                D3D11_MAPPED_SUBRESOURCE mapped_vb{};
                if (FAILED(context->Map(preview.sky_element_vb, 0,
                                        D3D11_MAP_WRITE_DISCARD, 0,
                                        &mapped_vb))) {
                    return;
                }
                std::memcpy(mapped_vb.pData, vertices, sizeof(vertices));
                context->Unmap(preview.sky_element_vb, 0);

                D3D11_MAPPED_SUBRESOURCE mapped_cb{};
                if (FAILED(context->Map(preview.cbuffer_sky_element, 0,
                                        D3D11_MAP_WRITE_DISCARD, 0,
                                        &mapped_cb))) {
                    return;
                }
                std::memcpy(mapped_cb.pData, colour, 16);
                context->Unmap(preview.cbuffer_sky_element, 0);

                context->OMSetBlendState(
                    additive && preview.bs_fx_add ? preview.bs_fx_add
                                                  : preview.bsAlpha,
                    blend_factor, 0xFFFFFFFF);
                const UINT stride = sizeof(ElementVertex);
                const UINT offset = 0;
                context->IASetInputLayout(preview.layout_sky_element);
                context->IASetVertexBuffers(
                    0, 1, &preview.sky_element_vb, &stride, &offset);
                context->VSSetShader(preview.vs_sky_element, nullptr, 0);
                context->PSSetShader(preview.ps_sky_element, nullptr, 0);
                context->PSSetConstantBuffers(
                    6, 1, &preview.cbuffer_sky_element);
                context->PSSetShaderResources(0, 1, &texture);
                ID3D11SamplerState* element_sampler =
                    preview.sampler_sky_clamp ? preview.sampler_sky_clamp
                                              : preview.sampler;
                context->PSSetSamplers(0, 1, &element_sampler);
                context->Draw(6, 0);
            };

            const float* element = frame.element_params;
            const float exposure = constants.dome_misc[2];
            const float gate = 0.000099999997f;

            auto tonemap_colour = [](float c[4]) {
                for (int i = 0; i < 3; ++i) c[i] = c[i] / (1.0f + c[i]);
            };

            const float* sun_tint = state.sun_element_colour;

            if (frame.sky_params[0] >= gate) {
                if (element[13] >= gate) {
                    float colour[4] = {
                        sun_tint[0] * element[13] * exposure,
                        sun_tint[1] * element[13] * exposure,
                        sun_tint[2] * element[13] * exposure,
                        1.0f};
                    tonemap_colour(colour);
                    draw_billboard(frame.sun_direction,
                                   SkyXex::kSunBeamsDistance,
                                   element[11] * SkyXex::kSunBeamsSizeScale,
                                   element[12] * SkyXex::kSunBeamsSizeScale,
                                   preview.sky_sun_beams_srv, colour,
                                   true, 0.0f, 1.0f);
                }
                {
                    float colour[4] = {
                        element[8] * element[7] * exposure,
                        element[9] * element[7] * exposure,
                        element[10] * element[7] * exposure,
                        1.0f};
                    tonemap_colour(colour);
                    draw_billboard(frame.sun_direction,
                                   SkyXex::kSunDiscDistance,
                                   element[6] * SkyXex::kSunDiscSizeScale,
                                   element[6] * SkyXex::kSunDiscSizeScale,
                                   preview.sky_sun_disc_srv, colour,
                                   false, 0.0f, 1.0f);
                }
                const float facing = dot3(frame.sun_direction,
                                          camera.forward);
                if (element[14] >= gate && facing > 0.0f) {

                    const float scale =
                        element[14] * facing * facing * exposure;
                    float colour[4] = {
                        sun_tint[0] * scale,
                        sun_tint[1] * scale,
                        sun_tint[2] * scale, 1.0f};
                    tonemap_colour(colour);
                    draw_billboard(frame.sun_direction,
                                   SkyXex::kSunGlareDistance,
                                   element[15] * SkyXex::kSunGlareSizeScale,
                                   element[15] * SkyXex::kSunGlareSizeScale,
                                   preview.sky_sun_glare_srv, colour,
                                   true, 0.0f, 1.0f);
                }
            }

            if (element[0] >= gate) {
                const int phase =
                    std::clamp(preview.moon_phase, 0, 7);
                const float u0 =
                    static_cast<float>(phase) * SkyXex::kMoonPhaseUStep;
                const float u1 = static_cast<float>(phase + 1) *
                                 SkyXex::kMoonPhaseUStep;
                {

                    float colour[4] = {
                        0.5f * element[0] * exposure,
                        0.5f * element[0] * exposure,
                        element[4] * exposure,
                        element[4]};
                    tonemap_colour(colour);
                    draw_billboard(frame.moon_direction,
                                   SkyXex::kMoonBillboardDistance,
                                   element[1] * SkyXex::kMoonSizeScale,
                                   element[1] * SkyXex::kMoonSizeScale,
                                   preview.sky_moon_srv, colour,
                                   false, u0, u1);
                }
                if (element[2] >= gate) {
                    const float scale = element[2] * exposure;
                    float colour[4] = {scale, scale, scale, 1.0f};
                    tonemap_colour(colour);
                    draw_billboard(frame.moon_direction,
                                   SkyXex::kMoonGlareDistance,
                                   element[3] * SkyXex::kMoonGlareSizeScale,
                                   element[3] * SkyXex::kMoonGlareSizeScale,
                                   preview.sky_moon_glare_srv, colour,
                                   true, 0.0f, 1.0f);
                }
            }

            if (element[5] >= gate && preview.vs_sky_stars &&
                preview.ps_sky_stars) {
                const float star_constants[4] = {
                    static_cast<float>(frame.elapsed_time),
                    element[5],

                    SkyDomeXex::kStarPointSize /
                        static_cast<float>(std::max(preview.width, 1)),
                    SkyDomeXex::kStarPointSize /
                        static_cast<float>(std::max(preview.height, 1))};
                D3D11_MAPPED_SUBRESOURCE star_cb{};
                if (SUCCEEDED(context->Map(preview.cbuffer_sky_element, 0,
                                           D3D11_MAP_WRITE_DISCARD, 0,
                                           &star_cb))) {
                    std::memcpy(star_cb.pData, star_constants, 16);
                    context->Unmap(preview.cbuffer_sky_element, 0);
                    context->OMSetBlendState(
                        preview.bs_fx_add ? preview.bs_fx_add
                                          : preview.bsAlpha,
                        blend_factor, 0xFFFFFFFF);
                    context->IASetInputLayout(nullptr);
                    context->VSSetShader(preview.vs_sky_stars, nullptr, 0);
                    context->PSSetShader(preview.ps_sky_stars, nullptr, 0);
                    context->VSSetConstantBuffers(
                        6, 1, &preview.cbuffer_sky_dome);
                    context->VSSetConstantBuffers(
                        7, 1, &preview.cbuffer_sky_element);
                    context->Draw(SkyXex::kStarCount * 6, 0);
                    context->VSSetConstantBuffers(7, 1, &null_buffer);
                }
            }

            context->OMSetBlendState(preview.bs, blend_factor, 0xFFFFFFFF);
            context->VSSetConstantBuffers(6, 1, &null_buffer);
            context->PSSetConstantBuffers(6, 1, &null_buffer);
            context->PSSetShaderResources(0, 1, &null_srv);
            if (debug_now) {
                std::ostringstream log;
                log << "  elements: beams_srv="
                    << (preview.sky_sun_beams_srv != nullptr)
                    << " disc_srv="
                    << (preview.sky_sun_disc_srv != nullptr)
                    << " glare_srv="
                    << (preview.sky_sun_glare_srv != nullptr)
                    << " moon_srv=" << (preview.sky_moon_srv != nullptr)
                    << " moonglare_srv="
                    << (preview.sky_moon_glare_srv != nullptr)
                    << " ep=[";
                for (int i = 0; i < 16; ++i) {
                    log << element[i] << (i == 15 ? "]" : ",");
                }
                log << " moon_dir=(" << frame.moon_direction[0] << ','
                    << frame.moon_direction[1] << ','
                    << frame.moon_direction[2] << ')';
                SkyDomeDebug(log.str());
            }
        }
        return;
    }

    struct SkyConstants {
        XMFLOAT4 top;
        XMFLOAT4 bottom;
        XMFLOAT4 sunset;
        XMFLOAT4 params;
        XMFLOAT4 cloud_layer[4];
        XMFLOAT4 cloud_shape[4];
        XMFLOAT4 cloud_motion[4];
        XMFLOAT4 cloud_light[4];
        XMFLOAT4 cloud_global;
        XMFLOAT4 cloud_density_flags;
        XMFLOAT4 sky_right;
        XMFLOAT4 sky_up;
        XMFLOAT4 sky_forward;
        XMFLOAT4 sky_sun;
        XMFLOAT4 sky_texture_flags;
    } constants{};
    auto finite_clamped = [](float value, float fallback) {
        return std::isfinite(value)
            ? std::clamp(value, 0.0f, 4.0f)
            : fallback;
    };
    constants.top = XMFLOAT4(
        finite_clamped(frame.sky_top[0], 0.42f),
        finite_clamped(frame.sky_top[1], 0.56f),
        finite_clamped(frame.sky_top[2], 0.76f), 1.0f);
    constants.bottom = XMFLOAT4(
        finite_clamped(frame.sky_bottom[0], 0.55f),
        finite_clamped(frame.sky_bottom[1], 0.60f),
        finite_clamped(frame.sky_bottom[2], 0.65f), 1.0f);
    constants.sunset = XMFLOAT4(
        finite_clamped(frame.sky_sunset[0], 1.0f),
        finite_clamped(frame.sky_sunset[1], 0.47f),
        finite_clamped(frame.sky_sunset[2], 0.22f), 1.0f);
    constants.params = XMFLOAT4(
        std::max(frame.sky_params[0], 0.0f),
        std::clamp(frame.sky_params[1], 0.0f, 1.0f),
        std::max(frame.sky_params[2], 0.05f),
        std::max(frame.sky_params[3], 0.05f));
    constants.cloud_global = XMFLOAT4(
        0.0f, 0.0f, 0.0f,
        std::max(preview.sky_moon_tiles[0], 1.0f));
    constants.sky_right = XMFLOAT4(
        camera.right[0], camera.right[1], camera.right[2],
        camera.tan_half_x);
    constants.sky_up = XMFLOAT4(
        camera.up[0], camera.up[1], camera.up[2], camera.tan_half_y);
    constants.sky_forward = XMFLOAT4(
        camera.forward[0], camera.forward[1], camera.forward[2], 0.0f);
    constants.sky_sun = XMFLOAT4(
        frame.sun_direction[0], frame.sun_direction[1],
        frame.sun_direction[2], 0.0f);
    constants.sky_texture_flags = XMFLOAT4(
        preview.sky_moon_srv ? 1.0f : 0.0f,
        preview.sky_moon_glare_srv ? 1.0f : 0.0f,
        preview.sky_sun_disc_srv ? 1.0f : 0.0f,
        std::max(preview.sky_moon_tiles[1], 1.0f));

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(
            preview.cbuffer_sky, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &constants, sizeof(constants));
        context->Unmap(preview.cbuffer_sky, 0);
    }
    float blend_factor[4] = {0, 0, 0, 0};
    context->RSSetState(preview.rs);
    context->OMSetDepthStencilState(preview.dssNoWrite, 0);
    context->OMSetBlendState(preview.bs, blend_factor, 0xFFFFFFFF);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(preview.vs_sky, nullptr, 0);
    context->PSSetShader(preview.ps_sky, nullptr, 0);
    context->VSSetConstantBuffers(4, 1, &preview.cbuffer_sky);
    context->PSSetConstantBuffers(4, 1, &preview.cbuffer_sky);
    ID3D11ShaderResourceView* textures[3] = {
        preview.sky_moon_srv,
        preview.sky_moon_glare_srv,
        preview.sky_sun_disc_srv,
    };
    context->PSSetShaderResources(12, 3, textures);
    ID3D11SamplerState* sampler = preview.sampler;
    context->PSSetSamplers(0, 1, &sampler);
    context->Draw(3, 0);
    ID3D11Buffer* null_buffer = nullptr;
    context->VSSetConstantBuffers(4, 1, &null_buffer);
    context->PSSetConstantBuffers(4, 1, &null_buffer);
    ID3D11ShaderResourceView* null_textures[3] = {
        nullptr, nullptr, nullptr,
    };
    context->PSSetShaderResources(12, 3, null_textures);
}

void DrawClouds(ID3D11DeviceContext* context,
                ModelPreview& preview,
                const FlyCam& camera,
                const CameraFrame& camera_frame,
                const FrameState& frame,
                const XMMATRIX& view,
                const XMMATRIX& projection)
{
    static int cloud_debug_frame = 0;
    const bool cloud_debug =
        (cloud_debug_frame < 5) || (cloud_debug_frame % 300 == 0);
    ++cloud_debug_frame;
    if (cloud_debug) {
        std::ostringstream log;
        log << "clouds frame " << cloud_debug_frame
            << ": has_cloud_theme=" << frame.has_cloud_theme
            << " show_sky=" << preview.show_sky
            << " vs_cloud=" << (preview.vs_cloud != nullptr)
            << " ps_cloud=" << (preview.ps_cloud != nullptr)
            << " layer_count=" << preview.cloud_layer_count
            << " density_srv=["
            << (preview.cloud_density_srv[0] != nullptr) << ','
            << (preview.cloud_density_srv[1] != nullptr) << ','
            << (preview.cloud_density_srv[2] != nullptr) << ','
            << (preview.cloud_density_srv[3] != nullptr) << ']';
        SkyDomeDebug(log.str());
    }
    if (!frame.has_cloud_theme || !preview.show_sky ||
        !preview.vs_cloud || !preview.ps_cloud || !preview.layout_cloud ||
        !preview.cbuffer_cloud || !preview.cloud_vb || !preview.cloud_ib ||
        !preview.sampler_cloud) {
        return;
    }

    if (!preview.cloud_runtime_initialised) {
        for (std::size_t i = 0; i < preview.cloud_runtime.size(); ++i) {
            CloudRuntime::InitialiseLayer(preview.cloud_runtime[i]);
            preview.cloud_density_binding[i] = {};
        }
        preview.cloud_runtime_initialised = true;
    }

    for (int i = 0; i < 4; ++i) {
        CloudRuntime::LayerThemeValues theme{};
        theme.transparency = frame.cloud_layer[i][0];
        theme.height = frame.cloud_layer[i][1];
        theme.texture_scale_x = frame.cloud_layer[i][2];
        theme.texture_scale_y = frame.cloud_layer[i][3];
        theme.size_x = frame.cloud_shape[i][0];
        theme.size_y = frame.cloud_shape[i][1];
        theme.position_x = frame.cloud_motion[i][0];
        theme.position_y = frame.cloud_motion[i][1];
        theme.velocity_x = frame.cloud_motion[i][2];
        theme.velocity_y = frame.cloud_motion[i][3];
        theme.brightness = frame.cloud_light[i][0];
        theme.ambient_light = frame.cloud_light[i][1];
        theme.normal_strength = frame.cloud_light[i][2];
        theme.translucency_strength = frame.cloud_light[i][3];
        theme.density_token = frame.cloud_density_token[i];

        const CloudRuntime::LayerConfig config =
            CloudRuntime::BuildConfig(theme);
        const CloudRuntime::DensityResourceBinding resolved_resource =
            preview.cloud_density_srv[i]
                ? CloudRuntime::DensityResourceBinding{
                      theme.density_token, theme.density_token}
                : CloudRuntime::DensityResourceBinding{};
        CloudRuntime::UpdateLayer(
            preview.cloud_runtime[i], config,
            frame.elapsed_time,
            preview.cloud_density_binding[i], resolved_resource);
    }

    const CloudRuntime::ActiveOrder order =
        CloudRuntime::BuildActiveOrder(preview.cloud_runtime);
    if (order.count == 0) return;

    struct CloudConstants {
        XMFLOAT4X4 view_projection;
        XMFLOAT4 viewer_position;
        XMFLOAT4 viewer_direction;
        XMFLOAT4 light_position;
        XMFLOAT4 light_colour;
        XMFLOAT4 layer_params;
        XMFLOAT4 uv_scale_offset;
        XMFLOAT4 globals;
    } constants{};
    static_assert(sizeof(CloudConstants) == 11 * 16);

    const XMMATRIX view_projection = XMMatrixTranspose(view * projection);
    XMStoreFloat4x4(&constants.view_projection, view_projection);
    constants.viewer_position = XMFLOAT4(
        camera.pos[0], camera.pos[1], camera.pos[2], 1.0f);
    constants.viewer_direction = XMFLOAT4(
        camera_frame.view_forward[0], camera_frame.view_forward[1],
        camera_frame.view_forward[2], 0.0f);
    constants.light_position = XMFLOAT4(
        camera.pos[0] - frame.sun_direction[0] * 2000.0f,
        camera.pos[1] - frame.sun_direction[1] * 2000.0f,
        camera.pos[2] - frame.sun_direction[2] * 2000.0f,
        1.0f);
    constants.light_colour = XMFLOAT4(
        frame.main_light_colour[0],
        frame.main_light_colour[1],
        frame.main_light_colour[2], 1.0f);

    const UINT vertex_stride = sizeof(CloudRuntime::Vertex);
    const UINT vertex_offset = 0;
    float blend_factor[4] = {0, 0, 0, 0};
    context->IASetInputLayout(preview.layout_cloud);
    context->IASetVertexBuffers(
        0, 1, &preview.cloud_vb, &vertex_stride, &vertex_offset);
    context->IASetIndexBuffer(
        preview.cloud_ib, DXGI_FORMAT_R16_UINT, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->RSSetState(preview.rs);

    context->OMSetDepthStencilState(preview.dssWrite, 0);
    context->OMSetBlendState(preview.bsAlpha, blend_factor, 0xFFFFFFFF);
    context->VSSetShader(preview.vs_cloud, nullptr, 0);
    context->PSSetShader(preview.ps_cloud, nullptr, 0);
    context->VSSetConstantBuffers(5, 1, &preview.cbuffer_cloud);
    context->PSSetConstantBuffers(5, 1, &preview.cbuffer_cloud);
    context->PSSetSamplers(0, 1, &preview.sampler_cloud);

    for (std::size_t sorted = 0; sorted < order.count; ++sorted) {
        const int layer_index = order.indices[sorted];
        const CloudRuntime::LayerRuntimeXex& layer =
            preview.cloud_runtime[layer_index];
        const auto vertices = CloudRuntime::BuildVertices(layer.config);

        D3D11_MAPPED_SUBRESOURCE vertex_map{};
        if (FAILED(context->Map(
                preview.cloud_vb, 0, D3D11_MAP_WRITE_DISCARD,
                0, &vertex_map))) {
            continue;
        }
        std::memcpy(vertex_map.pData, vertices.data(), sizeof(vertices));
        context->Unmap(preview.cloud_vb, 0);

        constants.layer_params = XMFLOAT4(
            layer.config.transparency,
            layer.config.ambient_light,
            layer.config.brightness,
            CloudRuntime::ShaderNormalStrength(
                layer.config.normal_strength));
        constants.uv_scale_offset = XMFLOAT4(
            layer.config.texture_scale_x,
            layer.config.texture_scale_y,
            layer.uv_x, layer.uv_y);

        constants.globals = XMFLOAT4(
            1.0f, 0.0f,
            CloudRuntime::AlphaTestThreshold(layer.config, true),
            0.0f);

        D3D11_MAPPED_SUBRESOURCE constant_map{};
        if (FAILED(context->Map(
                preview.cbuffer_cloud, 0, D3D11_MAP_WRITE_DISCARD,
                0, &constant_map))) {
            continue;
        }
        std::memcpy(
            constant_map.pData, &constants, sizeof(constants));
        context->Unmap(preview.cbuffer_cloud, 0);

        ID3D11ShaderResourceView* density =
            preview.cloud_density_srv[layer_index];
        context->PSSetShaderResources(8, 1, &density);
        context->DrawIndexed(6, 0, 0);
    }

    ID3D11ShaderResourceView* null_density = nullptr;
    context->PSSetShaderResources(8, 1, &null_density);
    ID3D11Buffer* null_buffer = nullptr;
    context->VSSetConstantBuffers(5, 1, &null_buffer);
    context->PSSetConstantBuffers(5, 1, &null_buffer);

    context->IASetInputLayout(preview.layout);
    context->VSSetShader(preview.vs, nullptr, 0);
    context->PSSetShader(preview.ps, nullptr, 0);
    ID3D11SamplerState* samplers[2] = {
        preview.sampler, preview.sampler_point,
    };
    context->PSSetSamplers(0, 2, samplers);
    context->RSSetState(
        (preview.wireframe && preview.rs_wire)
            ? preview.rs_wire
            : preview.rs);
}

}

#endif
