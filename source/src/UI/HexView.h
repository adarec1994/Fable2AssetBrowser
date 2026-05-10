#pragma once
#include <vector>
#include <string>
#include <cstdint>

#ifdef _WIN32
#include <d3d11.h>
#endif

struct ADBEntry {
    std::string name;
    std::vector<uint8_t> data;
};

std::vector<ADBEntry> decompress_adb(const std::string& path);
void open_hex_for_selected();

#ifdef _WIN32
void draw_hex_window(ID3D11Device *device);
#else
void draw_hex_window();
#endif
