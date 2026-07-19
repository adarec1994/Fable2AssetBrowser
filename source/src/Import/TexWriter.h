#pragma once





#include <cstdint>
#include <string>
#include <vector>

namespace TexWriter {

enum class Format {
    Auto,      
    BC1,       
    BC3,       
    BC5Normal, 
    RawARGB,   
};

struct Options {
    Format format        = Format::Auto;
    int    max_dimension = 1024;
    bool   generate_mips = true;
    bool   force_mip0_split = false;
    int    match_width     = 0;
    int    match_height    = 0;
    int    match_mip_count = 0;
    std::vector<uint32_t> match_comp_flags;
};

struct BuiltTex {
    std::vector<uint8_t> header;  
    std::vector<uint8_t> mip0;    
    std::vector<uint8_t> body;    
    uint32_t width  = 0;
    uint32_t height = 0;
    uint32_t pixel_format = 0;
    uint32_t mip_count = 0;
};



bool build_from_rgba(const uint8_t* rgba, int w, int h,
                     const Options& opt, BuiltTex& out, std::string& err);


bool rgba_has_alpha(const uint8_t* rgba, int w, int h);

}
