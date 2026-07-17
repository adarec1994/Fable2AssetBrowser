#pragma once



#include <cstdint>
#include <cstring>
#include <vector>

namespace X360Tile {

inline uint32_t xg_address_2d_tiled_x(uint32_t block_offset,
                                      uint32_t width_in_blocks,
                                      uint32_t texel_byte_pitch)
{
    uint32_t aligned_width = (width_in_blocks + 31u) & ~31u;
    uint32_t log_bpp = (texel_byte_pitch >> 2) +
                       ((texel_byte_pitch >> 1) >> (texel_byte_pitch >> 2));
    uint32_t offset_byte  = block_offset << log_bpp;
    uint32_t offset_tile  = ((offset_byte & ~0xFFFu) >> 3) +
                            ((offset_byte & 0x700u) >> 2) +
                            (offset_byte & 0x3Fu);
    uint32_t offset_macro = offset_tile >> (7 + log_bpp);

    uint32_t macro_x = (offset_macro % (aligned_width >> 5)) << 2;
    uint32_t tile    = (((offset_tile >> (5 + log_bpp)) & 2) +
                        (offset_byte >> 6)) & 3;
    uint32_t macro   = (macro_x + tile) << 3;
    uint32_t micro   = ((((offset_tile >> 1) & ~0xFu) + (offset_tile & 0xFu))
                        & ((texel_byte_pitch << 3) - 1)) >> log_bpp;

    return macro + micro;
}

inline uint32_t xg_address_2d_tiled_y(uint32_t block_offset,
                                      uint32_t width_in_blocks,
                                      uint32_t texel_byte_pitch)
{
    uint32_t aligned_width = (width_in_blocks + 31u) & ~31u;
    uint32_t log_bpp = (texel_byte_pitch >> 2) +
                       ((texel_byte_pitch >> 1) >> (texel_byte_pitch >> 2));
    uint32_t offset_byte  = block_offset << log_bpp;
    uint32_t offset_tile  = ((offset_byte & ~0xFFFu) >> 3) +
                            ((offset_byte & 0x700u) >> 2) +
                            (offset_byte & 0x3Fu);
    uint32_t offset_macro = offset_tile >> (7 + log_bpp);

    uint32_t macro_y = (offset_macro / (aligned_width >> 5)) << 2;
    uint32_t tile    = ((offset_tile >> (6 + log_bpp)) & 1) +
                       ((offset_byte & 0x800u) >> 10);
    uint32_t macro   = (macro_y + tile) << 3;
    uint32_t micro   = (((offset_tile & (((texel_byte_pitch << 6) - 1) & ~0x1Fu))
                         + ((offset_tile & 0xFu) << 1)) >> (3 + log_bpp)) & ~1u;

    return macro + micro + ((offset_tile & 0x10u) >> 4);
}



inline void tile_2d(const uint8_t* linear,
                    uint32_t blocks_w, uint32_t blocks_h, uint32_t pitch,
                    std::vector<uint8_t>& tiled_out)
{
    const uint32_t padded_w = (blocks_w + 31u) & ~31u;
    const uint32_t padded_h = (blocks_h + 31u) & ~31u;
    const uint32_t total    = padded_w * padded_h;
    tiled_out.assign((size_t)total * pitch, 0);
    for (uint32_t off = 0; off < total; ++off) {
        const uint32_t x = xg_address_2d_tiled_x(off, padded_w, pitch);
        const uint32_t y = xg_address_2d_tiled_y(off, padded_w, pitch);
        if (x >= blocks_w || y >= blocks_h) continue;
        std::memcpy(tiled_out.data() + (size_t)off * pitch,
                    linear + ((size_t)y * blocks_w + x) * pitch,
                    pitch);
    }
}


inline void swap_u16_endian(uint8_t* data, size_t size)
{
    for (size_t i = 0; i + 2 <= size; i += 2) {
        uint8_t t = data[i]; data[i] = data[i + 1]; data[i + 1] = t;
    }
}

}
