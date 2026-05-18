#pragma once

#ifdef _WIN32
struct ID3D11Device;
#endif

namespace UI {

#ifdef _WIN32
void draw_main_layout(ID3D11Device* device, float bottom_overlay = 0.0f);
#else
void draw_main_layout(float bottom_overlay = 0.0f);
#endif

}
