#pragma once
#include <d3d11.h>
#include <vector>
#include <string>
#include <cstdint>

struct ADBEntry {
    std::string name;
    std::vector<uint8_t> data;
};

std::vector<ADBEntry> decompress_adb(const std::string& path);
void open_hex_for_selected();
void draw_hex_window(ID3D11Device *device);