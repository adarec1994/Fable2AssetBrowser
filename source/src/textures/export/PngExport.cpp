
#include "TextureExport.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

bool tex_export_png(const std::string& path, const uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return false;

    return stbi_write_png(path.c_str(), w, h, 4, rgba, w * 4) != 0;
}
