#pragma once

#include <cstdint>
#include <string>
#include <vector>


namespace EhfPalette {

struct Entry {
    std::string diffuse_path;   
    std::string normal_path;
    float       tile_scale = 0.125f;  
    float       intensity  = 1.0f;
};

struct Palette {
    bool                 ok = false;
    std::vector<Entry>   entries;
    size_t               palette_offset = 0;  
};

Palette Parse(const std::vector<uint8_t>& ehf);

}  
