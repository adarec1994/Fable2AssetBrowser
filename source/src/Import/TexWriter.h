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
    // Force the top mip into the separate mip0 blob regardless of size.
    // Used when replacing a texture that has an entry in a shared
    // 1024mip0 bank: that entry must always hold the first mip's
    // def+payload (an empty entry stalls the game's streaming).
    bool   force_mip0_split = false;
    // Reproduce an existing texture's exact geometry (replace mode). When
    // match_width/height are non-zero the input is resampled to exactly
    // those dimensions instead of being clamped to a power of two, and the
    // mip chain is emitted to exactly match_mip_count levels. The engine's
    // streaming, atlas, and GPU descriptors all assume the stored geometry,
    // so a replacement that changes width/height/mipCount crashes or hangs
    // the game - especially for 1024 textures whose top mip streams from a
    // dedicated 1024mip0 bank.
    int    match_width     = 0;
    int    match_height    = 0;
    int    match_mip_count = 0;
    // Per-mip compression flags of the original texture (top mip first).
    // Chunk i is written as cf=1/11 (Lionhead-coded BC1) or cf=7 raw to
    // match; unsupported flags fall back to cf=7.
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
