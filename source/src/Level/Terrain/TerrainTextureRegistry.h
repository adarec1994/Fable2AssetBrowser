#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace TerrainTextureRegistry {

struct Entry {
    std::vector<uint8_t> rgba;
    int                  width  = 0;
    int                  height = 0;
};

void Register(const std::string&        name,
              std::vector<uint8_t>      rgba,
              int                       width,
              int                       height);

const Entry* Find(const std::string& name);

void Clear();

struct LodPaletteEntry {
    std::string base_diffuse;
    std::string base_normal;
    std::string detail_diffuse;
    std::string detail_normal;
    float       base_tile_scale = 0.125f;
    float       detail_tile_scale = 0.125f;
    float       base_intensity = 1.0f;
    float       detail_intensity = 1.0f;
};

void SetLodPalette(std::vector<LodPaletteEntry> entries);

const std::vector<LodPaletteEntry>& GetLodPalette();

}
