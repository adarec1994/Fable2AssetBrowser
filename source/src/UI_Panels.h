#pragma once

#ifdef _WIN32
#include <d3d11.h>
void draw_left_panel(ID3D11Device* device);
void draw_right_panel(ID3D11Device* device);
#else
void draw_left_panel();
void draw_right_panel();
#endif

void draw_folder_dialog();