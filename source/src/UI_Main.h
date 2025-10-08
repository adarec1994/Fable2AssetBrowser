#pragma once
#include <string>
#include <windows.h>

void pick_bnk(const std::string &path);
void refresh_file_table();
void open_folder_logic(const std::string &sel);
void draw_main(HWND hwnd);