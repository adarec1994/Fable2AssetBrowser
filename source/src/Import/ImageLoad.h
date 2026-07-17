#pragma once




#include <cstdint>
#include <string>
#include <vector>

namespace ImageLoad {

struct Image {
    std::vector<uint8_t> rgba;
    int width  = 0;
    int height = 0;
    bool has_alpha = false;
};


bool load_memory(const uint8_t* bytes, size_t size,
                 const std::string& name_hint, Image& out, std::string& err);

bool load_file(const std::string& path, Image& out, std::string& err);


bool extension_supported(const std::string& path);

}
