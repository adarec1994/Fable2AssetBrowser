
#pragma once

#include "imgui.h"

#ifdef _WIN32
struct ID3D11ShaderResourceView;
#endif

namespace Splash {

struct Sparkle {
    float x = 0, y = 0;
    float start_y = 0;
    int   texture_index = 0;
    float life_time = 0;
    float max_life  = 0;
    bool  active = false;
    bool  falls  = false;
};

void reset_sparkles(float logo_x, float logo_y, float scaled_w, float scaled_h);

#ifdef _WIN32
void update_and_draw_sparkles(ImDrawList* draw_list, ID3D11ShaderResourceView** sparkle_textures,
                              float logo_x, float logo_y, float scaled_w, float scaled_h, float dt);
#else
void update_and_draw_sparkles(ImDrawList* draw_list, unsigned int* sparkle_textures,
                              float logo_x, float logo_y, float scaled_w, float scaled_h, float dt);
#endif

}
