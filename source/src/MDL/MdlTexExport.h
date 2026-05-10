
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MdlTexExport {

enum class Format {
    DDS,
    PNG,
    JPG,
};

struct EncodedTex {
    std::vector<uint8_t> bytes;
    int  width  = 0;
    int  height = 0;
    const char* extension = "";
    const char* mime_type = "";
};

bool encode_largest_mip(const std::vector<unsigned char>& tex_buf,
                        Format fmt,
                        EncodedTex& out);

Format format_from_string(const std::string& s);
const char* string_from_format(Format f);

}
