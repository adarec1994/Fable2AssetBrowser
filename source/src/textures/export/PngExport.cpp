// PNG writer — single-image RGBA8 via stb_image_write. We define
// STB_IMAGE_WRITE_IMPLEMENTATION here (and ONLY here) so the symbols
// are emitted exactly once across the whole exe. JpgExport.cpp uses
// the same library but only #includes the header, no implementation.

#include "TextureExport.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

bool tex_export_png(const std::string& path, const uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return false;
    // stride_in_bytes = w * 4 — top-down rows, RGBA component order.
    return stbi_write_png(path.c_str(), w, h, /*comp=*/4, rgba, w * 4) != 0;
}
