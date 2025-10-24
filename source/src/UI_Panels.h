#pragma once

struct ID3D11Device;

void draw_left_panel(ID3D11Device* device);
void draw_right_panel(ID3D11Device* device);
void draw_file_table();
void draw_folder_dialog();