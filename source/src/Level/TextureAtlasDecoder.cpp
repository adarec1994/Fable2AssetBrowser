#include "TextureAtlasDecoder.h"

#include <cmath>
#include <cstring>
#include <sstream>
#include <zlib.h>

namespace {

inline bool rd32be(const uint8_t* p, size_t n, size_t off, uint32_t& out) {
    if (off + 4 > n) return false;
    out = (uint32_t(p[off+0]) << 24) | (uint32_t(p[off+1]) << 16) |
          (uint32_t(p[off+2]) <<  8) |  uint32_t(p[off+3]);
    return true;
}

inline uint8_t ex5(uint16_t v){ return (uint8_t)((v<<3)|(v>>2)); }
inline uint8_t ex6(uint16_t v){ return (uint8_t)((v<<2)|(v>>4)); }

void decode_bc1_block(const uint8_t* b, uint32_t* outRGBA) {
    uint16_t c0 = (uint16_t)(b[0] | (b[1]<<8));
    uint16_t c1 = (uint16_t)(b[2] | (b[3]<<8));
    uint8_t r0=ex5((c0>>11)&31), g0=ex6((c0>>5)&63),  b0=ex5(c0&31);
    uint8_t r1=ex5((c1>>11)&31), g1=ex6((c1>>5)&63),  b1=ex5(c1&31);
    uint32_t cols[4];
    cols[0] = (0xFFu<<24) | ((uint32_t)b0<<16) | ((uint32_t)g0<<8) | (uint32_t)r0;
    cols[1] = (0xFFu<<24) | ((uint32_t)b1<<16) | ((uint32_t)g1<<8) | (uint32_t)r1;
    if (c0 > c1) {
        cols[2] = (0xFFu<<24) | ((uint32_t)((2*b0+b1+1)/3)<<16) |
                  ((uint32_t)((2*g0+g1+1)/3)<<8) | (uint32_t)((2*r0+r1+1)/3);
        cols[3] = (0xFFu<<24) | ((uint32_t)((b0+2*b1+1)/3)<<16) |
                  ((uint32_t)((g0+2*g1+1)/3)<<8) | (uint32_t)((r0+2*r1+1)/3);
    } else {
        cols[2] = (0xFFu<<24) | ((uint32_t)((b0+b1)>>1)<<16) |
                  ((uint32_t)((g0+g1)>>1)<<8) | (uint32_t)((r0+r1)>>1);
        cols[3] = 0x00000000u;
    }
    const uint32_t idx = b[4] | (b[5]<<8) | (b[6]<<16) | (b[7]<<24);
    for (int py = 0; py < 4; ++py)
        for (int px = 0; px < 4; ++px)
            outRGBA[py*4+px] = cols[(idx >> (2*(py*4+px))) & 3];
}

void decode_bc4_channel(const uint8_t* b, uint8_t* out16) {
    uint8_t a0 = b[0], a1 = b[1];
    uint64_t abits = 0;
    for (int i = 0; i < 6; ++i) abits |= (uint64_t)b[2+i] << (8*i);
    uint8_t atab[8];
    atab[0] = a0; atab[1] = a1;
    if (a0 > a1) {
        for (int i = 1; i <= 6; ++i)
            atab[i+1] = (uint8_t)(((7-i)*a0 + i*a1 + 3) / 7);
    } else {
        for (int i = 1; i <= 4; ++i)
            atab[i+1] = (uint8_t)(((5-i)*a0 + i*a1 + 2) / 5);
        atab[6] = 0; atab[7] = 255;
    }
    for (int i = 0; i < 16; ++i)
        out16[i] = atab[(abits >> (3*i)) & 7];
}

void decode_bc3_block(const uint8_t* b, uint32_t* outRGBA) {
    uint8_t alpha16[16];
    decode_bc4_channel(b, alpha16);
    decode_bc1_block(b + 8, outRGBA);
    for (int i = 0; i < 16; ++i) {
        outRGBA[i] = (outRGBA[i] & 0x00FFFFFFu) |
                     ((uint32_t)alpha16[i] << 24);
    }
}

void swap16_words(uint8_t* data, size_t size) {
    for (size_t i = 0; i + 2 <= size; i += 2) {
        uint8_t t   = data[i];
        data[i]     = data[i+1];
        data[i+1]   = t;
    }
}

uint32_t xg_address_2d_tiled_x(uint32_t block_offset,
                               uint32_t width_in_blocks,
                               uint32_t texel_byte_pitch)
{
    uint32_t aligned_w = (width_in_blocks + 31u) & ~31u;
    uint32_t log_bpp   = (texel_byte_pitch >> 2) +
                         ((texel_byte_pitch >> 1) >> (texel_byte_pitch >> 2));
    uint32_t off_byte  = block_offset << log_bpp;
    uint32_t off_tile  = ((off_byte & ~0xFFFu) >> 3) +
                         ((off_byte & 0x700u) >> 2) +
                         (off_byte & 0x3Fu);
    uint32_t off_macro = off_tile >> (7 + log_bpp);

    uint32_t macro_x = (off_macro % (aligned_w >> 5)) << 2;
    uint32_t tile    = (((off_tile >> (5 + log_bpp)) & 2) +
                        (off_byte >> 6)) & 3;
    uint32_t macro   = (macro_x + tile) << 3;
    uint32_t micro   = ((((off_tile >> 1) & ~0xFu) + (off_tile & 0xFu))
                        & ((texel_byte_pitch << 3) - 1)) >> log_bpp;
    return macro + micro;
}

uint32_t xg_address_2d_tiled_y(uint32_t block_offset,
                               uint32_t width_in_blocks,
                               uint32_t texel_byte_pitch)
{
    uint32_t aligned_w = (width_in_blocks + 31u) & ~31u;
    uint32_t log_bpp   = (texel_byte_pitch >> 2) +
                         ((texel_byte_pitch >> 1) >> (texel_byte_pitch >> 2));
    uint32_t off_byte  = block_offset << log_bpp;
    uint32_t off_tile  = ((off_byte & ~0xFFFu) >> 3) +
                         ((off_byte & 0x700u) >> 2) +
                         (off_byte & 0x3Fu);
    uint32_t off_macro = off_tile >> (7 + log_bpp);

    uint32_t macro_y = (off_macro / (aligned_w >> 5)) << 2;
    uint32_t tile    = ((off_tile >> (6 + log_bpp)) & 1) +
                       ((off_byte & 0x800u) >> 10);
    uint32_t macro   = (macro_y + tile) << 3;
    uint32_t micro   = (((off_tile & (((texel_byte_pitch << 6) - 1) & ~0x1Fu))
                         + ((off_tile & 0xFu) << 1)) >> (3 + log_bpp)) & ~1u;
    return macro + micro + ((off_tile & 0x10u) >> 4);
}

void untile_xbox360_tile_major(const uint8_t* tiled,
                               size_t tiled_size,
                               std::vector<uint8_t>& linear_out,
                               int width_pixels, int height_pixels,
                               uint32_t block_pixel_size,
                               uint32_t texel_byte_pitch)
{
    const uint32_t blocks_w = (uint32_t)width_pixels  / block_pixel_size;
    const uint32_t blocks_h = (uint32_t)height_pixels / block_pixel_size;
    constexpr uint32_t TILE_BLOCKS = 32;
    const uint32_t tiles_w = (blocks_w + TILE_BLOCKS - 1) / TILE_BLOCKS;
    const uint32_t tiles_h = (blocks_h + TILE_BLOCKS - 1) / TILE_BLOCKS;

    linear_out.assign((size_t)blocks_w * (size_t)blocks_h
                      * (size_t)texel_byte_pitch, 0);

    size_t src_off = 0;
    for (uint32_t ty = 0; ty < tiles_h; ++ty) {
        for (uint32_t tx = 0; tx < tiles_w; ++tx) {
            for (uint32_t ly = 0; ly < TILE_BLOCKS; ++ly) {
                const uint32_t by = ty * TILE_BLOCKS + ly;
                for (uint32_t lx = 0; lx < TILE_BLOCKS; ++lx) {
                    const uint32_t bx = tx * TILE_BLOCKS + lx;
                    if (by < blocks_h && bx < blocks_w &&
                        src_off + texel_byte_pitch <= tiled_size)
                    {
                        const size_t dst_off =
                            ((size_t)by * blocks_w + bx) * texel_byte_pitch;
                        std::memcpy(linear_out.data() + dst_off,
                                    tiled + src_off, texel_byte_pitch);
                    }
                    src_off += texel_byte_pitch;
                }
            }
        }
    }
}

void untile_xbox360_imageheat(const uint8_t* tiled,
                              size_t tiled_size,
                              std::vector<uint8_t>& linear_out,
                              int width_pixels, int height_pixels,
                              uint32_t block_pixel_size,
                              uint32_t texel_byte_pitch)
{
    const uint32_t width_in_blocks  = (uint32_t)width_pixels  / block_pixel_size;
    const uint32_t height_in_blocks = (uint32_t)height_pixels / block_pixel_size;

    // Xbox 360 "packed mip" layout: textures 16 texels or smaller along an
    // axis live inside a shared 32x32-block tile at a 16-texel offset on
    // that axis (mip0 of the packed tail). Verified against the Fable 2
    // vista background-map pages (128x16 / 64x8 / 32x4: content sits at
    // block row 16/block_pixel_size, i.e. +16 texels in y).
    uint32_t block_off_x = 0;
    uint32_t block_off_y = 0;
    if (width_pixels <= 16 && height_pixels <= 16) {
        block_off_x = 16u / block_pixel_size;
        block_off_y = 16u / block_pixel_size;
    } else if (height_pixels <= 16) {
        block_off_y = 16u / block_pixel_size;
    } else if (width_pixels <= 16) {
        block_off_x = 16u / block_pixel_size;
    }

    const uint32_t padded_w = (block_off_x + width_in_blocks  + 31u) & ~31u;
    const uint32_t padded_h = (block_off_y + height_in_blocks + 31u) & ~31u;
    const uint32_t total    = padded_w * padded_h;

    linear_out.assign((size_t)width_in_blocks
                      * (size_t)height_in_blocks
                      * (size_t)texel_byte_pitch, 0);

    for (uint32_t off = 0; off < total; ++off) {
        const uint32_t x = xg_address_2d_tiled_x(off, padded_w, texel_byte_pitch);
        const uint32_t y = xg_address_2d_tiled_y(off, padded_w, texel_byte_pitch);
        if (x < block_off_x || y < block_off_y) continue;
        const uint32_t lx = x - block_off_x;
        const uint32_t ly = y - block_off_y;
        if (lx >= width_in_blocks || ly >= height_in_blocks) continue;
        const size_t src = (size_t)off * (size_t)texel_byte_pitch;
        if (src + texel_byte_pitch > tiled_size) continue;
        const size_t dst = ((size_t)ly * (size_t)width_in_blocks + (size_t)lx)
                         * (size_t)texel_byte_pitch;
        std::memcpy(linear_out.data() + dst,
                    tiled + src, (size_t)texel_byte_pitch);
    }
}

template<int Bytes>
void blit_bc_to_rgba(const uint8_t* src, int w, int h,
                     std::vector<uint8_t>& rgba,
                     void (*decode_block)(const uint8_t*, uint32_t*))
{
    const size_t bx = (size_t)((w + 3) / 4);
    const size_t by = (size_t)((h + 3) / 4);
    rgba.assign((size_t)w * (size_t)h * 4, 0xFF);
    size_t off = 0;
    for (size_t byy = 0; byy < by; ++byy) {
        for (size_t bxx = 0; bxx < bx; ++bxx) {
            uint32_t block[16];
            decode_block(src + off, block);
            off += Bytes;
            for (int py = 0; py < 4; ++py) {
                int yy = (int)byy * 4 + py;
                if (yy >= h) break;
                for (int px = 0; px < 4; ++px) {
                    int xx = (int)bxx * 4 + px;
                    if (xx >= w) break;
                    ((uint32_t*)rgba.data())[yy * w + xx] = block[py * 4 + px];
                }
            }
        }
    }
}

void blit_bc5_to_rgba(const uint8_t* src, int w, int h,
                      std::vector<uint8_t>& rgba)
{
    const size_t bx = (size_t)((w + 3) / 4);
    const size_t by = (size_t)((h + 3) / 4);
    rgba.assign((size_t)w * (size_t)h * 4, 0xFF);
    size_t off = 0;
    for (size_t byy = 0; byy < by; ++byy) {
        for (size_t bxx = 0; bxx < bx; ++bxx) {
            uint8_t xch[16], ych[16];
            decode_bc4_channel(src + off,     xch);
            decode_bc4_channel(src + off + 8, ych);
            off += 16;
            for (int py = 0; py < 4; ++py) {
                int yy = (int)byy * 4 + py;
                if (yy >= h) break;
                for (int px = 0; px < 4; ++px) {
                    int xx = (int)bxx * 4 + px;
                    if (xx >= w) break;
                    int idx = py * 4 + px;
                    int xi = xch[idx];
                    int yi = ych[idx];
                    float nx = (xi / 255.0f) * 2.0f - 1.0f;
                    float ny = (yi / 255.0f) * 2.0f - 1.0f;
                    float nz2 = 1.0f - nx*nx - ny*ny;
                    float nz = nz2 > 0.0f ? std::sqrt(nz2) : 0.0f;
                    int zi = (int)((nz * 0.5f + 0.5f) * 255.0f + 0.5f);
                    if (zi < 0)   zi = 0;
                    if (zi > 255) zi = 255;
                    uint8_t* p = rgba.data() + (yy * w + xx) * 4;
                    p[0] = (uint8_t)xi;
                    p[1] = (uint8_t)yi;
                    p[2] = (uint8_t)zi;
                    p[3] = 0xFF;
                }
            }
        }
    }
}

bool inflate_zlib(const uint8_t* in, size_t in_size,
                  std::vector<uint8_t>& out, size_t expected_raw)
{
    out.assign(expected_raw, 0);
    z_stream zs{};
    zs.next_in   = const_cast<Bytef*>(in);
    zs.avail_in  = (uInt)in_size;
    zs.next_out  = out.data();
    zs.avail_out = (uInt)expected_raw;
    if (inflateInit2(&zs, 15) != Z_OK) return false;
    int rc = inflate(&zs, Z_FINISH);
    const size_t produced = expected_raw - zs.avail_out;
    inflateEnd(&zs);
    if (produced == expected_raw) return true;
    return (rc == Z_OK || rc == Z_STREAM_END || rc == Z_BUF_ERROR) &&
           produced == expected_raw;
}

}

namespace TextureAtlas {

DecodedAtlas DecodeAtlas(const std::vector<uint8_t>& blob)
{
    DecodedAtlas r;
    const uint8_t* d = blob.data();
    const size_t   n = blob.size();

    if (n < 0x60) {
        r.error = "atlas: blob too small (" + std::to_string(n) + " bytes)";
        return r;
    }

    uint32_t magic     = 0;
    uint32_t W         = 0;
    uint32_t H         = 0;
    uint32_t PF        = 0;
    uint32_t mip_table = 0;
    if (!rd32be(d, n, 0x00, magic)     ||
        !rd32be(d, n, 0x10, W)         ||
        !rd32be(d, n, 0x14, H)         ||
        !rd32be(d, n, 0x18, PF)        ||
        !rd32be(d, n, 0x20, mip_table))
    {
        r.error = "atlas: header read failed";
        return r;
    }
    if (magic != 0xFFFFFFFEu) {
        std::ostringstream os;
        os << "atlas: unexpected magic 0x" << std::hex << magic;
        r.error = os.str();
        return r;
    }
    if (W == 0 || H == 0 || W > 16384 || H > 16384) {
        std::ostringstream os;
        os << "atlas: bad dimensions " << W << "x" << H;
        r.error = os.str();
        return r;
    }
    if (PF != 24u && PF != 35u && PF != 39u && PF != 40u) {
        std::ostringstream os;
        os << "atlas: unsupported PixelFormat " << PF
           << " (expected 24=L8A8(lightmap), 35=BC1, 39=BC3, 40=BC5)";
        r.error = os.str();
        return r;
    }

    const size_t mt = (size_t)mip_table;
    if (mt + 8 > n) {
        r.error = "atlas: mip table offset past EOF";
        return r;
    }
    uint32_t raw_size  = 0;
    uint32_t comp_size = 0;
    rd32be(d, n, mt + 0, raw_size);
    rd32be(d, n, mt + 4, comp_size);

    size_t   expected_raw = 0;
    uint32_t block_bytes  = 0;
    if (PF == 24u) {
        expected_raw = (size_t)raw_size;
        block_bytes = 2;
    } else {
        block_bytes = (PF == 35u) ? 8u : 16u;
        const uint32_t blocks_w = (W + 3u) / 4u;
        const uint32_t blocks_h = (H + 3u) / 4u;
        const uint32_t padded_w = (blocks_w + 31u) & ~31u;
        const uint32_t padded_h = (blocks_h + 31u) & ~31u;
        const size_t logical_raw =
            size_t(blocks_w) * size_t(blocks_h) * size_t(block_bytes);
        const size_t padded_raw =
            size_t(padded_w) * size_t(padded_h) * size_t(block_bytes);
        if ((size_t)raw_size < logical_raw ||
            (size_t)raw_size > padded_raw ||
            raw_size == 0)
        {
            std::ostringstream os;
            os << "atlas: raw_size " << raw_size
               << " outside logical/padded range ["
               << logical_raw << ".." << padded_raw << "] for "
               << W << "x" << H << " PF=" << PF;
            r.error = os.str();
            return r;
        }
        expected_raw = (size_t)raw_size;
    }

    const size_t zlib_off = mt + 8;
    if (zlib_off + 2 > n || d[zlib_off] != 0x78) {
        r.error = "atlas: no zlib magic at mip data offset";
        return r;
    }
    size_t avail_comp = n - zlib_off;
    if ((size_t)comp_size > avail_comp) comp_size = (uint32_t)avail_comp;

    std::vector<uint8_t> raw;
    if (!inflate_zlib(d + zlib_off, comp_size, raw, expected_raw)) {
        r.error = "atlas: zlib inflate failed";
        return r;
    }

    std::vector<uint8_t> linear;
    if (PF == 24u) {
        untile_xbox360_imageheat(raw.data(), raw.size(), linear,
                                 (int)W, (int)H,
                                 1,
                                 2);
    } else {
        untile_xbox360_imageheat(raw.data(), raw.size(), linear,
                                 (int)W, (int)H,
                                 4,
                                 block_bytes);
    }

    r.width  = (int)W;
    r.height = (int)H;
    r.pixel_format = PF;
    if (PF == 24u) {
        const size_t pix = (size_t)W * (size_t)H;
        r.rgba.assign(pix * 4, 0);
        for (size_t i = 0; i < pix; ++i) {
            r.rgba[i*4 + 0] = linear[i*2 + 0];
            r.rgba[i*4 + 1] = linear[i*2 + 1];
            r.rgba[i*4 + 2] = 0;
            r.rgba[i*4 + 3] = 0xFF;
        }
        r.ok = true;
        return r;
    }
    swap16_words(linear.data(), linear.size());
    if (PF == 35u) {
        blit_bc_to_rgba<8>(linear.data(), (int)W, (int)H, r.rgba,
                           decode_bc1_block);
    } else if (PF == 39u) {
        blit_bc_to_rgba<16>(linear.data(), (int)W, (int)H, r.rgba,
                            decode_bc3_block);
    } else {
        blit_bc5_to_rgba(linear.data(), (int)W, (int)H, r.rgba);
    }
    r.ok = true;
    return r;
}

namespace {
bool decode_zlib_bc_page_generic(const uint8_t* zlib_stream,
                                 size_t          comp_size,
                                 size_t          expected_raw,
                                 int             width_pixels,
                                 int             height_pixels,
                                 std::vector<uint8_t>& rgba,
                                 int             which )
{
    if (!zlib_stream || comp_size < 2 ||
        width_pixels <= 0 || height_pixels <= 0 ||
        expected_raw == 0) return false;
    if (zlib_stream[0] != 0x78) return false;

    std::vector<uint8_t> raw;
    if (!inflate_zlib(zlib_stream, comp_size, raw, expected_raw)) return false;

    const uint32_t block_bytes = (which == 1) ? 8u : 16u;

    std::vector<uint8_t> linear;
    untile_xbox360_imageheat(raw.data(), raw.size(), linear,
                             width_pixels, height_pixels,
                             4,
                             block_bytes);

    swap16_words(linear.data(), linear.size());
    if (which == 1) {
        blit_bc_to_rgba<8>(linear.data(), width_pixels, height_pixels,
                           rgba, decode_bc1_block);
    } else if (which == 3) {
        blit_bc_to_rgba<16>(linear.data(), width_pixels, height_pixels,
                            rgba, decode_bc3_block);
    } else {
        blit_bc5_to_rgba(linear.data(), width_pixels, height_pixels, rgba);
    }
    return true;
}
}

bool DecodeZlibBc1Page(const uint8_t* zlib_stream, size_t comp_size,
                       size_t expected_raw, int w, int h,
                       std::vector<uint8_t>& rgba)
{
    return decode_zlib_bc_page_generic(zlib_stream, comp_size, expected_raw,
                                       w, h, rgba, 1);
}
bool DecodeZlibBc3Page(const uint8_t* zlib_stream, size_t comp_size,
                       size_t expected_raw, int w, int h,
                       std::vector<uint8_t>& rgba)
{
    return decode_zlib_bc_page_generic(zlib_stream, comp_size, expected_raw,
                                       w, h, rgba, 3);
}
bool DecodeZlibBc5Page(const uint8_t* zlib_stream, size_t comp_size,
                       size_t expected_raw, int w, int h,
                       std::vector<uint8_t>& rgba)
{
    return decode_zlib_bc_page_generic(zlib_stream, comp_size, expected_raw,
                                       w, h, rgba, 5);
}

bool DecodeRawBc1ToRgba(const uint8_t* bc1, size_t bc1_size,
                        int W, int H, std::vector<uint8_t>& rgba)
{
    if (!bc1 || W <= 0 || H <= 0) return false;
    const size_t need = (size_t)((W + 3) / 4) * (size_t)((H + 3) / 4) * 8;
    if (bc1_size < need) return false;
    blit_bc_to_rgba<8>(bc1, W, H, rgba, decode_bc1_block);
    return true;
}

bool DecodePF99SplatMap(const uint8_t* pf99_blob, size_t blob_size,
                        std::vector<uint8_t>& out_indices,
                        int& out_w, int& out_h,
                        std::string& out_err)
{
    out_indices.clear();
    out_w = 0; out_h = 0;
    out_err.clear();

    if (!pf99_blob || blob_size < 0x60) {
        out_err = "splat: blob too small";
        return false;
    }

    uint32_t magic = 0, W = 0, H = 0, PF = 0, mt = 0;
    if (!rd32be(pf99_blob, blob_size, 0x00, magic) ||
        !rd32be(pf99_blob, blob_size, 0x10, W) ||
        !rd32be(pf99_blob, blob_size, 0x14, H) ||
        !rd32be(pf99_blob, blob_size, 0x18, PF) ||
        !rd32be(pf99_blob, blob_size, 0x20, mt)) {
        out_err = "splat: header read failed";
        return false;
    }
    if (magic != 0xFFFFFFFEu) {
        out_err = "splat: bad magic";
        return false;
    }
    if (PF != 99u) {
        std::ostringstream os;
        os << "splat: PixelFormat is " << PF << ", expected 99";
        out_err = os.str();
        return false;
    }
    if (W == 0 || H == 0 || W > 16384 || H > 16384) {
        out_err = "splat: bad dimensions";
        return false;
    }

    uint32_t raw_size = 0, comp_size = 0;
    if (!rd32be(pf99_blob, blob_size, mt + 0, raw_size) ||
        !rd32be(pf99_blob, blob_size, mt + 4, comp_size)) {
        out_err = "splat: mip table read failed";
        return false;
    }
    const size_t zlib_off = (size_t)mt + 8;
    if (zlib_off + 2 > blob_size || pf99_blob[zlib_off] != 0x78) {
        out_err = "splat: no zlib magic at mip data offset";
        return false;
    }
    size_t avail_comp = blob_size - zlib_off;
    if ((size_t)comp_size > avail_comp) comp_size = (uint32_t)avail_comp;

    std::vector<uint8_t> raw;
    if (!inflate_zlib(pf99_blob + zlib_off, comp_size, raw, raw_size)) {
        out_err = "splat: zlib inflate failed";
        return false;
    }

    const uint32_t blocks_w = (W + 3u) / 4u;
    const uint32_t blocks_h = (H + 3u) / 4u;
    const uint32_t padded_blocks_w = (blocks_w + 31u) & ~31u;
    const uint32_t padded_blocks_h = (blocks_h + 31u) & ~31u;
    const size_t expected_raw =
        (size_t)padded_blocks_w * padded_blocks_h * 8u;
    if (raw.size() < expected_raw) {
        std::ostringstream os;
        os << "splat: raw BC4 page too small: " << raw.size()
           << " < " << expected_raw;
        out_err = os.str();
        return false;
    }

    std::vector<uint8_t> linear((size_t)blocks_w * blocks_h * 8u, 0);
    const uint32_t total_blocks = padded_blocks_w * padded_blocks_h;
    for (uint32_t off = 0; off < total_blocks; ++off) {
        const uint32_t bx =
            xg_address_2d_tiled_x(off, padded_blocks_w, 8);
        const uint32_t by =
            xg_address_2d_tiled_y(off, padded_blocks_w, 8);
        if (bx >= blocks_w || by >= blocks_h) continue;

        const size_t src_off = (size_t)off * 8u;
        if (src_off + 8u > raw.size()) continue;
        const size_t dst_off =
            ((size_t)by * blocks_w + bx) * 8u;
        std::memcpy(linear.data() + dst_off, raw.data() + src_off, 8);
    }

    swap16_words(linear.data(), linear.size());

    out_indices.assign((size_t)W * (size_t)H, 0);
    for (uint32_t by = 0; by < blocks_h; ++by) {
        for (uint32_t bx = 0; bx < blocks_w; ++bx) {
            const uint8_t* b8 =
                linear.data() + ((size_t)by * blocks_w + bx) * 8u;
            for (int py = 0; py < 4; ++py) {
                const uint32_t yy = by * 4u + (uint32_t)py;
                if (yy >= H) break;
                const uint16_t row =
                    (uint16_t)(b8[py*2] | (b8[py*2 + 1] << 8));
                for (int pxn = 0; pxn < 4; ++pxn) {
                    const uint32_t xx = bx * 4u + (uint32_t)pxn;
                    if (xx >= W) continue;
                    out_indices[(size_t)yy * W + xx] =
                        (uint8_t)(((row >> (4 * pxn)) & 0xF) * 17);
                }
            }
        }
    }

    out_w = (int)W;
    out_h = (int)H;
    return true;
}

}
