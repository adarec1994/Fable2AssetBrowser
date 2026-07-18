static uint32_t xbox360_tiled_offset(uint32_t x, uint32_t y,
                                     uint32_t width_in_blocks,
                                     uint32_t texel_byte_size)
{
    uint32_t aligned_w = (width_in_blocks + 31u) & ~31u;

    uint32_t log_bpp = (texel_byte_size >> 2) +
                       ((texel_byte_size >> 1) >> (texel_byte_size >> 2));

    uint32_t macro = ((x >> 5) + (y >> 5) * (aligned_w >> 5)) << (log_bpp + 7);
    uint32_t micro = (((x & 7) + ((y & 6) << 2)) << log_bpp);
    uint32_t offset = macro + ((micro & ~15u) << 1) + (micro & 15u) +
                      ((y & 8u) << (3 + log_bpp)) + ((y & 16u) << 7);

    uint32_t final_off =
        ((offset & ~511u) << 3) + ((offset & 448u) << 2) + (offset & 63u) +
        ((y & 1u) << 4) + (((x & 7u) << 1) ^ ((y >> 1) & 1u));

    return final_off & ~((1u << log_bpp) - 1u);
}

static uint32_t xg_address_2d_tiled_x(uint32_t block_offset,
                                      uint32_t width_in_blocks,
                                      uint32_t texel_byte_pitch);
static uint32_t xg_address_2d_tiled_y(uint32_t block_offset,
                                      uint32_t width_in_blocks,
                                      uint32_t texel_byte_pitch);

static void untile_xbox360_bc(const uint8_t* tiled, size_t tiled_size,
                              std::vector<uint8_t>& linear_out,
                              int width, int height, uint32_t block_size)
{
    const int blocks_w = (width  + 3) / 4;
    const int blocks_h = (height + 3) / 4;
    const size_t out_size = (size_t)blocks_w * (size_t)blocks_h * block_size;
    linear_out.assign(out_size, 0);

    bool use_xg_scatter = false;
    {
        const size_t total_blocks = (size_t)blocks_w * (size_t)blocks_h;
        size_t oob = 0;
        const size_t max_seen_words = tiled_size / block_size + 1;
        std::vector<uint8_t> seen(max_seen_words, 0);
        size_t dup = 0;
        for (int by = 0; by < blocks_h; ++by) {
            for (int bx = 0; bx < blocks_w; ++bx) {
                uint32_t off = xbox360_tiled_offset((uint32_t)bx, (uint32_t)by,
                                                    (uint32_t)blocks_w,
                                                    block_size);
                if ((size_t)off + block_size > tiled_size) { ++oob; continue; }
                const size_t idx = off / block_size;
                if (idx < seen.size()) {
                    if (seen[idx]) ++dup;
                    seen[idx] = 1;
                }
            }
        }
        if (oob * 20 > total_blocks || dup * 20 > total_blocks) {
            use_xg_scatter = true;
        }
    }

    if (use_xg_scatter) {
        const size_t total_blocks = (size_t)blocks_w * (size_t)blocks_h;
        for (size_t i = 0; i < total_blocks; ++i) {
            const size_t src_off = i * block_size;
            if (src_off + block_size > tiled_size) continue;
            uint32_t dx = xg_address_2d_tiled_x((uint32_t)i,
                                                (uint32_t)blocks_w,
                                                block_size);
            uint32_t dy = xg_address_2d_tiled_y((uint32_t)i,
                                                (uint32_t)blocks_w,
                                                block_size);
            if (dx >= (uint32_t)blocks_w || dy >= (uint32_t)blocks_h) continue;
            uint8_t* dst = linear_out.data() +
                           ((size_t)dy * blocks_w + dx) * block_size;
            std::memcpy(dst, tiled + src_off, block_size);
        }
        return;
    }

    for (int by = 0; by < blocks_h; ++by) {
        for (int bx = 0; bx < blocks_w; ++bx) {
            uint32_t src_off = xbox360_tiled_offset((uint32_t)bx, (uint32_t)by,
                                                    (uint32_t)blocks_w,
                                                    block_size);
            if ((size_t)src_off + block_size > tiled_size) continue;
            uint8_t* dst = linear_out.data() +
                           ((size_t)by * blocks_w + bx) * block_size;
            std::memcpy(dst, tiled + src_off, block_size);
        }
    }
}

static uint32_t xg_address_2d_tiled_x(uint32_t block_offset,
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

static uint32_t xg_address_2d_tiled_y(uint32_t block_offset,
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

static void untile_xbox360_imageheat(const uint8_t* tiled,
                                     size_t          tiled_size,
                                     std::vector<uint8_t>& linear_out,
                                     int             width_pixels,
                                     int             height_pixels,
                                     uint32_t        block_pixel_size,
                                     uint32_t        texel_byte_pitch)
{
    const uint32_t width = (uint32_t)width_pixels;
    const uint32_t height = (uint32_t)height_pixels;
    const uint32_t width_in_blocks = width / block_pixel_size +
                                     (width % block_pixel_size != 0u);
    const uint32_t height_in_blocks = height / block_pixel_size +
                                      (height % block_pixel_size != 0u);
    const uint32_t padded_w = (width_in_blocks  + 31u) & ~31u;
    const uint32_t padded_h = (height_in_blocks + 31u) & ~31u;
    const uint32_t total    = padded_w * padded_h;

    linear_out.assign((size_t)width_in_blocks
                      * (size_t)height_in_blocks
                      * (size_t)texel_byte_pitch, 0);

    for (uint32_t off = 0; off < total; ++off) {
        const uint32_t x = xg_address_2d_tiled_x(off, padded_w, texel_byte_pitch);
        const uint32_t y = xg_address_2d_tiled_y(off, padded_w, texel_byte_pitch);
        if (x >= width_in_blocks || y >= height_in_blocks) continue;

        const size_t src = (size_t)off * (size_t)texel_byte_pitch;
        if (src + texel_byte_pitch > tiled_size) continue;
        const size_t dst = ((size_t)y * (size_t)width_in_blocks + (size_t)x)
                         * (size_t)texel_byte_pitch;
        std::memcpy(linear_out.data() + dst,
                    tiled + src,
                    (size_t)texel_byte_pitch);
    }
}
