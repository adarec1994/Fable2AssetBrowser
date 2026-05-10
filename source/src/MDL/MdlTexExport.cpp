
#include "MdlTexExport.h"

#include <cstring>
#include <cctype>
#include <vector>

extern "C" {

typedef void stbi_write_func(void* context, void* data, int size);
int stbi_write_png_to_func(stbi_write_func* func, void* context,
                            int w, int h, int comp,
                            const void* data, int stride_in_bytes);
int stbi_write_jpg_to_func(stbi_write_func* func, void* context,
                            int w, int h, int comp,
                            const void* data, int quality);
}

extern bool decode_tex_to_rgba(const std::vector<unsigned char>& blob,
                               std::vector<uint8_t>& rgba,
                               int& out_w, int& out_h, bool* out_has_alpha,
                               int mip_index);

namespace {
void stb_sink(void* ctx, void* data, int size) {
    auto* v = (std::vector<uint8_t>*)ctx;
    auto* p = (const uint8_t*)data;
    v->insert(v->end(), p, p + size);
}
}

namespace MdlTexExport {

namespace {

bool encode_dds_rgba(const std::vector<uint8_t>& rgba, int w, int h,
                     std::vector<uint8_t>& out) {
    if (rgba.size() != (size_t)w * (size_t)h * 4u) return false;

    auto put_u32 = [&](uint32_t v) {
        out.push_back((uint8_t)(v & 0xFF));
        out.push_back((uint8_t)((v >> 8) & 0xFF));
        out.push_back((uint8_t)((v >> 16) & 0xFF));
        out.push_back((uint8_t)((v >> 24) & 0xFF));
    };

    out.clear();
    out.reserve(128 + rgba.size());

    out.push_back('D'); out.push_back('D');
    out.push_back('S'); out.push_back(' ');

    put_u32(124);

    put_u32(0x1u | 0x2u | 0x4u | 0x1000u | 0x8u);
    put_u32((uint32_t)h);
    put_u32((uint32_t)w);
    put_u32((uint32_t)(w * 4));
    put_u32(0);
    put_u32(1);
    for (int i = 0; i < 11; ++i) put_u32(0);

    put_u32(32);
    put_u32(0x40u | 0x1u);
    put_u32(0);
    put_u32(32);
    put_u32(0x000000FFu);
    put_u32(0x0000FF00u);
    put_u32(0x00FF0000u);
    put_u32(0xFF000000u);

    put_u32(0x1000u);
    put_u32(0); put_u32(0); put_u32(0);
    put_u32(0);

    out.insert(out.end(), rgba.begin(), rgba.end());
    return true;
}

}

bool encode_largest_mip(const std::vector<unsigned char>& tex_buf,
                        Format fmt,
                        EncodedTex& out) {
    out.bytes.clear();
    out.width = 0; out.height = 0;
    out.extension = "";
    out.mime_type = "";

    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    bool has_alpha = false;
    if (!decode_tex_to_rgba(tex_buf, rgba, w, h, &has_alpha, /*mip=*/-1) ||
        rgba.empty() || w <= 0 || h <= 0) {
        return false;
    }
    out.width  = w;
    out.height = h;

    switch (fmt) {
        case Format::DDS: {
            if (!encode_dds_rgba(rgba, w, h, out.bytes)) return false;
            out.extension = ".dds";
            out.mime_type = "image/vnd-ms.dds";
            return true;
        }
        case Format::PNG: {
            int rc = stbi_write_png_to_func(
                &stb_sink, &out.bytes, w, h, 4, rgba.data(), w * 4);
            if (!rc) return false;
            out.extension = ".png";
            out.mime_type = "image/png";
            return true;
        }
        case Format::JPG: {

            int rc = stbi_write_jpg_to_func(
                &stb_sink, &out.bytes, w, h, 4, rgba.data(), 92);
            if (!rc) return false;
            out.extension = ".jpg";
            out.mime_type = "image/jpeg";
            return true;
        }
    }
    return false;
}

Format format_from_string(const std::string& s) {
    std::string up = s;
    for (auto& c : up) c = (char)std::toupper((unsigned char)c);
    if (up == "DDS") return Format::DDS;
    if (up == "PNG") return Format::PNG;
    if (up == "JPG" || up == "JPEG") return Format::JPG;
    return Format::DDS;
}

const char* string_from_format(Format f) {
    switch (f) {
        case Format::DDS: return "DDS";
        case Format::PNG: return "PNG";
        case Format::JPG: return "JPG";
    }
    return "DDS";
}

}
