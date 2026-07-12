#include "SkyXexDecomp.h"

namespace SkyXex {

const std::array<Float4, 8> kDomeCubeVertices = {{
    {0.0f, 0.0f, 0.0f, 1.0f},
    {1.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 1.0f, 0.0f, 1.0f},
    {1.0f, 1.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, 1.0f, 1.0f},
    {1.0f, 0.0f, 1.0f, 1.0f},
    {0.0f, 1.0f, 1.0f, 1.0f},
    {1.0f, 1.0f, 1.0f, 1.0f},
}};

const std::array<std::uint16_t, 48> kDomeCubeIndices = {{
    0, 1, 2, 3, 2, 1,
    2, 3, 6, 7, 6, 3,
    6, 7, 4, 5, 4, 7,
    2, 6, 0, 4, 0, 6,
    0, 4, 1, 5, 1, 4,
    1, 5, 3, 7, 3, 5,
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
}};

namespace {

inline void vec_scale_add(float out[3], const float dir[4], float scale,
                          const float base[4]) {

    out[0] = dir[0] * scale + base[0];
    out[1] = dir[1] * scale + base[1];
    out[2] = dir[2] * scale + base[2];
}

inline void camera_right_up(const RenderContext& ctx, float size_x,
                            float size_y, float right_half[3],
                            float up_half[3]) {

    right_half[0] = ctx.view_matrix[0] * size_x;
    right_half[1] = ctx.view_matrix[4] * size_x;
    right_half[2] = ctx.view_matrix[8] * size_x;
    up_half[0] = ctx.view_matrix[1] * size_y;
    up_half[1] = ctx.view_matrix[5] * size_y;
    up_half[2] = ctx.view_matrix[9] * size_y;
}

inline float clamp01(float v) {

    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

inline void set_billboard_states(RenderApi& api, bool alpha_test) {
    api.set_render_state(RS_CULL_MODE, 1);
    api.set_render_state(RS_DEPTH_WRITE, 0);
    api.set_render_state(RS_ALPHA_BLEND_ENABLE, 1);
    api.set_render_state(RS_ALPHA_TEST_ENABLE, alpha_test ? 1u : 0u);
    api.set_render_state(RS_SRC_BLEND, 1);
    api.set_render_state(RS_DST_BLEND, 1);
    api.set_render_state(RS_DEPTH_TEST, 1);
}

}

void SkyPrimitive_RenderCallback(RenderApi& api, SkyPrimitive& prim,
                                 RenderContext& ctx) {

    if (ctx.skip_clouds) {
        Sky_DrawAtmosphereDome(api, prim, ctx);
        return;
    }

    bool full_refresh = false;
    if (ctx.update_mode == 2) {

        full_refresh = true;
        Sky_DrawAtmosphereDome(api, prim, ctx);
        Sky_DrawMoon(api, prim.moon, ctx);
        Sky_DrawStars(api, prim, ctx);

    } else {
        Sky_DrawAtmosphereDome(api, prim, ctx);
    }
    (void)full_refresh;

}

void Sky_SetupDomeState(RenderApi& api, SkyPrimitive& prim,
                        RenderContext& ctx) {

    float pairs[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    ctx.theme->ReadTexturePairs(TEXSLOT_SKY_OVERLAY, pairs, 2);

    prim.overlay_texture_a = static_cast<std::uint32_t>(pairs[1]);
    prim.overlay_texture_b = static_cast<std::uint32_t>(pairs[3]);
    api.bind_texture(prim.overlay_texture_a, 0);
    api.bind_texture(prim.overlay_texture_b, 1);

    prim.overlay_blend = pairs[2];
    api.set_float(LC_SKY_OVERLAY_BLEND, pairs[2]);

    if (!ctx.no_background_map) {
        if (ctx.mist_volume != nullptr) {
            api.bind_background_map(ctx.mist_volume, &ctx);
        } else {
            api.clear_background_map();
        }
    }

    api.set_float(LC_DOME_MISC_B, prim.dome_misc_a);
    api.set_float(LC_DOME_MISC_A, prim.dome_misc_b);

}

void Sky_DrawAtmosphereDome(RenderApi& api, SkyPrimitive& prim,
                            RenderContext& ctx) {

    api.set_render_state(RS_DEPTH_WRITE, 0);

    api.select_vertex_shader(VS_SKY_DOME);
    api.select_pixel_shader(PS_SKY_DOME);

    Sky_SetupDomeState(api, prim, ctx);

    const float tint[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    api.set_float4(LC_BILLBOARD_TINT, tint);
    api.set_render_state(RS_CULL_MODE, 1);
    api.draw_dome_cube(kDomeFaceMaskMain);
}

void Sky_DrawMoon(RenderApi& api, MoonElement& moon, RenderContext& ctx) {
    const ThemeView& theme = *ctx.theme;

    const float intensity = theme.RecordValue(MOON_INTENSITY)[0];
    if (intensity < kIntensityGate)
        return;

    moon.moon_texture = theme.TopTextureHandle(TEXSLOT_MOON);
    if (moon.moon_texture == 0 || !api.prepare_texture(moon.moon_texture))
        return;

    set_billboard_states(api, false);

    api.select_vertex_shader(VS_CELESTIAL_BILLBOARD);
    api.select_pixel_shader(PS_MOON);
    api.bind_texture(moon.moon_texture, 0);

    const float transparency = theme.RecordValue(MOON_TRANSPARENCY)[0];
    const float c147[4] = {0.5f * transparency, 0.5f * transparency,
                           intensity, intensity};
    api.set_float4(LC_MOON_PARAMS, c147);

    const float u0 = static_cast<float>(moon.phase) * kMoonPhaseUStep;
    const float u1 = static_cast<float>(moon.phase + 1) * kMoonPhaseUStep;
    const float uv_rect[4] = {u0, 0.0f, u1, 1.0f};

    float centre[3];
    vec_scale_add(centre, moon.direction, kMoonBillboardDistance,
                  ctx.camera_pos);
    const float half = theme.RecordValue(MOON_SIZE)[0] * kMoonSizeScale;
    float right_half[3], up_half[3];
    camera_right_up(ctx, half, half, right_half, up_half);

    api.draw_billboard(centre, right_half, up_half, uv_rect);
}

void Sky_DrawStars(RenderApi& api, SkyPrimitive& prim, RenderContext& ctx) {
    (void)prim;
    const ThemeView& theme = *ctx.theme;

    const float brightness = theme.RecordValue(STARS_BRIGHTNESS)[0];
    if (brightness < kIntensityGate)
        return;

    api.select_vertex_shader(VS_STARS);
    api.select_pixel_shader(PS_STARS);

    const float time = static_cast<float>(api.global_time());
    const float c390[4] = {1.5f, 2.5f, time, brightness};
    api.set_float4(LC_STARS_PARAMS, c390);

    api.set_render_state(RS_CULL_MODE, 1);
    api.set_render_state(RS_ALPHA_TEST_ENABLE, 0);
    api.set_render_state(RS_ALPHA_BLEND_ENABLE, 1);
    api.set_render_state(RS_SRC_BLEND, 2);
    api.set_render_state(RS_DST_BLEND, 2);
    api.set_render_state(RS_POINT_SIZE_MIN, 0x3DCCCCCD);
    api.set_render_state(RS_POINT_SIZE_MAX, 0x42800000);
    api.set_render_state(RS_DEPTH_TEST, 1);
    api.set_render_state(RS_DEPTH_WRITE, 0);

    api.draw_points(0, kStarCount);
}

void Sky_DrawSunDisc(RenderApi& api, SunElement& sun, RenderContext& ctx) {
    const ThemeView& theme = *ctx.theme;

    if (theme.RecordValue(SUN_INTENSITY)[0] < kIntensityGate)
        return;

    sun.disc_texture = theme.TopTextureHandle(TEXSLOT_SUN_DISC);
    if (sun.disc_texture == 0 || !api.prepare_texture(sun.disc_texture))
        return;

    set_billboard_states(api, true);

    api.select_vertex_shader(VS_CELESTIAL_BILLBOARD);
    api.select_pixel_shader(PS_SUN_DISC);
    api.bind_texture(sun.disc_texture, 0);

    const float* colour = theme.RecordValue(SUN_DISC_COLOUR);
    const float disc_intensity = theme.RecordValue(SUN_DISC_INTENSITY)[0];
    const float c245[4] = {colour[0] * disc_intensity,
                           colour[1] * disc_intensity,
                           colour[2] * disc_intensity, 1.0f};
    api.set_float4(LC_SUN_DISC_COLOUR, c245);

    const float size = theme.RecordValue(SUN_DISC_SIZE)[0];
    std::uint32_t pixels = 0;
    bool issue_query = false;
    if (!api.occlusion_result(sun.occlusion_query, &pixels) ||
        sun.visibility < 0.0f) {
        sun.visibility =
            clamp01(static_cast<float>(pixels) /
                    (size * size * kSunDiscVisibilityDiv));
        api.occlusion_begin(sun.occlusion_query);
        issue_query = true;
    }

    float centre[3];
    vec_scale_add(centre, sun.direction, kSunDiscDistance, ctx.camera_pos);
    const float half = size * kSunDiscSizeScale;
    float right_half[3], up_half[3];
    camera_right_up(ctx, half, half, right_half, up_half);
    const float uv_rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    api.draw_billboard(centre, right_half, up_half, uv_rect);

    if (issue_query)
        api.occlusion_end(sun.occlusion_query);
}

void Sky_DrawSunBeams(RenderApi& api, SunElement& sun, RenderContext& ctx) {
    const ThemeView& theme = *ctx.theme;

    if (theme.RecordValue(SUN_INTENSITY)[0] < kIntensityGate)
        return;

    sun.beams_texture = theme.TopTextureHandle(TEXSLOT_SUN_BEAMS);
    if (sun.beams_texture == 0 || !api.prepare_texture(sun.beams_texture))
        return;

    api.set_render_state(RS_CULL_MODE, 1);
    api.set_render_state(RS_DEPTH_WRITE, 0);

    api.select_vertex_shader(VS_CELESTIAL_BILLBOARD);
    api.select_pixel_shader(PS_SUN_BEAMS);
    api.bind_texture(sun.beams_texture, 0);

    const float beams_intensity = theme.RecordValue(SUN_BEAMS_INTENSITY)[0];
    const float c244[4] = {sun.colour[0] * beams_intensity,
                           sun.colour[1] * beams_intensity,
                           sun.colour[2] * beams_intensity,
                           sun.colour[3] * beams_intensity};
    api.set_float4(LC_SUN_BEAMS_COLOUR, c244);

    float centre[3];
    vec_scale_add(centre, sun.direction, kSunBeamsDistance, ctx.camera_pos);
    const float half_w = theme.RecordValue(SUN_BEAMS_WIDTH)[0] * kSunBeamsSizeScale;
    const float half_h = theme.RecordValue(SUN_BEAMS_HEIGHT)[0] * kSunBeamsSizeScale;
    float right_half[3], up_half[3];
    camera_right_up(ctx, half_w, half_h, right_half, up_half);
    const float uv_rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    api.draw_billboard(centre, right_half, up_half, uv_rect);
}

void Sky_DrawSunGlareStreaks(RenderApi& api, SunElement& sun,
                             RenderContext& ctx) {
    const ThemeView& theme = *ctx.theme;

    if (theme.RecordValue(SUN_INTENSITY)[0] < kIntensityGate)
        return;

    sun.glare_texture = theme.TopTextureHandle(TEXSLOT_SUN_GLARE);
    sun.streaks_texture = theme.TopTextureHandle(TEXSLOT_SUN_STREAKS);
    if (sun.glare_texture == 0)
        return;

    const float facing = sun.direction[0] * ctx.view_forward[0] +
                         sun.direction[1] * ctx.view_forward[1] +
                         sun.direction[2] * ctx.view_forward[2];
    if (facing < 0.0f)
        return;

    if (!api.prepare_texture(sun.glare_texture))
        return;

    set_billboard_states(api, false);

    api.select_vertex_shader(VS_CELESTIAL_BILLBOARD);
    api.select_pixel_shader(PS_SUN_GLARE_STREAKS);
    api.bind_texture(sun.glare_texture, 0);
    api.bind_texture(sun.streaks_texture, 1);

    const float glare_intensity = theme.RecordValue(SUN_GLARE_INTENSITY)[0];
    const float scale = glare_intensity * (facing * facing) *
                        (sun.visibility * sun.visibility);
    const float c244[4] = {sun.colour[0] * scale, sun.colour[1] * scale,
                           sun.colour[2] * scale, sun.colour[3] * scale};
    api.set_float4(LC_SUN_BEAMS_COLOUR, c244);

    float centre[3];
    vec_scale_add(centre, sun.direction, kSunGlareDistance, ctx.camera_pos);
    const float half = theme.RecordValue(SUN_GLARE_SIZE)[0] * kSunGlareSizeScale;
    float right_half[3], up_half[3];
    camera_right_up(ctx, half, half, right_half, up_half);
    const float uv_rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    api.draw_billboard(centre, right_half, up_half, uv_rect);
}

void Sky_DrawMoonGlare(RenderApi& api, MoonElement& moon, RenderContext& ctx) {
    const ThemeView& theme = *ctx.theme;

    if (theme.RecordValue(MOON_INTENSITY)[0] < kIntensityGate)
        return;

    moon.glare_texture = theme.TopTextureHandle(TEXSLOT_MOON_GLARE);
    if (moon.glare_texture == 0 || !api.prepare_texture(moon.glare_texture))
        return;

    set_billboard_states(api, false);

    api.select_vertex_shader(VS_CELESTIAL_BILLBOARD);
    api.select_pixel_shader(PS_SUN_GLARE_STREAKS);
    api.bind_texture(moon.glare_texture, 0);

    const float glare_intensity = theme.RecordValue(MOON_GLARE_INTENSITY)[0];
    const float vis2 = moon.visibility * moon.visibility;
    const float scale = glare_intensity * vis2;
    const float c244[4] = {scale, scale, scale, scale};
    api.set_float4(LC_SUN_BEAMS_COLOUR, c244);

    float centre[3];
    vec_scale_add(centre, moon.direction, kMoonGlareDistance, ctx.camera_pos);
    const float half = theme.RecordValue(MOON_GLARE_SIZE)[0] * kMoonGlareSizeScale;
    float right_half[3], up_half[3];
    camera_right_up(ctx, half, half, right_half, up_half);
    const float uv_rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    api.draw_billboard(centre, right_half, up_half, uv_rect);
}

void Sky_DrawMoonOcclusion(RenderApi& api, MoonElement& moon,
                           RenderContext& ctx) {
    const ThemeView& theme = *ctx.theme;

    if (theme.RecordValue(MOON_INTENSITY)[0] < kIntensityGate)
        return;

    moon.moon_texture = theme.TopTextureHandle(TEXSLOT_MOON);
    if (moon.moon_texture == 0 || !api.prepare_texture(moon.moon_texture))
        return;

    set_billboard_states(api, true);

    api.select_vertex_shader(VS_CELESTIAL_BILLBOARD);
    api.select_pixel_shader(PS_MOON_OCCLUSION);
    api.bind_texture(moon.moon_texture, 0);

    const float u0 = static_cast<float>(moon.phase) * kMoonPhaseUStep;
    const float u1 = static_cast<float>(moon.phase + 1) * kMoonPhaseUStep;
    const float uv_rect[4] = {u0, 0.0f, u1, 1.0f};

    const float size = theme.RecordValue(MOON_SIZE)[0];
    std::uint32_t pixels = 0;
    bool issue_query = false;
    if (!api.occlusion_result(moon.occlusion_query, &pixels) ||
        moon.visibility < 0.0f) {
        moon.visibility =
            clamp01(static_cast<float>(pixels) /
                    (size * size * kMoonOcclusionVisibilityDiv));
        api.occlusion_begin(moon.occlusion_query);
        issue_query = true;
    }

    float centre[3];
    vec_scale_add(centre, moon.direction, kMoonBillboardDistance,
                  ctx.camera_pos);
    const float half = size * kMoonOcclusionSizeScale;
    float right_half[3], up_half[3];
    camera_right_up(ctx, half, half, right_half, up_half);
    api.draw_billboard(centre, right_half, up_half, uv_rect);

    if (issue_query)
        api.occlusion_end(moon.occlusion_query);
}

void SkyGlare_RenderCallback(RenderApi& api, SkyPrimitive& prim,
                             RenderContext& ctx) {
    if (ctx.update_mode == 2) {
        Sky_DrawSunGlareStreaks(api, prim.sun, ctx);
        Sky_DrawMoonGlare(api, prim.moon, ctx);
    }
}

void Sky_RenderOcclusionSourcePass(RenderApi& api, SkyPrimitive& prim,
                                   RenderContext& ctx) {

    Sky_DrawMoonOcclusion(api, prim.moon, ctx);
    Sky_DrawSunDisc(api, prim.sun, ctx);

}

void Sky_DrawBgMapDome(RenderApi& api, SkyPrimitive& prim, RenderContext& ctx) {
    api.select_vertex_shader(VS_SKY_DOME_BGMAP);
    api.select_pixel_shader(PS_SKY_DOME_BGMAP);

    Sky_SetupDomeState(api, prim, ctx);

    api.set_float(LC_BGMAP_DOME_PARAM, prim.dome_misc_a);

    api.set_render_state(RS_CULL_MODE, 0);
    api.set_render_state(RS_DEPTH_WRITE, 1);
    api.set_render_state(RS_UNKNOWN_A, 1);
    api.draw_dome_cube(kDomeFaceMaskBgMap);

}

}
