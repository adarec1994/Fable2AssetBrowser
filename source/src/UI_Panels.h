#pragma once
#include <string>

#ifdef _WIN32
#include <d3d11.h>
void draw_left_panel(ID3D11Device* device);
void draw_right_panel(ID3D11Device* device);
#else
void draw_left_panel();
void draw_right_panel();
#endif

void draw_folder_dialog();
void draw_file_table();
void draw_global_results_table();
void refresh_file_table();
void pick_bnk(const std::string &path);
void open_folder_logic(const std::string &sel);
