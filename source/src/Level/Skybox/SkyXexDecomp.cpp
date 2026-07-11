#include "SkyXexDecomp.h"

// 1:1 ports of the default.xex SKY primitive render path.  Each function body
// follows the original instruction flow; the repetitive inlined render-state
// dedup blocks (compare-cache around dword_834B8090 / dword_83490BF0) are
// expressed as api.set_render_state calls carrying the same values, in the
// same order, with the original cache-block address noted.
//
// Values named dword_83316Exx are entries of a global blend/cull mode table
// the XEX resolves at boot; the sky path uses:
//   0x83316EB0 -> src blend for premultiplied additive (glares, stars)
//   0x83316EBC / 0x83316EC0 -> src/dst blend for alpha blend (moon, discs)
//   0x83316EE8 -> cull value used by the bg-map dome pass
//   0x83316EF8 / 0x83316EFC -> cull modes (dome vs billboards)

namespace SkyXex {

// 0x83317EA0: 8 x float4 unit-cube corners (w = 1).
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

// 0x83317E38: 8 groups of 6 indices; groups 6/7 are zero padding.  Group g is
// emitted when bit (1 << g) is set in the face mask (sub_821D71E8).
const std::array<std::uint16_t, 48> kDomeCubeIndices = {{
    0, 1, 2, 3, 2, 1,  // face 0  (z = 0)
    2, 3, 6, 7, 6, 3,  // face 1  (y = 1)
    6, 7, 4, 5, 4, 7,  // face 2  (z = 1) -- skipped by the bg-map pass
    2, 6, 0, 4, 0, 6,  // face 3  (x = 0)
    0, 4, 1, 5, 1, 4,  // face 4  (y = 0)
    1, 5, 3, 7, 3, 5,  // face 5  (x = 1)
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
}};

namespace {

inline void vec_scale_add(float out[3], const float dir[4], float scale,
                          const float base[4]) {
    // vmaddfp v_out = dir * splat(scale) + base
    out[0] = dir[0] * scale + base[0];
    out[1] = dir[1] * scale + base[1];
    out[2] = dir[2] * scale + base[2];
}

// The four billboard element draws all pass axis-aligned half-extent vectors
// derived from the camera basis; sub_82A5EFD8 extracts right/up from the
// transposed view matrix at ctx+4 -> +336 (sub_82270C80 transpose).
inline void camera_right_up(const RenderContext& ctx, float size_x,
                            float size_y, float right_half[3],
                            float up_half[3]) {
    // rows of the transposed view matrix = world-space camera basis vectors
    right_half[0] = ctx.view_matrix[0] * size_x;
    right_half[1] = ctx.view_matrix[4] * size_x;
    right_half[2] = ctx.view_matrix[8] * size_x;
    up_half[0] = ctx.view_matrix[1] * size_y;
    up_half[1] = ctx.view_matrix[5] * size_y;
    up_half[2] = ctx.view_matrix[9] * size_y;
}

inline float clamp01(float v) {
    // fsel-based clamp at 0x821D8xxx / 0x82279xxx (unk_82100170 = {1.0, 0.0})
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

// Common state block shared by the alpha-blended celestial billboards
// (moon 0x821D3928, sun disc 0x821D8310, glares 0x8227DBA0 / 0x821D5478):
//   cull = 0x83316EFC value      (0x834BC118 block)
//   depth write = off            (0x834BC0D0 block)
//   alpha blend = on             (0x834BC148 block)
//   alpha test = on/off per fn   (0x834BC160 block)
//   src blend = table value      (0x834BC190 block)
//   dst blend = table value      (0x834BC1A8 block)
//   depth test = on              (0x834BC0E8 block)
inline void set_billboard_states(RenderApi& api, bool alpha_test) {
    api.set_render_state(RS_CULL_MODE, 1);         // dword_83316EFC
    api.set_render_state(RS_DEPTH_WRITE, 0);       // 0x834BC0D0
    api.set_render_state(RS_ALPHA_BLEND_ENABLE, 1);// 0x834BC148
    api.set_render_state(RS_ALPHA_TEST_ENABLE, alpha_test ? 1u : 0u);
    api.set_render_state(RS_SRC_BLEND, 1);         // dword_83316EBC
    api.set_render_state(RS_DST_BLEND, 1);         // dword_83316EC0
    api.set_render_state(RS_DEPTH_TEST, 1);        // 0x834BC0E8
}

}  // namespace

// ---------------------------------------------------------------------------
// SkyPrimitive_RenderCallback @ 0x8228EBA8
// ---------------------------------------------------------------------------
void SkyPrimitive_RenderCallback(RenderApi& api, SkyPrimitive& prim,
                                 RenderContext& ctx) {
    // ctx+1798: cloud-less path -- dome only.
    if (ctx.skip_clouds) {
        Sky_DrawAtmosphereDome(api, prim, ctx);
        return;
    }

    bool full_refresh = false;
    if (ctx.update_mode == 2) {  // ctx+1788
        // sub_82295E50: find the mist volume containing ctx+1680 (cam pos);
        // result is forwarded to the dome pass for bg-map binding.
        // (host resolves ctx.mist_volume before calling)
        full_refresh = true;
        Sky_DrawAtmosphereDome(api, prim, ctx);
        Sky_DrawMoon(api, prim.moon, ctx);      // prim+208
        Sky_DrawStars(api, prim, ctx);
        // CloudLayer_UpdateFromTheme @ 0x822675D0 for layers 0..3
        // (already ported in CloudRuntime; not repeated here).
    } else {
        Sky_DrawAtmosphereDome(api, prim, ctx);
    }
    (void)full_refresh;
    // CloudLayers_UpdateSortDraw @ 0x82267818 follows in both paths.
}

// ---------------------------------------------------------------------------
// sub_821DB590 @ 0x821DB590 -- dome texture / constant state.
// ---------------------------------------------------------------------------
void Sky_SetupDomeState(RenderApi& api, SkyPrimitive& prim,
                        RenderContext& ctx) {
    // ThemeRecord_ReadPairs(theme+16, slot SKY_OVERLAY, out, 2): the top two
    // (weight, texture) blend entries of texture record 1.
    float pairs[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    ctx.theme->ReadTexturePairs(TEXSLOT_SKY_OVERLAY, pairs, 2);

    // prim+272 / prim+276: ref-counted texture assigns (0x8227FF40), then
    // sampler setup (sub_821C3098) for both.
    prim.overlay_texture_a = static_cast<std::uint32_t>(pairs[1]);
    prim.overlay_texture_b = static_cast<std::uint32_t>(pairs[3]);
    api.bind_texture(prim.overlay_texture_a, 0);  // binding table +2676
    api.bind_texture(prim.overlay_texture_b, 1);  // binding table +2688

    // Logical constant 225 <- weight of the second overlay entry (pairs[2]):
    // the cross-fade factor while a theme transition is in flight.
    prim.overlay_blend = pairs[2];
    api.set_float(LC_SKY_OVERLAY_BLEND, pairs[2]);

    // sub_822150E0(1, 1, ...) -- sampler bank commit (host-side).

    // ctx+1802 gates the background-map binding; inside a mist volume the
    // prebaked backdrop replaces the far scene behind the dome.
    if (!ctx.no_background_map) {
        if (ctx.mist_volume != nullptr) {
            api.bind_background_map(ctx.mist_volume, &ctx);
        } else {
            api.clear_background_map();
        }
    }

    api.set_float(LC_DOME_MISC_B, prim.dome_misc_a);  // 266 <- prim+76
    api.set_float(LC_DOME_MISC_A, prim.dome_misc_b);  // 265 <- prim+80
    // Tail: publishes the overlay blend vector into the deferred constant
    // scratch at dword_8336019C+6320 when flag bit 5 of qword_83491590 is set
    // (deferred-context replay); host-side concern.
}

// ---------------------------------------------------------------------------
// sub_8229B1E0 @ 0x8229B1E0 -- atmosphere dome draw.
// ---------------------------------------------------------------------------
void Sky_DrawAtmosphereDome(RenderApi& api, SkyPrimitive& prim,
                            RenderContext& ctx) {
    // State: cull block 0x834BC0D0 set from scope entry; depth-test config for
    // background depth (0x834BC0D0/0x834BC0E4 pair at function head).
    api.set_render_state(RS_DEPTH_WRITE, 0);

    // sub_82B16690(ctx): per-pass camera constant upload (host-side).
    api.select_vertex_shader(VS_SKY_DOME);   // 132
    api.select_pixel_shader(PS_SKY_DOME);    // 133

    Sky_SetupDomeState(api, prim, ctx);

    // sub_821A58C0(prim, ctx, 63): logical constant 405 <- (0, 1, 0, ...)
    // default tint, then emit all six cube faces.
    const float tint[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    api.set_float4(LC_BILLBOARD_TINT, tint);
    api.set_render_state(RS_CULL_MODE, 1);  // 0x834BC118 <- dword_83316EFC
    api.draw_dome_cube(kDomeFaceMaskMain);  // sub_821D71E8(63)
}

// ---------------------------------------------------------------------------
// sub_821D3928 @ 0x821D3928 -- moon billboard, main view.
// ---------------------------------------------------------------------------
void Sky_DrawMoon(RenderApi& api, MoonElement& moon, RenderContext& ctx) {
    const ThemeView& theme = *ctx.theme;

    // Gate: MOON_INTENSITY (record 65, theme+1056) >= 1e-4.
    const float intensity = theme.RecordValue(MOON_INTENSITY)[0];
    if (intensity < kIntensityGate)
        return;

    // Texture: top blend entry of slot 6 (SKY_MOONTEXTURE) -> prim+28,
    // RenderResource_Prepare; bail while mips not resident (+84==0x7FFFFFFF).
    moon.moon_texture = theme.TopTextureHandle(TEXSLOT_MOON);
    if (moon.moon_texture == 0 || !api.prepare_texture(moon.moon_texture))
        return;

    set_billboard_states(api, /*alpha_test=*/false);

    api.select_vertex_shader(VS_CELESTIAL_BILLBOARD);  // 139
    api.select_pixel_shader(PS_MOON);                  // 143
    api.bind_texture(moon.moon_texture, 0);            // binding +1776

    // Logical constant 147: (intensity, transparency-derived pack).
    // v = (0.5, 0.5, ...) * splat(MOON_TRANSPARENCY record 69, theme+1104)
    // merged with the intensity vector (theme+1056).
    const float transparency = theme.RecordValue(MOON_TRANSPARENCY)[0];
    const float c147[4] = {0.5f * transparency, 0.5f * transparency,
                           intensity, intensity};
    api.set_float4(LC_MOON_PARAMS, c147);

    // Phase strip UVs: u0 = phase/8, u1 = (phase+1)/8 (prim+20, 0..7).
    const float u0 = static_cast<float>(moon.phase) * kMoonPhaseUStep;
    const float u1 = static_cast<float>(moon.phase + 1) * kMoonPhaseUStep;
    const float uv_rect[4] = {u0, 0.0f, u1, 1.0f};

    // centre = camPos (ctx+1680) + moonDir * 4000 (0x82000C24);
    // half size = MOON_SIZE (record 66, theme+1056+16) * 300 (0x8209BE64).
    float centre[3];
    vec_scale_add(centre, moon.direction, kMoonBillboardDistance,
                  ctx.camera_pos);
    const float half = theme.RecordValue(MOON_SIZE)[0] * kMoonSizeScale;
    float right_half[3], up_half[3];
    camera_right_up(ctx, half, half, right_half, up_half);

    api.draw_billboard(centre, right_half, up_half, uv_rect);  // 0x82A5EFD8
}

// ---------------------------------------------------------------------------
// sub_821C4FB8 @ 0x821C4FB8 -- 512-point starfield.
// ---------------------------------------------------------------------------
void Sky_DrawStars(RenderApi& api, SkyPrimitive& prim, RenderContext& ctx) {
    (void)prim;
    const ThemeView& theme = *ctx.theme;

    // Gate: STARS_BRIGHTNESS (record 70, theme+1136) >= 1e-4.
    const float brightness = theme.RecordValue(STARS_BRIGHTNESS)[0];
    if (brightness < kIntensityGate)
        return;

    // Function head bulk-invalidates every state cache page (loop over
    // dword_834BA094..dword_834BE0A4) -- full state reset before points.
    api.select_vertex_shader(VS_STARS);  // 150
    api.select_pixel_shader(PS_STARS);   // 151

    // Logical constant 390: (1.5, 2.5, globalTime, brightness).
    // dbl_83496C48 is the engine clock -> twinkle phase in the VS.
    const float time = static_cast<float>(api.global_time());
    const float c390[4] = {1.5f, 2.5f, time, brightness};
    api.set_float4(LC_STARS_PARAMS, c390);

    // Logical VS constant 291 <- frame vector at *(ctx+4)+80 (sub_821C56C0
    // inline setter, z zeroed).  Star dome orientation.
    // Host supplies it from the frame data.

    // States (order as in the XEX):
    api.set_render_state(RS_CULL_MODE, 1);          // 0x834BC118
    api.set_render_state(RS_ALPHA_TEST_ENABLE, 0);  // 0x834BC160
    api.set_render_state(RS_ALPHA_BLEND_ENABLE, 1); // 0x834BC148
    api.set_render_state(RS_SRC_BLEND, 2);          // 0x834BC190 <- 0x83316EB0
    api.set_render_state(RS_DST_BLEND, 2);          // 0x834BC1A8 <- 0x83316EB0
    api.set_render_state(RS_POINT_SIZE_MIN, 0x3DCCCCCD);  // 0.1f, 0x834BC220
    api.set_render_state(RS_POINT_SIZE_MAX, 0x42800000);  // 64.0f, 0x834BC238
    api.set_render_state(RS_DEPTH_TEST, 1);         // 0x834BC0E8
    api.set_render_state(RS_DEPTH_WRITE, 0);        // 0x834BC0D0

    // sub_8222C788(0, 512): DrawPrimitive(POINTLIST, 0, 512); positions are
    // synthesised from the vertex index in VS 150.
    api.draw_points(0, kStarCount);
}

// ---------------------------------------------------------------------------
// sub_821D8310 @ 0x821D8310 -- sun disc with occlusion query.
// ---------------------------------------------------------------------------
void Sky_DrawSunDisc(RenderApi& api, SunElement& sun, RenderContext& ctx) {
    const ThemeView& theme = *ctx.theme;

    // Gate: SUN_INTENSITY (record 7, theme+128 == records+112) >= 1e-4.
    if (theme.RecordValue(SUN_INTENSITY)[0] < kIntensityGate)
        return;

    // Texture slot 3 (SKY_SUNDISC) -> prim+48; mip selected from texture
    // width/256 (RenderResource_Prepare mip-bias arg in the XEX).
    sun.disc_texture = theme.TopTextureHandle(TEXSLOT_SUN_DISC);
    if (sun.disc_texture == 0 || !api.prepare_texture(sun.disc_texture))
        return;

    set_billboard_states(api, /*alpha_test=*/true);  // 0x834BC160 <- 1 here

    api.select_vertex_shader(VS_CELESTIAL_BILLBOARD);  // 139
    api.select_pixel_shader(PS_SUN_DISC);              // 141
    api.bind_texture(sun.disc_texture, 0);             // binding +2952

    // Logical constant 245: SUN_DISC_COLOUR (record 93, records+1488)
    //   * SUN_DISC_INTENSITY (record 94, records+1504) -- packed with the
    //   size vector (record 92, records+1472) in the XEX vperm sequence.
    const float* colour = theme.RecordValue(SUN_DISC_COLOUR);
    const float disc_intensity = theme.RecordValue(SUN_DISC_INTENSITY)[0];
    const float c245[4] = {colour[0] * disc_intensity,
                           colour[1] * disc_intensity,
                           colour[2] * disc_intensity, 1.0f};
    api.set_float4(LC_SUN_DISC_COLOUR, c245);

    // Occlusion query bookkeeping: poll last frame's result BEFORE drawing.
    // visibility = pixels / (SUN_DISC_SIZE^2 * 4000), clamped to [0,1]
    // (prim+36); a fresh query is begun only when the old one has completed
    // (or visibility was reset negative).
    const float size = theme.RecordValue(SUN_DISC_SIZE)[0];
    std::uint32_t pixels = 0;
    bool issue_query = false;
    if (!api.occlusion_result(sun.occlusion_query, &pixels) ||
        sun.visibility < 0.0f) {
        sun.visibility =
            clamp01(static_cast<float>(pixels) /
                    (size * size * kSunDiscVisibilityDiv));
        api.occlusion_begin(sun.occlusion_query);  // sub_822195E8(q, 2)
        issue_query = true;
    }

    // centre = camPos + sunDir (prim+16) * 1900; half = size * 300.
    float centre[3];
    vec_scale_add(centre, sun.direction, kSunDiscDistance, ctx.camera_pos);
    const float half = size * kSunDiscSizeScale;
    float right_half[3], up_half[3];
    camera_right_up(ctx, half, half, right_half, up_half);
    const float uv_rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    api.draw_billboard(centre, right_half, up_half, uv_rect);

    if (issue_query)
        api.occlusion_end(sun.occlusion_query);  // sub_822195E8(q, 1)
}

// ---------------------------------------------------------------------------
// sub_821D4028 @ 0x821D4028 -- sun beams (godrays) billboard.
// ---------------------------------------------------------------------------
void Sky_DrawSunBeams(RenderApi& api, SunElement& sun, RenderContext& ctx) {
    const ThemeView& theme = *ctx.theme;

    if (theme.RecordValue(SUN_INTENSITY)[0] < kIntensityGate)
        return;

    // Texture slot 2 (SKY_SUNBEAMS) -> prim+44.
    sun.beams_texture = theme.TopTextureHandle(TEXSLOT_SUN_BEAMS);
    if (sun.beams_texture == 0 || !api.prepare_texture(sun.beams_texture))
        return;

    api.set_render_state(RS_CULL_MODE, 1);   // 0x834BC118
    api.set_render_state(RS_DEPTH_WRITE, 0); // 0x834BC0D0

    api.select_vertex_shader(VS_CELESTIAL_BILLBOARD);  // 139
    api.select_pixel_shader(PS_SUN_BEAMS);             // 145
    api.bind_texture(sun.beams_texture, 0);            // binding +2964

    // Logical constant 244: prim colour (prim+0) * SUN_BEAMS_INTENSITY
    // (record 97, records+1552).
    const float beams_intensity = theme.RecordValue(SUN_BEAMS_INTENSITY)[0];
    const float c244[4] = {sun.colour[0] * beams_intensity,
                           sun.colour[1] * beams_intensity,
                           sun.colour[2] * beams_intensity,
                           sun.colour[3] * beams_intensity};
    api.set_float4(LC_SUN_BEAMS_COLOUR, c244);

    // centre = camPos + sunDir * 1900; half extents =
    //   SUN_BEAMS_WIDTH (record 95, records+1520) * 600 horizontally,
    //   SUN_BEAMS_HEIGHT (record 96, records+1536) * 600 vertically.
    float centre[3];
    vec_scale_add(centre, sun.direction, kSunBeamsDistance, ctx.camera_pos);
    const float half_w = theme.RecordValue(SUN_BEAMS_WIDTH)[0] * kSunBeamsSizeScale;
    const float half_h = theme.RecordValue(SUN_BEAMS_HEIGHT)[0] * kSunBeamsSizeScale;
    float right_half[3], up_half[3];
    camera_right_up(ctx, half_w, half_h, right_half, up_half);
    const float uv_rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    api.draw_billboard(centre, right_half, up_half, uv_rect);
}

// ---------------------------------------------------------------------------
// sub_8227DBA0 @ 0x8227DBA0 -- sun glare + streaks billboard.
// ---------------------------------------------------------------------------
void Sky_DrawSunGlareStreaks(RenderApi& api, SunElement& sun,
                             RenderContext& ctx) {
    const ThemeView& theme = *ctx.theme;

    if (theme.RecordValue(SUN_INTENSITY)[0] < kIntensityGate)
        return;

    // Textures: slot 5 (glare) -> prim+52, slot 4 (streaks) -> prim+56.
    sun.glare_texture = theme.TopTextureHandle(TEXSLOT_SUN_GLARE);
    sun.streaks_texture = theme.TopTextureHandle(TEXSLOT_SUN_STREAKS);
    if (sun.glare_texture == 0)
        return;

    // facing = dot(sunDir (prim+16), viewFwd (ctx+1696)); cull when the sun
    // is behind the camera.
    const float facing = sun.direction[0] * ctx.view_forward[0] +
                         sun.direction[1] * ctx.view_forward[1] +
                         sun.direction[2] * ctx.view_forward[2];
    if (facing < 0.0f)
        return;

    if (!api.prepare_texture(sun.glare_texture))
        return;

    set_billboard_states(api, /*alpha_test=*/false);

    api.select_vertex_shader(VS_CELESTIAL_BILLBOARD);  // 139
    api.select_pixel_shader(PS_SUN_GLARE_STREAKS);     // 140
    api.bind_texture(sun.glare_texture, 0);            // binding +2964
    api.bind_texture(sun.streaks_texture, 1);          // binding +2976

    // Logical constant 244: prim colour * SUN_GLARE_INTENSITY (record 100,
    // records+1600) * facing^2 * visibility^2 (prim+36 from the disc query).
    const float glare_intensity = theme.RecordValue(SUN_GLARE_INTENSITY)[0];
    const float scale = glare_intensity * (facing * facing) *
                        (sun.visibility * sun.visibility);
    const float c244[4] = {sun.colour[0] * scale, sun.colour[1] * scale,
                           sun.colour[2] * scale, sun.colour[3] * scale};
    api.set_float4(LC_SUN_BEAMS_COLOUR, c244);

    // centre = camPos + sunDir * 1999; half = SUN_GLARE_SIZE (record 101,
    // records+1616) * 2000.
    float centre[3];
    vec_scale_add(centre, sun.direction, kSunGlareDistance, ctx.camera_pos);
    const float half = theme.RecordValue(SUN_GLARE_SIZE)[0] * kSunGlareSizeScale;
    float right_half[3], up_half[3];
    camera_right_up(ctx, half, half, right_half, up_half);
    const float uv_rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    api.draw_billboard(centre, right_half, up_half, uv_rect);
}

// ---------------------------------------------------------------------------
// sub_821D5478 @ 0x821D5478 -- moon glare billboard.
// ---------------------------------------------------------------------------
void Sky_DrawMoonGlare(RenderApi& api, MoonElement& moon, RenderContext& ctx) {
    const ThemeView& theme = *ctx.theme;

    // Gate >= 1e-4 (0x82099AE0) on MOON_INTENSITY.
    if (theme.RecordValue(MOON_INTENSITY)[0] < kIntensityGate)
        return;

    // Texture slot 7 (SKY_MOONGLARETEXTURE).
    moon.glare_texture = theme.TopTextureHandle(TEXSLOT_MOON_GLARE);
    if (moon.glare_texture == 0 || !api.prepare_texture(moon.glare_texture))
        return;

    set_billboard_states(api, /*alpha_test=*/false);

    api.select_vertex_shader(VS_CELESTIAL_BILLBOARD);  // 139
    api.select_pixel_shader(PS_SUN_GLARE_STREAKS);     // 140 (shared)
    api.bind_texture(moon.glare_texture, 0);

    // Constant 244: MOON_GLARE_INTENSITY (record 67) scaled by the moon
    // visibility^2 (prim+16, from Sky_DrawMoonOcclusion) with the 0.025
    // (0x83320DDC) floor factor from the XEX tail.
    const float glare_intensity = theme.RecordValue(MOON_GLARE_INTENSITY)[0];
    const float vis2 = moon.visibility * moon.visibility;
    const float scale = glare_intensity * vis2;
    const float c244[4] = {scale, scale, scale, scale};
    api.set_float4(LC_SUN_BEAMS_COLOUR, c244);

    // centre = camPos + moonDir * 1000 (0x8209BAAC);
    // half = MOON_GLARE_SIZE (record 68, records+1088) * 300 (0x8209BE64).
    float centre[3];
    vec_scale_add(centre, moon.direction, kMoonGlareDistance, ctx.camera_pos);
    const float half = theme.RecordValue(MOON_GLARE_SIZE)[0] * kMoonGlareSizeScale;
    float right_half[3], up_half[3];
    camera_right_up(ctx, half, half, right_half, up_half);
    const float uv_rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    api.draw_billboard(centre, right_half, up_half, uv_rect);
}

// ---------------------------------------------------------------------------
// sub_822792D0 @ 0x822792D0 -- moon disc into the occlusion source pass.
// ---------------------------------------------------------------------------
void Sky_DrawMoonOcclusion(RenderApi& api, MoonElement& moon,
                           RenderContext& ctx) {
    const ThemeView& theme = *ctx.theme;

    // Gate: MOON_INTENSITY (records+1040) >= 1e-4.
    if (theme.RecordValue(MOON_INTENSITY)[0] < kIntensityGate)
        return;

    moon.moon_texture = theme.TopTextureHandle(TEXSLOT_MOON);
    if (moon.moon_texture == 0 || !api.prepare_texture(moon.moon_texture))
        return;

    set_billboard_states(api, /*alpha_test=*/true);

    api.select_vertex_shader(VS_CELESTIAL_BILLBOARD);  // 139
    api.select_pixel_shader(PS_MOON_OCCLUSION);        // 144
    api.bind_texture(moon.moon_texture, 0);

    // Phase UVs as in the main view draw.
    const float u0 = static_cast<float>(moon.phase) * kMoonPhaseUStep;
    const float u1 = static_cast<float>(moon.phase + 1) * kMoonPhaseUStep;
    const float uv_rect[4] = {u0, 0.0f, u1, 1.0f};

    // Query poll: visibility (prim+16) = pixels / (MOON_SIZE^2 * 6000
    // (0x82000C20)), clamped; query object at prim+24.
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

    // centre = camPos + moonDir * 4000; half = MOON_SIZE * 400 (0x8209BE48)
    // -- slightly larger than the visible moon so the query is conservative.
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

// ---------------------------------------------------------------------------
// sub_822A0AC0 @ 0x822A0AC0 -- post-cloud glare render callback.
// ---------------------------------------------------------------------------
void SkyGlare_RenderCallback(RenderApi& api, SkyPrimitive& prim,
                             RenderContext& ctx) {
    if (ctx.update_mode == 2) {  // ctx+1788
        Sky_DrawSunGlareStreaks(api, prim.sun, ctx);  // prim+144
        Sky_DrawMoonGlare(api, prim.moon, ctx);       // prim+208
    }
}

// ---------------------------------------------------------------------------
// sub_82277240 @ 0x82277240 -- half-res occlusion source pass.
// ---------------------------------------------------------------------------
void Sky_RenderOcclusionSourcePass(RenderApi& api, SkyPrimitive& prim,
                                   RenderContext& ctx) {
    // XEX body: acquire a (displayW/2, displayH/2) render target
    // (sub_821FBC60, formats 15/15), set viewport, clear-state, then:
    Sky_DrawMoonOcclusion(api, prim.moon, ctx);  // prim+208
    Sky_DrawSunDisc(api, prim.sun, ctx);         // prim+144
    // then resolve the RT to a texture (sub_822069C0) and release
    // (sub_82237060 / sub_821FBE60).  The resolve feeds nothing directly;
    // the value of this pass is the two occlusion queries above.  Callers:
    // SkyCloud_DepthRepopulate @ 0x821CB3A0, which also invokes
    // Sky_DrawSunBeams first.
}

// ---------------------------------------------------------------------------
// sub_821D1450 @ 0x821D1450 -- background-map / lighting-gen dome pass.
// ---------------------------------------------------------------------------
void Sky_DrawBgMapDome(RenderApi& api, SkyPrimitive& prim, RenderContext& ctx) {
    api.select_vertex_shader(VS_SKY_DOME_BGMAP);  // 153
    api.select_pixel_shader(PS_SKY_DOME_BGMAP);   // 154

    // sub_821BA418 / sub_821BA578: bind water env maps for the generated
    // backdrop (texture record 12, WATER_NORMAL_MAP path); host-side.

    // Mist volume for the pass origin (ctx+1680), then the shared dome state.
    Sky_SetupDomeState(api, prim, ctx);

    // Logical constant 412 <- prim+76 scalar.
    api.set_float(LC_BGMAP_DOME_PARAM, prim.dome_misc_a);

    // sub_821E4848(prim+80, records) + sub_822B9A90(prim+80, time): cloud
    // scroll UV update for the baked backdrop (CloudRuntime handles the
    // equivalent in the preview).

    // Full state-cache invalidation, cull from 0x83316EF8, depth-write on
    // (0x834BC0D0 <- 1), extra state 0x834BC0A0 <- 0x83316EE8, then the cube
    // without face 2:
    api.set_render_state(RS_CULL_MODE, 0);   // dword_83316EF8 variant
    api.set_render_state(RS_DEPTH_WRITE, 1);
    api.set_render_state(RS_UNKNOWN_A, 1);   // 0x834BC0A0 <- dword_83316EE8
    api.draw_dome_cube(kDomeFaceMaskBgMap);  // sub_821D71E8(59)
    // Tail: --byte_83496CCE (pending bg-map generation counter).
}

}  // namespace SkyXex
