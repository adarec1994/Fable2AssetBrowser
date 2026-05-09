#define IMGUI_DEFINE_MATH_OPERATORS
#include "SplashSparkles.h"
#include "LogoMap.h"
#include <cstdlib>

namespace Splash {

static constexpr int kNumSparkles = 400;
static Sparkle g_sparkles[kNumSparkles];
static bool g_initialized = false;

static void seed_one(Sparkle& s, float logo_x, float logo_y, float scaled_w, float scaled_h) {
    const auto& positions = get_letter_positions();
    if (positions.empty()) {
        s.active = false;
        return;
    }
    int idx = std::rand() % (int)positions.size();
    float norm_x = positions[idx].first;
    float norm_y = positions[idx].second - 0.08f;
    float jitter_x = (((float)std::rand() / RAND_MAX) - 0.5f) * 0.08f;
    float jitter_y = (((float)std::rand() / RAND_MAX) - 0.5f) * 0.06f;
    s.x = logo_x + (norm_x + jitter_x) * scaled_w;
    s.start_y = logo_y + (norm_y + jitter_y) * scaled_h;
    s.y = s.start_y;
    s.texture_index = std::rand() % 8;
    s.life_time = 0.0f;
    s.max_life  = 0.4f + ((float)std::rand() / RAND_MAX) * 0.4f;
    s.active = true;
    s.falls  = (std::rand() % 100) < 70;
}

void reset_sparkles(float logo_x, float logo_y, float scaled_w, float scaled_h) {
    const auto& positions = get_letter_positions();
    if (positions.empty()) return;
    for (int i = 0; i < kNumSparkles; ++i) {
        seed_one(g_sparkles[i], logo_x, logo_y, scaled_w, scaled_h);
        // Spread initial life_time across the full range so they don't all
        // pulse together on the first frame.
        g_sparkles[i].life_time = ((float)std::rand() / RAND_MAX) * g_sparkles[i].max_life;
    }
    g_initialized = true;
}

#ifdef _WIN32
void update_and_draw_sparkles(ImDrawList* draw_list, ID3D11ShaderResourceView** sparkle_textures,
                              float logo_x, float logo_y, float scaled_w, float scaled_h, float dt)
#else
void update_and_draw_sparkles(ImDrawList* draw_list, unsigned int* sparkle_textures,
                              float logo_x, float logo_y, float scaled_w, float scaled_h, float dt)
#endif
{
    if (!g_initialized) {
        reset_sparkles(logo_x, logo_y, scaled_w, scaled_h);
    }

    constexpr float kSparkleSize = 32.0f;
    constexpr float kFallDistance = 30.0f;

    for (int i = 0; i < kNumSparkles; ++i) {
        Sparkle& s = g_sparkles[i];
        if (!s.active) continue;
        s.life_time += dt;
        if (s.life_time >= s.max_life) {
            seed_one(s, logo_x, logo_y, scaled_w, scaled_h);
            s.life_time = 0.0f;
        }

        const float phase = s.life_time / s.max_life;
        if (s.falls) s.y = s.start_y + phase * kFallDistance;
        else         s.y = s.start_y;

        const int tex_idx = s.texture_index;
        if (!sparkle_textures[tex_idx]) continue;

        // Fade in for the first half of life, then fade out.
        float alpha = (phase < 0.5f) ? (phase * 2.0f) : ((1.0f - phase) * 2.0f);
        alpha *= 0.7f;
        const ImU32 col      = IM_COL32(255, 255, 255, (int)(alpha * 255));
        const ImU32 glow_col = IM_COL32(255, 255, 200, (int)(alpha * 100));

        const float half_size = kSparkleSize * 0.5f;
        const float glow_half = kSparkleSize * 0.65f;
        const ImVec2 sp_min(s.x - half_size, s.y - half_size);
        const ImVec2 sp_max(s.x + half_size, s.y + half_size);
        const ImVec2 glow_min(s.x - glow_half, s.y - glow_half);
        const ImVec2 glow_max(s.x + glow_half, s.y + glow_half);

#ifdef _WIN32
        ImTextureID tex = (ImTextureID)sparkle_textures[tex_idx];
#else
        ImTextureID tex = (ImTextureID)(intptr_t)sparkle_textures[tex_idx];
#endif
        draw_list->AddImage(tex, sp_min,   sp_max,   ImVec2(0,0), ImVec2(1,1), col);
        draw_list->AddImage(tex, glow_min, glow_max, ImVec2(0,0), ImVec2(1,1), glow_col);
    }
}

} // namespace Splash
