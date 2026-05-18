#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>
#include <optional>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <sstream>
#include <limits>
#include <mutex>
#include <fstream>
#include <unordered_map>
#include <functional>
#include "ModelPreview.h"
#include "../Level/TerrainSplat.h"
#include "../Utilities/Files.h"
#include "../Utilities/Utils.h"
#include "../Utilities/State.h"
#include "../BNKCore.cpp"
#include "../textures/TexParser.h"
#include "../textures/LhTexCodec.h"
#include "OutputLog.h"
#include <zlib.h>
#ifdef _WIN32
#include <initguid.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
using namespace DirectX;
#else
#include <GL/glew.h>
#endif
FlyCam g_flycam;
static std::mutex g_mp_debug_mutex;
static void mp_level_load_debug(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(g_mp_debug_mutex);
    std::ofstream f(std::filesystem::current_path() / "level_load_debug.txt",
                    std::ios::app);
    if (f) f << msg << "\n";
}

static inline std::string tolower_copy(std::string s){ std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }
static inline std::string basename_lower_noext(const std::string& s){
    auto b = std::filesystem::path(s).filename().string();
    auto p = b.find_last_of('.');
    if(p!=std::string::npos) b = b.substr(0,p);
    return tolower_copy(b);
}
static inline std::string force_tex_ext(const std::string& s){
    std::string base = std::filesystem::path(s).filename().string();
    auto p = base.find_last_of('.');
    if(p!=std::string::npos) base = base.substr(0,p);
    return base + ".tex";
}
static std::optional<std::string> find_any_textures_bnk(){
    if(auto p1 = find_bnk_by_filename("globals_textures.bnk"); p1) return p1;
    return find_bnk_by_filename("global_textures.bnk");
}
static inline uint8_t ex5(uint16_t v){ return (uint8_t)((v<<3)|(v>>2)); }
static inline uint8_t ex6(uint16_t v){ return (uint8_t)((v<<2)|(v>>4)); }
static void decode_bc1_block(const uint8_t* b, uint32_t* outRGBA) {
    uint16_t c0 = (uint16_t)(b[0] | (b[1]<<8));
    uint16_t c1 = (uint16_t)(b[2] | (b[3]<<8));
    uint8_t r0=ex5((c0>>11)&31), g0=ex6((c0>>5)&63),  b0=ex5(c0&31);
    uint8_t r1=ex5((c1>>11)&31), g1=ex6((c1>>5)&63),  b1=ex5(c1&31);
    uint32_t cols[4];

    cols[0] = (0xFFu<<24) | ((uint32_t)b0<<16) | ((uint32_t)g0<<8) | (uint32_t)r0;
    cols[1] = (0xFFu<<24) | ((uint32_t)b1<<16) | ((uint32_t)g1<<8) | (uint32_t)r1;
    if(c0 > c1){

        cols[2] = (0xFFu<<24) | ((uint32_t)((2*b0+b1+1)/3)<<16) | ((uint32_t)((2*g0+g1+1)/3)<<8) | (uint32_t)((2*r0+r1+1)/3);
        cols[3] = (0xFFu<<24) | ((uint32_t)((b0+2*b1+1)/3)<<16) | ((uint32_t)((g0+2*g1+1)/3)<<8) | (uint32_t)((r0+2*r1+1)/3);
    }else{
        cols[2] = (0xFFu<<24) | ((uint32_t)((b0+b1)>>1)<<16) | ((uint32_t)((g0+g1)>>1)<<8) | (uint32_t)((r0+r1)>>1);
        cols[3] = 0x00000000u;
    }
    const uint32_t idx = b[4] | (b[5]<<8) | (b[6]<<16) | (b[7]<<24);
    for(int py=0; py<4; ++py){
        for(int px=0; px<4; ++px){
            int s = (idx >> (2*(py*4+px))) & 3;
            outRGBA[py*4+px] = cols[s];
        }
    }
}
static void decode_bc3_block(const uint8_t* b, uint32_t* outRGBA){
    uint8_t a0=b[0], a1=b[1];
    uint64_t abits = 0;
    for(int i=0;i<6;++i) abits |= (uint64_t)b[2+i] << (8*i);
    uint8_t atab[8];
    atab[0]=a0; atab[1]=a1;

    if(a0>a1){ for(int i=1;i<=6;i++) atab[i+1]=(uint8_t)(((7-i)*a0 + i*a1 + 3)/7); }
    else{ for(int i=1;i<=4;i++) atab[i+1]=(uint8_t)(((5-i)*a0 + i*a1 + 2)/5); atab[6]=0; atab[7]=255; }
    uint32_t color[16];
    decode_bc1_block(b+8, color);
    for(int i=0;i<16;++i){
        uint8_t ai = (uint8_t)((abits>>(3*i)) & 7);
        color[i] = (color[i] & 0x00FFFFFFu) | ( ((uint32_t)atab[ai])<<24 );
    }
    for(int i=0;i<16;++i) outRGBA[i]=color[i];
}
static void swap_bc1_endian(uint8_t* data, size_t size) {
    for(size_t i = 0; i + 8 <= size; i += 8) {
        uint16_t c0 = (data[i+0] << 8) | data[i+1];
        uint16_t c1 = (data[i+2] << 8) | data[i+3];
        uint32_t idx = (data[i+4] << 24) | (data[i+5] << 16) | (data[i+6] << 8) | data[i+7];
        data[i+0] = c0 & 0xFF;
        data[i+1] = (c0 >> 8) & 0xFF;
        data[i+2] = c1 & 0xFF;
        data[i+3] = (c1 >> 8) & 0xFF;
        data[i+4] = idx & 0xFF;
        data[i+5] = (idx >> 8) & 0xFF;
        data[i+6] = (idx >> 16) & 0xFF;
        data[i+7] = (idx >> 24) & 0xFF;
    }
}
static void swap_bc3_endian(uint8_t* data, size_t size) {
    for(size_t i = 0; i + 16 <= size; i += 16) {
        uint64_t alpha_bits = 0;
        for(int j = 0; j < 6; j++) {
            alpha_bits |= ((uint64_t)data[i+2+j]) << (j*8);
        }
        uint64_t alpha_swapped = 0;
        for(int j = 0; j < 6; j++) {
            alpha_swapped |= ((alpha_bits >> (j*8)) & 0xFF) << ((5-j)*8);
        }
        for(int j = 0; j < 6; j++) {
            data[i+2+j] = (alpha_swapped >> (j*8)) & 0xFF;
        }
        swap_bc1_endian(data + i + 8, 8);
    }
}

static void decode_bc4_block(const uint8_t* b, uint8_t* out16) {
    uint8_t a0 = b[0], a1 = b[1];
    uint64_t abits = 0;
    for (int i = 0; i < 6; ++i) abits |= (uint64_t)b[2+i] << (8*i);
    uint8_t atab[8];
    atab[0] = a0; atab[1] = a1;
    if (a0 > a1) {

        for (int i = 1; i <= 6; i++)
            atab[i+1] = (uint8_t)(((7-i)*a0 + i*a1 + 3) / 7);
    } else {

        for (int i = 1; i <= 4; i++)
            atab[i+1] = (uint8_t)(((5-i)*a0 + i*a1 + 2) / 5);
        atab[6] = 0; atab[7] = 255;
    }
    for (int i = 0; i < 16; ++i) {
        uint8_t ai = (uint8_t)((abits >> (3*i)) & 7);
        out16[i] = atab[ai];
    }
}

static void swap_bc5_endian(uint8_t* data, size_t size) {
    for (size_t i = 0; i + 16 <= size; i += 16) {

        for (int half = 0; half < 2; ++half) {
            uint8_t* blk = data + i + 8 * half;
            uint64_t bits = 0;
            for (int j = 0; j < 6; j++) bits |= ((uint64_t)blk[2+j]) << (j*8);
            uint64_t sw = 0;
            for (int j = 0; j < 6; j++) sw |= ((bits >> (j*8)) & 0xFF) << ((5-j)*8);
            for (int j = 0; j < 6; j++) blk[2+j] = (sw >> (j*8)) & 0xFF;
        }
    }
}

static void blit_bc1_to_rgba(const uint8_t* src, int w, int h,
                             std::vector<uint8_t>& rgba) {
    const size_t bx = (size_t)((w + 3) / 4);
    const size_t by = (size_t)((h + 3) / 4);
    rgba.assign((size_t)w * (size_t)h * 4, 0xFF);
    size_t off = 0;
    for (size_t byy = 0; byy < by; ++byy) {
        for (size_t bxx = 0; bxx < bx; ++bxx) {
            uint32_t block[16];
            decode_bc1_block(src + off, block);
            off += 8;
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

static void blit_bc3_to_rgba(const uint8_t* src, int w, int h,
                             std::vector<uint8_t>& rgba) {
    const size_t bx = (size_t)((w + 3) / 4);
    const size_t by = (size_t)((h + 3) / 4);
    rgba.assign((size_t)w * (size_t)h * 4, 0xFF);
    size_t off = 0;
    for (size_t byy = 0; byy < by; ++byy) {
        for (size_t bxx = 0; bxx < bx; ++bxx) {
            uint32_t block[16];
            decode_bc3_block(src + off, block);
            off += 16;
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

static void blit_bc5_to_rgba(const uint8_t* src, int w, int h,
                             std::vector<uint8_t>& rgba) {
    const size_t bx = (size_t)((w + 3) / 4);
    const size_t by = (size_t)((h + 3) / 4);
    rgba.assign((size_t)w * (size_t)h * 4, 0xFF);
    size_t off = 0;
    for (size_t byy = 0; byy < by; ++byy) {
        for (size_t bxx = 0; bxx < bx; ++bxx) {
            uint8_t xch[16], ych[16];
            decode_bc4_block(src + off,     xch);
            decode_bc4_block(src + off + 8, ych);
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
                    float nz = nz2 > 0.0f ? sqrtf(nz2) : 0.0f;
                    int zi = (int)((nz * 0.5f + 0.5f) * 255.0f + 0.5f);
                    if (zi < 0) zi = 0;
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

static void untile_xbox360_bc(const uint8_t* tiled, size_t tiled_size,
                              std::vector<uint8_t>& linear_out,
                              int width, int height, uint32_t block_size)
{
    const int blocks_w = (width  + 3) / 4;
    const int blocks_h = (height + 3) / 4;
    linear_out.assign((size_t)blocks_w * (size_t)blocks_h * block_size, 0);

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
    const uint32_t width_in_blocks  = (uint32_t)width_pixels  / block_pixel_size;
    const uint32_t height_in_blocks = (uint32_t)height_pixels / block_pixel_size;
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

static thread_local std::string g_last_decode_fail_reason;
static thread_local std::string g_last_decode_info;

const std::string& mp_last_decode_fail_reason() { return g_last_decode_fail_reason; }
const std::string& mp_last_decode_info()        { return g_last_decode_info; }

#define DEC_FAIL(reason) do { g_last_decode_fail_reason = (reason); return false; } while (0)

bool decode_tex_to_rgba(const std::vector<unsigned char>& blob,
                        std::vector<uint8_t>& rgba,
                        int& out_w, int& out_h, bool* out_has_alpha,
                        int mip_index) {
    g_last_decode_fail_reason.clear();
    g_last_decode_info.clear();
    if (out_has_alpha) *out_has_alpha = false;
    TexInfo ti{};
    if (!parse_tex_info(blob, ti)) DEC_FAIL("parse_tex_info_failed");
    if (ti.Mips.empty())            DEC_FAIL("zero_mips");
    {
        std::ostringstream os;
        os << "pf=" << (int)ti.PixelFormat
           << " mips=" << ti.Mips.size()
           << " w=" << (int)ti.TextureWidth
           << " h=" << (int)ti.TextureHeight;
        if (!ti.Mips.empty()) {
            os << " cf0=" << (int)ti.Mips[0].CompFlag
               << " ds0=" << (size_t)ti.Mips[0].DataSize;
        }
        g_last_decode_info = os.str();
    }

    auto mip_wh = [&](size_t i, int& mw, int& mh) {
        const auto& m = ti.Mips[i];
        mw = m.HasWH ? (int)m.MipWidth  : std::max(1, (int)ti.TextureWidth  >> (int)i);
        mh = m.HasWH ? (int)m.MipHeight : std::max(1, (int)ti.TextureHeight >> (int)i);
    };

    auto any_alpha_lt_255 = [&](const std::vector<uint8_t>& buf) -> bool {
        const uint8_t* p = buf.data();
        size_t n = buf.size();
        for (size_t i = 3; i < n; i += 4)
            if (p[i] < 255) return true;
        return false;
    };

    size_t best = 0;
    if (mip_index >= 0 && (size_t)mip_index < ti.Mips.size()) {
        best = (size_t)mip_index;
    } else {
        size_t match_idx = ti.Mips.size();
        for (size_t i = 0; i < ti.Mips.size(); ++i) {
            int w_i = 0, h_i = 0; mip_wh(i, w_i, h_i);
            if ((uint32_t)w_i == ti.TextureWidth &&
                (uint32_t)h_i == ti.TextureHeight) {
                match_idx = i;
                break;
            }
        }
        if (match_idx < ti.Mips.size()) {
            best = match_idx;
        } else {
            int bw = 0, bh = 0; mip_wh(0, bw, bh);
            size_t best_area = (size_t)bw * (size_t)bh;
            for (size_t i = 1; i < ti.Mips.size(); ++i) {
                int wm = 0, hm = 0; mip_wh(i, wm, hm);
                size_t area = (size_t)wm * (size_t)hm;
                if (area > best_area) { best_area = area; best = i; }
            }
        }
    }

    const auto& m = ti.Mips[best];
    int w = 0, h = 0; mip_wh(best, w, h);

    const bool is_zlib_sentinel =
        (m.CompFlag >= 200 && m.CompFlag <= 203);
    if (!is_zlib_sentinel &&
        m.MipDataOffset + m.MipDataSizeParsed > blob.size()) {
        std::ostringstream os;
        os << "mip[" << best << "] data out of bounds (offset=" << m.MipDataOffset
           << " size=" << m.MipDataSizeParsed << " blob=" << blob.size() << ")";
        DEC_FAIL("mip_oob");
    }

    if (m.CompFlag == 200 || m.CompFlag == 201 ||
        m.CompFlag == 202 || m.CompFlag == 203)
    {
        const size_t expected_raw = (size_t)m.Unknown_3;
        if (expected_raw == 0) DEC_FAIL("zlib_no_raw_size");

        std::vector<uint8_t> raw(expected_raw);
        z_stream zs{};
        zs.next_in   = const_cast<Bytef*>(blob.data() + m.MipDataOffset);
        zs.avail_in  = (uInt)m.MipDataSizeParsed;
        zs.next_out  = raw.data();
        zs.avail_out = (uInt)expected_raw;
        if (inflateInit2(&zs, 15) != Z_OK) DEC_FAIL("zlib_init_fail");
        int rc = inflate(&zs, Z_FINISH);
        const size_t produced = expected_raw - zs.avail_out;
        inflateEnd(&zs);
        if (produced != expected_raw &&
            !(rc == Z_OK || rc == Z_STREAM_END || rc == Z_BUF_ERROR)) {
            std::ostringstream os;
            os << "zlib_inflate_fail rc=" << rc
               << " produced=" << produced;
            g_last_decode_info += " " + os.str();
            DEC_FAIL("zlib_inflate_fail");
        }

        out_w = w; out_h = h;

        if (m.CompFlag == 200) {
            std::vector<uint8_t> linear;
            untile_xbox360_imageheat(raw.data(), raw.size(), linear,
                                     w, h, 4, 8);
            swap_bc1_endian(linear.data(), linear.size());
            blit_bc1_to_rgba(linear.data(), w, h, rgba);
            if (out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
            return true;
        }
        if (m.CompFlag == 201) {
            std::vector<uint8_t> linear;
            untile_xbox360_imageheat(raw.data(), raw.size(), linear,
                                     w, h, 4, 16);
            swap_bc3_endian(linear.data(), linear.size());
            blit_bc3_to_rgba(linear.data(), w, h, rgba);
            if (out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
            return true;
        }
        if (m.CompFlag == 202) {
            std::vector<uint8_t> linear;
            untile_xbox360_imageheat(raw.data(), raw.size(), linear,
                                     w, h, 4, 16);
            swap_bc5_endian(linear.data(), linear.size());
            blit_bc5_to_rgba(linear.data(), w, h, rgba);
            if (out_has_alpha) *out_has_alpha = false;
            return true;
        }
        const size_t pixels = (size_t)w * (size_t)h;
        if (raw.size() < pixels * 4) DEC_FAIL("zlib_argb8_size_short");
        rgba.assign(pixels * 4, 0);
        const uint32_t W2 = (uint32_t)w;
        const uint32_t H2 = (uint32_t)h;
        const uint32_t padded_W = (W2 + 31u) & ~31u;
        const uint32_t padded_H = (H2 + 31u) & ~31u;
        const uint32_t total    = padded_W * padded_H;
        for (uint32_t off = 0; off < total; ++off) {
            const uint32_t sx = xg_address_2d_tiled_x(off, padded_W, 4);
            const uint32_t sy = xg_address_2d_tiled_y(off, padded_W, 4);
            if (sx >= W2 || sy >= H2) continue;
            const size_t src_byte = (size_t)off * 4;
            if (src_byte + 4 > raw.size()) continue;
            const uint8_t A = raw[src_byte + 0];
            const uint8_t R = raw[src_byte + 1];
            const uint8_t G = raw[src_byte + 2];
            const uint8_t B = raw[src_byte + 3];
            const size_t dst = ((size_t)sy * W2 + sx) * 4;
            rgba[dst + 0] = R;
            rgba[dst + 1] = G;
            rgba[dst + 2] = B;
            rgba[dst + 3] = A;
        }
        if (out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
        return true;
    }

    if (m.CompFlag == 99 || m.CompFlag == 100) {
        const size_t pixels = (size_t)w * (size_t)h;
        if (m.MipDataSizeParsed < pixels * 4) {
            DEC_FAIL("argb8_size_short");
        }

        const uint8_t* tiled   = blob.data() + m.MipDataOffset;
        const size_t   tiled_n = m.MipDataSizeParsed;
        const uint32_t W       = (uint32_t)w;
        const uint32_t H       = (uint32_t)h;
        const uint32_t padded_W = (W + 31u) & ~31u;
        const uint32_t padded_H = (H + 31u) & ~31u;
        const uint32_t total    = padded_W * padded_H;

        rgba.assign(pixels * 4, 0);

        for (uint32_t off = 0; off < total; ++off) {
            const uint32_t sx = xg_address_2d_tiled_x(off, padded_W, 4);
            const uint32_t sy = xg_address_2d_tiled_y(off, padded_W, 4);
            if (sx >= W || sy >= H) continue;

            const size_t src_byte = (size_t)off * 4;
            if (src_byte + 4 > tiled_n) continue;

            const uint8_t A = tiled[src_byte + 0];
            const uint8_t R = tiled[src_byte + 1];
            const uint8_t G = tiled[src_byte + 2];
            const uint8_t B = tiled[src_byte + 3];
            const size_t  dst = ((size_t)sy * W + sx) * 4;
            rgba[dst + 0] = R;
            rgba[dst + 1] = G;
            rgba[dst + 2] = B;
            rgba[dst + 3] = (m.CompFlag == 100) ? 0xFFu : A;
        }

        out_w = w; out_h = h;
        if (out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
        return true;
    }

    const size_t bx = (size_t)((w + 3) / 4);
    const size_t by = (size_t)((h + 3) / 4);
    const size_t sz_bc1 = bx * by * 8;
    const size_t sz_bc3 = bx * by * 16;
    const uint8_t* src = blob.data() + m.MipDataOffset;

    if (m.CompFlag == 7) {
        if (ti.PixelFormat == 35) {
            if (m.MipDataSizeParsed < sz_bc1) {
                DEC_FAIL("c7_pf35_size_short");
            }
            std::vector<uint8_t> linear;
            untile_xbox360_bc(src, m.MipDataSizeParsed, linear, w, h, 8);
            swap_bc1_endian(linear.data(), linear.size());
            blit_bc1_to_rgba(linear.data(), w, h, rgba);
            out_w = w; out_h = h;
            if (out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
            return true;
        }
        if (ti.PixelFormat == 39) {
            if (m.MipDataSizeParsed < sz_bc3) {
                DEC_FAIL("c7_pf39_size_short");
            }
            std::vector<uint8_t> linear;
            untile_xbox360_bc(src, m.MipDataSizeParsed, linear, w, h, 16);
            swap_bc3_endian(linear.data(), linear.size());
            blit_bc3_to_rgba(linear.data(), w, h, rgba);
            out_w = w; out_h = h;
            if (out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
            return true;
        }
        if (ti.PixelFormat == 40) {

            if (m.MipDataSizeParsed < sz_bc3) {
                DEC_FAIL("c7_pf40_size_short");
            }
            std::vector<uint8_t> linear;
            untile_xbox360_bc(src, m.MipDataSizeParsed, linear, w, h, 16);
            swap_bc5_endian(linear.data(), linear.size());
            blit_bc5_to_rgba(linear.data(), w, h, rgba);
            out_w = w; out_h = h;
            if (out_has_alpha) *out_has_alpha = false;
            return true;
        }

        size_t sz_raw = (size_t)w * (size_t)h * 4;
        if (m.MipDataSizeParsed < sz_raw) {
            std::ostringstream os;
            os << "unknown raw format and data too small for RGBA ("
               << m.MipDataSizeParsed << " < " << sz_raw
               << ", PixelFormat=" << ti.PixelFormat << ")";
            DEC_FAIL("c7_raw_size_short");
        }
        rgba.assign(sz_raw, 0xFF);
        memcpy(rgba.data(), src, sz_raw);
        out_w = w; out_h = h;
        if (out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
        return true;
    }

    if (m.CompFlag == 3 && (ti.PixelFormat == 40 || ti.PixelFormat == 4)) {
        const size_t x_body_start = m.DefOffset + 48;
        const size_t x_body_size  = m.DataSize;
        const bool   has_y_sub    = (m.Unknown_3 == 3) && (m.Unknown_5 > 0) &&
                                    (m.Unknown_4 == 48 + m.DataSize);
        const size_t y_body_start = m.DefOffset + (size_t)m.Unknown_4;
        const size_t y_body_size  = (size_t)m.Unknown_5;

        const bool ranges_ok =
            (x_body_start + x_body_size <= blob.size()) &&
            (!has_y_sub || (y_body_start + y_body_size <= blob.size()));

        if (ranges_ok) {
            std::vector<uint8_t> bc4_x, bc4_y;
            std::string err_x, err_y;
            const bool ok_x = lh_decode_variant_2_3_4(
                blob.data() + x_body_start, x_body_size, 2, w, h, bc4_x, &err_x);
            const bool ok_y = has_y_sub
                ? lh_decode_variant_2_3_4(
                      blob.data() + y_body_start, y_body_size, 2, w, h, bc4_y, &err_y)
                : false;

            if (ok_x) {
                const size_t n_blocks = (size_t)(w / 4) * (size_t)(h / 4);
                std::vector<uint8_t> bc5_blocks(n_blocks * 16);
                for (size_t i = 0; i < n_blocks; ++i) {

                    memcpy(bc5_blocks.data() + i * 16, bc4_x.data() + i * 8, 8);
                    if (ok_y) {

                        memcpy(bc5_blocks.data() + i * 16 + 8, bc4_y.data() + i * 8, 8);
                    } else {

                        bc5_blocks[i * 16 + 8] = 0x80;
                        bc5_blocks[i * 16 + 9] = 0x80;
                        for (int k = 10; k < 16; ++k) bc5_blocks[i * 16 + k] = 0;
                    }
                }
                blit_bc5_to_rgba(bc5_blocks.data(), w, h, rgba);
                out_w = w; out_h = h;
                if (out_has_alpha) *out_has_alpha = false;
                return true;
            } else {
                std::ostringstream os;
                os << "variant_2_3_4 BC5 X-channel (" << w << "x" << h
                   << ") failed: " << err_x << " — falling back to comp=7";

            }
        }
    }

    {

        const bool pf_is_bc1_family =
            (ti.PixelFormat == 35) || (ti.PixelFormat == 0) || (ti.PixelFormat == 12);
        const bool pf_is_bc3_family =
            (ti.PixelFormat == 39) || (ti.PixelFormat == 1) ||
            (ti.PixelFormat == 2)  || (ti.PixelFormat == 3);
        if (!pf_is_bc1_family && !pf_is_bc3_family) {

            int fallback_idx = -1;
            int fallback_area = 0;
            for (size_t i = 0; i < ti.Mips.size(); ++i) {
                if (ti.Mips[i].CompFlag != 7) continue;
                int fw = 0, fh = 0; mip_wh(i, fw, fh);
                int area = fw * fh;
                if (area > fallback_area) { fallback_area = area; fallback_idx = (int)i; }
            }
            if (fallback_idx < 0) {
                std::ostringstream os;
                os << "PixelFormat " << ti.PixelFormat
                   << " is compressed and no comp=7 fallback exists; "
                      "Lionhead codec port currently handles BC1 (35) only";
                DEC_FAIL("fallback_unhandled_pf");
            }
            const auto& fm = ti.Mips[fallback_idx];
            int fw = 0, fh = 0; mip_wh(fallback_idx, fw, fh);
            const size_t fbx = (size_t)((fw + 3) / 4);
            const size_t fby = (size_t)((fh + 3) / 4);
            if (fm.MipDataOffset + fm.MipDataSizeParsed > blob.size()) {
                DEC_FAIL("fallback_mip_oob");
            }
            const uint8_t* fsrc = blob.data() + fm.MipDataOffset;
            if (ti.PixelFormat == 39 || ti.PixelFormat == 1 ||
                ti.PixelFormat == 2  || ti.PixelFormat == 3) {
                if (fm.MipDataSizeParsed < fbx * fby * 16) {
                    DEC_FAIL("fallback_bc3_size_short");
                }
                std::vector<uint8_t> linear;
                untile_xbox360_bc(fsrc, fm.MipDataSizeParsed, linear, fw, fh, 16);
                swap_bc3_endian(linear.data(), linear.size());
                blit_bc3_to_rgba(linear.data(), fw, fh, rgba);
                out_w = fw; out_h = fh;
                if (out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
                return true;
            }
            if (ti.PixelFormat == 40) {

                if (fm.MipDataSizeParsed < fbx * fby * 16) {
                    DEC_FAIL("fallback_bc5_size_short");
                }
                std::vector<uint8_t> linear;
                untile_xbox360_bc(fsrc, fm.MipDataSizeParsed, linear, fw, fh, 16);
                swap_bc5_endian(linear.data(), linear.size());
                blit_bc5_to_rgba(linear.data(), fw, fh, rgba);
                out_w = fw; out_h = fh;
                if (out_has_alpha) *out_has_alpha = false;
                return true;
            }
            DEC_FAIL("fallback_no_match");
        }

        const size_t body_start = m.DefOffset + 48;
        const size_t body_size  = m.DataSize;
        if (body_start + body_size > blob.size()) {
            std::ostringstream os;
            os << "compressed mip body OOB (start=" << body_start
               << " size=" << body_size << " blob=" << blob.size() << ")";
            DEC_FAIL("comp_body_oob");
        }
        const uint8_t* body_ptr = blob.data() + body_start;

        std::vector<uint8_t> bc1;
        int dec_w = 0, dec_h = 0;
        std::string err;

        const bool comp11 = (m.CompFlag == 11);
        bool ok = lh_decode_compressed_mip(body_ptr, body_size,
                                           dec_w, dec_h, bc1, &err, comp11);
        if (!ok) {
            g_last_decode_info += " lh_err=\"" + err + "\"";
            DEC_FAIL("lh_decode_failed");
        }

        if (dec_w != w || dec_h != h) {
            std::ostringstream os;
            os << "WARNING: codec reported " << dec_w << "x" << dec_h
               << " but TexInfo says " << w << "x" << h << "; trusting codec";
            w = dec_w; h = dec_h;
        }
        blit_bc1_to_rgba(bc1.data(), w, h, rgba);
        out_w = w; out_h = h;

        if (pf_is_bc3_family && m.Unknown_5 > 0) {
            const size_t a_body_start = m.DefOffset + (size_t)m.Unknown_4;
            const size_t a_body_size  = (size_t)m.Unknown_5;
            const uint32_t a_cf = m.Unknown_3;

            if (a_body_start + a_body_size <= blob.size()) {
                std::vector<uint8_t> alpha_blocks;
                int adec_w = 0, adec_h = 0;
                std::string aerr;
                bool aok = false;

                if (a_cf == 1 || a_cf == 11) {

                    aok = lh_decode_compressed_mip(
                        blob.data() + a_body_start, a_body_size,
                        adec_w, adec_h, alpha_blocks, &aerr,
                        (a_cf == 11));
                } else if (a_cf == 3 || a_cf == 2 || a_cf == 4) {

                    aok = lh_decode_variant_2_3_4(
                        blob.data() + a_body_start, a_body_size,
                        2, w, h, alpha_blocks, &aerr);
                    adec_w = w; adec_h = h;
                } else if (a_cf == 7) {

                    const size_t expected = (size_t)((w + 3) / 4)
                                          * (size_t)((h + 3) / 4) * 8;
                    if (a_body_size >= expected) {
                        alpha_blocks.assign(blob.data() + a_body_start,
                                            blob.data() + a_body_start + expected);

                        for (size_t i = 0; i + 8 <= alpha_blocks.size(); i += 8) {
                            uint8_t* blk = alpha_blocks.data() + i;
                            uint64_t bits = 0;
                            for (int j = 0; j < 6; j++)
                                bits |= ((uint64_t)blk[2+j]) << (j * 8);
                            uint64_t sw = 0;
                            for (int j = 0; j < 6; j++)
                                sw |= ((bits >> (j * 8)) & 0xFF) << ((5 - j) * 8);
                            for (int j = 0; j < 6; j++)
                                blk[2 + j] = (uint8_t)((sw >> (j * 8)) & 0xFF);
                        }
                        adec_w = w; adec_h = h;
                        aok = true;
                    } else {
                        aerr = "alpha sub-block too small for raw BC4";
                    }
                } else {
                    std::ostringstream os;
                    os << "unhandled BC3 alpha CompFlag=" << a_cf;
                    aerr = os.str();
                }

                if (aok && adec_w == w && adec_h == h) {
                    const size_t bx_n = (size_t)((w + 3) / 4);
                    const size_t by_n = (size_t)((h + 3) / 4);
                    if (alpha_blocks.size() >= bx_n * by_n * 8) {
                        for (size_t byy = 0; byy < by_n; ++byy) {
                            for (size_t bxx = 0; bxx < bx_n; ++bxx) {
                                uint8_t avals[16];
                                decode_bc4_block(alpha_blocks.data() + (byy * bx_n + bxx) * 8,
                                                 avals);
                                for (int py = 0; py < 4; ++py) {
                                    int yy = (int)byy * 4 + py;
                                    if (yy >= h) break;
                                    for (int px = 0; px < 4; ++px) {
                                        int xx = (int)bxx * 4 + px;
                                        if (xx >= w) break;
                                        rgba[(yy * w + xx) * 4 + 3] = avals[py * 4 + px];
                                    }
                                }
                            }
                        }
                    }
                } else if (!aok) {

                }
            }
        }

        if (out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
        return true;
    }
}
static bool extract_tex_bytes_by_candidate(const std::vector<std::string>& candidates, std::vector<unsigned char>& out){
    auto pOpt = find_any_textures_bnk();
    if(!pOpt) return false;
    BNKReader r(*pOpt);
    std::vector<std::string> wanted;
    for(const auto& c : candidates){
        if(c.empty()) continue;
        wanted.push_back(tolower_copy(c));
        std::string fname = std::filesystem::path(c).filename().string();
        wanted.push_back(tolower_copy(fname));
        wanted.push_back(tolower_copy(force_tex_ext(c)));
        wanted.push_back(tolower_copy(force_tex_ext(fname)));
        wanted.push_back(basename_lower_noext(c));
    }
    std::sort(wanted.begin(), wanted.end());
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
    int best_idx = -1;
    size_t best_area = 0;
    for(size_t i=0;i<r.list_files().size();++i){
        const auto& e = r.list_files()[i];
        std::string fn = std::filesystem::path(e.name).filename().string();
        std::string fn_low = tolower_copy(fn);
        std::string fn_base_noext = basename_lower_noext(fn);
        bool match = false;
        for(const auto& w : wanted){
            if(fn_low == w || fn_base_noext == w){ match = true; break; }
        }
        if(!match) continue;
        std::vector<unsigned char> blob;
        try{
            auto dir = std::filesystem::temp_directory_path()/ "f2_tex_pick";
            std::error_code ec; std::filesystem::create_directories(dir, ec);
            auto outp = dir/("tex_"+std::to_string((uint64_t)i)+".bin");
            extract_one(*pOpt, (int)i, outp.string());
            blob = read_all_bytes(outp);
            std::filesystem::remove(outp, ec);
        }catch(...){ continue; }
        if(blob.empty()) continue;
        TexInfo ti{};
        if(!parse_tex_info(blob, ti)) continue;

        if (ti.Mips.empty()) continue;
        size_t area = (size_t)ti.TextureWidth * (size_t)ti.TextureHeight;
        if(area > best_area){ best_area = area; best_idx = (int)i; out.swap(blob); }
    }
    return best_idx >= 0 && !out.empty();
}
void FlyCam_Reset(FlyCam& cam, float cx, float cy, float cz, float radius) {
    cam.pos[0] = cx;
    cam.pos[1] = cy;
    cam.pos[2] = cz + radius * 3.0f;
    cam.yaw = 3.14159265f;
    cam.pitch = 0.0f;
    cam.move_speed = radius * 2.0f;
    cam.is_looking = false;
}
void FlyCam_Update(FlyCam& cam, float dt, bool w, bool s, bool a, bool d, bool q, bool e, float mouse_dx, float mouse_dy) {
    if (cam.is_looking) {
        const float sx = S.cam_invert_x ? -1.0f : 1.0f;
        const float sy = S.cam_invert_y ? -1.0f : 1.0f;
        cam.yaw   -= sx * mouse_dx * cam.look_sensitivity;
        cam.pitch += sy * mouse_dy * cam.look_sensitivity;
        const float max_pitch = 1.5f;
        if (cam.pitch > max_pitch) cam.pitch = max_pitch;
        if (cam.pitch < -max_pitch) cam.pitch = -max_pitch;
    }
    float cy = cosf(cam.yaw);
    float sy = sinf(cam.yaw);
    float cp = cosf(cam.pitch);
    float sp = sinf(cam.pitch);
    float forward[3] = { sy * cp, sp, cy * cp };
    float right[3] = { cy, 0.0f, -sy };
    float up[3] = { 0.0f, 1.0f, 0.0f };
    float speed = cam.move_speed * dt;
    if (w) {
        cam.pos[0] += forward[0] * speed;
        cam.pos[1] += forward[1] * speed;
        cam.pos[2] += forward[2] * speed;
    }
    if (s) {
        cam.pos[0] -= forward[0] * speed;
        cam.pos[1] -= forward[1] * speed;
        cam.pos[2] -= forward[2] * speed;
    }
    if (a) {
        cam.pos[0] += right[0] * speed;
        cam.pos[1] += right[1] * speed;
        cam.pos[2] += right[2] * speed;
    }
    if (d) {
        cam.pos[0] -= right[0] * speed;
        cam.pos[1] -= right[1] * speed;
        cam.pos[2] -= right[2] * speed;
    }
    if (e) {
        cam.pos[1] += speed;
    }
    if (q) {
        cam.pos[1] -= speed;
    }
}
#ifdef _WIN32
static void mp_release_mesh(MPPerMesh& m){
    if(m.vb){ m.vb->Release(); m.vb=nullptr; }
    if(m.ib){ m.ib->Release(); m.ib=nullptr; }
    if(m.srv_diffuse){ m.srv_diffuse->Release(); m.srv_diffuse=nullptr; }
    if(m.srv_normal){ m.srv_normal->Release(); m.srv_normal=nullptr; }
    if(m.srv_specular){ m.srv_specular->Release(); m.srv_specular=nullptr; }
    if(m.srv_metallic){ m.srv_metallic->Release(); m.srv_metallic=nullptr; }
    if(m.srv_extra){ m.srv_extra->Release(); m.srv_extra=nullptr; }
    m.index_count = 0;
}
static void mp_release(ModelPreview& mp){
    for(auto& m: mp.meshes) mp_release_mesh(m);
    mp.meshes.clear();
    if(mp.vs){ mp.vs->Release(); mp.vs=nullptr; }
    if(mp.ps){ mp.ps->Release(); mp.ps=nullptr; }
    if(mp.vs_terrain){ mp.vs_terrain->Release(); mp.vs_terrain=nullptr; }
    if(mp.ps_terrain){ mp.ps_terrain->Release(); mp.ps_terrain=nullptr; }
    if(mp.cbuffer_terrain){ mp.cbuffer_terrain->Release(); mp.cbuffer_terrain=nullptr; }
    if(mp.vs_water){ mp.vs_water->Release(); mp.vs_water=nullptr; }
    if(mp.ps_water){ mp.ps_water->Release(); mp.ps_water=nullptr; }
    if(mp.cbuffer_water){ mp.cbuffer_water->Release(); mp.cbuffer_water=nullptr; }
    if(mp.sampler_point){ mp.sampler_point->Release(); mp.sampler_point=nullptr; }
    if(mp.layout){ mp.layout->Release(); mp.layout=nullptr; }
    if(mp.cbuffer){ mp.cbuffer->Release(); mp.cbuffer=nullptr; }
    if(mp.bone_cb){ mp.bone_cb->Release(); mp.bone_cb=nullptr; }
    if(mp.sampler){ mp.sampler->Release(); mp.sampler=nullptr; }
    if(mp.rs){ mp.rs->Release(); mp.rs=nullptr; }
    if(mp.rs_wire){ mp.rs_wire->Release(); mp.rs_wire=nullptr; }
    if(mp.bs){ mp.bs->Release(); mp.bs=nullptr; }
    if(mp.bsAlpha){ mp.bsAlpha->Release(); mp.bsAlpha=nullptr; }
    if(mp.rtv){ mp.rtv->Release(); mp.rtv=nullptr; }
    if(mp.srv){ mp.srv->Release(); mp.srv=nullptr; }
    if(mp.color){ mp.color->Release(); mp.color=nullptr; }
    if(mp.dsv){ mp.dsv->Release(); mp.dsv=nullptr; }
    if(mp.depth){ mp.depth->Release(); mp.depth=nullptr; }
    if(mp.default_srv){ mp.default_srv->Release(); mp.default_srv=nullptr; }
    if(mp.dssWrite){ mp.dssWrite->Release(); mp.dssWrite=nullptr; }
    if(mp.dssNoWrite){ mp.dssNoWrite->Release(); mp.dssNoWrite=nullptr; }
    mp.has_model = false;
}
static bool compile_shader(const char* src, const char* entry, const char* profile, ID3DBlob** blob){
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, profile, flags, 0, blob, &err);
    if(err){ err->Release(); }
    return SUCCEEDED(hr);
}
static const char* g_vs = R"(
cbuffer CB : register(b0){
    float4x4 mvp;
    float4   lightDir;
    float4x4 mv;
    float4   params;
}

cbuffer Bones : register(b1){
    row_major float4x4 bones[256];
}
struct VSIN{
    float3 p   : POSITION;
    float3 n   : NORMAL;
    float2 t   : TEXCOORD0;
    uint4  bid : BONEIDS;
    float4 bw  : BONEWEIGHTS;
};
struct VSOUT{ float4 p:SV_Position; float3 n:NORMAL; float2 t:TEXCOORD0; };
VSOUT VS(VSIN i){
    VSOUT o;

    float4x4 skin =
        bones[i.bid.x] * i.bw.x +
        bones[i.bid.y] * i.bw.y +
        bones[i.bid.z] * i.bw.z +
        bones[i.bid.w] * i.bw.w;

    float4 p_skin = mul(float4(i.p, 1.0), skin);
    float3 n_skin = mul(i.n, (float3x3)skin);

    o.p = mul(p_skin, mvp);
    float3 n = mul(n_skin, (float3x3)mv);
    o.n = normalize(n);
    o.t = i.t;
    return o;
}
)";
static const char* g_ps = R"(
cbuffer CB : register(b0){
    float4x4 mvp;
    float4   lightDir;
    float4x4 mv;
    float4   params;
}
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
Texture2D tex3 : register(t3);
Texture2D tex4 : register(t4);
SamplerState smp : register(s0);
struct VSOUT{ float4 p:SV_Position; float3 n:NORMAL; float2 t:TEXCOORD0; };
float4 PS(VSOUT i) : SV_Target {

    float4 diffSamp = tex0.Sample(smp, i.t);
    float3 albedo   = diffSamp.rgb;
    float  alpha    = diffSamp.a;

    float3 nSamp = tex1.Sample(smp, i.t).rgb;
    bool   nIsDefault = (nSamp.r > 0.98 && nSamp.g > 0.98 && nSamp.b > 0.98);
    float3 N_geo = normalize(i.n);
    float3 N     = N_geo;
    if (!nIsDefault) {
        float3 N_m = nSamp * 2.0 - 1.0;
        N = normalize(N_geo + N_m * 0.5);
    }

    float3 L = normalize(float3(0.3, 0.7, 0.5));
    float3 V = float3(0.0, 0.0, 1.0);
    float3 H = normalize(L + V);

    float ndotl = abs(dot(N, L));
    float diff_term = 0.55 + 0.45 * ndotl;

    float3 sSamp = tex2.Sample(smp, i.t).rgb;
    bool   sIsDefault = (sSamp.r > 0.98 && sSamp.g > 0.98 && sSamp.b > 0.98);
    float  spec_mask  = sIsDefault ? 0.0 : sSamp.r;
    float  ndoth = saturate(abs(dot(N, H)));
    float  spec  = pow(ndoth, 24.0) * spec_mask * 0.6;

    float3 color = albedo * diff_term + spec.xxx;

    if (params.z > 0.5) {
        float3 hi = float3(0.15, 0.45, 1.00);
        color = lerp(color, hi, 0.65);
    }

    if (alpha < 0.25) discard;

    return float4(color, alpha);
}
)";

static const char* g_terrain_vs = R"(
cbuffer CB : register(b0){
    float4x4 mvp;
    float4   lightDir;
    float4x4 mv;
    float4   params;
}
cbuffer Bones : register(b1){
    row_major float4x4 bones[256];
}
struct VSIN{
    float3 p   : POSITION;
    float3 n   : NORMAL;
    float2 t   : TEXCOORD0;
    uint4  bid : BONEIDS;
    float4 bw  : BONEWEIGHTS;
};
struct VSOUT{
    float4 p   : SV_Position;
    float3 n   : NORMAL;
    float2 t   : TEXCOORD0;
    float3 wp  : TEXCOORD1;
};
VSOUT VS(VSIN i){
    VSOUT o;
    o.p  = mul(float4(i.p, 1.0), mvp);
    o.n  = normalize(i.n);
    o.t  = i.t;
    o.wp = i.p;
    return o;
}
)";

static const char* g_terrain_ps = R"(
cbuffer CB : register(b0){
    float4x4 mvp;
    float4   lightDir;
    float4x4 mv;
    float4   params;
}
cbuffer TerrainCB : register(b2){
    /* xy = world origin (XZ), zw = chunk extent (XZ) */
    float4 chunk_origin_extent;
    /* x = chunk_w, y = chunk_h, z = lod_count, w = max blend (255 max in u8) */
    float4 chunk_grid_size;
    /* x = blend_scale  (u8 / blend_scale → [0..1])
       y = world-space material texture scale */
    float4 splat_params;
    /* Affine mesh→world transform for chunk lookup:
       world_xy = mesh_xy * mesh_xform.xy + mesh_xform.zw
       Needed when the mesh was built with the wrong tile_size — for
       chapter3 the .ghf has tile_size=0 so the mesh ends up 2× the
       true world extent.  This is the cheap fix: shader rescales. */
    float4 mesh_xform;
}
Texture2DArray  lod_array     : register(t0);
Texture2DArray  chunk_idx     : register(t1);
Texture2DArray  chunk_blend   : register(t2);
Texture2D       lightmap      : register(t3);
SamplerState    smp_wrap      : register(s0);
SamplerState    smp_point     : register(s1);

struct VSOUT{
    float4 p   : SV_Position;
    float3 n   : NORMAL;
    float2 t   : TEXCOORD0;
    float3 wp  : TEXCOORD1;
};

float3 sample_lod(int slice, float2 uv){
    /* Sample uses screen-space derivatives implicitly → GPU mipmap. */
    return lod_array.Sample(smp_wrap, float3(uv, slice)).rgb;
}

float4 PS(VSOUT i) : SV_Target {
    /* Map mesh-space → world-space (chunk-grid space).  Mesh may be
       at a different scale than the .ehf chunk world when .ghf tile
       data is bogus (chapter3 has tile_size=0 in .ghf, so the mesh
       gets built with the fallback tile=1 → 2× too big). */
    float2 mesh_xy  = float2(i.wp.x, i.wp.z);
    float2 world_xy = mesh_xy * mesh_xform.xy + mesh_xform.zw;
    float2 origin   = chunk_origin_extent.xy;
    float2 extent   = chunk_origin_extent.zw;
    float  CW       = chunk_grid_size.x;
    float  CH       = chunk_grid_size.y;
    float  max_lod  = chunk_grid_size.z;
    float2 mat_uv   = world_xy * splat_params.y;

    /* Map world → chunk-grid coords [0, CW] × [0, CH]. */
    float2 chunk_co = (world_xy - origin) / extent;

    /* Clamp to valid chunk range. */
    float2 chunk_clamped = clamp(chunk_co,
                                 float2(0, 0),
                                 float2(CW - 0.001, CH - 0.001));
    int2   chunk_xy = int2(floor(chunk_clamped));
    float2 corner_uv = frac(chunk_clamped);

    /* 4 bilinear corner weights. */
    float wx = corner_uv.x, wy = corner_uv.y;
    float w00 = (1.0 - wx) * (1.0 - wy);
    float w10 =        wx  * (1.0 - wy);
    float w01 = (1.0 - wx) *        wy ;
    float w11 =        wx  *        wy ;

    float3 final = float3(0.0, 0.0, 0.0);
    float  alpha_sum = 0.0;

    /* Walk up to 16 layers, alpha-stacking.  Runtime loop with
       early-out at the 0xFF sentinel so most chunks (which have only
       2-3 layers) skip the unused slots cheaply. */
    [loop]
    for (int L = 0; L < 16; ++L) {
        float4 idx_norm = chunk_idx.Load(int4(chunk_xy, L, 0));
        float4 idx255   = round(idx_norm * 255.0);
        if (idx255.x > 254.5) break;

        float4 bln_norm = chunk_blend.Load(int4(chunk_xy, L, 0));
        /* blend_scale lets us recover the original 0..max_blend range
           (chapter3's max was 3.0 so we map u8 [0,255] → [0, max]) */
        float bscale = splat_params.x;
        float4 bln   = bln_norm * 255.0 / bscale;

        /* Sample 4 corner textures.  Indices may legitimately equal
           each other; this is fine, the shader just does 4 samples. */
        float3 c00 = sample_lod((int)idx255.x, mat_uv);
        float3 c10 = sample_lod((int)idx255.y, mat_uv);
        float3 c01 = sample_lod((int)idx255.z, mat_uv);
        float3 c11 = sample_lod((int)idx255.w, mat_uv);

        /* Bilinear-blend the 4 corner samples by position weights. */
        float3 lc = c00 * w00 + c10 * w10 + c01 * w01 + c11 * w11;

        /* Bilinear-blend the 4 corner blend amounts → per-pixel alpha. */
        float la = saturate(bln.x * w00 + bln.y * w10
                          + bln.z * w01 + bln.w * w11);

        /* Alpha-over composite. */
        float one_minus = 1.0 - la;
        final     = final     * one_minus + lc * la;
        alpha_sum = saturate(alpha_sum + la * (1.0 - alpha_sum));
    }

    /* Fallback: if no layer contributed (e.g. all blends zero), use
       LOD slice 0 directly so we never render black terrain.       */
    if (alpha_sum < 0.05) {
        final = sample_lod(0, mat_uv);
    }

    /* Lightmap AO modulation.  Lightmap is sized to the heightfield;
       map chunk_co (in [0, CW]×[0, CH]) to lightmap UV [0,1].      */
    float2 lm_uv = chunk_co / float2(CW, CH);
    float  ao    = lightmap.Sample(smp_wrap, lm_uv).r;
    final *= (ao * 0.55 + 0.45);

    /* Cheap headlamp-like diffuse term. */
    float3 N = normalize(i.n);
    float3 L = normalize(float3(0.3, 0.7, 0.5));
    float  ndotl = saturate(dot(N, L));
    float  shade = 0.55 + 0.45 * ndotl;
    final *= shade;

    /* Highlight tint (same as main shader). */
    if (params.z > 0.5) {
        float3 hi = float3(0.15, 0.45, 1.00);
        final = lerp(final, hi, 0.65);
    }

    return float4(final, 1.0);
}
)";

static const char* g_terrain_ps_live = R"(
cbuffer CB : register(b0){
    float4x4 mvp;
    float4   lightDir;
    float4x4 mv;
    float4   params;
}
cbuffer TerrainCB : register(b2){
    float4 chunk_origin_extent;
    float4 chunk_grid_size;
    float4 splat_params;
    float4 mesh_xform;
    float4 weight_params;
    float4 material_params[32];
}
Texture2DArray  lod_array                : register(t0);
Texture2DArray  chunk_idx                 : register(t1);
Texture2DArray  chunk_blend               : register(t2);
Texture2DArray  chunk_uv                  : register(t3);
Texture2D       splat_mask                : register(t4);
Texture2D       lightmap                  : register(t5);
Texture2DArray  lod_detail_array          : register(t6);
Texture2DArray  material_weights          : register(t7);
Texture2DArray  lod_normal_array          : register(t8);
Texture2DArray  lod_detail_normal_array   : register(t9);
SamplerState    smp_wrap                  : register(s0);
SamplerState    smp_point                 : register(s1);

struct VSOUT{
    float4 p   : SV_Position;
    float3 n   : NORMAL;
    float2 t   : TEXCOORD0;
    float3 wp  : TEXCOORD1;
};

void sample_material(int slice, float2 world_xy, float slope_w,
                     out float3 out_color, out float3 out_normal_ts){
    int max_slice = max((int)chunk_grid_size.z - 1, 0);
    int s = min(max(slice, 0), max_slice);
    float4 mp = material_params[s];
    float base_scale = (mp.x > 0.0) ? mp.x : splat_params.y;
    float detail_scale = (mp.y > 0.0) ? mp.y : base_scale;
    float base_intensity = (mp.z > 0.0) ? mp.z : 1.0;
    float3 base = lod_array.Sample(
        smp_wrap, float3(world_xy * base_scale, s)).rgb;
    float3 detail = lod_detail_array.Sample(
        smp_wrap, float3(world_xy * detail_scale, s)).rgb;
    float3 base_n = lod_normal_array.Sample(
        smp_wrap, float3(world_xy * base_scale, s)).rgb * 2.0 - 1.0;
    float3 detail_n = lod_detail_normal_array.Sample(
        smp_wrap, float3(world_xy * detail_scale, s)).rgb * 2.0 - 1.0;
    float detail_w = slope_w * saturate(mp.w);
    out_color = lerp(base * base_intensity, detail, detail_w);
    out_normal_ts = lerp(base_n, detail_n, detail_w);
}

float4 PS(VSOUT i) : SV_Target {
    float2 mesh_xy  = float2(i.wp.x, i.wp.z);
    float2 world_xy = mesh_xy * mesh_xform.xy + mesh_xform.zw;
    float2 origin   = chunk_origin_extent.xy;
    float2 extent   = chunk_origin_extent.zw;
    float  CW       = chunk_grid_size.x;
    float  CH       = chunk_grid_size.y;
    float3 geo_n    = normalize(i.n);
    float slope_w   = saturate((0.82 - abs(geo_n.y)) / 0.35);

    float2 chunk_co = (world_xy - origin) / extent;
    float2 chunk_clamped = clamp(chunk_co,
                                 float2(0.0, 0.0),
                                 float2(CW - 0.001, CH - 0.001));
    int2   chunk_xy = int2(floor(chunk_clamped));
    float2 local_uv = frac(chunk_clamped);
    float wx = local_uv.x;
    float wy = local_uv.y;
    float w00 = (1.0 - wx) * (1.0 - wy);
    float w10 =        wx  * (1.0 - wy);
    float w01 = (1.0 - wx) *        wy ;
    float w11 =        wx  *        wy ;

    int material_count = min((int)chunk_grid_size.z, 32);
    float3 final_color = float3(0.0, 0.0, 0.0);
    float3 final_n_ts  = float3(0.0, 0.0, 0.0);
    float3 first_color = float3(0.0, 0.0, 0.0);
    float3 first_n_ts  = float3(0.0, 0.0, 0.0);
    float  coverage    = 0.0;
    bool   have_first  = false;

    [loop]
    for (int L = 0; L < 16; ++L) {
        float4 idx255 = round(chunk_idx.Load(int4(chunk_xy, L, 0)) * 255.0);
        if (idx255.x > 254.5) break;

        int material = clamp((int)idx255.x, 0, material_count - 1);
        float4 bln = chunk_blend.Load(int4(chunk_xy, L, 0))
                   * (255.0 / max(splat_params.x, 1.0));
        float layer_alpha = saturate(bln.x * w00 + bln.y * w10
                                   + bln.z * w01 + bln.w * w11);

        float2 mask_base = chunk_uv.Load(int4(chunk_xy, L, 0)).xy;
        float2 mask_uv = mask_base + local_uv * splat_params.zw;
        layer_alpha *= splat_mask.SampleLevel(smp_point, mask_uv, 0).r;

        float3 mc, mn;
        sample_material(material, world_xy, slope_w, mc, mn);
        if (!have_first) {
            first_color = mc;
            first_n_ts = mn;
            have_first = true;
        }

        if (layer_alpha <= 0.001) continue;
        float keep = 1.0 - layer_alpha;
        final_color = final_color * keep + mc * layer_alpha;
        final_n_ts  = final_n_ts  * keep + mn * layer_alpha;
        coverage = coverage * keep + layer_alpha;
    }

    if (coverage < 0.999 && have_first) {
        float fill = 1.0 - coverage;
        final_color += first_color * fill;
        final_n_ts  += first_n_ts  * fill;
        coverage = 1.0;
    }

    if (coverage <= 0.001) {
        sample_material(0, world_xy, slope_w, final_color, final_n_ts);
    }

    // Build a simple TBN around the geometric normal so the
    // tangent-space normal map can perturb the lighting.
    float3 N = geo_n;
    float3 ref = (abs(N.y) < 0.999) ? float3(0.0, 1.0, 0.0)
                                    : float3(1.0, 0.0, 0.0);
    float3 T = normalize(cross(ref, N));
    float3 B = normalize(cross(N, T));
    float3 nrm = normalize(
        T * final_n_ts.x + B * final_n_ts.y + N * max(final_n_ts.z, 0.1));

    float3 L = normalize(-lightDir.xyz);
    float ndotl = saturate(dot(nrm, L));
    float ambient = 0.35;
    float lit = ambient + ndotl * 0.7;
    final_color *= lit;

    if (params.z > 0.5) {
        float3 hi = float3(0.15, 0.45, 1.00);
        final_color = lerp(final_color, hi, 0.65);
    }

    return float4(final_color, 1.0);
}
)";

static const char* g_water_vs = R"(
cbuffer CB : register(b0){
    float4x4 mvp;
    float4   lightDir;
    float4x4 mv;
    float4   params;       // params.w = time (seconds)
}
cbuffer WaterCB : register(b3){
    /* xy = wave dir 0 (unit XZ), z = wavelength 0, w = amplitude 0 */
    float4 wave0;
    float4 wave1;
    float4 wave2;
    float4 wave3;
    /* x = scroll_speed_0, y = scroll_speed_1, z = normal_intensity,
       w = base_water_y                                                */
    float4 wparams;
}
struct VSIN{
    float3 p   : POSITION;
    float3 n   : NORMAL;
    float2 t   : TEXCOORD0;
    uint4  bid : BONEIDS;
    float4 bw  : BONEWEIGHTS;
};
struct VSOUT{
    float4 p   : SV_Position;
    float3 n   : NORMAL;
    float2 t   : TEXCOORD0;
    float3 wp  : TEXCOORD1;
};

float wave_disp(float4 w, float2 xz, float t){
    if (w.z <= 0.0001) return 0.0;
    float k = 6.28318 / w.z;                  // 2π / wavelength
    float phase = dot(w.xy, xz) * k + t * w.z * 2.0;
    return sin(phase) * w.w;
}

VSOUT VS(VSIN i){
    VSOUT o;
    float3 p = i.p;
    float2 xz = float2(p.x, p.z);
    float t = params.w;
    float dy = wave_disp(wave0, xz, t) +
               wave_disp(wave1, xz, t) +
               wave_disp(wave2, xz, t) +
               wave_disp(wave3, xz, t);
    p.y += dy;
    o.p  = mul(float4(p, 1.0), mvp);
    o.n  = float3(0.0, 1.0, 0.0);
    o.t  = i.t;
    o.wp = p;
    return o;
}
)";

static const char* g_water_ps = R"(
cbuffer CB : register(b0){
    float4x4 mvp;
    float4   lightDir;
    float4x4 mv;
    float4   params;       // params.w = time (seconds)
}
cbuffer WaterCB : register(b3){
    float4 wave0;
    float4 wave1;
    float4 wave2;
    float4 wave3;
    float4 wparams;        // x=scroll0, y=scroll1, z=normal_intensity, w=base_y
}
Texture2D    tex_normal : register(t0);
SamplerState smp        : register(s0);

struct PSIN{
    float4 p   : SV_Position;
    float3 n   : NORMAL;
    float2 t   : TEXCOORD0;
    float3 wp  : TEXCOORD1;
};

float4 PS(PSIN i) : SV_Target {
    float t = params.w;

    // Two scrolling samples of the normal map summed together for a
    // cheap "rippling" surface.
    float2 uv0 = i.t * 0.20 + float2( 0.0,  1.0) * t * wparams.x;
    float2 uv1 = i.t * 0.13 + float2( 1.0, -0.4) * t * wparams.y;
    float3 n0 = tex_normal.Sample(smp, uv0).rgb * 2.0 - 1.0;
    float3 n1 = tex_normal.Sample(smp, uv1).rgb * 2.0 - 1.0;
    float3 n_ts = normalize(n0 + n1);
    n_ts.xy *= wparams.z;
    float3 N = normalize(float3(n_ts.x, max(n_ts.z, 0.1) + 0.6, n_ts.y));

    float3 L = normalize(-lightDir.xyz);
    float ndotl = saturate(dot(N, L));
    float ambient = 0.35;

    // Two-tone water colour: deep blue at glancing angles, lighter
    // greenish at shallow viewing angles.
    float3 deep    = float3(0.02, 0.10, 0.18);
    float3 shallow = float3(0.18, 0.42, 0.48);
    // We don't have a real view vector handy without a camera CB, so
    // approximate Fresnel with the projected screen-space Y from
    // SV_Position — works well enough for a flat surface viewed from
    // above.
    float fresnel = saturate(1.0 - N.y);
    float3 base = lerp(deep, shallow, 1.0 - fresnel * 0.5);
    base *= (ambient + ndotl * 0.7);

    // Specular highlight along light direction.
    float3 H = normalize(L + float3(0.0, 1.0, 0.0));
    float spec = pow(saturate(dot(N, H)), 64.0);
    base += float3(1.0, 1.0, 0.95) * spec * 0.8;

    if (params.z > 0.5) {
        float3 hi = float3(0.15, 0.45, 1.00);
        base = lerp(base, hi, 0.65);
    }

    return float4(base, 0.78);
}
)";

static bool create_white_srv(ID3D11Device* dev, ID3D11ShaderResourceView** out_srv){
    *out_srv = nullptr;
    UINT px = 0xFFFFFFFFu;
    D3D11_TEXTURE2D_DESC td{}; td.Width=1; td.Height=1; td.MipLevels=1; td.ArraySize=1; td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_IMMUTABLE; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem=&px; sd.SysMemPitch=4;
    ID3D11Texture2D* tex=nullptr; if(FAILED(dev->CreateTexture2D(&td,&sd,&tex))) return false;
    ID3D11ShaderResourceView* srv=nullptr; if(FAILED(dev->CreateShaderResourceView(tex,nullptr,&srv))){ tex->Release(); return false; }
    tex->Release(); *out_srv=srv; return true;
}
ID3D11ShaderResourceView* create_srv_from_rgba(ID3D11Device* dev, int w, int h, const std::vector<uint8_t>& rgba){
    constexpr int kMaxUploadDim = 8192;
    if (!dev || w <= 0 || h <= 0 || w > kMaxUploadDim || h > kMaxUploadDim) {
        OutputLog::warn("texture upload skipped: invalid RGBA dimensions " +
                        std::to_string(w) + "x" + std::to_string(h));
        return nullptr;
    }

    const uint64_t expected =
        uint64_t(w) * uint64_t(h) * uint64_t(4);
    if (expected == 0 ||
        expected > std::numeric_limits<UINT>::max() ||
        rgba.size() < expected) {
        OutputLog::warn("texture upload skipped: invalid RGBA payload " +
                        std::to_string(w) + "x" + std::to_string(h) +
                        " bytes=" + std::to_string(rgba.size()));
        return nullptr;
    }

    D3D11_TEXTURE2D_DESC td{};
    td.Width = (UINT)w;
    td.Height = (UINT)h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = rgba.data();
    sd.SysMemPitch = (UINT)w * 4u;
    sd.SysMemSlicePitch = (UINT)expected;

    ID3D11Texture2D* t = nullptr;
    if (FAILED(dev->CreateTexture2D(&td, &sd, &t))) {
        OutputLog::warn("texture upload failed: CreateTexture2D " +
                        std::to_string(w) + "x" + std::to_string(h));
        return nullptr;
    }
    ID3D11ShaderResourceView* v = nullptr;
    if (FAILED(dev->CreateShaderResourceView(t, nullptr, &v))) {
        OutputLog::warn("texture upload failed: CreateShaderResourceView " +
                        std::to_string(w) + "x" + std::to_string(h));
        t->Release();
        return nullptr;
    }
    t->Release();
    return v;
}
static bool srv_from_tex_blob_auto(ID3D11Device* dev, const std::vector<unsigned char>& blob, ID3D11ShaderResourceView** out_srv, bool* out_has_alpha){
    *out_srv = nullptr;
    std::vector<uint8_t> rgba;
    int w, h;
    if(!decode_tex_to_rgba(blob, rgba, w, h, out_has_alpha)) return false;
    *out_srv = create_srv_from_rgba(dev, w, h, rgba);
    return (*out_srv != nullptr);
}
static bool create_target(ID3D11Device* dev, ModelPreview& mp, int w, int h){
    if(mp.rtv){ mp.rtv->Release(); mp.rtv=nullptr; }
    if(mp.srv){ mp.srv->Release(); mp.srv=nullptr; }
    if(mp.color){ mp.color->Release(); mp.color=nullptr; }
    if(mp.dsv){ mp.dsv->Release(); mp.dsv=nullptr; }
    if(mp.depth){ mp.depth->Release(); mp.depth=nullptr; }
    mp.width = w; mp.height = h;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w; td.Height = h; td.MipLevels=1; td.ArraySize=1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count=1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if(FAILED(dev->CreateTexture2D(&td, nullptr, &mp.color))) return false;
    if(FAILED(dev->CreateRenderTargetView(mp.color, nullptr, &mp.rtv))) return false;
    if(FAILED(dev->CreateShaderResourceView(mp.color, nullptr, &mp.srv))) return false;
    D3D11_TEXTURE2D_DESC dd{};
    dd.Width=w; dd.Height=h; dd.MipLevels=1; dd.ArraySize=1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count=1;
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if(FAILED(dev->CreateTexture2D(&dd,nullptr,&mp.depth))) return false;
    if(FAILED(dev->CreateDepthStencilView(mp.depth,nullptr,&mp.dsv))) return false;
    return true;
}
static bool create_pipeline(ID3D11Device* dev, ModelPreview& mp){
    ID3DBlob* vsb=nullptr; ID3DBlob* psb=nullptr;
    if(!compile_shader(g_vs,"VS","vs_5_0",&vsb)) return false;
    if(!compile_shader(g_ps,"PS","ps_5_0",&psb)){ if(vsb) vsb->Release(); return false; }
    if(FAILED(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &mp.vs))){ vsb->Release(); psb->Release(); return false; }
    if(FAILED(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &mp.ps))){ vsb->Release(); psb->Release(); return false; }

    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BONEIDS",    0, DXGI_FORMAT_R8G8B8A8_UINT,      0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BONEWEIGHTS",0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if(FAILED(dev->CreateInputLayout(il,5,vsb->GetBufferPointer(),vsb->GetBufferSize(),&mp.layout))){ vsb->Release(); psb->Release(); return false; }
    vsb->Release(); psb->Release();
    struct CB { XMFLOAT4X4 mvp; XMFLOAT4 lightDir; XMFLOAT4X4 mv; XMFLOAT4 params; };
    D3D11_BUFFER_DESC cbd{};
    cbd.BindFlags=D3D11_BIND_CONSTANT_BUFFER; cbd.ByteWidth=sizeof(CB); cbd.Usage=D3D11_USAGE_DYNAMIC; cbd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    if(FAILED(dev->CreateBuffer(&cbd,nullptr,&mp.cbuffer))) return false;

    D3D11_BUFFER_DESC bcb{};
    bcb.BindFlags     = D3D11_BIND_CONSTANT_BUFFER;
    bcb.ByteWidth     = (UINT)(MP_MAX_BONES * sizeof(XMFLOAT4X4));
    bcb.Usage         = D3D11_USAGE_DYNAMIC;
    bcb.CPUAccessFlags= D3D11_CPU_ACCESS_WRITE;
    if(FAILED(dev->CreateBuffer(&bcb, nullptr, &mp.bone_cb))) return false;
    D3D11_SAMPLER_DESC ssd{}; ssd.Filter=D3D11_FILTER_ANISOTROPIC; ssd.MaxAnisotropy=16; ssd.AddressU=ssd.AddressV=ssd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP; ssd.MaxLOD=D3D11_FLOAT32_MAX;
    if(FAILED(dev->CreateSamplerState(&ssd,&mp.sampler))) return false;

    {
        ID3DBlob* tvsb = nullptr; ID3DBlob* tpsb = nullptr;
        if (compile_shader(g_terrain_vs, "VS", "vs_5_0", &tvsb) &&
            compile_shader(g_terrain_ps_live, "PS", "ps_5_0", &tpsb))
        {
            dev->CreateVertexShader(tvsb->GetBufferPointer(),
                                    tvsb->GetBufferSize(),
                                    nullptr, &mp.vs_terrain);
            dev->CreatePixelShader(tpsb->GetBufferPointer(),
                                   tpsb->GetBufferSize(),
                                   nullptr, &mp.ps_terrain);
        }
        if (tvsb) tvsb->Release();
        if (tpsb) tpsb->Release();
    }
    {
        D3D11_BUFFER_DESC tcb{};
        tcb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        tcb.ByteWidth = 80 + 32 * 16;
        tcb.Usage = D3D11_USAGE_DYNAMIC;
        tcb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&tcb, nullptr, &mp.cbuffer_terrain);
    }

    {
        ID3DBlob* wvsb = nullptr; ID3DBlob* wpsb = nullptr;
        if (compile_shader(g_water_vs, "VS", "vs_5_0", &wvsb) &&
            compile_shader(g_water_ps, "PS", "ps_5_0", &wpsb))
        {
            dev->CreateVertexShader(wvsb->GetBufferPointer(),
                                    wvsb->GetBufferSize(),
                                    nullptr, &mp.vs_water);
            dev->CreatePixelShader(wpsb->GetBufferPointer(),
                                   wpsb->GetBufferSize(),
                                   nullptr, &mp.ps_water);
        }
        if (wvsb) wvsb->Release();
        if (wpsb) wpsb->Release();
    }
    {
        D3D11_BUFFER_DESC wcb{};
        wcb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        wcb.ByteWidth = 5 * 16;
        wcb.Usage = D3D11_USAGE_DYNAMIC;
        wcb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&wcb, nullptr, &mp.cbuffer_water);
    }
    {
        D3D11_SAMPLER_DESC psd{};
        psd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        psd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        psd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        psd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        psd.MaxLOD = D3D11_FLOAT32_MAX;
        dev->CreateSamplerState(&psd, &mp.sampler_point);
    }
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = FALSE;
    if(FAILED(dev->CreateRasterizerState(&rd,&mp.rs))) return false;

    D3D11_RASTERIZER_DESC rd_w = rd;
    rd_w.FillMode = D3D11_FILL_WIREFRAME;
    if(FAILED(dev->CreateRasterizerState(&rd_w, &mp.rs_wire))) return false;
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable=FALSE;
    bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    if(FAILED(dev->CreateBlendState(&bd,&mp.bs))) return false;
    D3D11_BLEND_DESC bda{};
    bda.RenderTarget[0].BlendEnable           = TRUE;
    bda.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
    bda.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
    bda.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
    bda.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
    bda.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_INV_SRC_ALPHA;
    bda.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
    bda.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if(FAILED(dev->CreateBlendState(&bda,&mp.bsAlpha))) return false;
    D3D11_DEPTH_STENCIL_DESC dssw{};
    dssw.DepthEnable = TRUE;
    dssw.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dssw.DepthFunc = D3D11_COMPARISON_LESS;
    if(FAILED(dev->CreateDepthStencilState(&dssw, &mp.dssWrite))) return false;
    D3D11_DEPTH_STENCIL_DESC dssn{};
    dssn.DepthEnable = TRUE;
    dssn.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dssn.DepthFunc = D3D11_COMPARISON_LESS;
    if(FAILED(dev->CreateDepthStencilState(&dssn, &mp.dssNoWrite))) return false;
    if(!create_white_srv(dev, &mp.default_srv)) return false;
    return true;
}
bool MP_Init(ID3D11Device* dev, ModelPreview& mp, int w, int h){
    if(!create_target(dev, mp, w, h)) return false;
    if(!create_pipeline(dev, mp)) return false;
    return true;
}
void MP_Resize(ID3D11Device* dev, ModelPreview& mp, int w, int h){
    if(w == mp.width && h == mp.height) return;
    create_target(dev, mp, w, h);
}
void MP_Release(ModelPreview& mp){
    mp_release(mp);
}

namespace {
struct TexCacheKey {
    std::string name_lower;
    std::string preferred_bnk;
    bool operator==(const TexCacheKey& o) const {
        return name_lower == o.name_lower && preferred_bnk == o.preferred_bnk;
    }
};
struct TexCacheKeyHash {
    size_t operator()(const TexCacheKey& k) const noexcept {
        return std::hash<std::string>{}(k.name_lower)
             ^ (std::hash<std::string>{}(k.preferred_bnk) << 1);
    }
};
struct TexCacheEntry {
#ifdef _WIN32
    ID3D11ShaderResourceView* srv = nullptr;
#endif
    bool has_alpha = false;
    bool tried     = false;
};

std::mutex& tex_cache_mutex() {
    static std::mutex m;
    return m;
}
std::unordered_map<TexCacheKey, TexCacheEntry, TexCacheKeyHash>& tex_cache_table() {
    static std::unordered_map<TexCacheKey, TexCacheEntry, TexCacheKeyHash> t;
    return t;
}
}

void MP_TextureCache_Clear() {
#ifdef _WIN32
    std::lock_guard<std::mutex> lk(tex_cache_mutex());
    for (auto& kv : tex_cache_table()) {
        if (kv.second.srv) kv.second.srv->Release();
    }
    tex_cache_table().clear();
#endif
}

static XMMATRIX bone_local_matrix(const float* tf, const float* delta ){
    XMVECTOR q = XMVectorSet(tf[0], tf[1], tf[2], tf[3]);
    XMVECTOR t = XMVectorSet(tf[4], tf[5], tf[6], 0.0f);
    XMVECTOR s = XMVectorSet(tf[7], tf[8], tf[9], 1.0f);
    XMMATRIX S_ = XMMatrixScalingFromVector(s);
    XMMATRIX R_ = XMMatrixRotationQuaternion(q);
    if (delta) {
        XMVECTOR qd = XMVectorSet(delta[0], delta[1], delta[2], delta[3]);

        XMMATRIX D_ = XMMatrixRotationQuaternion(qd);
        R_ = D_ * R_;
    }
    XMMATRIX T_ = XMMatrixTranslationFromVector(t);
    return S_ * R_ * T_;
}

static void compute_rest_world(const MDLInfo& info,
                               uint32_t n_cap,
                               std::vector<XMFLOAT4X4>& out_world){
    const uint32_t n = std::min<uint32_t>(info.BoneCount, n_cap);
    out_world.assign(n, XMFLOAT4X4());
    XMFLOAT4X4 ident_f; XMStoreFloat4x4(&ident_f, XMMatrixIdentity());
    for (uint32_t i = 0; i < n; ++i) out_world[i] = ident_f;

    if (n == 0 || !info.HasBoneTransforms) return;
    if (info.Bones.size() != info.BoneTransforms.size()) return;

    std::vector<XMFLOAT4X4> local(n);
    for (uint32_t i = 0; i < n; ++i){
        const auto& tf = info.BoneTransforms[i];
        XMMATRIX L = (tf.size() >= 10)
                     ? bone_local_matrix(tf.data(), nullptr)
                     : XMMatrixIdentity();
        XMStoreFloat4x4(&local[i], L);
    }
    std::vector<uint8_t> done(n, 0);
    for (uint32_t i = 0; i < n; ++i){
        if (done[i]) continue;
        std::vector<int> chain;
        int cur = (int)i;
        while (cur >= 0 && cur < (int)n && !done[cur]){
            chain.push_back(cur);
            cur = info.Bones[cur].ParentID;
        }
        XMMATRIX accum;
        if (cur >= 0 && cur < (int)n) {
            accum = XMLoadFloat4x4(&out_world[cur]);
        } else {
            accum = XMMatrixIdentity();
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it){
            XMMATRIX L = XMLoadFloat4x4(&local[*it]);
            accum = L * accum;
            XMStoreFloat4x4(&out_world[*it], accum);
            done[*it] = 1;
        }
    }
}

bool MP_Build(ID3D11Device* dev, const std::vector<MDLMeshGeom>& geoms, const MDLInfo& info, ModelPreview& mp){
    for(auto& m : mp.meshes){
        if(m.vb){m.vb->Release();}
        if(m.ib){m.ib->Release();}
        if(m.srv_diffuse && m.srv_diffuse != mp.default_srv){m.srv_diffuse->Release();}
        if(m.srv_normal && m.srv_normal != mp.default_srv){m.srv_normal->Release();}
        if(m.srv_specular && m.srv_specular != mp.default_srv){m.srv_specular->Release();}
        if(m.srv_metallic && m.srv_metallic != mp.default_srv){m.srv_metallic->Release();}
        if(m.srv_extra && m.srv_extra != mp.default_srv){m.srv_extra->Release();}
    }
    mp.meshes.clear();
    mp.lod_count    = 1;
    mp.selected_lod = -1;

    auto extract_lod = [](std::string& name) -> uint32_t {
        size_t pos = name.rfind("|lod");
        if (pos == std::string::npos) return 0;
        const char* p = name.c_str() + pos + 4;
        if (*p == '\0' || *p < '0' || *p > '9') return 0;
        uint32_t v = 0;
        const char* q = p;
        while (*q >= '0' && *q <= '9') {
            v = v * 10 + uint32_t(*q - '0');
            ++q;
        }
        if (*q != '\0') return 0;
        name.erase(pos);
        return v;
    };

    float minx=1e9f,miny=1e9f,minz=1e9f,maxx=-1e9f,maxy=-1e9f,maxz=-1e9f;
    for(const auto& g: geoms){
        for(size_t i=0;i+2<g.positions.size();i+=3){
            float x=g.positions[i],y=g.positions[i+1],z=g.positions[i+2];
            if(x<minx)minx=x; if(y<miny)miny=y; if(z<minz)minz=z;
            if(x>maxx)maxx=x; if(y>maxy)maxy=y; if(z>maxz)maxz=z;
        }
    }
    if(!(minx<maxx)){ minx=-1;maxx=1;miny=-1;maxy=1;minz=-1;maxz=1; }
    mp.center[0]=(minx+maxx)*0.5f; mp.center[1]=(miny+maxy)*0.5f; mp.center[2]=(minz+maxz)*0.5f;
    mp.radius = std::max(std::max(maxx-minx,maxy-miny),maxz-minz)*0.5f; if(mp.radius<0.0001f) mp.radius=1.0f;
    FlyCam_Reset(g_flycam, mp.center[0], mp.center[1], mp.center[2], mp.radius);
    for(size_t i=0;i<geoms.size();++i){
        const auto& g = geoms[i];
        size_t vcount = g.positions.size()/3;
        if(vcount==0 || g.indices.empty()) continue;
        std::vector<MPVertex> vtx(vcount);
        bool hasN = (g.normals.size()==vcount*3);
        bool hasT = (g.uvs.size()==vcount*2);

        bool hasBI = (g.bone_ids.size()     == vcount * 4);
        bool hasBW = (g.bone_weights.size() == vcount * 4);
        for(size_t v=0; v<vcount; ++v){
            vtx[v].px = g.positions[v*3+0];
            vtx[v].py = g.positions[v*3+1];
            vtx[v].pz = g.positions[v*3+2];
            vtx[v].nx = hasN ? g.normals[v*3+0] : 0.0f;
            vtx[v].ny = hasN ? g.normals[v*3+1] : 1.0f;
            vtx[v].nz = hasN ? g.normals[v*3+2] : 0.0f;
            vtx[v].u  = hasT ? g.uvs[v*2+0] : 0.0f;
            vtx[v].v  = hasT ? g.uvs[v*2+1] : 0.0f;

            auto cap = [](uint16_t id) -> uint8_t {
                uint32_t x = (uint32_t)id;
                if (x >= MP_MAX_BONES) x = 0;
                return (uint8_t)x;
            };
            if (hasBI) {
                vtx[v].b0 = cap(g.bone_ids[v*4+0]);
                vtx[v].b1 = cap(g.bone_ids[v*4+1]);
                vtx[v].b2 = cap(g.bone_ids[v*4+2]);
                vtx[v].b3 = cap(g.bone_ids[v*4+3]);
            } else {
                vtx[v].b0 = vtx[v].b1 = vtx[v].b2 = vtx[v].b3 = 0;
            }
            if (hasBW) {
                vtx[v].w0 = g.bone_weights[v*4+0];
                vtx[v].w1 = g.bone_weights[v*4+1];
                vtx[v].w2 = g.bone_weights[v*4+2];
                vtx[v].w3 = g.bone_weights[v*4+3];
            } else {
                vtx[v].w0 = 1.0f;
                vtx[v].w1 = vtx[v].w2 = vtx[v].w3 = 0.0f;
            }
        }
        MPPerMesh m;

        {
            float mnx =  1e30f, mny =  1e30f, mnz =  1e30f;
            float mxx = -1e30f, mxy = -1e30f, mxz = -1e30f;
            for (size_t v = 0; v + 2 < g.positions.size(); v += 3) {
                const float x = g.positions[v + 0];
                const float y = g.positions[v + 1];
                const float z = g.positions[v + 2];
                if (x < mnx) mnx = x; if (y < mny) mny = y; if (z < mnz) mnz = z;
                if (x > mxx) mxx = x; if (y > mxy) mxy = y; if (z > mxz) mxz = z;
            }
            if (mnx < mxx) {
                m.center[0] = (mnx + mxx) * 0.5f;
                m.center[1] = (mny + mxy) * 0.5f;
                m.center[2] = (mnz + mxz) * 0.5f;
                const float dx = mxx - mnx;
                const float dy = mxy - mny;
                const float dz = mxz - mnz;
                m.radius = std::max(std::max(dx, dy), dz) * 0.5f;
                if (m.radius < 0.0001f) m.radius = 0.25f;
            } else {
                m.center[0] = m.center[1] = m.center[2] = 0.0f;
                m.radius = 0.25f;
            }
        }
        const uint64_t vb_bytes =
            uint64_t(vtx.size()) * uint64_t(sizeof(MPVertex));
        const uint64_t ib_bytes =
            uint64_t(g.indices.size()) * uint64_t(sizeof(uint32_t));
        const std::string mesh_log_name =
            g.name.empty() ? std::to_string(i) : g.name;
        mp_level_load_debug(
            "MP_Build mesh begin " + std::to_string(i + 1) + "/" +
            std::to_string(geoms.size()) +
            " v=" + std::to_string(vcount) +
            " idx=" + std::to_string(g.indices.size()) +
            " vb=" + std::to_string(vb_bytes) +
            " ib=" + std::to_string(ib_bytes) +
            " name=" + mesh_log_name);
        if (vb_bytes == 0 || ib_bytes == 0 ||
            vb_bytes > std::numeric_limits<UINT>::max() ||
            ib_bytes > std::numeric_limits<UINT>::max()) {
            OutputLog::warn("MP_Build: skipped oversized mesh '" +
                            mesh_log_name +
                            "' vb=" + std::to_string(vb_bytes) +
                            " ib=" + std::to_string(ib_bytes));
            mp_level_load_debug("MP_Build mesh skipped oversized name=" +
                                mesh_log_name);
            continue;
        }

        D3D11_BUFFER_DESC vb{}; vb.BindFlags=D3D11_BIND_VERTEX_BUFFER; vb.ByteWidth=(UINT)vb_bytes; vb.Usage=D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA vsd{}; vsd.pSysMem=vtx.data();
        if(FAILED(dev->CreateBuffer(&vb,&vsd,&m.vb))) {
            OutputLog::warn("MP_Build: vertex buffer create failed for '" +
                            mesh_log_name +
                            "' bytes=" + std::to_string(vb_bytes));
            mp_level_load_debug("MP_Build mesh vertex buffer failed name=" +
                                mesh_log_name);
            continue;
        }
        D3D11_BUFFER_DESC ib{}; ib.BindFlags=D3D11_BIND_INDEX_BUFFER; ib.ByteWidth=(UINT)ib_bytes; ib.Usage=D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA isd{}; isd.pSysMem=g.indices.data();
        if(FAILED(dev->CreateBuffer(&ib,&isd,&m.ib))) {
            OutputLog::warn("MP_Build: index buffer create failed for '" +
                            mesh_log_name +
                            "' bytes=" + std::to_string(ib_bytes));
            m.vb->Release();
            mp_level_load_debug("MP_Build mesh index buffer failed name=" +
                                mesh_log_name);
            continue;
        }
        m.index_count = (UINT)g.indices.size();
        bool hasA = false;
        m.diffuse_tex_name  = g.diffuse_tex_name;
        m.normal_tex_name   = g.normal_tex_name;
        m.specular_tex_name = g.specular_tex_name;
        m.metallic_tex_name = g.metallic_tex_name;
        m.extra_tex_name    = g.extra_tex_name;
        m.diffuse_visible   = true;
        m.normal_visible    = true;
        m.specular_visible  = true;
        m.metallic_visible  = true;
        m.extra_visible     = true;

        if (!g.name.empty()) {
            m.name = g.name;
        } else {
            m.name = "mesh_" + std::to_string(g.MeshIndex)
                   + "_sub_" + std::to_string(g.SubMeshIndex);
        }
        m.lod_index = extract_lod(m.name);
        if (m.lod_index + 1 > mp.lod_count) mp.lod_count = m.lod_index + 1;

        m.highlight = false;
        m.isolated  = false;
        m.is_terrain = g.is_terrain;
        m.is_water   = g.is_water;

        m.source_mesh_idx = (uint32_t)i;

        std::string preferred_for_tex =
            (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty())
                ? S.selected_nested_temp_path
                : S.selected_bnk;
        auto load_named_srv = [&](const std::string& tex_name,
                                  ID3D11ShaderResourceView** out_srv,
                                  bool* out_has_alpha) {
            if (tex_name.empty()) return;

            std::string key_name = tex_name;
            std::transform(key_name.begin(), key_name.end(), key_name.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            TexCacheKey ck{ key_name, preferred_for_tex };
            {
                std::lock_guard<std::mutex> lk(tex_cache_mutex());
                auto it = tex_cache_table().find(ck);
                if (it != tex_cache_table().end()) {
                    if (it->second.srv) {
                        *out_srv = it->second.srv;
                        (*out_srv)->AddRef();
                        if (out_has_alpha) *out_has_alpha = it->second.has_alpha;
                    }
                    if (it->second.tried) return;
                }
            }

            std::vector<unsigned char> tex_buf;
            const bool found = build_any_tex_buffer_for_name(tex_name, tex_buf, preferred_for_tex);
            if (!found) {
                OutputLog::warn("texture '" + tex_name +
                                "' not found in any loaded BNK");
            }

            bool dummyA = false;
            bool* alpha_ptr = out_has_alpha ? out_has_alpha : &dummyA;
            if (found) {
                srv_from_tex_blob_auto(dev, tex_buf, out_srv, alpha_ptr);
                if (!*out_srv) {
                    std::string reason = mp_last_decode_fail_reason();
                    std::string info   = mp_last_decode_info();
                    std::string msg = "texture '" + tex_name + "' failed to decode";
                    if (!reason.empty()) msg += " (" + reason + ")";
                    if (!info.empty())   msg += " [" + info + "]";
                    msg += " — bytes=" + std::to_string(tex_buf.size());
                    OutputLog::error(msg);
                }
            }

            {
                std::lock_guard<std::mutex> lk(tex_cache_mutex());
                auto& slot = tex_cache_table()[ck];
                slot.tried = true;
                if (*out_srv && slot.srv == nullptr) {
                    (*out_srv)->AddRef();
                    slot.srv       = *out_srv;
                    slot.has_alpha = *alpha_ptr;
                } else if (*out_srv && slot.srv != nullptr) {
                }
            }
        };
        load_named_srv(g.diffuse_tex_name,  &m.srv_diffuse,  &hasA);
        load_named_srv(g.normal_tex_name,   &m.srv_normal,   nullptr);
        load_named_srv(g.specular_tex_name, &m.srv_specular, nullptr);
        load_named_srv(g.metallic_tex_name, &m.srv_metallic, nullptr);
        load_named_srv(g.extra_tex_name,    &m.srv_extra,    nullptr);
        if (!m.srv_diffuse && mp.default_srv) { m.srv_diffuse = mp.default_srv; m.srv_diffuse->AddRef(); }
        if (!m.srv_normal  && mp.default_srv) { m.srv_normal  = mp.default_srv; m.srv_normal->AddRef(); }
        if (!m.srv_specular&& mp.default_srv) { m.srv_specular= mp.default_srv; m.srv_specular->AddRef(); }
        if (!m.srv_metallic     && mp.default_srv) { m.srv_metallic     = mp.default_srv; m.srv_metallic->AddRef(); }
        if (!m.srv_extra    && mp.default_srv) { m.srv_extra    = mp.default_srv; m.srv_extra->AddRef(); }
        m.has_alpha = hasA || m.is_water;
        mp.meshes.push_back(m);
        mp_level_load_debug("MP_Build mesh done " + std::to_string(i + 1) +
                            "/" + std::to_string(geoms.size()) +
                            " name=" + mesh_log_name);
    }

    if (mp.lod_count > 1) {
        mp.selected_lod = 0;
    }

    mp.bone_count = 0;
    mp.bone_parents.clear();
    mp.local_rest.clear();
    mp.inv_bind.clear();

    if (info.HasBoneTransforms && info.BoneCount > 0 &&
        info.Bones.size() == info.BoneTransforms.size()) {

        const uint32_t n = std::min<uint32_t>(info.BoneCount, MP_MAX_BONES);
        mp.bone_count = n;
        mp.bone_parents.resize(n);
        mp.local_rest.resize((size_t)n * 11);
        mp.inv_bind.resize((size_t)n * 16);

        for (uint32_t i = 0; i < n; ++i){
            int pid = info.Bones[i].ParentID;

            if (pid >= (int)n) pid = -1;
            mp.bone_parents[i] = pid;

            const auto& tf = info.BoneTransforms[i];
            for (int k = 0; k < 11; ++k){
                mp.local_rest[(size_t)i * 11 + k] = (k < (int)tf.size()) ? tf[k] : 0.0f;
            }
        }

        std::vector<XMFLOAT4X4> rest_world;
        compute_rest_world(info, n, rest_world);
        for (uint32_t i = 0; i < n && i < rest_world.size(); ++i){
            XMMATRIX W   = XMLoadFloat4x4(&rest_world[i]);
            XMMATRIX inv = XMMatrixInverse(nullptr, W);
            XMFLOAT4X4 m;
            XMStoreFloat4x4(&m, inv);
            std::memcpy(&mp.inv_bind[(size_t)i * 16], &m, sizeof(float) * 16);
        }
    }

    S.bone_rot_deltas.assign((size_t)mp.bone_count * 4, 0.0f);
    for (uint32_t i = 0; i < mp.bone_count; ++i){
        S.bone_rot_deltas[(size_t)i * 4 + 3] = 1.0f;
    }
    S.selected_bone     = -1;
    S.bone_rotate_mode  = false;

    mp.has_model = !mp.meshes.empty();
    mp_level_load_debug("MP_Build internal finished meshes=" +
                        std::to_string(mp.meshes.size()));
    return true;
}
void MP_Render(ID3D11Device* dev, ModelPreview& mp, const FlyCam& cam){
    if(!mp.has_model) return;
    ID3D11DeviceContext* ctx=nullptr; dev->GetImmediateContext(&ctx); if(!ctx) return;
    D3D11_VIEWPORT vp{}; vp.TopLeftX=0; vp.TopLeftY=0; vp.Width=(FLOAT)mp.width; vp.Height=(FLOAT)mp.height; vp.MinDepth=0; vp.MaxDepth=1;
    ctx->RSSetViewports(1,&vp);
    float clear[4] = {0.22f,0.22f,0.22f,1.0f};
    ctx->OMSetRenderTargets(1,&mp.rtv, mp.dsv);
    ctx->ClearRenderTargetView(mp.rtv, clear);
    ctx->ClearDepthStencilView(mp.dsv, D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL, 1.0f, 0);
    ctx->IASetInputLayout(mp.layout);
    ctx->VSSetShader(mp.vs,nullptr,0);
    ctx->PSSetShader(mp.ps,nullptr,0);
    ID3D11SamplerState* samplers[2] = { mp.sampler, mp.sampler_point };
    ctx->PSSetSamplers(0, 2, samplers);

    ctx->RSSetState((mp.wireframe && mp.rs_wire) ? mp.rs_wire : mp.rs);
    float cy = cosf(cam.yaw);
    float sy = sinf(cam.yaw);
    float cp = cosf(cam.pitch);
    float sp = sinf(cam.pitch);
    float forward[3] = { sy * cp, sp, cy * cp };
    XMVECTOR eye = XMVectorSet(cam.pos[0], cam.pos[1], cam.pos[2], 1);
    XMVECTOR at = XMVectorSet(cam.pos[0] + forward[0], cam.pos[1] + forward[1], cam.pos[2] + forward[2], 1);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    XMMATRIX V = XMMatrixLookAtLH(eye, at, up);
    float fov = XMConvertToRadians(60.0f);
    float aspect = (float)mp.width / (float)mp.height;
    float far_plane = mp.radius * 100.0f;
    XMMATRIX P = XMMatrixPerspectiveFovLH(fov, aspect, 0.05f, far_plane);
    XMMATRIX W = XMMatrixIdentity();
    if (!mp.no_tilt) {
        const float tiltX = -XM_PIDIV2;
        XMMATRIX Tm = XMMatrixTranslation(-mp.center[0], -mp.center[1], -mp.center[2]);
        XMMATRIX R  = XMMatrixRotationX(tiltX);
        XMMATRIX Tp = XMMatrixTranslation( mp.center[0],  mp.center[1],  mp.center[2]);
        W  = Tm * R * Tp;
        XMMATRIX FlipX = XMMatrixScaling(-1.0f, 1.0f, 1.0f);
        W = W * FlipX;
    }
    XMMATRIX MVP = XMMatrixTranspose(W * V * P);
    XMMATRIX MV  = XMMatrixTranspose(W * V);
    XMVECTOR lightDirV = XMVector3Normalize(XMVectorSet(0.5f, 1.0f, 0.3f, 0.0f));
    XMFLOAT4 lightDirF; XMStoreFloat4(&lightDirF, lightDirV);
    struct CB { XMFLOAT4X4 mvp; XMFLOAT4 lightDir; XMFLOAT4X4 mv; XMFLOAT4 params; } cb;
    XMStoreFloat4x4(&cb.mvp, MVP);
    XMStoreFloat4x4(&cb.mv,  MV);
    cb.lightDir = lightDirF;
    cb.params   = XMFLOAT4(0.4f, 48.0f, 0.0f, 0.0f);
    D3D11_MAPPED_SUBRESOURCE ms{};
    if(SUCCEEDED(ctx->Map(mp.cbuffer,0,D3D11_MAP_WRITE_DISCARD,0,&ms))){
        memcpy(ms.pData, &cb, sizeof(cb));
        ctx->Unmap(mp.cbuffer,0);
    }
    ctx->VSSetConstantBuffers(0,1,&mp.cbuffer);
    ctx->PSSetConstantBuffers(0,1,&mp.cbuffer);

    std::vector<XMFLOAT4X4> bone_mats(MP_MAX_BONES);
    for (uint32_t i = 0; i < MP_MAX_BONES; ++i){
        XMStoreFloat4x4(&bone_mats[i], XMMatrixIdentity());
    }
    if (mp.bone_count > 0) {
        const uint32_t n = mp.bone_count;

        std::vector<XMFLOAT4X4> local(n);
        bool have_deltas = (S.bone_rot_deltas.size() >= (size_t)n * 4);
        for (uint32_t i = 0; i < n; ++i){
            const float* tf = &mp.local_rest[(size_t)i * 11];
            const float* dq = have_deltas ? &S.bone_rot_deltas[(size_t)i * 4] : nullptr;
            XMMATRIX L = bone_local_matrix(tf, dq);
            XMStoreFloat4x4(&local[i], L);
        }

        std::vector<XMFLOAT4X4> world(n);
        std::vector<uint8_t> done(n, 0);
        for (uint32_t i = 0; i < n; ++i){
            if (done[i]) continue;
            std::vector<int> chain;
            int cur = (int)i;
            while (cur >= 0 && cur < (int)n && !done[cur]){
                chain.push_back(cur);
                cur = mp.bone_parents[cur];
            }
            XMMATRIX accum;
            if (cur >= 0 && cur < (int)n) accum = XMLoadFloat4x4(&world[cur]);
            else                          accum = XMMatrixIdentity();
            for (auto it = chain.rbegin(); it != chain.rend(); ++it){
                XMMATRIX L = XMLoadFloat4x4(&local[*it]);
                accum = L * accum;
                XMStoreFloat4x4(&world[*it], accum);
                done[*it] = 1;
            }
        }

        for (uint32_t i = 0; i < n; ++i){
            XMFLOAT4X4 ib_f;
            std::memcpy(&ib_f, &mp.inv_bind[(size_t)i * 16], sizeof(float) * 16);
            XMMATRIX ib = XMLoadFloat4x4(&ib_f);
            XMMATRIX W  = XMLoadFloat4x4(&world[i]);
            XMMATRIX skin = ib * W;
            XMStoreFloat4x4(&bone_mats[i], skin);
        }
    }
    if (mp.bone_cb) {
        D3D11_MAPPED_SUBRESOURCE bms{};
        if (SUCCEEDED(ctx->Map(mp.bone_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &bms))){
            std::memcpy(bms.pData, bone_mats.data(),
                        sizeof(XMFLOAT4X4) * MP_MAX_BONES);
            ctx->Unmap(mp.bone_cb, 0);
        }
        ctx->VSSetConstantBuffers(1, 1, &mp.bone_cb);
    }

    bool any_isolated = false;
    for (const auto& mm : mp.meshes) { if (mm.isolated) { any_isolated = true; break; } }

    const float water_time = (float)ImGui::GetTime();
    auto upload_per_mesh_cb = [&](bool highlight){

        cb.params = XMFLOAT4(0.4f, 48.0f,
                             highlight ? 1.0f : 0.0f, water_time);
        D3D11_MAPPED_SUBRESOURCE pms{};
        if (SUCCEEDED(ctx->Map(mp.cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &pms))) {
            std::memcpy(pms.pData, &cb, sizeof(cb));
            ctx->Unmap(mp.cbuffer, 0);
        }
    };

    float blend_factor[4] = {0,0,0,0};

    auto draw_one = [&](const MPPerMesh& m, ID3D11BlendState* bs) {
        UINT stride=sizeof(MPVertex), offset=0;
        ctx->IASetVertexBuffers(0,1,&m.vb,&stride,&offset);
        ctx->IASetIndexBuffer(m.ib, DXGI_FORMAT_R32_UINT, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->OMSetBlendState(bs, blend_factor, 0xFFFFFFFF);

        const auto& R = TerrainSplat::Get();
        const bool use_terrain_shader =
            m.is_terrain && R.ok &&
            R.lod_diffuse_array && R.lod_detail_array &&
            R.lod_normal_array && R.lod_detail_normal_array &&
            R.chunk_idx_array && R.chunk_blend_array && R.chunk_uv_array &&
            R.splat_mask && R.material_weight_array && R.lightmap &&
            mp.vs_terrain && mp.ps_terrain && mp.cbuffer_terrain;

        if (use_terrain_shader) {
            static uint32_t s_logged_splat_generation = 0;
            if (s_logged_splat_generation != R.generation) {
                s_logged_splat_generation = R.generation;
                OutputLog::success("terrain SPLAT shader draw active: "
                    + std::to_string(R.chunk_w) + "x"
                    + std::to_string(R.chunk_h) + " chunks, "
                    + std::to_string(R.lod_count) + " material slices");
            }
            D3D11_MAPPED_SUBRESOURCE tms{};
            if (SUCCEEDED(ctx->Map(mp.cbuffer_terrain, 0,
                                   D3D11_MAP_WRITE_DISCARD, 0, &tms))) {
                struct TCB {
                    float origin_extent[4];
                    float grid_size[4];
                    float splat_params[4];
                    float mesh_xform[4];
                    float weight_params[4];
                    float material_params[32][4];
                } t{};
                t.origin_extent[0] = R.world_origin_x;
                t.origin_extent[1] = R.world_origin_z;
                t.origin_extent[2] = R.chunk_extent_x;
                t.origin_extent[3] = R.chunk_extent_z;
                t.grid_size[0] = (float)R.chunk_w;
                t.grid_size[1] = (float)R.chunk_h;
                t.grid_size[2] = (float)R.lod_count;
                t.grid_size[3] = 255.f;
                t.splat_params[0] = 3.0f;
                t.splat_params[1] = (R.tile_scale > 0.0f)
                    ? R.tile_scale : 0.125f;
                t.splat_params[2] = (R.splat_w > 0)
                    ? 16.0f / float(R.splat_w) : 0.0f;
                t.splat_params[3] = (R.splat_h > 0)
                    ? 16.0f / float(R.splat_h) : 0.0f;
                t.mesh_xform[0] = 1.0f;
                t.mesh_xform[1] = 1.0f;
                t.mesh_xform[2] = R.mesh_to_world_x;
                t.mesh_xform[3] = R.mesh_to_world_z;
                t.weight_params[0] = (float)R.weight_w;
                t.weight_params[1] = (float)R.weight_h;
                t.weight_params[2] = 0.0f;
                t.weight_params[3] = 0.0f;
                for (int mi = 0; mi < 32; ++mi) {
                    for (int mj = 0; mj < 4; ++mj) {
                        t.material_params[mi][mj] =
                            R.material_params[mi][mj];
                    }
                }
                std::memcpy(tms.pData, &t, sizeof(t));
                ctx->Unmap(mp.cbuffer_terrain, 0);
            }

            ctx->VSSetShader(mp.vs_terrain, nullptr, 0);
            ctx->PSSetShader(mp.ps_terrain, nullptr, 0);
            ctx->VSSetConstantBuffers(2, 1, &mp.cbuffer_terrain);
            ctx->PSSetConstantBuffers(2, 1, &mp.cbuffer_terrain);

            ID3D11ShaderResourceView* srvs[10] = {
                R.lod_diffuse_array,
                R.chunk_idx_array,
                R.chunk_blend_array,
                R.chunk_uv_array,
                R.splat_mask,
                R.lightmap,
                R.lod_detail_array,
                R.material_weight_array,
                R.lod_normal_array,
                R.lod_detail_normal_array
            };
            ctx->PSSetShaderResources(0, 10, srvs);

            ctx->DrawIndexed(m.index_count, 0, 0);

            ID3D11ShaderResourceView* nulls[10] = {
                nullptr, nullptr, nullptr, nullptr, nullptr,
                nullptr, nullptr, nullptr, nullptr, nullptr
            };
            ctx->PSSetShaderResources(0, 10, nulls);
            ID3D11Buffer* null_cb = nullptr;
            ctx->VSSetConstantBuffers(2, 1, &null_cb);
            ctx->PSSetConstantBuffers(2, 1, &null_cb);

            ctx->VSSetShader(mp.vs, nullptr, 0);
            ctx->PSSetShader(mp.ps, nullptr, 0);
            return;
        }

        const bool use_water_shader =
            m.is_water && mp.vs_water && mp.ps_water && mp.cbuffer_water;

        if (use_water_shader) {

            D3D11_MAPPED_SUBRESOURCE wms{};
            if (SUCCEEDED(ctx->Map(mp.cbuffer_water, 0,
                                   D3D11_MAP_WRITE_DISCARD, 0, &wms))) {
                struct WCB {
                    float wave0[4];
                    float wave1[4];
                    float wave2[4];
                    float wave3[4];
                    float wparams[4];
                } w{};

                w.wave0[0] =  0.8f; w.wave0[1] =  0.6f; w.wave0[2] = 6.0f; w.wave0[3] = 0.06f;
                w.wave1[0] = -0.5f; w.wave1[1] =  0.86f; w.wave1[2] = 3.4f; w.wave1[3] = 0.035f;
                w.wave2[0] =  0.3f; w.wave2[1] = -0.95f; w.wave2[2] = 2.1f; w.wave2[3] = 0.025f;
                w.wave3[0] = -0.94f; w.wave3[1] = -0.34f; w.wave3[2] = 1.3f; w.wave3[3] = 0.012f;
                w.wparams[0] = 0.04f;
                w.wparams[1] = 0.025f;
                w.wparams[2] = 0.55f;
                w.wparams[3] = 0.0f;
                std::memcpy(wms.pData, &w, sizeof(w));
                ctx->Unmap(mp.cbuffer_water, 0);
            }

            ctx->VSSetShader(mp.vs_water, nullptr, 0);
            ctx->PSSetShader(mp.ps_water, nullptr, 0);
            ctx->VSSetConstantBuffers(3, 1, &mp.cbuffer_water);
            ctx->PSSetConstantBuffers(3, 1, &mp.cbuffer_water);

            ID3D11ShaderResourceView* nrm_srv =
                m.srv_diffuse ? m.srv_diffuse : mp.default_srv;
            ctx->PSSetShaderResources(0, 1, &nrm_srv);

            ctx->DrawIndexed(m.index_count, 0, 0);

            ID3D11ShaderResourceView* null_srv = nullptr;
            ctx->PSSetShaderResources(0, 1, &null_srv);
            ID3D11Buffer* null_cb = nullptr;
            ctx->VSSetConstantBuffers(3, 1, &null_cb);
            ctx->PSSetConstantBuffers(3, 1, &null_cb);

            ctx->VSSetShader(mp.vs, nullptr, 0);
            ctx->PSSetShader(mp.ps, nullptr, 0);
            return;
        }

        ID3D11ShaderResourceView* diffuse_to_use  =
            (m.diffuse_visible  && m.srv_diffuse)  ? m.srv_diffuse  : mp.default_srv;
        ID3D11ShaderResourceView* normal_to_use   =
            (m.normal_visible   && m.srv_normal)   ? m.srv_normal   : mp.default_srv;
        ID3D11ShaderResourceView* specular_to_use =
            (m.specular_visible && m.srv_specular) ? m.srv_specular : mp.default_srv;
        ID3D11ShaderResourceView* metallic_to_use =
            (m.metallic_visible && m.srv_metallic) ? m.srv_metallic : mp.default_srv;
        ID3D11ShaderResourceView* extra_to_use    =
            (m.extra_visible    && m.srv_extra)    ? m.srv_extra    : mp.default_srv;
        ID3D11ShaderResourceView* srvs[5] = { diffuse_to_use, normal_to_use, specular_to_use,
                                              metallic_to_use, extra_to_use };
        ctx->PSSetShaderResources(0, 5, srvs);
        ctx->DrawIndexed(m.index_count, 0, 0);
        ID3D11ShaderResourceView* nullsrvs[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
        ctx->PSSetShaderResources(0, 5, nullsrvs);
    };

    ctx->OMSetDepthStencilState(mp.dssWrite, 0);
    for(const auto& m : mp.meshes){
        if(!m.vb || !m.ib || m.index_count==0 || m.has_alpha) continue;
        if (any_isolated && !m.isolated) continue;
        if (mp.selected_lod >= 0 &&
            m.lod_index != (uint32_t)mp.selected_lod) continue;
        upload_per_mesh_cb(m.highlight);
        draw_one(m, mp.bs);
    }
    ctx->OMSetDepthStencilState(mp.dssNoWrite, 0);
    for(const auto& m : mp.meshes){
        if(!m.vb || !m.ib || m.index_count==0 || !m.has_alpha) continue;
        if (any_isolated && !m.isolated) continue;
        if (mp.selected_lod >= 0 &&
            m.lod_index != (uint32_t)mp.selected_lod) continue;
        upload_per_mesh_cb(m.highlight);
        draw_one(m, mp.bsAlpha);
    }
    ctx->Release();
}

void MP_ComputeWorldPose(const ModelPreview& mp,
                         const std::vector<float>& deltas,
                         std::vector<float>& out_world_pose){
    out_world_pose.clear();
    if (mp.bone_count == 0) return;

    const uint32_t n = mp.bone_count;

    if (mp.local_rest.size()   < (size_t)n * 11) return;
    if (mp.bone_parents.size() < (size_t)n)       return;

    const bool have_deltas = (deltas.size() >= (size_t)n * 4);

    std::vector<XMFLOAT4X4> local(n);
    for (uint32_t i = 0; i < n; ++i){
        const float* tf = &mp.local_rest[(size_t)i * 11];
        const float* dq = have_deltas ? &deltas[(size_t)i * 4] : nullptr;
        XMMATRIX L = bone_local_matrix(tf, dq);
        XMStoreFloat4x4(&local[i], L);
    }

    std::vector<XMFLOAT4X4> world(n);
    std::vector<uint8_t> done(n, 0);
    for (uint32_t i = 0; i < n; ++i){
        if (done[i]) continue;
        std::vector<int> chain;
        int cur = (int)i;
        while (cur >= 0 && cur < (int)n && !done[cur]){
            chain.push_back(cur);
            cur = mp.bone_parents[cur];
        }
        XMMATRIX accum;
        if (cur >= 0 && cur < (int)n) accum = XMLoadFloat4x4(&world[cur]);
        else                          accum = XMMatrixIdentity();
        for (auto it = chain.rbegin(); it != chain.rend(); ++it){
            XMMATRIX L = XMLoadFloat4x4(&local[*it]);
            accum = L * accum;
            XMStoreFloat4x4(&world[*it], accum);
            done[*it] = 1;
        }
    }

    out_world_pose.resize((size_t)n * 16);
    for (uint32_t i = 0; i < n; ++i){
        std::memcpy(&out_world_pose[(size_t)i * 16],
                    &world[i], sizeof(float) * 16);
    }
}

#else
static const char* gl_vs = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
uniform mat4 uMVP;
uniform mat4 uMV;
out vec3 vNormal;
out vec2 vTexCoord;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vNormal = normalize(mat3(uMV) * aNormal);
    vTexCoord = aTexCoord;
}
)";
static const char* gl_fs = R"(
#version 330 core
in vec3 vNormal;
in vec2 vTexCoord;
uniform sampler2D uTexDiffuse;
uniform sampler2D uTexNormal;
uniform sampler2D uTexSpecular;
uniform sampler2D uTexMetallic;
uniform sampler2D uTexExtra;
out vec4 FragColor;
void main() {

    vec4 diffSamp = texture(uTexDiffuse, vTexCoord);
    vec3 albedo = diffSamp.rgb;
    float alpha = diffSamp.a;

    vec3 nSamp = texture(uTexNormal, vTexCoord).rgb;
    bool nIsDefault = (nSamp.r > 0.98 && nSamp.g > 0.98 && nSamp.b > 0.98);
    vec3 N_geo = normalize(vNormal);
    vec3 N = N_geo;
    if (!nIsDefault) {
        vec3 N_m = nSamp * 2.0 - 1.0;
        N = normalize(N_geo + N_m * 0.5);
    }

    vec3 L = normalize(vec3(0.3, 0.7, 0.5));
    vec3 V = vec3(0.0, 0.0, 1.0);
    vec3 H = normalize(L + V);

    float ndotl = abs(dot(N, L));
    float diff_term = 0.55 + 0.45 * ndotl;

    vec3 sSamp = texture(uTexSpecular, vTexCoord).rgb;
    bool sIsDefault = (sSamp.r > 0.98 && sSamp.g > 0.98 && sSamp.b > 0.98);
    float spec_mask = sIsDefault ? 0.0 : sSamp.r;
    float ndoth = clamp(abs(dot(N, H)), 0.0, 1.0);
    float spec = pow(ndoth, 24.0) * spec_mask * 0.6;

    vec3 color = albedo * diff_term + vec3(spec);
    FragColor = vec4(color, alpha);
}
)";
static unsigned int compile_gl_shader(const char* src, GLenum type) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) { glDeleteShader(shader); return 0; }
    return shader;
}
static unsigned int create_gl_program(const char* vs_src, const char* fs_src) {
    unsigned int vs = compile_gl_shader(vs_src, GL_VERTEX_SHADER);
    if (!vs) return 0;
    unsigned int fs = compile_gl_shader(fs_src, GL_FRAGMENT_SHADER);
    if (!fs) { glDeleteShader(vs); return 0; }
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    int success;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) { glDeleteProgram(prog); return 0; }
    return prog;
}
unsigned int create_gl_texture_from_rgba(int w, int h, const uint8_t* data) {
    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    float maxAniso = 0.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    if (maxAniso > 0.0f) { glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(maxAniso, 16.0f)); }
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}
static unsigned int create_white_tex() {
    uint32_t px = 0xFFFFFFFF;
    return create_gl_texture_from_rgba(1, 1, (const uint8_t*)&px);
}
static void mp_release_mesh_gl(MPPerMesh& m) {
    if (m.vao) { glDeleteVertexArrays(1, &m.vao); m.vao = 0; }
    if (m.vbo) { glDeleteBuffers(1, &m.vbo); m.vbo = 0; }
    if (m.ibo) { glDeleteBuffers(1, &m.ibo); m.ibo = 0; }
    if (m.tex_diffuse) { glDeleteTextures(1, &m.tex_diffuse); m.tex_diffuse = 0; }
    if (m.tex_normal) { glDeleteTextures(1, &m.tex_normal); m.tex_normal = 0; }
    if (m.tex_specular) { glDeleteTextures(1, &m.tex_specular); m.tex_specular = 0; }
    if (m.tex_metallic) { glDeleteTextures(1, &m.tex_metallic); m.tex_metallic = 0; }
    if (m.tex_extra) { glDeleteTextures(1, &m.tex_extra); m.tex_extra = 0; }
    m.index_count = 0;
}
static void mp_release_gl(ModelPreview& mp) {
    for (auto& m : mp.meshes) mp_release_mesh_gl(m);
    mp.meshes.clear();
    if (mp.fbo) { glDeleteFramebuffers(1, &mp.fbo); mp.fbo = 0; }
    if (mp.color_tex) { glDeleteTextures(1, &mp.color_tex); mp.color_tex = 0; }
    if (mp.depth_rbo) { glDeleteRenderbuffers(1, &mp.depth_rbo); mp.depth_rbo = 0; }
    if (mp.shader_program) { glDeleteProgram(mp.shader_program); mp.shader_program = 0; }
    if (mp.default_tex) { glDeleteTextures(1, &mp.default_tex); mp.default_tex = 0; }
    mp.has_model = false;
}
bool MP_Init(ModelPreview& mp, int w, int h) {
    mp.width = w;
    mp.height = h;
    mp.shader_program = create_gl_program(gl_vs, gl_fs);
    if (!mp.shader_program) return false;
    mp.mvp_loc = glGetUniformLocation(mp.shader_program, "uMVP");
    mp.mv_loc = glGetUniformLocation(mp.shader_program, "uMV");
    mp.tex_diffuse_loc = glGetUniformLocation(mp.shader_program, "uTexDiffuse");
    mp.tex_normal_loc = glGetUniformLocation(mp.shader_program, "uTexNormal");
    mp.tex_specular_loc = glGetUniformLocation(mp.shader_program, "uTexSpecular");
    mp.tex_metallic_loc = glGetUniformLocation(mp.shader_program, "uTexMetallic");
    mp.tex_extra_loc = glGetUniformLocation(mp.shader_program, "uTexExtra");
    glGenFramebuffers(1, &mp.fbo);
    glGenTextures(1, &mp.color_tex);
    glGenRenderbuffers(1, &mp.depth_rbo);
    glBindFramebuffer(GL_FRAMEBUFFER, mp.fbo);
    glBindTexture(GL_TEXTURE_2D, mp.color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mp.color_tex, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, mp.depth_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, mp.depth_rbo);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    mp.default_tex = create_white_tex();
    return true;
}
void MP_Release(ModelPreview& mp) {
    mp_release_gl(mp);
}
void MP_Resize(ModelPreview& mp, int w, int h) {
    if (w == mp.width && h == mp.height) return;
    mp.width = w;
    mp.height = h;
    glBindTexture(GL_TEXTURE_2D, mp.color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindRenderbuffer(GL_RENDERBUFFER, mp.depth_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
}
static unsigned int load_tex_from_name(const std::string& name, bool* out_has_alpha) {
    if (name.empty()) return 0;
    std::vector<unsigned char> tex_buf;

    std::string preferred_for_tex =
        (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty())
            ? S.selected_nested_temp_path
            : S.selected_bnk;
    if (build_any_tex_buffer_for_name(name, tex_buf, preferred_for_tex)) {
        std::vector<uint8_t> rgba;
        int w, h;
        if (decode_tex_to_rgba(tex_buf, rgba, w, h, out_has_alpha)) {
            return create_gl_texture_from_rgba(w, h, rgba.data());
        }
    }
    return 0;
}
bool MP_Build(const std::vector<MDLMeshGeom>& geoms, const MDLInfo& info, ModelPreview& mp) {
    for (auto& m : mp.meshes) mp_release_mesh_gl(m);
    mp.meshes.clear();
    float minx = 1e9f, miny = 1e9f, minz = 1e9f, maxx = -1e9f, maxy = -1e9f, maxz = -1e9f;
    for (const auto& g : geoms) {
        for (size_t i = 0; i + 2 < g.positions.size(); i += 3) {
            float x = g.positions[i], y = g.positions[i + 1], z = g.positions[i + 2];
            if (x < minx) minx = x; if (y < miny) miny = y; if (z < minz) minz = z;
            if (x > maxx) maxx = x; if (y > maxy) maxy = y; if (z > maxz) maxz = z;
        }
    }
    if (!(minx < maxx)) { minx = -1; maxx = 1; miny = -1; maxy = 1; minz = -1; maxz = 1; }
    mp.center[0] = (minx + maxx) * 0.5f; mp.center[1] = (miny + maxy) * 0.5f; mp.center[2] = (minz + maxz) * 0.5f;
    mp.radius = std::max(std::max(maxx - minx, maxy - miny), maxz - minz) * 0.5f;
    if (mp.radius < 0.0001f) mp.radius = 1.0f;
    FlyCam_Reset(g_flycam, mp.center[0], mp.center[1], mp.center[2], mp.radius);
    for (size_t i = 0; i < geoms.size(); ++i) {
        const auto& g = geoms[i];
        size_t vcount = g.positions.size() / 3;
        if (vcount == 0 || g.indices.empty()) continue;
        std::vector<MPVertex> vtx(vcount);
        bool hasN = (g.normals.size() == vcount * 3);
        bool hasT = (g.uvs.size() == vcount * 2);
        for (size_t v = 0; v < vcount; ++v) {
            vtx[v].px = g.positions[v * 3 + 0];
            vtx[v].py = g.positions[v * 3 + 1];
            vtx[v].pz = g.positions[v * 3 + 2];
            vtx[v].nx = hasN ? g.normals[v * 3 + 0] : 0.0f;
            vtx[v].ny = hasN ? g.normals[v * 3 + 1] : 1.0f;
            vtx[v].nz = hasN ? g.normals[v * 3 + 2] : 0.0f;
            vtx[v].u = hasT ? g.uvs[v * 2 + 0] : 0.0f;
            vtx[v].v = hasT ? g.uvs[v * 2 + 1] : 0.0f;
        }
        MPPerMesh m;
        glGenVertexArrays(1, &m.vao);
        glGenBuffers(1, &m.vbo);
        glGenBuffers(1, &m.ibo);
        glBindVertexArray(m.vao);
        glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
        glBufferData(GL_ARRAY_BUFFER, vtx.size() * sizeof(MPVertex), vtx.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, g.indices.size() * sizeof(uint32_t), g.indices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MPVertex), (void*)offsetof(MPVertex, px));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MPVertex), (void*)offsetof(MPVertex, nx));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(MPVertex), (void*)offsetof(MPVertex, u));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
        m.index_count = (unsigned int)g.indices.size();
        bool hasA = false;
        m.diffuse_tex_name  = g.diffuse_tex_name;
        m.normal_tex_name   = g.normal_tex_name;
        m.specular_tex_name = g.specular_tex_name;
        m.metallic_tex_name = g.metallic_tex_name;
        m.extra_tex_name    = g.extra_tex_name;
        m.diffuse_visible   = true;
        m.normal_visible    = true;
        m.specular_visible  = true;
        m.metallic_visible  = true;
        m.extra_visible     = true;

        if (!g.name.empty()) {
            m.name = g.name;
        } else {
            m.name = "mesh_" + std::to_string(g.MeshIndex)
                   + "_sub_" + std::to_string(g.SubMeshIndex);
        }
        m.highlight = false;
        m.isolated  = false;
        if (!g.diffuse_tex_name.empty())  { m.tex_diffuse  = load_tex_from_name(g.diffuse_tex_name,  &hasA); }
        if (!g.normal_tex_name.empty())   { m.tex_normal   = load_tex_from_name(g.normal_tex_name,   nullptr); }
        if (!g.specular_tex_name.empty()) { m.tex_specular = load_tex_from_name(g.specular_tex_name, nullptr); }
        if (!g.metallic_tex_name.empty()) { m.tex_metallic = load_tex_from_name(g.metallic_tex_name, nullptr); }
        if (!g.extra_tex_name.empty())    { m.tex_extra    = load_tex_from_name(g.extra_tex_name,    nullptr); }
        if (!m.tex_diffuse) m.tex_diffuse = mp.default_tex;
        if (!m.tex_normal) m.tex_normal = mp.default_tex;
        if (!m.tex_specular) m.tex_specular = mp.default_tex;
        if (!m.tex_metallic) m.tex_metallic = mp.default_tex;
        if (!m.tex_extra) m.tex_extra = mp.default_tex;
        m.has_alpha = hasA;
        mp.meshes.push_back(m);
    }
    mp.has_model = !mp.meshes.empty();
    return true;
}
static void mat4_identity(float* m) { memset(m, 0, 16 * sizeof(float)); m[0] = m[5] = m[10] = m[15] = 1.0f; }
static void mat4_perspective(float* m, float fov, float aspect, float znear, float zfar) {
    float f = 1.0f / tanf(fov * 0.5f);
    memset(m, 0, 16 * sizeof(float));
    m[0] = f / aspect; m[5] = f; m[10] = (zfar + znear) / (znear - zfar); m[11] = -1.0f; m[14] = (2.0f * zfar * znear) / (znear - zfar);
}
static void mat4_lookat(float* m, float ex, float ey, float ez, float cx, float cy, float cz, float ux, float uy, float uz) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float fl = sqrtf(fx * fx + fy * fy + fz * fz); fx /= fl; fy /= fl; fz /= fl;
    float sx = fy * uz - fz * uy, sy = fz * ux - fx * uz, sz = fx * uy - fy * ux;
    float sl = sqrtf(sx * sx + sy * sy + sz * sz); sx /= sl; sy /= sl; sz /= sl;
    float uux = sy * fz - sz * fy, uuy = sz * fx - sx * fz, uuz = sx * fy - sy * fx;
    m[0] = sx; m[4] = sy; m[8] = sz; m[12] = -(sx * ex + sy * ey + sz * ez);
    m[1] = uux; m[5] = uuy; m[9] = uuz; m[13] = -(uux * ex + uuy * ey + uuz * ez);
    m[2] = -fx; m[6] = -fy; m[10] = -fz; m[14] = (fx * ex + fy * ey + fz * ez);
    m[3] = 0; m[7] = 0; m[11] = 0; m[15] = 1;
}
static void mat4_rotateX(float* m, float angle) { mat4_identity(m); float c = cosf(angle), s = sinf(angle); m[5] = c; m[6] = s; m[9] = -s; m[10] = c; }
static void mat4_translate(float* m, float x, float y, float z) { mat4_identity(m); m[12] = x; m[13] = y; m[14] = z; }
static void mat4_mult(float* out, const float* a, const float* b) {
    float tmp[16];
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) tmp[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] + a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
    memcpy(out, tmp, 16 * sizeof(float));
}
void MP_Render(ModelPreview& mp, const FlyCam& cam) {
    if (!mp.has_model) return;
    glBindFramebuffer(GL_FRAMEBUFFER, mp.fbo);
    glViewport(0, 0, mp.width, mp.height);
    glClearColor(0.22f, 0.22f, 0.22f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDisable(GL_CULL_FACE);
    glUseProgram(mp.shader_program);
    float cy = cosf(cam.yaw);
    float sy = sinf(cam.yaw);
    float cp = cosf(cam.pitch);
    float sp = sinf(cam.pitch);
    float forward[3] = { sy * cp, sp, cy * cp };
    float atx = cam.pos[0] + forward[0];
    float aty = cam.pos[1] + forward[1];
    float atz = cam.pos[2] + forward[2];
    float V[16], P[16], W[16], Tm[16], R[16], Tp[16], tmp[16];
    mat4_lookat(V, cam.pos[0], cam.pos[1], cam.pos[2], atx, aty, atz, 0, 1, 0);
    float fov = 60.0f * 3.14159265f / 180.0f, aspect = (float)mp.width / (float)mp.height;
    float far_plane = mp.radius * 100.0f;
    mat4_perspective(P, fov, aspect, 0.05f, far_plane);
    mat4_identity(W);
    if (!mp.no_tilt) {
        const float tiltX = -3.14159265f / 2.0f;
        mat4_translate(Tm, -mp.center[0], -mp.center[1], -mp.center[2]);
        mat4_rotateX(R, tiltX);
        mat4_translate(Tp, mp.center[0], mp.center[1], mp.center[2]);
        mat4_mult(tmp, R, Tm); mat4_mult(W, Tp, tmp);
    }
    float MV[16], MVP[16];
    mat4_mult(MV, V, W); mat4_mult(MVP, P, MV);
    glUniformMatrix4fv(mp.mvp_loc, 1, GL_FALSE, MVP);
    glUniformMatrix4fv(mp.mv_loc, 1, GL_FALSE, MV);
    glDepthMask(GL_TRUE);
    for (const auto& m : mp.meshes) {
        if (!m.vao || m.index_count == 0 || m.has_alpha) continue;
        if (mp.selected_lod >= 0 &&
            m.lod_index != (uint32_t)mp.selected_lod) continue;
        glDisable(GL_BLEND);
        unsigned int diffuse_to_use  = (m.diffuse_visible  && m.tex_diffuse)  ? m.tex_diffuse  : mp.default_tex;
        unsigned int normal_to_use   = (m.normal_visible   && m.tex_normal)   ? m.tex_normal   : mp.default_tex;
        unsigned int specular_to_use = (m.specular_visible && m.tex_specular) ? m.tex_specular : mp.default_tex;
        unsigned int metallic_to_use = (m.metallic_visible && m.tex_metallic) ? m.tex_metallic : mp.default_tex;
        unsigned int extra_to_use    = (m.extra_visible    && m.tex_extra)    ? m.tex_extra    : mp.default_tex;
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, diffuse_to_use); glUniform1i(mp.tex_diffuse_loc, 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, normal_to_use); glUniform1i(mp.tex_normal_loc, 1);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, specular_to_use); glUniform1i(mp.tex_specular_loc, 2);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, metallic_to_use); glUniform1i(mp.tex_metallic_loc, 3);
        glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, extra_to_use); glUniform1i(mp.tex_extra_loc, 4);
        glBindVertexArray(m.vao); glDrawElements(GL_TRIANGLES, m.index_count, GL_UNSIGNED_INT, 0); glBindVertexArray(0);
    }
    glDepthMask(GL_FALSE);
    for (const auto& m : mp.meshes) {
        if (!m.vao || m.index_count == 0 || !m.has_alpha) continue;
        if (mp.selected_lod >= 0 &&
            m.lod_index != (uint32_t)mp.selected_lod) continue;
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        unsigned int diffuse_to_use  = (m.diffuse_visible  && m.tex_diffuse)  ? m.tex_diffuse  : mp.default_tex;
        unsigned int normal_to_use   = (m.normal_visible   && m.tex_normal)   ? m.tex_normal   : mp.default_tex;
        unsigned int specular_to_use = (m.specular_visible && m.tex_specular) ? m.tex_specular : mp.default_tex;
        unsigned int metallic_to_use = (m.metallic_visible && m.tex_metallic) ? m.tex_metallic : mp.default_tex;
        unsigned int extra_to_use    = (m.extra_visible    && m.tex_extra)    ? m.tex_extra    : mp.default_tex;
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, diffuse_to_use); glUniform1i(mp.tex_diffuse_loc, 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, normal_to_use); glUniform1i(mp.tex_normal_loc, 1);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, specular_to_use); glUniform1i(mp.tex_specular_loc, 2);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, metallic_to_use); glUniform1i(mp.tex_metallic_loc, 3);
        glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, extra_to_use); glUniform1i(mp.tex_extra_loc, 4);
        glBindVertexArray(m.vao); glDrawElements(GL_TRIANGLES, m.index_count, GL_UNSIGNED_INT, 0); glBindVertexArray(0);
    }
    glDepthMask(GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
unsigned int MP_GetTexture(ModelPreview& mp) { return mp.color_tex; }
#endif
