// JPG writer — stb_image_write. JPG can't carry alpha, so the writer
// reads only the RGB triplet from each pixel and ignores the A byte.
// stbi_write_jpg with comp=4 actually does this internally; passing
// comp=3 would require a separate RGB-only buffer.

#include "TextureExport.h"
#include "stb_image_write.h"  // implementation lives in PngExport.cpp

bool tex_export_jpg(const std::string& path, const uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return false;
    // Quality 90 — visually lossless on photographic textures, ~1/10
    // the size of PNG. The user wants a quick preview-friendly export
    // here, not a pristine archival copy (use PNG/TIFF for that).
    return stbi_write_jpg(path.c_str(), w, h, /*comp=*/4, rgba, /*quality=*/90) != 0;
}
