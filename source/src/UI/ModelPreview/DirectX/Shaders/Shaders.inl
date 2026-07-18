static bool compile_shader(const char* src, const char* entry, const char* profile, ID3DBlob** blob){
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, profile, flags, 0, blob, &err);
    if (FAILED(hr) && err) {
        const char* msg = static_cast<const char*>(err->GetBufferPointer());
        OutputLog::error(std::string("shader compile failed ")
            + profile + "/" + entry + ": "
            + (msg ? msg : "unknown error"));
    }
    if(err){ err->Release(); }
    return SUCCEEDED(hr);
}
static const char* g_fog_hlsl = R"(
cbuffer FogCB : register(b5){
    float4 fog_colour;
    float4 fog_dist;
    float4 fog_density;
    float4 fog_misc;
}
float3 apply_env_fog(float3 col, float depth, float wy){
    if (fog_colour.w < 0.5) return col;
    float dn = max(depth - fog_dist.x, 0.0) * fog_dist.y;
    float od = fog_dist.w * pow(min(dn, 1.25), fog_dist.z);
    float f = 1.0 - exp(-od);
    float mist = 0.0;
    if (fog_density.z > 0.0001) {
        float below = saturate((fog_misc.x - wy) / max(fog_misc.y, 0.5));
        mist = fog_density.z * below *
               saturate(depth / max(fog_density.w, 1.0));
    }
    f = saturate(f + mist);
    return lerp(col, fog_colour.rgb, f);
}
)";

static const char* g_vs = R"(
cbuffer CB : register(b0){
    float4x4 mvp;
    float4   lightDir;
    float4x4 mv;
    float4   params;
    float4   edit_off;
    float4   edit_rot;
    float4   edit_piv;
}

cbuffer Bones : register(b1){
    row_major float4x4 bones[256];
}
struct VSIN{
    float3 p   : POSITION;
    float3 n   : NORMAL;
    float2 t   : TEXCOORD0;
    uint4  bid : BONEIDS;
    float4 bw  : BONEWEIGHTS;
};
struct VSOUT{ float4 p:SV_Position; float3 n:NORMAL; float2 t:TEXCOORD0; float3 wp:TEXCOORD1; };
VSOUT VS(VSIN i){
    VSOUT o;

    float4x4 skin = (params.y > 0.5)
        ? float4x4(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1)
        : bones[i.bid.x] * i.bw.x +
          bones[i.bid.y] * i.bw.y +
          bones[i.bid.z] * i.bw.z +
          bones[i.bid.w] * i.bw.w;

    float3 pin = i.p;
    float3 nin = i.n;
    if (edit_off.w > 0.5) {
        float3 q = (pin - edit_piv.xyz) * edit_piv.w;
        float3 tq = 2.0 * cross(edit_rot.xyz, q);
        q = q + edit_rot.w * tq + cross(edit_rot.xyz, tq);
        pin = q + edit_piv.xyz + edit_off.xyz;
        float3 tn = 2.0 * cross(edit_rot.xyz, nin);
        nin = nin + edit_rot.w * tn + cross(edit_rot.xyz, tn);
    }
    float4 p_skin = mul(float4(pin, 1.0), skin);
    float3 n_skin = mul(nin, (float3x3)skin);

    o.p = mul(p_skin, mvp);
    float3 n = mul(n_skin, (float3x3)mv);
    o.n = normalize(n);
    o.t = i.t;
    o.wp = p_skin.xyz;
    return o;
}
)";
static const char* g_ps = R"(
cbuffer CB : register(b0){
    float4x4 mvp;
    float4   lightDir;
    float4x4 mv;
    float4   params;
}
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
Texture2D tex3 : register(t3);
Texture2D tex4 : register(t4);
SamplerState smp : register(s0);
struct VSOUT{ float4 p:SV_Position; float3 n:NORMAL; float2 t:TEXCOORD0; float3 wp:TEXCOORD1; };
float4 PS(VSOUT i) : SV_Target {

    float4 diffSamp = tex0.Sample(smp, i.t);
    float3 albedo   = diffSamp.rgb;
    float  alpha    = diffSamp.a;

    float3 nSamp = tex1.Sample(smp, i.t).rgb;
    bool   nIsDefault = (nSamp.r > 0.98 && nSamp.g > 0.98 && nSamp.b > 0.98);
    float3 N_geo = normalize(i.n);
    float3 N     = N_geo;
    if (!nIsDefault) {
        float3 N_m = nSamp * 2.0 - 1.0;
        N = normalize(N_geo + N_m * 0.5);
    }

    float3 L = normalize(float3(0.3, 0.7, 0.5));
    float3 V = float3(0.0, 0.0, 1.0);
    float3 H = normalize(L + V);

    float ndotl = abs(dot(N, L));
    float diff_term = 0.55 + 0.45 * ndotl;

    float3 sSamp = tex2.Sample(smp, i.t).rgb;
    bool   sIsDefault = (sSamp.r > 0.98 && sSamp.g > 0.98 && sSamp.b > 0.98);
    float  spec_mask  = sIsDefault ? 0.0 : sSamp.r;
    float  ndoth = saturate(abs(dot(N, H)));
    float  spec  = pow(ndoth, 24.0) * spec_mask * 0.6;

    float3 color = albedo * diff_term + spec.xxx;

    if (params.z > 1.5) {
        return float4(float3(0.20, 0.95, 0.40) * (0.4 + 0.6 * diff_term), 1.0);
    }
    if (params.z > 0.5) {
        float3 hi = float3(0.15, 0.45, 1.00);
        color = lerp(color, hi, 0.65);
    }

    if (params.x > 0.5 && alpha < 0.25) discard;
    if (params.x < 0.5) alpha = 1.0;

    color = apply_env_fog(color, i.p.w, i.wp.y);
    return float4(color, alpha);
}
)";

static const char* g_terrain_vs = R"(
cbuffer CB : register(b0){
    float4x4 mvp;
    float4   lightDir;
    float4x4 mv;
    float4   params;
}
cbuffer Bones : register(b1){
    row_major float4x4 bones[256];
}
struct VSIN{
    float3 p   : POSITION;
    float3 n   : NORMAL;
    float2 t   : TEXCOORD0;
    uint4  bid : BONEIDS;
    float4 bw  : BONEWEIGHTS;
};
struct VSOUT{
    float4 p   : SV_Position;
    float3 n   : NORMAL;
    float2 t   : TEXCOORD0;
    float3 wp  : TEXCOORD1;
};
VSOUT VS(VSIN i){
    VSOUT o;
    o.p  = mul(float4(i.p, 1.0), mvp);
    o.n  = normalize(i.n);
    o.t  = i.t;
    o.wp = i.p;
    return o;
}
)";

static const char* g_terrain_ps_paint = R"(
cbuffer CB : register(b0){
    float4x4 mvp;
    float4   lightDir;
    float4x4 mv;
    float4   params;
}
cbuffer PaintCB : register(b2){
    float4 paint_info;
    float4 layer_scale[4];
    uint4  paint_flags;
}
Texture2D d0  : register(t0);
Texture2D d1  : register(t1);
Texture2D d2  : register(t2);
Texture2D d3  : register(t3);
Texture2D d4  : register(t4);
Texture2D d5  : register(t5);
Texture2D d6  : register(t6);
Texture2D d7  : register(t7);
Texture2D d8  : register(t8);
Texture2D d9  : register(t9);
Texture2D d10 : register(t10);
Texture2D d11 : register(t11);
Texture2D d12 : register(t12);
Texture2D d13 : register(t13);
Texture2D d14 : register(t14);
Texture2D d15 : register(t15);
Texture2D n0  : register(t16);
Texture2D n1  : register(t17);
Texture2D n2  : register(t18);
Texture2D n3  : register(t19);
Texture2D n4  : register(t20);
Texture2D n5  : register(t21);
Texture2D n6  : register(t22);
Texture2D n7  : register(t23);
Texture2D n8  : register(t24);
Texture2D n9  : register(t25);
Texture2D n10 : register(t26);
Texture2D n11 : register(t27);
Texture2D n12 : register(t28);
Texture2D n13 : register(t29);
Texture2D n14 : register(t30);
Texture2D n15 : register(t31);
Texture2DArray paint_weight : register(t32);
SamplerState smp_wrap : register(s0);
SamplerState smp_linear : register(s1);

struct VSOUT{
    float4 p  : SV_Position;
    float3 n  : NORMAL;
    float2 t  : TEXCOORD0;
    float3 wp : TEXCOORD1;
};

float3 sample_diffuse(int layer, float2 uv) {
    if (layer == 0) return d0.Sample(smp_wrap, uv).rgb;
    if (layer == 1) return d1.Sample(smp_wrap, uv).rgb;
    if (layer == 2) return d2.Sample(smp_wrap, uv).rgb;
    if (layer == 3) return d3.Sample(smp_wrap, uv).rgb;
    if (layer == 4) return d4.Sample(smp_wrap, uv).rgb;
    if (layer == 5) return d5.Sample(smp_wrap, uv).rgb;
    if (layer == 6) return d6.Sample(smp_wrap, uv).rgb;
    if (layer == 7) return d7.Sample(smp_wrap, uv).rgb;
    if (layer == 8) return d8.Sample(smp_wrap, uv).rgb;
    if (layer == 9) return d9.Sample(smp_wrap, uv).rgb;
    if (layer == 10) return d10.Sample(smp_wrap, uv).rgb;
    if (layer == 11) return d11.Sample(smp_wrap, uv).rgb;
    if (layer == 12) return d12.Sample(smp_wrap, uv).rgb;
    if (layer == 13) return d13.Sample(smp_wrap, uv).rgb;
    if (layer == 14) return d14.Sample(smp_wrap, uv).rgb;
    return d15.Sample(smp_wrap, uv).rgb;
}

float3 sample_normal(int layer, float2 uv) {
    if (layer == 0) return n0.Sample(smp_wrap, uv).rgb;
    if (layer == 1) return n1.Sample(smp_wrap, uv).rgb;
    if (layer == 2) return n2.Sample(smp_wrap, uv).rgb;
    if (layer == 3) return n3.Sample(smp_wrap, uv).rgb;
    if (layer == 4) return n4.Sample(smp_wrap, uv).rgb;
    if (layer == 5) return n5.Sample(smp_wrap, uv).rgb;
    if (layer == 6) return n6.Sample(smp_wrap, uv).rgb;
    if (layer == 7) return n7.Sample(smp_wrap, uv).rgb;
    if (layer == 8) return n8.Sample(smp_wrap, uv).rgb;
    if (layer == 9) return n9.Sample(smp_wrap, uv).rgb;
    if (layer == 10) return n10.Sample(smp_wrap, uv).rgb;
    if (layer == 11) return n11.Sample(smp_wrap, uv).rgb;
    if (layer == 12) return n12.Sample(smp_wrap, uv).rgb;
    if (layer == 13) return n13.Sample(smp_wrap, uv).rgb;
    if (layer == 14) return n14.Sample(smp_wrap, uv).rgb;
    return n15.Sample(smp_wrap, uv).rgb;
}

float layer_uv_scale(int layer) {
    return layer_scale[layer >> 2][layer & 3];
}

float4 PS(VSOUT i) : SV_Target {
    float2 texel = paint_info.yz;
    float2 weight_uv = clamp(i.t, texel * 0.5, 1.0 - texel * 0.5);
    float3 albedo = float3(0.376, 0.376, 0.361);
    float3 mapped_normal = float3(0.0, 0.0, 1.0);
    int layer_count = (int)paint_info.x;

    [loop]
    for (int layer = 0; layer < 16; ++layer) {
        if (layer >= layer_count) break;
        float weight = paint_weight.SampleLevel(
            smp_linear, float3(weight_uv, layer), 0).r;
        if (weight <= 0.001) continue;
        float2 uv = i.wp.xz * layer_uv_scale(layer);
        albedo = lerp(albedo, sample_diffuse(layer, uv), weight);
        if ((paint_flags.x & (1u << layer)) != 0) {
            float3 sampled = sample_normal(layer, uv) * 2.0 - 1.0;
            mapped_normal = lerp(mapped_normal, sampled, weight);
        }
    }

    float3 N_geo = normalize(i.n);
    float3 N_map = float3(mapped_normal.x,
                          mapped_normal.z,
                          mapped_normal.y);
    float3 N = normalize(N_geo + N_map * 0.5);
    float3 L = normalize(float3(0.3, 0.7, 0.5));
    float shade = 0.55 + 0.45 * saturate(dot(N, L));
    float3 final = albedo * shade;

    if (params.z > 0.5) {
        final = lerp(final, float3(0.10, 0.45, 1.0), 0.65);
    }
    final = apply_env_fog(final, i.p.w, i.wp.y);
    return float4(final, 1.0);
}
)";

static const char* g_terrain_ps = R"(
cbuffer CB : register(b0){
    float4x4 mvp;
    float4   lightDir;
    float4x4 mv;
    float4   params;
}
cbuffer TerrainCB : register(b2){

    float4 chunk_origin_extent;

    float4 chunk_grid_size;

    float4 splat_params;

    float4 mesh_xform;
}
Texture2DArray  lod_array     : register(t0);
Texture2DArray  chunk_idx     : register(t1);
Texture2DArray  chunk_blend   : register(t2);
Texture2DArray  chunk_uv      : register(t3);
Texture2D       splat_mask    : register(t4);
Texture2D       lightmap      : register(t5);
SamplerState    smp_wrap      : register(s0);
SamplerState    smp_point     : register(s1);

struct VSOUT{
    float4 p   : SV_Position;
    float3 n   : NORMAL;
    float2 t   : TEXCOORD0;
    float3 wp  : TEXCOORD1;
};

float3 sample_lod(int slice, float2 uv){

    return lod_array.Sample(smp_wrap, float3(uv, slice)).rgb;
}

float4 PS(VSOUT i) : SV_Target {

    float2 mesh_xy  = float2(i.wp.x, i.wp.z);
    float2 world_xy = mesh_xy * mesh_xform.xy + mesh_xform.zw;
    float2 origin   = chunk_origin_extent.xy;
    float2 extent   = chunk_origin_extent.zw;
    float  CW       = chunk_grid_size.x;
    float  CH       = chunk_grid_size.y;
    float  max_lod  = chunk_grid_size.z;
    float2 mat_uv   = world_xy * splat_params.y;

    float2 chunk_co = (world_xy - origin) / extent;

    float2 chunk_clamped = clamp(chunk_co,
                                 float2(0, 0),
                                 float2(CW - 0.001, CH - 0.001));
    int2   chunk_xy = int2(floor(chunk_clamped));
    float2 corner_uv = frac(chunk_clamped);

    float wx = corner_uv.x, wy = corner_uv.y;
    float w00 = (1.0 - wx) * (1.0 - wy);
    float w10 =        wx  * (1.0 - wy);
    float w01 = (1.0 - wx) *        wy ;
    float w11 =        wx  *        wy ;

    float3 final = float3(0.0, 0.0, 0.0);
    float  weight_sum = 0.0;

    [loop]
    for (int L = 0; L < 16; ++L) {
        float4 idx_norm = chunk_idx.Load(int4(chunk_xy, L, 0));
        float4 idx255   = round(idx_norm * 255.0);
        if (idx255.x > 254.5) break;

        float4 bln_norm = chunk_blend.Load(int4(chunk_xy, L, 0));
        float4 uv_info  = chunk_uv.Load(int4(chunk_xy, L, 0));

        float bscale = splat_params.x;
        float4 bln   = bln_norm * 255.0 / bscale;
        float2 mask_uv = uv_info.xy + corner_uv * uv_info.zw * 2.0;
        float mask_w = splat_mask.SampleLevel(smp_point, mask_uv, 0).r;

        float3 c00 = sample_lod((int)idx255.x, mat_uv);
        float3 c10 = sample_lod((int)idx255.y, mat_uv);
        float3 c01 = sample_lod((int)idx255.z, mat_uv);
        float3 c11 = sample_lod((int)idx255.w, mat_uv);

        float cw00 = mask_w * saturate(bln.x) * w00;
        float cw10 = mask_w * saturate(bln.y) * w10;
        float cw01 = mask_w * saturate(bln.z) * w01;
        float cw11 = mask_w * saturate(bln.w) * w11;

        final += c00 * cw00 + c10 * cw10 + c01 * cw01 + c11 * cw11;
        weight_sum += cw00 + cw10 + cw01 + cw11;
    }

    if (weight_sum > 0.001) {
        final /= weight_sum;
    } else {
        final = sample_lod(0, mat_uv);
    }

    float2 lm_uv = chunk_co / float2(CW, CH);
    float  ao    = lightmap.Sample(smp_wrap, lm_uv).r;
    final *= (ao * 0.55 + 0.45);

    float3 N = normalize(i.n);
    float3 light_vec = normalize(float3(0.3, 0.7, 0.5));
    float  ndotl = saturate(dot(N, light_vec));
    float  shade = 0.55 + 0.45 * ndotl;
    final *= shade;

    if (params.z > 0.5) {
        float3 hi = float3(0.15, 0.45, 1.00);
        final = lerp(final, hi, 0.65);
    }

    final = apply_env_fog(final, i.p.w, i.wp.y);
    return float4(final, 1.0);
}
)";

static const char* g_terrain_ps_live = R"(
cbuffer CB : register(b0){
    float4x4 mvp;
    float4   lightDir;
    float4x4 mv;
    float4   params;
}
cbuffer TerrainCB : register(b2){
    float4 chunk_origin_extent;
    float4 chunk_grid_size;
    float4 splat_params;
    float4 mesh_xform;
    float4 weight_size;
    float4 material_params[32];
}
Texture2DArray  lod_array     : register(t0);
Texture2DArray  chunk_idx     : register(t1);
Texture2DArray  chunk_blend   : register(t2);
Texture2DArray  chunk_uv      : register(t3);
Texture2D       splat_mask    : register(t4);
Texture2D       lightmap      : register(t5);
Texture2DArray  lod_detail_array : register(t6);
Texture2DArray  material_weight  : register(t7);
SamplerState    smp_wrap      : register(s0);
SamplerState    smp_point     : register(s1);

struct VSOUT{
    float4 p   : SV_Position;
    float3 n   : NORMAL;
    float2 t   : TEXCOORD0;
    float3 wp  : TEXCOORD1;
};

float3 sample_material(int slice, float2 world_xy, float slope_w){
    int max_slice = max((int)chunk_grid_size.z - 1, 0);
    int s = min(max(slice, 0), max_slice);
    float4 mp = material_params[s];
    float base_scale = splat_params.y;
    float detail_scale = splat_params.y;
    float3 base = lod_array.Sample(
        smp_wrap, float3(world_xy * base_scale, s)).rgb;
    float3 detail = lod_detail_array.Sample(
        smp_wrap, float3(world_xy * detail_scale, s)).rgb;
    float detail_w = slope_w * saturate(mp.z) * saturate(mp.w);
    return lerp(base, detail, detail_w);
}

float4 PS(VSOUT i) : SV_Target {
    float2 mesh_xy  = float2(i.wp.x, i.wp.z);
    float2 world_xy = mesh_xy * mesh_xform.xy + mesh_xform.zw;
    float2 origin   = chunk_origin_extent.xy;
    float2 extent   = chunk_origin_extent.zw;
    float  CW       = chunk_grid_size.x;
    float  CH       = chunk_grid_size.y;

    float2 chunk_co = (world_xy - origin) / extent;
    float2 chunk_clamped = clamp(chunk_co,
                                 float2(0, 0),
                                 float2(CW - 0.001, CH - 0.001));
    float slope_w = saturate((0.90 - abs(normalize(i.n).y)) / 0.10);

    float3 final = float3(0.0, 0.0, 0.0);
    float  weight_sum = 0.0;
    float2 inv_weight_size = weight_size.zw;
    float2 weight_uv = (chunk_clamped * 32.0 + 0.5) * inv_weight_size;
    float2 edge_uv = 0.5 * inv_weight_size;
    weight_uv = clamp(weight_uv, edge_uv, 1.0 - edge_uv);

    [loop]
    for (int mat = 0; mat < 32; ++mat) {
        if (mat >= (int)chunk_grid_size.z) break;
        float w = material_weight.SampleLevel(
            smp_point, float3(weight_uv, mat), 0).r;
        if (w <= 0.001) continue;
        final += sample_material(mat, world_xy, slope_w) * w;
        weight_sum += w;
    }

    if (weight_sum > 0.001) {
        final /= weight_sum;
    } else {
        final = sample_material(0, world_xy, slope_w);
    }

    if (params.z > 0.5) {
        float3 hi = float3(0.10, 0.95, 0.25);
        final = lerp(final, hi, 0.65);
    }

    final = apply_env_fog(final, i.p.w, i.wp.y);
    return float4(final, 1.0);
}
)";

static const char* g_water_vs = R"(
cbuffer CB : register(b0){
    float4x4 mvp;
    float4   lightDir;
    float4x4 mv;
    float4   params;
}
struct VSIN{
    float3 p   : POSITION;
    float3 n   : NORMAL;
    float2 t   : TEXCOORD0;
    uint4  bid : BONEIDS;
    float4 bw  : BONEWEIGHTS;
};
struct VSOUT{
    float4 p   : SV_Position;
    float3 n   : NORMAL;
    float2 t   : TEXCOORD0;
    float3 wp  : TEXCOORD1;
};






VSOUT VS(VSIN i){
    VSOUT o;
    o.p  = mul(float4(i.p, 1.0), mvp);
    o.n  = float3(0.0, 1.0, 0.0);
    o.t  = i.t;
    o.wp = i.p;
    return o;
}
)";

static const char* g_water_ps = R"(
cbuffer CB : register(b0){
    float4x4 mvp;
    float4   lightDir;
    float4x4 mv;
    float4   params;
}
cbuffer WaterCB : register(b3){
    float4 w_bias;

    float4 w_ph01;
    float4 w_sc01;
    float4 w_surface;
    float4 w_deep;
    float4 w_reflp;

    float4 w_glit;

    float4 w_eye;
    float4 w_sun;

    float4 w_light;

}
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
    float4 sky_celestial;
}
Texture2D    tex_normal : register(t0);
SamplerState smp        : register(s0);



Texture2D    water_sky_lut : register(t4);
SamplerState smp_clamp     : register(s2);
cbuffer SkyDomeXexCB : register(b6){
    row_major float4x4 dome_view_projection;
    float4 dome_camera_position;
    float4 dome_camera_position_engine;
    float4 dome_beta_rayleigh;
    float4 dome_beta_mie;
    float4 dome_sun_direction;
    float4 dome_scattering_misc;
    float4 dome_overlay_blend;
    float4 dome_misc;
    float4 dome_bgmap_c72;
    float4 dome_bgmap_c73;
    float4 dome_bgmap_c74;
    float4 dome_bgmap_c75;
    float4 dome_bgmap_c76;
    float4 dome_bgmap_c28;
    float4 dome_camera_right;
    float4 dome_camera_up;
    float4 dome_camera_forward;
}

struct PSIN{
    float4 p   : SV_Position;
    float3 n   : NORMAL;
    float2 t   : TEXCOORD0;
    float3 wp  : TEXCOORD1;
};




float3 water_sky(float3 ray){
    if (w_glit.z < 0.5) {
        return float3(0.58, 0.62, 0.57);
    }
    float3 sun_dir = normalize(sky_sun.xyz);
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
    col = lerp(col, sky_sunset.rgb * (0.4 + 0.8 * phaseM), sunset_w * 0.45);
    float night = saturate((-sun_dir.y + 0.05) / 0.45);
    col = lerp(col, col * 0.22 + float3(0.010, 0.018, 0.050), night);
    return col;
}






float3 dome_reflect(float3 ray_pv){
    float3 n = normalize(float3(ray_pv.x, ray_pv.z, ray_pv.y));
    float lut_u = saturate(dot(dome_sun_direction.xyz, n));
    float3 lut = water_sky_lut.Sample(smp_clamp,
                                      float2(lut_u, 0.5)).xyz;
    float nz_pow = pow(max(abs(n.z), 1e-5), 0.2);
    float depth_base = dome_scattering_misc.y * (1.05 + nz_pow);
    float od_r = (dome_misc.x + depth_base) * nz_pow + depth_base;
    float od_m = (dome_misc.y + depth_base) * nz_pow + depth_base;
    float3 od = od_m * dome_beta_mie.xyz +
                od_r * dome_beta_rayleigh.xyz;
    float3 T = exp2(-od * 1.4427);
    float3 hdr = (1.0 - T) * lut * dome_misc.z;
    return hdr / (1.0 + hdr);
}




































float4 PS(PSIN i) : SV_Target {



    float2 uv0 = i.wp.xz * w_sc01.xy + w_ph01.xy;
    float2 uv1 = i.wp.xz * w_sc01.zw + w_ph01.zw;
    float2 n0 = tex_normal.Sample(smp, uv0).xy * 2.0 - 1.0;
    float2 n1 = tex_normal.Sample(smp, uv1).xy * 2.0 - 1.0;
    float z0 = sqrt(saturate(1.0 - dot(n0, n0)));
    float z1 = sqrt(saturate(1.0 - dot(n1, n1)));
    float2 nxy = n0 + n1;
    float  nz  = z0 + z1;


    float3 view = w_eye.xyz - i.wp;
    float dist = length(view);
    float3 V = view / max(dist, 1e-5);





    float3 Nf = normalize(float3(nxy.x, w_bias.z * nz, nxy.y));
    float fres = saturate((1.0 - dot(V, Nf)) + w_bias.x);


    float3 watercol = lerp(w_deep.rgb, w_surface.rgb, fres);


    float shadow_f = 1.0 * 0.95 + 0.05;

    float frefl = saturate(fres + w_bias.y);

    float refl_str = saturate(w_deep.w);



    float3 Nr = normalize(float3(nxy.x * w_reflp.x * 0.5, 1.0,
                                 nxy.y * w_reflp.x * 0.5));
    float3 R = reflect(-V, Nr);
    R.y = abs(R.y);
    float3 sky_refl = (w_glit.w > 0.5) ? dome_reflect(R) : water_sky(R);
    float3 refl = sky_refl * shadow_f;








    float distf = saturate(dist * (1.0 / 75.0));
    float refr_k = (1.0 - distf) * refl_str * (1.0 - frefl);
    float3 col = watercol * (1.0 - refl_str)
               + refl * (refl_str * lerp(frefl, 1.0, distf));




    float3 g = normalize(float3(Nf.x, -max(w_reflp.z, 0.0) * Nf.y, Nf.z));
    float3 L = normalize(w_light.xyz);
    float3 Rl = L - 2.0 * dot(L, g) * g;
    float spec = saturate(dot(V, Rl));
    float glint = pow(max(spec, 1e-6), max(w_glit.x, 1.0));
    col += w_sun.rgb * (glint * max(w_reflp.w, 0.0));

    if (w_glit.y > 0.5) {
        float3 hi = float3(0.20, 0.55, 0.85);
        col = lerp(col, hi, 0.55);
    }



    col = apply_env_fog(col, i.p.w, i.wp.y);



    return float4(col, refr_k);
}
)";

static const char* g_weather_vs = R"(
cbuffer WeatherCB : register(b6){
    float4x4 wvp;
    float4 cam_time;
    float4 wind_vec;
    float4 rain_p;
    float4 snow_p;
    float4 area;
    float4 cam_right;
    float4 cam_up;
}
struct VSOUT{
    float4 p    : SV_Position;
    float2 uv   : TEXCOORD0;
    float2 meta : TEXCOORD1;
};

float wx_hash(float n){ return frac(sin(n) * 43758.5453123); }

VSOUT VS(uint id : SV_VertexID){
    uint pid = id / 6;
    uint corner = id % 6;
    float2 uv;
    if (corner == 0) uv = float2(0.0, 0.0);
    else if (corner == 1) uv = float2(1.0, 0.0);
    else if (corner == 2) uv = float2(0.0, 1.0);
    else if (corner == 3) uv = float2(1.0, 0.0);
    else if (corner == 4) uv = float2(1.0, 1.0);
    else uv = float2(0.0, 1.0);

    float t = cam_time.w;
    bool is_rain = (float)pid < rain_p.x;
    float seed = (float)pid * (is_rain ? 1.6180339 : 2.2360679)
               + (is_rain ? 0.0 : 71.0);
    float h1 = wx_hash(seed + 1.3);
    float h2 = wx_hash(seed + 7.7);
    float h3 = wx_hash(seed + 3.9);
    float h4 = wx_hash(seed + 9.1);

    float3 center;
    float fade = 1.0;
    float3 off = float3(0.0, 0.0, 0.0);
    if (is_rain) {
        float R = area.x;
        float H = area.y;
        float speed = max(rain_p.z, 1.0);
        float3 fall = float3(wind_vec.x * 0.14, -speed, wind_vec.z * 0.14);
        float yfrac = 1.0 - frac(h2 + t * speed / H);
        center.x = cam_time.x + (h1 * 2.0 - 1.0) * R;
        center.z = cam_time.z + (h3 * 2.0 - 1.0) * R;
        center.y = cam_time.y - H * 0.5 + yfrac * H;
        float len = 0.55 * max(rain_p.y, 0.15);
        float wdt = 0.016 * max(rain_p.y, 0.15);
        float3 d = normalize(fall);
        float3 to_cam = center - cam_time.xyz;
        float3 side = cross(d, to_cam + float3(0.003, 0.0, 0.007));
        float side_len = length(side);
        side = (side_len > 0.0001) ? side / side_len : float3(1.0, 0.0, 0.0);
        off = side * (uv.x - 0.5) * wdt + d * (uv.y - 0.5) * len;
        fade = saturate(1.35 - length(to_cam) / R);
    } else {
        float R = area.z;
        float H = area.w;
        float speed = max(snow_p.z, 0.15);
        float yfrac = 1.0 - frac(h2 + t * speed / H);
        float drift_x = wind_vec.x * t * 0.35 / max(2.0 * R, 1.0);
        float drift_z = wind_vec.z * t * 0.35 / max(2.0 * R, 1.0);
        center.x = cam_time.x + (frac(h1 + drift_x) * 2.0 - 1.0) * R;
        center.z = cam_time.z + (frac(h3 + drift_z) * 2.0 - 1.0) * R;
        center.y = cam_time.y - H * 0.5 + yfrac * H;
        center.x += sin(t * 0.9 + h4 * 6.2831) * 0.45 * snow_p.w;
        center.z += cos(t * 0.7 + h4 * 12.566) * 0.35 * snow_p.w;





        float s = wx_hash(seed + 5.3) * saturate(snow_p.y) * 0.109375
                * 2.0 * cam_right.w;
        off = cam_right.xyz * (uv.x - 0.5) * s +
              cam_up.xyz * (uv.y - 0.5) * s;
        float3 to_cam = center - cam_time.xyz;
        fade = saturate(1.5 - length(to_cam) / R);
    }

    VSOUT o;
    o.p = mul(float4(center + off, 1.0), wvp);
    o.uv = uv;
    o.meta = float2(is_rain ? 0.0 : 1.0, fade);
    return o;
}
)";

static const char* g_weather_ps = R"(
cbuffer WeatherCB : register(b6){
    float4x4 wvp;
    float4 cam_time;
    float4 wind_vec;
    float4 rain_p;
    float4 snow_p;
    float4 area;
    float4 cam_right;
    float4 cam_up;
}
struct PSIN{
    float4 p    : SV_Position;
    float2 uv   : TEXCOORD0;
    float2 meta : TEXCOORD1;
};
float4 PS(PSIN i) : SV_Target {
    float alpha;
    float3 col;
    if (i.meta.x < 0.5) {
        float ax = 1.0 - abs(i.uv.x * 2.0 - 1.0);
        float ay = 1.0 - abs(i.uv.y * 2.0 - 1.0);
        alpha = pow(saturate(ax), 2.2) * saturate(ay) *
                rain_p.w * i.meta.y;
        col = float3(0.62, 0.68, 0.78);
    } else {
        float2 d = i.uv * 2.0 - 1.0;
        float r = length(d);
        alpha = smoothstep(1.0, 0.25, r) * 0.9 * i.meta.y;
        col = float3(0.92, 0.94, 0.98);
    }
    if (alpha < 0.004) discard;
    return float4(col, alpha);
}
)";
