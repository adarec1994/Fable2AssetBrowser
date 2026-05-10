#pragma once

#ifdef _WIN32
struct ID3D11Device;
#endif

namespace UI {

#ifdef _WIN32
void draw_main_layout(ID3D11Device* device);
#else
void draw_main_layout();
#endif

}
