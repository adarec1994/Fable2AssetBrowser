// MdlTexExport — implementation. See header for design notes.

#include "MdlTexExport.h"

#include <cstring>
#include <cctype>
#include <vector>

// stb_image_write: handles PNG + JPG encode-to-memory via callback
// functions. We wire a thin sink that appends to a std::vector. The
// implementation lives in UI_Main.cpp via STB_IMAGE_WRITE_IMPLEMENTATION
// — no double definition needed here.
extern "C" {
// stb_image_write does NOT auto-include the impl in non-impl TUs;
// the prototypes we need:
typedef void stbi_write_func(void* context, void* data, int size);
int stbi_write_png_to_func(stbi_write_func* func, void* context,
                            int w, int h, int comp,
                            const void* data, int stride_in_bytes);
int stbi_write_jpg_to_func(stbi_write_func* func, void* context,
                            int w, int h, int comp,
                            const void* data, int quality);
}

// Forward-declared at file scope above the anonymous namespace —
// matches the same pattern in mdl_converter.cpp.
extern bool decode_tex_to_rgba(const std::vector<unsigned char>& blob,
                               std::vector<uint8_t>& rgba,
                               int& out_w, int& out_h, bool* out_has_alpha,
                               int mip_index);

// stb_image_write's "to-memory" callback expects a void* context
// it can append to. Trivial vector-append wrapper.
namespace {
void stb_sink(void* ctx, void* data, int size) {
    auto* v = (std::vector<uint8_t>*)ctx;
    auto* p = (const uint8_t*)data;
    v->insert(v->end(), p, p + size);
}
} // anonymous

namespace MdlTexExport {

namespace {

// ---------------------------------------------------------------------------
// Uncompressed RGBA8 DDS encoder.
// ---------------------------------------------------------------------------
// Header is 128 bytes total: 4-byte magic + 124-byte DDS_HEADER (which
// includes the embedded 32-byte DDS_PIXELFORMAT). We declare RGBA byte
// order via masks that put R in the low byte of each 32-bit pixel —
// reading back this DDS yields the same RGBA bytes the encoder
// received, no swizzle in either direction.
//
// DDPF flags / DDSCAPS values are spelled out as raw constants so we
// don't drag in <ddraw.h> (which Windows-only and pulls a lot of
// unrelated declarations).
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

    // 'DDS ' magic.
    out.push_back('D'); out.push_back('D');
    out.push_back('S'); out.push_back(' ');

    // DDS_HEADER (124 bytes).
    put_u32(124);                         // dwSize
    // dwFlags: CAPS | HEIGHT | WIDTH | PIXELFORMAT | PITCH
    put_u32(0x1u | 0x2u | 0x4u | 0x1000u | 0x8u);
    put_u32((uint32_t)h);                 // dwHeight
    put_u32((uint32_t)w);                 // dwWidth
    put_u32((uint32_t)(w * 4));           // dwPitchOrLinearSize (RGBA8)
    put_u32(0);                            // dwDepth
    put_u32(1);                            // dwMipMapCount (single mip)
    for (int i = 0; i < 11; ++i) put_u32(0);  // dwReserved1[11]

    // DDS_PIXELFORMAT (32 bytes inside the header).
    put_u32(32);                           // dwSize
    put_u32(0x40u | 0x1u);                 // DDPF_RGB | DDPF_ALPHAPIXELS
    put_u32(0);                            // dwFourCC (unused)
    put_u32(32);                           // dwRGBBitCount
    put_u32(0x000000FFu);                  // dwRBitMask  — low byte = R
    put_u32(0x0000FF00u);                  // dwGBitMask
    put_u32(0x00FF0000u);                  // dwBBitMask
    put_u32(0xFF000000u);                  // dwABitMask  — high byte = A

    // dwCaps* + dwReserved2.
    put_u32(0x1000u);                      // DDSCAPS_TEXTURE
    put_u32(0); put_u32(0); put_u32(0);
    put_u32(0);

    // Pixel data — straight RGBA8 row-major top-down.
    out.insert(out.end(), rgba.begin(), rgba.end());
    return true;
}

} // anonymous

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
            // JPG quality 92 is the sweet spot for asset previews —
            // visually lossless on diffuse maps, and ~1/3 the size
            // of PNG. Alpha is dropped (JPG can't carry it); for
            // textures with meaningful alpha (e.g. UI / particle)
            // the user should switch to PNG / DDS.
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
    return Format::DDS;   // default — matches Settings default.
}

const char* string_from_format(Format f) {
    switch (f) {
        case Format::DDS: return "DDS";
        case Format::PNG: return "PNG";
        case Format::JPG: return "JPG";
    }
    return "DDS";
}

} // namespace MdlTexExport
