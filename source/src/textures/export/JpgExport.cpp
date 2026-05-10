
#include "TextureExport.h"
#include "stb_image_write.h"

bool tex_export_jpg(const std::string& path, const uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return false;

    return stbi_write_jpg(path.c_str(), w, h, /*comp=*/4, rgba, /*quality=*/90) != 0;
}
