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
