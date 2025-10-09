#pragma once
#include <string>
#include <windows.h>

struct ID3D11Device;

void pick_bnk(const std::string &path);
void refresh_file_table();
void open_folder_logic(const std::string &sel);
void draw_main(HWND hwnd, ID3D11Device* device);