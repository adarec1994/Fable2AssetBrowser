// MdlTexExport — encode the largest mip of a Fable 2 .tex blob into
// a portable image format for embedding in MDL exports (GLB / FBX).
//
// The model exporters previously hardcoded PNG and used a custom
// inline decoder that filtered to comp=7 mips only — silently
// dropping the highest-resolution mip on every Lionhead-codec
// texture (which is most of them). This module replaces that with a
// single entry point that:
//
//   1. Decodes mip 0 (largest) regardless of CompFlag (raw / Lionhead
//      BC1 / variant_2_3_4) via the unified decode_tex_to_rgba.
//   2. Re-encodes the resulting RGBA buffer as the user-selected
//      format (DDS / PNG / JPG) for embedding inside the GLB / FBX.
//
// DDS is uncompressed RGBA8 — the smallest delta from the source
// bytes that's universally readable by Maya / Blender / 3ds Max
// without recompression. PNG and JPG go through stb_image_write.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MdlTexExport {

enum class Format {
    DDS,    // uncompressed RGBA8 DDS — default; large files but loss-less
    PNG,    // PNG via stb_image_write — loss-less, compressed
    JPG,    // JPEG via stb_image_write — lossy, smallest for non-alpha
};

// Output bundle — bytes plus the metadata the GLB / FBX writers need
// to reference the texture (Image.mimeType for glTF, Filename for
// FBX Video nodes).
struct EncodedTex {
    std::vector<uint8_t> bytes;
    int  width  = 0;
    int  height = 0;
    const char* extension = "";  // ".dds", ".png", ".jpg"
    const char* mime_type = "";  // "image/png" etc — used by glTF
};

// Decode mip 0 of `tex_buf` and re-encode in `fmt` into `out`.
// Returns false if either step fails (parse, decode, encode); on
// success `out.bytes` carries the encoded image bytes ready to embed.
bool encode_largest_mip(const std::vector<unsigned char>& tex_buf,
                        Format fmt,
                        EncodedTex& out);

// String <-> enum helpers used by the Settings UI / config persistence.
Format format_from_string(const std::string& s);
const char* string_from_format(Format f);

} // namespace MdlTexExport
