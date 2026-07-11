#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <filesystem>
#include <optional>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <sstream>
#include <limits>
#include <mutex>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <chrono>
#include "ModelPreview.h"
#include "../Level/TerrainSplat.h"
#include "../Level/Skybox/SkyboxRenderer.h"
// Temporary water-flashing diagnosis log -> water_debug.txt (app CWD).
// Truncated at app start, capped so it cannot grow unbounded.
static void water_debug_line(const char* fmt, ...) {
    static FILE* f = nullptr;
    static int lines = -1;
    if (lines < 0) {
        f = std::fopen("water_debug.txt", "w");
        lines = 0;
    }
    if (!f || lines >= 900) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fflush(f);
    if (++lines >= 900) {
        std::fclose(f);
        f = nullptr;
    }
}

// Level FX produced by the level loader (particle placements + parsed bank).
extern std::vector<Fx::Placement> g_pending_level_fx;
extern Fx::Bank                   g_particle_bank;
extern bool                       g_particle_bank_loaded;
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

static inline std::string tolower_copy(std::string s){ std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }
static inline std::string basename_lower_noext(const std::string& s){
    auto b = std::filesystem::path(s).filename().string();
    auto p = b.find_last_of('.');
    if(p!=std::string::npos) b = b.substr(0,p);
    return tolower_copy(b);
}
static bool mp_is_adjacent_terrain_mesh(const MPPerMesh& m)
{
    return m.name.rfind("adjacent terrain", 0) == 0;
}
bool g_mp_vista_only = false;   // debug: draw ONLY adjacent (vista) terrain
static bool mp_should_hide_mesh(const MPPerMesh& m)
{
    if (g_mp_vista_only && !mp_is_adjacent_terrain_mesh(m)) return true;
    return !S.show_adjacent_terrain && mp_is_adjacent_terrain_mesh(m);
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
    for (size_t i = 0; i + 2 <= size; i += 2) {
        uint8_t t = data[i]; data[i] = data[i+1]; data[i+1] = t;
    }
}
static void swap_bc3_endian(uint8_t* data, size_t size) {
    swap_bc1_endian(data, size);
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
        atab[6] = 0;
        atab[7] = 255;
    }
    for (int i = 0; i < 16; ++i) {
        uint8_t ai = (uint8_t)((abits >> (3*i)) & 7);
        out16[i] = atab[ai];
    }
}

static void swap_bc5_endian(uint8_t* data, size_t size) {
    swap_bc1_endian(data, size);
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
                const size_t n_blocks =
                    ((size_t)w / 4u + ((size_t)w % 4u != 0u)) *
                    ((size_t)h / 4u + ((size_t)h % 4u != 0u));
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
    Skybox::ReleaseD3D11Resources(mp);
    if(mp.vs){ mp.vs->Release(); mp.vs=nullptr; }
    if(mp.ps){ mp.ps->Release(); mp.ps=nullptr; }
    if(mp.vs_terrain){ mp.vs_terrain->Release(); mp.vs_terrain=nullptr; }
    if(mp.ps_terrain){ mp.ps_terrain->Release(); mp.ps_terrain=nullptr; }
    if(mp.ps_terrain_direct){ mp.ps_terrain_direct->Release(); mp.ps_terrain_direct=nullptr; }
    if(mp.ps_terrain_landscape){ mp.ps_terrain_landscape->Release(); mp.ps_terrain_landscape=nullptr; }
    if(mp.cbuffer_terrain){ mp.cbuffer_terrain->Release(); mp.cbuffer_terrain=nullptr; }
    if(mp.vs_water){ mp.vs_water->Release(); mp.vs_water=nullptr; }
    if(mp.ps_water){ mp.ps_water->Release(); mp.ps_water=nullptr; }
    if(mp.cbuffer_water){ mp.cbuffer_water->Release(); mp.cbuffer_water=nullptr; }
    if(mp.vs_weather){ mp.vs_weather->Release(); mp.vs_weather=nullptr; }
    if(mp.ps_weather){ mp.ps_weather->Release(); mp.ps_weather=nullptr; }
    if(mp.cbuffer_weather){ mp.cbuffer_weather->Release(); mp.cbuffer_weather=nullptr; }
    if(mp.cbuffer_fog){ mp.cbuffer_fog->Release(); mp.cbuffer_fog=nullptr; }
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
    if(mp.dssNoWriteLEqual){ mp.dssNoWriteLEqual->Release(); mp.dssNoWriteLEqual=nullptr; }
    if(mp.dssWaterOnce){ mp.dssWaterOnce->Release(); mp.dssWaterOnce=nullptr; }
    for(auto& kv : mp.fx_tex_srv){ if(kv.second) kv.second->Release(); }
    mp.fx_tex_srv.clear();
    if(mp.vs_fx){ mp.vs_fx->Release(); mp.vs_fx=nullptr; }
    if(mp.ps_fx){ mp.ps_fx->Release(); mp.ps_fx=nullptr; }
    if(mp.layout_fx){ mp.layout_fx->Release(); mp.layout_fx=nullptr; }
    if(mp.cbuffer_fx){ mp.cbuffer_fx->Release(); mp.cbuffer_fx=nullptr; }
    if(mp.fx_vb){ mp.fx_vb->Release(); mp.fx_vb=nullptr; }
    mp.fx_vb_capacity = 0;
    if(mp.bs_fx_alpha){ mp.bs_fx_alpha->Release(); mp.bs_fx_alpha=nullptr; }
    if(mp.bs_fx_add){ mp.bs_fx_add->Release(); mp.bs_fx_add=nullptr; }
    for (auto& kv : mp.fx_blend_states)
        if (kv.second) kv.second->Release();
    mp.fx_blend_states.clear();
    if(mp.bs_water){ mp.bs_water->Release(); mp.bs_water=nullptr; }
    mp.fx_system.clear();
    mp.has_model = false;
}
static bool compile_shader(const char* src, const char* entry, const char* profile, ID3DBlob** blob){
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, profile, flags, 0, blob, &err);
    if (FAILED(hr) && err) {
        const char* msg = static_cast<const char*>(err->GetBufferPointer());
        OutputLog::error(std::string("shader compile failed ")
            + profile + "/" + entry + ": "
            + (msg ? msg : "unknown error"));
    }
    if(err){ err->Release(); }
    return SUCCEEDED(hr);
}
static const char* g_fog_hlsl = R"(
cbuffer FogCB : register(b5){
    float4 fog_colour;
    float4 fog_dist;
    float4 fog_density;
    float4 fog_misc;
}
float3 apply_env_fog(float3 col, float depth, float wy){
    if (fog_colour.w < 0.5) return col;
    float dn = max(depth - fog_dist.x, 0.0) * fog_dist.y;
    float od = fog_dist.w * pow(min(dn, 1.25), fog_dist.z);
    float f = 1.0 - exp(-od);
    float mist = 0.0;
    if (fog_density.z > 0.0001) {
        float below = saturate((fog_misc.x - wy) / max(fog_misc.y, 0.5));
        mist = fog_density.z * below *
               saturate(depth / max(fog_density.w, 1.0));
    }
    f = saturate(f + mist);
    return lerp(col, fog_colour.rgb, f);
}
)";

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
struct VSOUT{ float4 p:SV_Position; float3 n:NORMAL; float2 t:TEXCOORD0; float3 wp:TEXCOORD1; };
VSOUT VS(VSIN i){
    VSOUT o;

    float4x4 skin = (params.y > 0.5)
        ? float4x4(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1)
        : bones[i.bid.x] * i.bw.x +
          bones[i.bid.y] * i.bw.y +
          bones[i.bid.z] * i.bw.z +
          bones[i.bid.w] * i.bw.w;

    float4 p_skin = mul(float4(i.p, 1.0), skin);
    float3 n_skin = mul(i.n, (float3x3)skin);

    o.p = mul(p_skin, mvp);
    float3 n = mul(n_skin, (float3x3)mv);
    o.n = normalize(n);
    o.t = i.t;
    o.wp = p_skin.xyz;
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
struct VSOUT{ float4 p:SV_Position; float3 n:NORMAL; float2 t:TEXCOORD0; float3 wp:TEXCOORD1; };
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

    if (params.z > 1.5) {
        return float4(float3(0.20, 0.95, 0.40) * (0.4 + 0.6 * diff_term), 1.0);
    }
    if (params.z > 0.5) {
        float3 hi = float3(0.15, 0.45, 1.00);
        color = lerp(color, hi, 0.65);
    }

    if (params.x > 0.5 && alpha < 0.25) discard;
    if (params.x < 0.5) alpha = 1.0;

    color = apply_env_fog(color, i.p.w, i.wp.y);
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
    
    float4 chunk_origin_extent;
    
    float4 chunk_grid_size;
    

    float4 splat_params;
    




    float4 mesh_xform;
}
Texture2DArray  lod_array     : register(t0);
Texture2DArray  chunk_idx     : register(t1);
Texture2DArray  chunk_blend   : register(t2);
Texture2DArray  chunk_uv      : register(t3);
Texture2D       splat_mask    : register(t4);
Texture2D       lightmap      : register(t5);
SamplerState    smp_wrap      : register(s0);
SamplerState    smp_point     : register(s1);

struct VSOUT{
    float4 p   : SV_Position;
    float3 n   : NORMAL;
    float2 t   : TEXCOORD0;
    float3 wp  : TEXCOORD1;
};

float3 sample_lod(int slice, float2 uv){
    
    return lod_array.Sample(smp_wrap, float3(uv, slice)).rgb;
}

float4 PS(VSOUT i) : SV_Target {
    



    float2 mesh_xy  = float2(i.wp.x, i.wp.z);
    float2 world_xy = mesh_xy * mesh_xform.xy + mesh_xform.zw;
    float2 origin   = chunk_origin_extent.xy;
    float2 extent   = chunk_origin_extent.zw;
    float  CW       = chunk_grid_size.x;
    float  CH       = chunk_grid_size.y;
    float  max_lod  = chunk_grid_size.z;
    float2 mat_uv   = world_xy * splat_params.y;

    
    float2 chunk_co = (world_xy - origin) / extent;

    
    float2 chunk_clamped = clamp(chunk_co,
                                 float2(0, 0),
                                 float2(CW - 0.001, CH - 0.001));
    int2   chunk_xy = int2(floor(chunk_clamped));
    float2 corner_uv = frac(chunk_clamped);

    
    float wx = corner_uv.x, wy = corner_uv.y;
    float w00 = (1.0 - wx) * (1.0 - wy);
    float w10 =        wx  * (1.0 - wy);
    float w01 = (1.0 - wx) *        wy ;
    float w11 =        wx  *        wy ;

    float3 final = float3(0.0, 0.0, 0.0);
    float  weight_sum = 0.0;

    


    [loop]
    for (int L = 0; L < 16; ++L) {
        float4 idx_norm = chunk_idx.Load(int4(chunk_xy, L, 0));
        float4 idx255   = round(idx_norm * 255.0);
        if (idx255.x > 254.5) break;

        float4 bln_norm = chunk_blend.Load(int4(chunk_xy, L, 0));
        float4 uv_info  = chunk_uv.Load(int4(chunk_xy, L, 0));
        

        float bscale = splat_params.x;
        float4 bln   = bln_norm * 255.0 / bscale;
        float2 mask_uv = uv_info.xy + corner_uv * uv_info.zw * 2.0;
        float mask_w = splat_mask.SampleLevel(smp_point, mask_uv, 0).r;

        

        float3 c00 = sample_lod((int)idx255.x, mat_uv);
        float3 c10 = sample_lod((int)idx255.y, mat_uv);
        float3 c01 = sample_lod((int)idx255.z, mat_uv);
        float3 c11 = sample_lod((int)idx255.w, mat_uv);

        
        float cw00 = mask_w * saturate(bln.x) * w00;
        float cw10 = mask_w * saturate(bln.y) * w10;
        float cw01 = mask_w * saturate(bln.z) * w01;
        float cw11 = mask_w * saturate(bln.w) * w11;

        final += c00 * cw00 + c10 * cw10 + c01 * cw01 + c11 * cw11;
        weight_sum += cw00 + cw10 + cw01 + cw11;
    }

    

    if (weight_sum > 0.001) {
        final /= weight_sum;
    } else {
        final = sample_lod(0, mat_uv);
    }

    

    float2 lm_uv = chunk_co / float2(CW, CH);
    float  ao    = lightmap.Sample(smp_wrap, lm_uv).r;
    final *= (ao * 0.55 + 0.45);

    
    float3 N = normalize(i.n);
    float3 light_vec = normalize(float3(0.3, 0.7, 0.5));
    float  ndotl = saturate(dot(N, light_vec));
    float  shade = 0.55 + 0.45 * ndotl;
    final *= shade;

    
    if (params.z > 0.5) {
        float3 hi = float3(0.15, 0.45, 1.00);
        final = lerp(final, hi, 0.65);
    }

    final = apply_env_fog(final, i.p.w, i.wp.y);
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
    float4 weight_size;
    float4 material_params[32];
}
Texture2DArray  lod_array     : register(t0);
Texture2DArray  chunk_idx     : register(t1);
Texture2DArray  chunk_blend   : register(t2);
Texture2DArray  chunk_uv      : register(t3);
Texture2D       splat_mask    : register(t4);
Texture2D       lightmap      : register(t5);
Texture2DArray  lod_detail_array : register(t6);
Texture2DArray  material_weight  : register(t7);
SamplerState    smp_wrap      : register(s0);
SamplerState    smp_point     : register(s1);

struct VSOUT{
    float4 p   : SV_Position;
    float3 n   : NORMAL;
    float2 t   : TEXCOORD0;
    float3 wp  : TEXCOORD1;
};

float3 sample_material(int slice, float2 world_xy, float slope_w){
    int max_slice = max((int)chunk_grid_size.z - 1, 0);
    int s = min(max(slice, 0), max_slice);
    float4 mp = material_params[s];
    float base_scale = splat_params.y;
    float detail_scale = splat_params.y;
    float3 base = lod_array.Sample(
        smp_wrap, float3(world_xy * base_scale, s)).rgb;
    float3 detail = lod_detail_array.Sample(
        smp_wrap, float3(world_xy * detail_scale, s)).rgb;
    float detail_w = slope_w * saturate(mp.z) * saturate(mp.w);
    return lerp(base, detail, detail_w);
}

float4 PS(VSOUT i) : SV_Target {
    float2 mesh_xy  = float2(i.wp.x, i.wp.z);
    float2 world_xy = mesh_xy * mesh_xform.xy + mesh_xform.zw;
    float2 origin   = chunk_origin_extent.xy;
    float2 extent   = chunk_origin_extent.zw;
    float  CW       = chunk_grid_size.x;
    float  CH       = chunk_grid_size.y;

    float2 chunk_co = (world_xy - origin) / extent;
    float2 chunk_clamped = clamp(chunk_co,
                                 float2(0, 0),
                                 float2(CW - 0.001, CH - 0.001));
    float slope_w = saturate((0.90 - abs(normalize(i.n).y)) / 0.10);

    float3 final = float3(0.0, 0.0, 0.0);
    float  weight_sum = 0.0;
    float2 inv_weight_size = weight_size.zw;
    float2 weight_uv = (chunk_clamped * 32.0 + 0.5) * inv_weight_size;
    float2 edge_uv = 0.5 * inv_weight_size;
    weight_uv = clamp(weight_uv, edge_uv, 1.0 - edge_uv);

    [loop]
    for (int mat = 0; mat < 32; ++mat) {
        if (mat >= (int)chunk_grid_size.z) break;
        float w = material_weight.SampleLevel(
            smp_point, float3(weight_uv, mat), 0).r;
        if (w <= 0.001) continue;
        final += sample_material(mat, world_xy, slope_w) * w;
        weight_sum += w;
    }

    if (weight_sum > 0.001) {
        final /= weight_sum;
    } else {
        final = sample_material(0, world_xy, slope_w);
    }

    if (params.z > 0.5) {
        float3 hi = float3(0.10, 0.95, 0.25);
        final = lerp(final, hi, 0.65);
    }

    final = apply_env_fog(final, i.p.w, i.wp.y);
    return float4(final, 1.0);
}
)";

static const char* g_terrain_ps_landscape = R"(
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
    float4 weight_size;
    float4 material_params[32];
}
Texture2DArray  lod_array     : register(t0);
Texture2DArray  chunk_idx     : register(t1);
Texture2DArray  chunk_blend   : register(t2);
Texture2DArray  chunk_uv      : register(t3);
Texture2D       splat_mask    : register(t4);
Texture2D       lightmap      : register(t5);
Texture2DArray  lod_detail_array : register(t6);
Texture2DArray  material_weight  : register(t7);
SamplerState    smp_wrap      : register(s0);
SamplerState    smp_point     : register(s1);

struct VSOUT{
    float4 p   : SV_Position;
    float3 n   : NORMAL;
    float2 t   : TEXCOORD0;
    float3 wp  : TEXCOORD1;
};

float3 sample_material(int slice, float2 world_xy, float slope_w){
    int max_slice = max((int)chunk_grid_size.z - 1, 0);
    int s = min(max(slice, 0), max_slice);
    float4 mp = material_params[s];
    float base_scale   = (mp.x > 0.0) ? mp.x : splat_params.y;
    float detail_scale = (mp.y > 0.0) ? mp.y : base_scale;
    float3 base = lod_array.Sample(
        smp_wrap, float3(world_xy * base_scale, s)).rgb;
    float3 detail = lod_detail_array.Sample(
        smp_wrap, float3(world_xy * detail_scale, s)).rgb;
    float detail_w = slope_w * saturate(mp.z) * saturate(mp.w);
    return lerp(base, detail, detail_w);
}

float4 PS(VSOUT i) : SV_Target {
    float2 mesh_xy  = float2(i.wp.x, i.wp.z);
    float2 world_xy = mesh_xy * mesh_xform.xy + mesh_xform.zw;
    float2 origin   = chunk_origin_extent.xy;
    float2 extent   = chunk_origin_extent.zw;
    float  CW       = chunk_grid_size.x;
    float  CH       = chunk_grid_size.y;

    float2 chunk_co = (world_xy - origin) / extent;
    float2 chunk_clamped = clamp(chunk_co,
                                 float2(0, 0),
                                 float2(CW - 0.001, CH - 0.001));
    float slope_w = saturate((0.90 - abs(normalize(i.n).y)) / 0.10);

    float3 final = float3(0.0, 0.0, 0.0);
    float  weight_sum = 0.0;
    float2 inv_weight_size = weight_size.zw;
    float2 weight_uv = (chunk_clamped * 32.0 + 0.5) * inv_weight_size;
    float2 edge_uv = 0.5 * inv_weight_size;
    weight_uv = clamp(weight_uv, edge_uv, 1.0 - edge_uv);

    [loop]
    for (int mat = 0; mat < 32; ++mat) {
        if (mat >= (int)chunk_grid_size.z) break;
        float w = material_weight.SampleLevel(
            smp_point, float3(weight_uv, mat), 0).r;
        if (w <= 0.001) continue;
        final += sample_material(mat, world_xy, slope_w) * w;
        weight_sum += w;
    }

    if (weight_sum > 0.001) {
        final /= weight_sum;
    } else {
        final = sample_material(0, world_xy, slope_w);
    }

    if (params.z > 0.5) {
        float3 hi = float3(0.10, 0.95, 0.25);
        final = lerp(final, hi, 0.65);
    }

    final = apply_env_fog(final, i.p.w, i.wp.y);
    return float4(final, 1.0);
}
)";

static const char* g_water_vs = R"(
cbuffer CB : register(b0){
    float4x4 mvp;
    float4   lightDir;
    float4x4 mv;
    float4   params;
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
// Retail water VS = ShadersRelease.sbk program 56 (entries 63/64): flat
// plane through the viewproj, world pos + two scrolled bump UV pairs +
// detail UVs + a per-vertex fresnel out. The UVs are linear in world XY so
// the preview computes them per-pixel (identical result); the detail UVs
// and vertex fresnel are never read by pixel program 57, and the cell-mask
// vertex fetch is moot because preview water meshes are pre-cut per cell.
VSOUT VS(VSIN i){
    VSOUT o;
    o.p  = mul(float4(i.p, 1.0), mvp);
    o.n  = float3(0.0, 1.0, 0.0);
    o.t  = i.t;
    o.wp = i.p;
    return o;
}
)";

static const char* g_water_ps = R"(
cbuffer CB : register(b0){
    float4x4 mvp;
    float4   lightDir;
    float4x4 mv;
    float4   params;
}
cbuffer WaterCB : register(b3){
    float4 w_bias;    // x FresnelBias  y ReflectionBias  z NormalScale
                      // w theme opacity (unused by the 1:1 PS)
    float4 w_ph01;    // xy m_BumpmapPhases[0]  zw m_BumpmapPhases[1]
    float4 w_sc01;    // xy m_BumpmapScales[0]  zw m_BumpmapScales[1]
    float4 w_surface; // rgb m_SurfaceWaterColour  w m_DiffuseAbsorption
    float4 w_deep;    // rgb m_DeepWaterColour     w m_ReflectionStrength
    float4 w_reflp;   // x m_ReflectionScale  y m_RefractionScale
                      // z m_GlitteringNormalBendFactor  w m_GlitteringStrength
    float4 w_glit;    // x m_GlitteringPower  y highlight  z sky_ok
                      // w exact-dome-reflection available
    float4 w_eye;     // xyz eye world pos  w time
    float4 w_sun;     // rgb light colour (retail c143: theme
                      // MainLightColour)  w sun elevation
    float4 w_light;   // xyz light direction, direction the light travels
                      // (retail c142: sun by day, moon by night)
}
cbuffer SkyCB : register(b4){
    float4 sky_top;
    float4 sky_bottom;
    float4 sky_sunset;
    float4 sky_params;
    float4 cloud_layer[4];
    float4 cloud_shape[4];
    float4 cloud_motion[4];
    float4 cloud_light[4];
    float4 cloud_global;
    float4 cloud_density_flags;
    float4 sky_right;
    float4 sky_up;
    float4 sky_forward;
    float4 sky_sun;
    float4 sky_texture_flags;
    float4 sky_celestial;
}
Texture2D    tex_normal : register(t0);
SamplerState smp        : register(s0);
// Exact dome reflection inputs: the sky pass's in-scatter LUT and constant
// block, bound at the water draw so the reflection is computed from the very
// data the visible sky is drawn with.
Texture2D    water_sky_lut : register(t4);
SamplerState smp_clamp     : register(s2);
cbuffer SkyDomeXexCB : register(b6){
    row_major float4x4 dome_view_projection;
    float4 dome_camera_position;
    float4 dome_camera_position_engine;
    float4 dome_beta_rayleigh;   // c64
    float4 dome_beta_mie;        // c65
    float4 dome_sun_direction;   // c66 (engine space, z-up)
    float4 dome_scattering_misc; // c67 (y = 1900 horizon distance)
    float4 dome_overlay_blend;
    float4 dome_misc;            // x=150 y=100 z=exposure(c20.w)
    float4 dome_bgmap_c72;
    float4 dome_bgmap_c73;
    float4 dome_bgmap_c74;
    float4 dome_bgmap_c75;
    float4 dome_bgmap_c76;
    float4 dome_bgmap_c28;
    float4 dome_camera_right;
    float4 dome_camera_up;
    float4 dome_camera_forward;
}

struct PSIN{
    float4 p   : SV_Position;
    float3 n   : NORMAL;
    float2 t   : TEXCOORD0;
    float3 wp  : TEXCOORD1;
};

// Reflection colour along a ray: same Hoffman-Preetham single-scattering model
// as the sky pass (the engine's ReflectionTile is the scene+sky rendered
// offscreen; sky dominates on water).
float3 water_sky(float3 ray){
    if (w_glit.z < 0.5) {
        return float3(0.58, 0.62, 0.57);
    }
    float3 sun_dir = normalize(sky_sun.xyz);
    float rayleigh = max(sky_params.z, 0.05);
    float mie = max(sky_params.w, 0.05);
    float3 betaR = float3(0.007337, 0.009459, 0.0257276) * rayleigh;
    float3 betaM = float3(0.0056149, 0.0063754, 0.0105143) * mie;
    float cosT = dot(ray, sun_dir);
    float phaseR = 0.059683103 * (1.0 + cosT * cosT);
    float gm = 0.80;
    float hg = 1.0 + gm * gm - 2.0 * gm * cosT;
    float phaseM = 0.079577468 * (1.0 - gm * gm) /
                   max(pow(abs(hg), 1.5), 0.0001);
    float elev = max(ray.y, 0.004);
    float path = 1.0 / (elev + 0.09);
    float3 od = (betaR + betaM) * path * 26.0;
    float3 extinct = exp(-od);
    float3 beta_sum = max(betaR + betaM, 0.00001);
    float3 inscatter =
        (betaR * phaseR + betaM * phaseM) / beta_sum * (1.0 - extinct);
    float sun_h = saturate(sun_dir.y * 2.2 + 0.12);
    float3 col = inscatter * (7.2 * sun_h) * sky_top.rgb;
    float horizon = 1.0 - saturate(elev * 3.2);
    float bias = saturate(sky_params.y);
    col = lerp(col, sky_bottom.rgb * (0.35 + 0.65 * sun_h),
               horizon * (0.55 + 0.30 * bias));
    float sunset_w = saturate(1.0 - abs(sun_dir.y) * 5.0) *
                     saturate(cosT * 0.5 + 0.5);
    col = lerp(col, sky_sunset.rgb * (0.4 + 0.8 * phaseM), sunset_w * 0.45);
    float night = saturate((-sun_dir.y + 0.05) / 0.45);
    col = lerp(col, col * 0.22 + float3(0.010, 0.018, 0.050), night);
    return col;
}

// Retail reflection tile = the sky dome rendered offscreen. Evaluate the
// exact dome core (packed program 111 packets 10-56: in-scatter LUT x
// (1 - transmittance)) along the reflected ray, then the same exposure and
// tone bound the sky pass applies, so the reflected sky matches the drawn
// sky. Cloud overlays and the bgmap/mist branch are omitted (deviation).
float3 dome_reflect(float3 ray_pv){
    float3 n = normalize(float3(ray_pv.x, ray_pv.z, ray_pv.y)); // y-up -> z-up
    float lut_u = saturate(dot(dome_sun_direction.xyz, n));     // p14-15
    float3 lut = water_sky_lut.Sample(smp_clamp,
                                      float2(lut_u, 0.5)).xyz;  // p40
    float nz_pow = pow(max(abs(n.z), 1e-5), 0.2);               // p20-22
    float depth_base = dome_scattering_misc.y * (1.05 + nz_pow);   // p24,28
    float od_r = (dome_misc.x + depth_base) * nz_pow + depth_base; // p31,48
    float od_m = (dome_misc.y + depth_base) * nz_pow + depth_base; // p33,48
    float3 od = od_m * dome_beta_mie.xyz +
                od_r * dome_beta_rayleigh.xyz;                  // p49-50
    float3 T = exp2(-od * 1.4427);                              // p51-54
    float3 hdr = (1.0 - T) * lut * dome_misc.z;                 // p55-56,100
    return hdr / (1.0 + hdr);   // same host tone bound as the sky pass
}

// 1:1 port of the retail default main-view water surface pixel shader:
// ShadersRelease.sbk program 57 (shader-table entry 65), the pair of vertex
// program 56 (entries 63/64). The entry table at 0x83317880/0x833178A0 is
// written by WaterBody_CreatePatchGrids; entries 66/68 are the same shader
// plus the bgmap-probe reflection fallback, 71-74 are depth-only passes.
// g_WaterConstants occupies PS c116..c141, ONE member per row, in packer
// order (sub_82B2A008): c116 FresnelBias, c117 ReflectionBias, c118/119
// BumpmapPhases, c120/121 BumpmapScales, c122..125 detail phases/scales
// (NOT sampled by this program), c126 SurfaceWaterColour, c127
// DeepWaterColour, c128 NormalScale, c129 ReflectionScale, c130
// RefractionScale, c131 ReflectionStrength, c132-135 diffuse/specular
// params (unused here), c136 GlitteringNormalBendFactor, c137
// GlitteringStrength, c138 GlitteringPower, c139 ShadowScaleBias
// {0.95,0.05}, c140 MaxRefractDistanceFactor (packer hardcodes 1/75),
// c141 EdgeBlendParameters. c142/c143 = light direction/colour.
// Packet numbers below reference the decoded Xenos microcode.
//
// HOST STAND-INS (everything else is the exact microcode math):
//  - f13 reflection tile: retail renders sky+landscape offscreen and samples
//    it at screen UV + Nsum.yx*ReflectionScale*0.5 (packets 21,26,28,30).
//    The preview evaluates its sky model along the equivalent perturbed
//    reflection ray instead (no offscreen scene pass).
//  - f14 refraction tile: a copy of the scene. The microcode's exact
//    coefficient on that sample, (1-distf)*ReflectionStrength*(1-frefl),
//    is emitted as alpha with a ONE/SRC_ALPHA blend so the framebuffer
//    behind the surface supplies the term (screen offset dropped).
//  - f1 cell-mask kill (packets 10,15-16): preview water meshes are already
//    cut per cell, so the kill is a no-op here.
//  - f8 screen-space shadow buffer (packets 49-50): no shadow pass in the
//    preview -> sample = 1; the c139 {0.95,0.05} scale-bias is kept.
//  - c141 depth edge blend (packets 11-13): needs the scene depth buffer;
//    edge factor = 1.
//  - fog (packets 76-87): retail runs the shared atmosphere-LUT fog
//    (f4/f5, c66-68) — the same model the preview applies scene-wide as
//    apply_env_fog, which is used here so water matches the terrain.
float4 PS(PSIN i) : SV_Target {
    // packets 19-20,24-25,33-39: dual normal-map fetch. Both samples are
    // unpacked n*2-1 and SUMMED (xy in [-2,2]); z = sqrt(1-x^2-y^2) per
    // sample, then added. Preview axes: game XY plane = wp.xz, game Z = y.
    float2 uv0 = i.wp.xz * w_sc01.xy + w_ph01.xy;
    float2 uv1 = i.wp.xz * w_sc01.zw + w_ph01.zw;
    float2 n0 = tex_normal.Sample(smp, uv0).xy * 2.0 - 1.0;
    float2 n1 = tex_normal.Sample(smp, uv1).xy * 2.0 - 1.0;
    float z0 = sqrt(saturate(1.0 - dot(n0, n0)));
    float z1 = sqrt(saturate(1.0 - dot(n1, n1)));
    float2 nxy = n0 + n1;
    float  nz  = z0 + z1;

    // packets 37-38,41-42: view vector camera-pos, length, normalize.
    float3 view = w_eye.xyz - i.wp;
    float dist = length(view);
    float3 V = view / max(dist, 1e-5);

    // packets 27,35-36,40-46: the fresnel normal scales the UP component by
    // m_NormalScale before normalizing (direction (nx, ny, NormalScale*nz)),
    // then fres = saturate(1 - dot(V,Nf) + FresnelBias). NormalScale=0.05
    // makes the fresnel normal nearly horizontal — that is retail behaviour.
    float3 Nf = normalize(float3(nxy.x, w_bias.z * nz, nxy.y));
    float fres = saturate((1.0 - dot(V, Nf)) + w_bias.x);

    // packets 39-47: watercol = DeepColour + (SurfaceColour-Deep)*fres.
    float3 watercol = lerp(w_deep.rgb, w_surface.rgb, fres);

    // packets 49-50: shadow*0.95+0.05 with shadow=1 (stand-in).
    float shadow_f = 1.0 * 0.95 + 0.05;
    // packet 51: reflection fresnel = saturate(fres + ReflectionBias).
    float frefl = saturate(fres + w_bias.y);
    // packet 14: ReflectionStrength saturated.
    float refl_str = saturate(w_deep.w);

    // f13 stand-in: sky along the reflected ray about the normal whose xy
    // perturbation matches the retail screen-UV offset Nsum*ReflScale*0.5.
    float3 Nr = normalize(float3(nxy.x * w_reflp.x * 0.5, 1.0,
                                 nxy.y * w_reflp.x * 0.5));
    float3 R = reflect(-V, Nr);
    R.y = abs(R.y);
    float3 sky_refl = (w_glit.w > 0.5) ? dome_reflect(R) : water_sky(R);
    float3 refl = sky_refl * shadow_f;

    // packets 52-59, exact combine with `refr` = refraction sample:
    //   reflMix = watercol + (refl - watercol)*refl_str
    //   refrMix = watercol + ((refr-watercol) + (refl-refr)*frefl)*refl_str
    //   out     = refrMix + (reflMix - refrMix)*distf
    //   distf   = saturate(dist * MaxRefractDistanceFactor)   [1/75]
    // Grouping by refr: coefficient (1-distf)*refl_str*(1-frefl) -> alpha;
    // everything else below.
    float distf = saturate(dist * (1.0 / 75.0));
    float refr_k = (1.0 - distf) * refl_str * (1.0 - frefl);
    float3 col = watercol * (1.0 - refl_str)
               + refl * (refl_str * lerp(frefl, 1.0, distf));

    // packets 60-75: glitter. g = normalize(Nf.x, Nf.y, -GlitterBend*Nf.z)
    // (game z = preview y); spec = saturate(dot(V, reflect(L, g)));
    // col += LightColour * GlitterStrength * spec^GlitterPower.
    float3 g = normalize(float3(Nf.x, -max(w_reflp.z, 0.0) * Nf.y, Nf.z));
    float3 L = normalize(w_light.xyz);   // c142 (sun by day, moon by night)
    float3 Rl = L - 2.0 * dot(L, g) * g;
    float spec = saturate(dot(V, Rl));
    float glint = pow(max(spec, 1e-6), max(w_glit.x, 1.0));
    col += w_sun.rgb * (glint * max(w_reflp.w, 0.0));

    if (w_glit.y > 0.5) {
        float3 hi = float3(0.20, 0.55, 0.85);
        col = lerp(col, hi, 0.55);
    }

    // packets 76-87 fog (shared scene fog stands in, see header comment);
    // packet 88 exposure is applied by the preview's common path.
    col = apply_env_fog(col, i.p.w, i.wp.y);

    // packet 89 export; alpha = refraction coefficient for the
    // ONE/SRC_ALPHA composite (retail alpha is the depth edge factor).
    return float4(col, refr_k);
}
)";

static const char* g_weather_vs = R"(
cbuffer WeatherCB : register(b6){
    float4x4 wvp;
    float4 cam_time;
    float4 wind_vec;
    float4 rain_p;
    float4 snow_p;
    float4 area;
    float4 cam_right;
    float4 cam_up;
}
struct VSOUT{
    float4 p    : SV_Position;
    float2 uv   : TEXCOORD0;
    float2 meta : TEXCOORD1;
};

float wx_hash(float n){ return frac(sin(n) * 43758.5453123); }

VSOUT VS(uint id : SV_VertexID){
    uint pid = id / 6;
    uint corner = id % 6;
    float2 uv;
    if (corner == 0) uv = float2(0.0, 0.0);
    else if (corner == 1) uv = float2(1.0, 0.0);
    else if (corner == 2) uv = float2(0.0, 1.0);
    else if (corner == 3) uv = float2(1.0, 0.0);
    else if (corner == 4) uv = float2(1.0, 1.0);
    else uv = float2(0.0, 1.0);

    float t = cam_time.w;
    bool is_rain = (float)pid < rain_p.x;
    float seed = (float)pid * (is_rain ? 1.6180339 : 2.2360679)
               + (is_rain ? 0.0 : 71.0);
    float h1 = wx_hash(seed + 1.3);
    float h2 = wx_hash(seed + 7.7);
    float h3 = wx_hash(seed + 3.9);
    float h4 = wx_hash(seed + 9.1);

    float3 center;
    float fade = 1.0;
    float3 off = float3(0.0, 0.0, 0.0);
    if (is_rain) {
        float R = area.x;
        float H = area.y;
        float speed = max(rain_p.z, 1.0);
        float3 fall = float3(wind_vec.x * 0.14, -speed, wind_vec.z * 0.14);
        float yfrac = 1.0 - frac(h2 + t * speed / H);
        center.x = cam_time.x + (h1 * 2.0 - 1.0) * R;
        center.z = cam_time.z + (h3 * 2.0 - 1.0) * R;
        center.y = cam_time.y - H * 0.5 + yfrac * H;
        float len = 0.55 * max(rain_p.y, 0.15);
        float wdt = 0.016 * max(rain_p.y, 0.15);
        float3 d = normalize(fall);
        float3 to_cam = center - cam_time.xyz;
        float3 side = cross(d, to_cam + float3(0.003, 0.0, 0.007));
        float side_len = length(side);
        side = (side_len > 0.0001) ? side / side_len : float3(1.0, 0.0, 0.0);
        off = side * (uv.x - 0.5) * wdt + d * (uv.y - 0.5) * len;
        fade = saturate(1.35 - length(to_cam) / R);
    } else {
        float R = area.z;
        float H = area.w;
        float speed = max(snow_p.z, 0.15);
        float yfrac = 1.0 - frac(h2 + t * speed / H);
        float drift_x = wind_vec.x * t * 0.35 / max(2.0 * R, 1.0);
        float drift_z = wind_vec.z * t * 0.35 / max(2.0 * R, 1.0);
        center.x = cam_time.x + (frac(h1 + drift_x) * 2.0 - 1.0) * R;
        center.z = cam_time.z + (frac(h3 + drift_z) * 2.0 - 1.0) * R;
        center.y = cam_time.y - H * 0.5 + yfrac * H;
        center.x += sin(t * 0.9 + h4 * 6.2831) * 0.45 * snow_p.w;
        center.z += cos(t * 0.7 + h4 * 12.566) * 0.35 * snow_p.w;
        // Retail flake size (XEX VS 148, packets 83/609/611): point sprite
        // pixels = frac(hash) * viewportWidth * min(SNOW_SIZE,1) * 0.109375
        // / depth, i.e. a constant world-space diameter of
        // rand[0,1) * min(SNOW_SIZE,1) * 0.109375 * 2 * tan(fov_x/2).
        // cam_right.w carries tan(fov_x/2).
        float s = wx_hash(seed + 5.3) * saturate(snow_p.y) * 0.109375
                * 2.0 * cam_right.w;
        off = cam_right.xyz * (uv.x - 0.5) * s +
              cam_up.xyz * (uv.y - 0.5) * s;
        float3 to_cam = center - cam_time.xyz;
        fade = saturate(1.5 - length(to_cam) / R);
    }

    VSOUT o;
    o.p = mul(float4(center + off, 1.0), wvp);
    o.uv = uv;
    o.meta = float2(is_rain ? 0.0 : 1.0, fade);
    return o;
}
)";

static const char* g_weather_ps = R"(
cbuffer WeatherCB : register(b6){
    float4x4 wvp;
    float4 cam_time;
    float4 wind_vec;
    float4 rain_p;
    float4 snow_p;
    float4 area;
    float4 cam_right;
    float4 cam_up;
}
struct PSIN{
    float4 p    : SV_Position;
    float2 uv   : TEXCOORD0;
    float2 meta : TEXCOORD1;
};
float4 PS(PSIN i) : SV_Target {
    float alpha;
    float3 col;
    if (i.meta.x < 0.5) {
        float ax = 1.0 - abs(i.uv.x * 2.0 - 1.0);
        float ay = 1.0 - abs(i.uv.y * 2.0 - 1.0);
        alpha = pow(saturate(ax), 2.2) * saturate(ay) *
                rain_p.w * i.meta.y;
        col = float3(0.62, 0.68, 0.78);
    } else {
        float2 d = i.uv * 2.0 - 1.0;
        float r = length(d);
        alpha = smoothstep(1.0, 0.25, r) * 0.9 * i.meta.y;
        col = float3(0.92, 0.94, 0.98);
    }
    if (alpha < 0.004) discard;
    return float4(col, alpha);
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

// Like create_srv_from_rgba but with a full mip chain. Vista/background
// terrain recedes to the horizon at grazing angles, so without mips its pages
// minify into RGB static. The chain is built on the CPU (box filter) and
// uploaded as an IMMUTABLE texture -- GenerateMips proved unreliable in this
// device/context setup, this is deterministic.
ID3D11ShaderResourceView* create_srv_from_rgba_mipped(
        ID3D11Device* dev, int w, int h, const std::vector<uint8_t>& rgba){
    constexpr int kMaxUploadDim = 8192;
    if (!dev || w <= 0 || h <= 0 || w > kMaxUploadDim || h > kMaxUploadDim) {
        return nullptr;
    }
    const uint64_t expected = uint64_t(w) * uint64_t(h) * 4ull;
    if (expected == 0 || rgba.size() < expected) return nullptr;

    // Build the box-filtered mip chain down to 1x1.
    std::vector<std::vector<uint8_t>> mips;
    std::vector<std::pair<int,int>> dims;
    mips.emplace_back(rgba.begin(), rgba.begin() + size_t(expected));
    dims.emplace_back(w, h);
    while (dims.back().first > 1 || dims.back().second > 1) {
        const int sw = dims.back().first;
        const int sh = dims.back().second;
        const int dw = std::max(1, sw / 2);
        const int dh = std::max(1, sh / 2);
        const std::vector<uint8_t>& src = mips.back();
        std::vector<uint8_t> dst(size_t(dw) * dh * 4);
        for (int y = 0; y < dh; ++y) {
            const int y0 = std::min(y * 2, sh - 1);
            const int y1 = std::min(y0 + 1, sh - 1);
            for (int x = 0; x < dw; ++x) {
                const int x0 = std::min(x * 2, sw - 1);
                const int x1 = std::min(x0 + 1, sw - 1);
                const uint8_t* a = &src[(size_t(y0) * sw + x0) * 4];
                const uint8_t* b = &src[(size_t(y0) * sw + x1) * 4];
                const uint8_t* c = &src[(size_t(y1) * sw + x0) * 4];
                const uint8_t* d = &src[(size_t(y1) * sw + x1) * 4];
                uint8_t* o = &dst[(size_t(y) * dw + x) * 4];
                for (int k = 0; k < 4; ++k) {
                    o[k] = uint8_t((int(a[k]) + b[k] + c[k] + d[k] + 2) / 4);
                }
            }
        }
        mips.push_back(std::move(dst));
        dims.emplace_back(dw, dh);
    }

    D3D11_TEXTURE2D_DESC td{};
    td.Width = (UINT)w;
    td.Height = (UINT)h;
    td.MipLevels = (UINT)mips.size();
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    std::vector<D3D11_SUBRESOURCE_DATA> sd(mips.size());
    for (size_t i = 0; i < mips.size(); ++i) {
        sd[i].pSysMem = mips[i].data();
        sd[i].SysMemPitch = (UINT)dims[i].first * 4u;
        sd[i].SysMemSlicePitch = 0;
    }

    ID3D11Texture2D* t = nullptr;
    if (FAILED(dev->CreateTexture2D(&td, sd.data(), &t)) || !t) return nullptr;
    ID3D11ShaderResourceView* v = nullptr;
    if (FAILED(dev->CreateShaderResourceView(t, nullptr, &v))) {
        t->Release();
        return nullptr;
    }
    t->Release();
    return v;
}

static bool srv_from_tex_blob_auto(ID3D11Device* dev, const std::vector<unsigned char>& blob, ID3D11ShaderResourceView** out_srv, bool* out_has_alpha,
                                   int* out_w = nullptr, int* out_h = nullptr){
    *out_srv = nullptr;
    std::vector<uint8_t> rgba;
    int w, h;
    if(!decode_tex_to_rgba(blob, rgba, w, h, out_has_alpha)) return false;
    *out_srv = create_srv_from_rgba(dev, w, h, rgba);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return (*out_srv != nullptr);
}
// Particle FX billboard shader. Vertices are pre-transformed to world space by
// the CPU sim (camera-facing quads); the VS only applies view-projection.
static const char* g_fx_vs = R"(
cbuffer FxCB : register(b0){ float4x4 vp; }
struct VSIN { float3 p:POSITION; float2 t:TEXCOORD0; float4 c:COLOR0; };
struct VSOUT{ float4 p:SV_Position; float2 t:TEXCOORD0; float4 c:COLOR0; };
VSOUT VS(VSIN i){ VSOUT o; o.p=mul(float4(i.p,1.0),vp); o.t=i.t; o.c=i.c; return o; }
)";
// 1:1 port of the retail particle pixel shader (ShadersRelease.sbk entry 244,
// program 210 — the base variant of the 16-entry particle PS table registered
// at 0x8335F648). Exact microcode:
//   o0.rgb = vertexAlpha * c20.w * vertexColour.rgb * tex.rgb * tex.rgb
//   o0.a   = tex.a * vertexAlpha
// The texture is SQUARED (pfx textures are sqrt-encoded); vertex alpha is
// premultiplied into rgb while o0.a carries tex.a for the fixed-function
// destination factor. HOST STAND-INS: c20.w exposure = 1 (the preview scene
// has no exposure pass); the multi-texture cross-fade variants (entries
// 248-259) are approximated with complementary quads CPU-side; the soft-depth
// variants (f13 depth fetch) are not run (no scene-depth SRV bound here).
static const char* g_fx_ps = R"(
Texture2D tex0 : register(t0);
SamplerState smp : register(s0);
struct VSOUT{ float4 p:SV_Position; float2 t:TEXCOORD0; float4 c:COLOR0; };
float4 PS(VSOUT i) : SV_Target {
    float4 s = tex0.Sample(smp, i.t);
    float exposure = 1.0;              // c20.w host stand-in
    float3 rgb = i.c.a * exposure * i.c.rgb * s.rgb * s.rgb;
    float  a   = s.a * i.c.a;
    return float4(rgb, a);
}
)";

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
    const std::string ps_fog_src = std::string(g_fog_hlsl) + g_ps;
    if(!compile_shader(ps_fog_src.c_str(),"PS","ps_5_0",&psb)){ if(vsb) vsb->Release(); return false; }
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
        ID3DBlob* tdpsb = nullptr; ID3DBlob* tlpsb = nullptr;
        const std::string tps_live_src =
            std::string(g_fog_hlsl) + g_terrain_ps_live;
        const std::string tps_direct_src =
            std::string(g_fog_hlsl) + g_terrain_ps;
        const std::string tps_landscape_src =
            std::string(g_fog_hlsl) + g_terrain_ps_landscape;
        if (compile_shader(g_terrain_vs, "VS", "vs_5_0", &tvsb) &&
            compile_shader(tps_live_src.c_str(), "PS", "ps_5_0", &tpsb))
        {
            dev->CreateVertexShader(tvsb->GetBufferPointer(),
                                    tvsb->GetBufferSize(),
                                    nullptr, &mp.vs_terrain);
            dev->CreatePixelShader(tpsb->GetBufferPointer(),
                                   tpsb->GetBufferSize(),
                                   nullptr, &mp.ps_terrain);
        }
        if (compile_shader(tps_direct_src.c_str(), "PS", "ps_5_0", &tdpsb)) {
            dev->CreatePixelShader(tdpsb->GetBufferPointer(),
                                   tdpsb->GetBufferSize(),
                                   nullptr, &mp.ps_terrain_direct);
        }
        if (compile_shader(tps_landscape_src.c_str(), "PS", "ps_5_0", &tlpsb)) {
            dev->CreatePixelShader(tlpsb->GetBufferPointer(),
                                   tlpsb->GetBufferSize(),
                                   nullptr, &mp.ps_terrain_landscape);
        }
        if (tvsb) tvsb->Release();
        if (tpsb) tpsb->Release();
        if (tdpsb) tdpsb->Release();
        if (tlpsb) tlpsb->Release();
    }
    {
        D3D11_BUFFER_DESC tcb{};
        tcb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        tcb.ByteWidth = 80 + 32 * 16;
        tcb.Usage = D3D11_USAGE_DYNAMIC;
        tcb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&tcb, nullptr, &mp.cbuffer_terrain);
    }

    Skybox::CreateD3D11Resources(dev, mp);

    {
        ID3DBlob* wvsb = nullptr; ID3DBlob* wpsb = nullptr;
        const std::string wps_src = std::string(g_fog_hlsl) + g_water_ps;
        if (compile_shader(g_water_vs, "VS", "vs_5_0", &wvsb) &&
            compile_shader(wps_src.c_str(), "PS", "ps_5_0", &wpsb))
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

        wcb.ByteWidth = 10 * 16;
        wcb.Usage = D3D11_USAGE_DYNAMIC;
        wcb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&wcb, nullptr, &mp.cbuffer_water);
    }
    {
        ID3DBlob* xvsb = nullptr; ID3DBlob* xpsb = nullptr;
        if (compile_shader(g_weather_vs, "VS", "vs_5_0", &xvsb) &&
            compile_shader(g_weather_ps, "PS", "ps_5_0", &xpsb))
        {
            dev->CreateVertexShader(xvsb->GetBufferPointer(),
                                    xvsb->GetBufferSize(),
                                    nullptr, &mp.vs_weather);
            dev->CreatePixelShader(xpsb->GetBufferPointer(),
                                   xpsb->GetBufferSize(),
                                   nullptr, &mp.ps_weather);
        }
        if (xvsb) xvsb->Release();
        if (xpsb) xpsb->Release();

        D3D11_BUFFER_DESC xcb{};
        xcb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        xcb.ByteWidth = 64 + 7 * 16;
        xcb.Usage = D3D11_USAGE_DYNAMIC;
        xcb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&xcb, nullptr, &mp.cbuffer_weather);

        D3D11_BUFFER_DESC fcb{};
        fcb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        fcb.ByteWidth = 4 * 16;
        fcb.Usage = D3D11_USAGE_DYNAMIC;
        fcb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&fcb, nullptr, &mp.cbuffer_fog);
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
    // Water: SRC=ONE, DEST=SRC_ALPHA. The retail water PS reads an explicit
    // refraction copy of the scene (sampler f14); the preview PS emits the
    // microcode's exact refraction coefficient as alpha so the framebuffer
    // already behind the surface supplies that term.
    D3D11_BLEND_DESC bdw = bda;
    bdw.RenderTarget[0].SrcBlend       = D3D11_BLEND_ONE;
    bdw.RenderTarget[0].DestBlend      = D3D11_BLEND_SRC_ALPHA;
    bdw.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
    bdw.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    if(FAILED(dev->CreateBlendState(&bdw,&mp.bs_water))) return false;
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
    D3D11_DEPTH_STENCIL_DESC dssle = dssn;
    dssle.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if(FAILED(dev->CreateDepthStencilState(&dssle, &mp.dssNoWriteLEqual))) return false;
    // Water: levels load one .water per heightfield (main + every
    // vista/adjacent), so the same surface is often present several times at
    // the same height. Retail can draw the copies on top of each other
    // (identical output, z-fight invisible), but the preview's framebuffer
    // refraction composite is dst-dependent, so a second coplanar copy would
    // re-composite and z-fighting would flash. Stencil-gate the pass: a
    // pixel takes the water composite once (stencil EQUAL 0, INCR on pass).
    D3D11_DEPTH_STENCIL_DESC dsswat = dssw;
    dsswat.StencilEnable = TRUE;
    dsswat.StencilReadMask = 0xFF;
    dsswat.StencilWriteMask = 0xFF;
    dsswat.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
    dsswat.FrontFace.StencilPassOp = D3D11_STENCIL_OP_INCR_SAT;
    dsswat.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    dsswat.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    dsswat.BackFace = dsswat.FrontFace;
    if(FAILED(dev->CreateDepthStencilState(&dsswat, &mp.dssWaterOnce))) return false;
    if(!create_white_srv(dev, &mp.default_srv)) return false;

    // ---- particle FX pipeline ----
    {
        ID3DBlob* fvs = nullptr; ID3DBlob* fps = nullptr;
        if (compile_shader(g_fx_vs, "VS", "vs_5_0", &fvs) &&
            compile_shader(g_fx_ps, "PS", "ps_5_0", &fps)) {
            dev->CreateVertexShader(fvs->GetBufferPointer(), fvs->GetBufferSize(),
                                    nullptr, &mp.vs_fx);
            dev->CreatePixelShader(fps->GetBufferPointer(), fps->GetBufferSize(),
                                   nullptr, &mp.ps_fx);
            D3D11_INPUT_ELEMENT_DESC el[] = {
                {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,  D3D11_INPUT_PER_VERTEX_DATA,0},
                {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,   0,12, D3D11_INPUT_PER_VERTEX_DATA,0},
                {"COLOR",   0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,20,D3D11_INPUT_PER_VERTEX_DATA,0},
            };
            dev->CreateInputLayout(el, 3, fvs->GetBufferPointer(),
                                   fvs->GetBufferSize(), &mp.layout_fx);
        }
        if (fvs) fvs->Release();
        if (fps) fps->Release();

        D3D11_BUFFER_DESC cb{};
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cb.ByteWidth = 4 * 16;          // one float4x4
        cb.Usage = D3D11_USAGE_DYNAMIC;
        cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&cb, nullptr, &mp.cbuffer_fx);

        // Premultiplied-alpha output => SrcBlend = ONE for both modes.
        D3D11_BLEND_DESC ba{};
        ba.RenderTarget[0].BlendEnable = TRUE;
        ba.RenderTarget[0].SrcBlend  = D3D11_BLEND_ONE;
        ba.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        ba.RenderTarget[0].BlendOp   = D3D11_BLEND_OP_ADD;
        ba.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
        ba.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        ba.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
        ba.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        dev->CreateBlendState(&ba, &mp.bs_fx_alpha);
        D3D11_BLEND_DESC badd = ba;
        badd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        dev->CreateBlendState(&badd, &mp.bs_fx_add);
    }
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

void MP_BuildLevelFx(ID3D11Device* dev, ModelPreview& mp){
    mp.fx_system.clear();
    for (auto& kv : mp.fx_tex_srv) { if (kv.second) kv.second->Release(); }
    mp.fx_tex_srv.clear();
    {
        std::ofstream f("C:\\Users\\pwd12\\OneDrive\\Documents\\GitHub\\"
                        "Fable2AssetBrowser\\fx_debug.log", std::ios::app);
        if (f) f << "MP_BuildLevelFx called: pending_fx="
                 << g_pending_level_fx.size() << " bank_loaded="
                 << g_particle_bank_loaded << "\n";
    }
    if (g_pending_level_fx.empty() || !g_particle_bank_loaded) return;

    mp.fx_system.build(g_particle_bank, g_pending_level_fx);

    const std::string preferred =
        (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty())
            ? S.selected_nested_temp_path : S.selected_bnk;
    for (const auto& t : mp.fx_system.textures()) {
        if (t.empty() || mp.fx_tex_srv.count(t)) continue;
        std::vector<unsigned char> buf;
        if (!build_any_tex_buffer_for_name(t, buf, preferred)) {
            mp.fx_tex_srv[t] = nullptr; continue;
        }
        bool ha = false; ID3D11ShaderResourceView* srv = nullptr;
        srv_from_tex_blob_auto(dev, buf, &srv, &ha);
        mp.fx_tex_srv[t] = srv;
    }
    mp.fx_last_time = 0.0;   // first render frame clamps the initial dt

    // Diagnostics: which effects actually resolved to real bank data, and
    // which fell back to the name heuristic (so missing/wrong FX are visible).
    {
        std::map<std::string, int> resolved, unresolved;
        for (const auto& p : g_pending_level_fx)
            (p.resolved ? resolved : unresolved)[p.effect_name]++;
        auto join = [](const std::map<std::string, int>& m) {
            std::string s; int shown = 0;
            for (const auto& kv : m) {
                if (shown++) s += ", ";
                s += kv.first;
                if (kv.second > 1) s += " x" + std::to_string(kv.second);
                if (shown >= 40) { s += ", ..."; break; }
            }
            return s;
        };
        OutputLog::success("fx: built " +
            std::to_string(mp.fx_system.instance_count()) +
            " instance(s); " + std::to_string(mp.fx_system.resolved_count()) +
            " resolved to bank effects, " +
            std::to_string(unresolved.size()) + " distinct unresolved");
        if (!resolved.empty())
            OutputLog::info("fx resolved: " + join(resolved));
        if (!unresolved.empty())
            OutputLog::warn("fx UNRESOLVED (name heuristic only): " +
                            join(unresolved));
        std::ofstream f("C:\\Users\\pwd12\\OneDrive\\Documents\\GitHub\\"
                        "Fable2AssetBrowser\\fx_debug.log", std::ios::app);
        if (f) f << "MP_BuildLevelFx built: instances="
                 << mp.fx_system.instance_count() << " resolved="
                 << mp.fx_system.resolved_count() << " textures="
                 << mp.fx_tex_srv.size() << " fx_show=" << mp.fx_show << "\n";
    }
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

static XMMATRIX bone_local_matrix_anim_delta(const float* tf,
                                             const float* anim_q,
                                             const float* anim_t) {
    XMVECTOR q = XMVectorSet(tf[0], tf[1], tf[2], tf[3]);
    XMVECTOR t = XMVectorSet(tf[4], tf[5], tf[6], 0.0f);
    XMVECTOR s = XMVectorSet(tf[7], tf[8], tf[9], 1.0f);
    XMMATRIX S_ = XMMatrixScalingFromVector(s);
    XMMATRIX R_ = XMMatrixRotationQuaternion(q);
    if (anim_q) {
        XMVECTOR qd = XMVectorSet(anim_q[0], anim_q[1],
                                  anim_q[2], anim_q[3]);
        R_ = XMMatrixRotationQuaternion(qd);
    }
    if (anim_t) {
        XMVECTOR dt = XMVectorSet(anim_t[0], anim_t[1], anim_t[2], 0.0f);
        t = XMVectorAdd(t, dt);
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

struct ClothSim {
    std::vector<float>    bind;
    std::vector<uint8_t>  bid;
    std::vector<float>    bw;
    std::vector<uint32_t> idx;
    std::vector<uint32_t> e0, e1;
    std::vector<float>    rest;
    std::vector<uint8_t>  pinned;
    std::vector<float>    pos, prev;
    std::vector<MPVertex> vtx;
    float scale = 1.0f;
    float damping = 0.05f;
    bool  inited = false;
};

static std::shared_ptr<ClothSim> mp_build_cloth(const MDLMeshGeom& g,
                                                std::vector<MPVertex>&& vtx){
    const size_t N = g.positions.size()/3;
    if(N==0 || g.indices.size()<3) return nullptr;
    auto c = std::make_shared<ClothSim>();
    c->bind = g.positions;
    c->idx  = g.indices;
    c->vtx  = std::move(vtx);
    const bool hasBI = (g.bone_ids.size()==N*4), hasBW = (g.bone_weights.size()==N*4);
    c->bid.assign(N*4,0); c->bw.assign(N*4,0.0f);
    for(size_t v=0;v<N;v++) for(int k=0;k<4;k++){
        uint32_t id = hasBI ? g.bone_ids[v*4+k] : 0; if(id>=MP_MAX_BONES) id=0;
        c->bid[v*4+k]=(uint8_t)id;
        c->bw[v*4+k] = hasBW ? g.bone_weights[v*4+k] : (k==0?1.0f:0.0f);
    }
    std::unordered_set<uint64_t> seen;
    auto addEdge=[&](uint32_t a,uint32_t b){
        if(a==b) return; if(a>b) std::swap(a,b);
        if(!seen.insert(((uint64_t)a<<32)|b).second) return;
        const float dx=g.positions[a*3]-g.positions[b*3];
        const float dy=g.positions[a*3+1]-g.positions[b*3+1];
        const float dz=g.positions[a*3+2]-g.positions[b*3+2];
        c->e0.push_back(a); c->e1.push_back(b);
        c->rest.push_back(std::sqrt(dx*dx+dy*dy+dz*dz));
    };
    for(size_t t=0;t+2<c->idx.size();t+=3){
        addEdge(c->idx[t],c->idx[t+1]); addEdge(c->idx[t+1],c->idx[t+2]); addEdge(c->idx[t+2],c->idx[t]);
    }
    float mnx=1e30f,mny=1e30f,mnz=1e30f,mxx=-1e30f,mxy=-1e30f,mxz=-1e30f;
    for(size_t v=0;v<N;v++){
        float x=g.positions[v*3],y=g.positions[v*3+1],z=g.positions[v*3+2];
        mnx=std::min(mnx,x);mny=std::min(mny,y);mnz=std::min(mnz,z);
        mxx=std::max(mxx,x);mxy=std::max(mxy,y);mxz=std::max(mxz,z);
    }
    c->scale = std::sqrt((mxx-mnx)*(mxx-mnx)+(mxy-mny)*(mxy-mny)+(mxz-mnz)*(mxz-mnz));
    if(c->scale<1e-4f) c->scale=1.0f;
    c->damping = g.cloth_damping;

    c->pinned.assign(N,0);
    size_t realPins=0;
    if(g.cloth_pin.size()==N)
        for(size_t v=0;v<N;v++) if(g.cloth_pin[v]){ c->pinned[v]=1; ++realPins; }
    if(!(realPins>0 && realPins < N*9/10)) std::fill(c->pinned.begin(),c->pinned.end(),0);

    std::vector<uint32_t> par(N); for(uint32_t v=0;v<(uint32_t)N;v++) par[v]=v;
    std::function<uint32_t(uint32_t)> find=[&](uint32_t x){ while(par[x]!=x){ par[x]=par[par[x]]; x=par[x]; } return x; };
    for(size_t e=0;e<c->e0.size();e++){ uint32_t a=find(c->e0[e]),b=find(c->e1[e]); if(a!=b) par[a]=b; }
    std::unordered_map<uint32_t,float> cMax,cMin; std::unordered_map<uint32_t,uint8_t> cPinned;
    for(uint32_t v=0;v<(uint32_t)N;v++){
        uint32_t r=find(v); float y=g.positions[v*3+1];
        auto it=cMax.find(r);
        if(it==cMax.end()){ cMax[r]=y; cMin[r]=y; } else { it->second=std::max(it->second,y); cMin[r]=std::min(cMin[r],y); }
        if(c->pinned[v]) cPinned[r]=1;
    }
    for(uint32_t v=0;v<(uint32_t)N;v++){
        uint32_t r=find(v); if(cPinned.count(r)) continue;
        float t = cMax[r] - (cMax[r]-cMin[r])*0.18f;
        if(g.positions[v*3+1]>=t) c->pinned[v]=1;
    }
    c->pos.assign(N*3,0.0f); c->prev.assign(N*3,0.0f);
    return c;
}

static void mp_step_cloth(ClothSim& c, const XMFLOAT4X4* bmats, uint32_t nbones){
    const size_t N = c.pos.size()/3;
    std::vector<float> tgt(N*3);
    for(size_t v=0; v<N; ++v){
        XMMATRIX acc = XMMatrixSet(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0);
        float wsum=0.0f;
        for(int k=0;k<4;k++){
            float w=c.bw[v*4+k]; if(w<=0.0f) continue;
            uint32_t id=c.bid[v*4+k]; if(id>=nbones) id=0;
            acc = acc + XMLoadFloat4x4(&bmats[id])*w; wsum+=w;
        }
        if(wsum<1e-4f) acc = XMMatrixIdentity();
        XMVECTOR wp = XMVector4Transform(XMVectorSet(c.bind[v*3],c.bind[v*3+1],c.bind[v*3+2],1.0f), acc);
        tgt[v*3]=XMVectorGetX(wp); tgt[v*3+1]=XMVectorGetY(wp); tgt[v*3+2]=XMVectorGetZ(wp);
    }
    if(!c.inited){ c.pos=tgt; c.prev=tgt; c.inited=true; }

    const float damp = 1.0f - std::min(std::max(c.damping,0.0f),0.4f);
    const float grav = c.scale * 0.0016f;
    const float attract = 0.03f;
    for(size_t v=0;v<N;v++){
        if(c.pinned[v]){
            for(int a=0;a<3;a++){ c.pos[v*3+a]=tgt[v*3+a]; c.prev[v*3+a]=tgt[v*3+a]; }
            continue;
        }
        for(int a=0;a<3;a++){
            float x=c.pos[v*3+a], px=c.prev[v*3+a];
            float nx = x + (x-px)*damp + (a==1 ? -grav : 0.0f);
            nx += (tgt[v*3+a]-x)*attract;
            c.prev[v*3+a]=x; c.pos[v*3+a]=nx;
        }
    }
    for(int it=0; it<10; ++it){
        for(size_t e=0;e<c.rest.size();e++){
            const uint32_t a=c.e0[e], b=c.e1[e];
            float dx=c.pos[b*3]-c.pos[a*3], dy=c.pos[b*3+1]-c.pos[a*3+1], dz=c.pos[b*3+2]-c.pos[a*3+2];
            float d=std::sqrt(dx*dx+dy*dy+dz*dz); if(d<1e-6f) continue;
            float wa=c.pinned[a]?0.0f:1.0f, wb=c.pinned[b]?0.0f:1.0f, ws=wa+wb; if(ws<1e-6f) continue;
            float diff=(d-c.rest[e])/d, sa=wa/ws, sb=wb/ws;
            c.pos[a*3]+=dx*diff*sa; c.pos[a*3+1]+=dy*diff*sa; c.pos[a*3+2]+=dz*diff*sa;
            c.pos[b*3]-=dx*diff*sb; c.pos[b*3+1]-=dy*diff*sb; c.pos[b*3+2]-=dz*diff*sb;
        }
    }
    std::vector<float> nrm(N*3,0.0f);
    for(size_t t=0;t+2<c.idx.size();t+=3){
        uint32_t ia=c.idx[t],ib=c.idx[t+1],ic=c.idx[t+2];
        float ax=c.pos[ia*3],ay=c.pos[ia*3+1],az=c.pos[ia*3+2];
        float ux=c.pos[ib*3]-ax,uy=c.pos[ib*3+1]-ay,uz=c.pos[ib*3+2]-az;
        float vx=c.pos[ic*3]-ax,vy=c.pos[ic*3+1]-ay,vz=c.pos[ic*3+2]-az;
        float nx=uy*vz-uz*vy, ny=uz*vx-ux*vz, nz=ux*vy-uy*vx;
        nrm[ia*3]+=nx;nrm[ia*3+1]+=ny;nrm[ia*3+2]+=nz;
        nrm[ib*3]+=nx;nrm[ib*3+1]+=ny;nrm[ib*3+2]+=nz;
        nrm[ic*3]+=nx;nrm[ic*3+1]+=ny;nrm[ic*3+2]+=nz;
    }
    for(size_t v=0;v<N && v<c.vtx.size();v++){
        c.vtx[v].px=c.pos[v*3]; c.vtx[v].py=c.pos[v*3+1]; c.vtx[v].pz=c.pos[v*3+2];
        float x=nrm[v*3],y=nrm[v*3+1],z=nrm[v*3+2], l=std::sqrt(x*x+y*y+z*z);
        if(l>1e-6f){ c.vtx[v].nx=x/l; c.vtx[v].ny=y/l; c.vtx[v].nz=z/l; }
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
    Skybox::ResetForBuild(mp);

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
                float r2 = 0.0f;
                for (size_t v = 0; v + 2 < g.positions.size(); v += 3) {
                    const float x = g.positions[v + 0] - m.center[0];
                    const float y = g.positions[v + 1] - m.center[1];
                    const float z = g.positions[v + 2] - m.center[2];
                    r2 = std::max(r2, x * x + y * y + z * z);
                }
                m.radius = std::sqrt(r2);
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
        if (vb_bytes == 0 || ib_bytes == 0 ||
            vb_bytes > std::numeric_limits<UINT>::max() ||
            ib_bytes > std::numeric_limits<UINT>::max()) {
            OutputLog::warn("MP_Build: skipped oversized mesh '" +
                            mesh_log_name +
                            "' vb=" + std::to_string(vb_bytes) +
                            " ib=" + std::to_string(ib_bytes));
            continue;
        }

        D3D11_BUFFER_DESC vb{}; vb.BindFlags=D3D11_BIND_VERTEX_BUFFER; vb.ByteWidth=(UINT)vb_bytes;
        if(g.cloth_sim){ vb.Usage=D3D11_USAGE_DYNAMIC; vb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE; }
        else           { vb.Usage=D3D11_USAGE_IMMUTABLE; }
        D3D11_SUBRESOURCE_DATA vsd{}; vsd.pSysMem=vtx.data();
        if(FAILED(dev->CreateBuffer(&vb,&vsd,&m.vb))) {
            OutputLog::warn("MP_Build: vertex buffer create failed for '" +
                            mesh_log_name +
                            "' bytes=" + std::to_string(vb_bytes));
            continue;
        }
        if(g.cloth_sim){ m.cloth = mp_build_cloth(g, std::move(vtx)); }
        D3D11_BUFFER_DESC ib{}; ib.BindFlags=D3D11_BIND_INDEX_BUFFER; ib.ByteWidth=(UINT)ib_bytes; ib.Usage=D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA isd{}; isd.pSysMem=g.indices.data();
        if(FAILED(dev->CreateBuffer(&ib,&isd,&m.ib))) {
            OutputLog::warn("MP_Build: index buffer create failed for '" +
                            mesh_log_name +
                            "' bytes=" + std::to_string(ib_bytes));
            m.vb->Release();
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
        m.is_cloth   = g.is_cloth;
        m.alpha_test = g.alpha_test;
        m.cloth_sim  = g.cloth_sim && (bool)m.cloth;
        std::memcpy(m.water_params, g.water_params,
                    sizeof(m.water_params));
        m.has_water_theme = g.has_water_theme;
        m.water_opacity = g.water_opacity;
        std::memcpy(m.water_shallow_colour, g.water_shallow_colour,
                    sizeof(m.water_shallow_colour));
        std::memcpy(m.water_deep_colour, g.water_deep_colour,
                    sizeof(m.water_deep_colour));
        std::memcpy(m.water_theme_params, g.water_theme_params,
                    sizeof(m.water_theme_params));

        m.source_mesh_idx = (uint32_t)i;
        m.pick_ranges.clear();
        m.pick_ranges.reserve(g.pick_ranges.size());
        for (const auto& pr : g.pick_ranges) {
            MPPerMesh::PickRange mr;
            mr.selection_id = pr.selection_id;
            mr.index_start = pr.index_start;
            mr.index_count = pr.index_count;
            mr.center[0] = pr.center[0];
            mr.center[1] = pr.center[1];
            mr.center[2] = pr.center[2];
            mr.radius = pr.radius;
            m.pick_ranges.push_back(mr);
        }
        if (!m.pick_ranges.empty()) {
            m.pick_positions = g.positions;
            m.pick_indices = g.indices;
        }

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
        m.has_alpha = m.is_water ? m.has_water_theme : hasA;
        // Water normal maps: the retail water pixel program fetches them
        // with a full mip chain and 16x anisotropic filtering (tfetch
        // mag/min/mip=3/3/3 aniso=7). The shared texture cache uploads mip 0
        // only, which minifies into per-frame static at a distance — and the
        // retail fresnel normal normalize(nx, ny, NormalScale*nz) amplifies
        // that into reflection flashing. Rebuild the water normal SRV with a
        // CPU-built mip chain (same approach as the vista pages).
        if (m.is_water && !g.diffuse_tex_name.empty()) {
            std::vector<unsigned char> wtex;
            if (build_any_tex_buffer_for_name(g.diffuse_tex_name, wtex,
                                              preferred_for_tex)) {
                std::vector<uint8_t> wrgba;
                int ww = 0, wh = 0;
                bool wha = false;
                if (decode_tex_to_rgba(wtex, wrgba, ww, wh, &wha)) {
                    ID3D11ShaderResourceView* mip_srv =
                        create_srv_from_rgba_mipped(dev, ww, wh, wrgba);
                    if (mip_srv) {
                        if (m.srv_diffuse) m.srv_diffuse->Release();
                        m.srv_diffuse = mip_srv;
                    }
                }
            }
        }
        mp.meshes.push_back(m);
    }

    if (mp.lod_count > 1) {
        mp.selected_lod = 0;
    }
    mp.selected_pick_id = 0;

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
    S.bone_anim_rot_absolute.clear();
    S.bone_anim_rot_present.clear();
    S.bone_anim_trans_delta.clear();
    S.bone_anim_trans_present.clear();
    S.bone_anim_pose_active = false;
    S.selected_bone     = -1;
    S.bone_rotate_mode  = false;

    mp.has_model = !mp.meshes.empty();
    return true;
}
void MP_Render(ID3D11Device* dev, ModelPreview& mp, const FlyCam& cam){
    if(!mp.has_model) return;
    ID3D11DeviceContext* ctx=nullptr; dev->GetImmediateContext(&ctx); if(!ctx) return;
    D3D11_VIEWPORT vp{}; vp.TopLeftX=0; vp.TopLeftY=0; vp.Width=(FLOAT)mp.width; vp.Height=(FLOAT)mp.height; vp.MinDepth=0; vp.MaxDepth=1;
    ctx->RSSetViewports(1,&vp);
    const Skybox::CameraFrame sky_camera =
        Skybox::BuildCameraFrame(cam, mp.width, mp.height);
    const float* forward = sky_camera.view_forward;
    const XMFLOAT3 sky_right_f{
        sky_camera.right[0], sky_camera.right[1], sky_camera.right[2]};
    const XMFLOAT3 sky_up_f{
        sky_camera.up[0], sky_camera.up[1], sky_camera.up[2]};
    const float fov = sky_camera.fov_radians;
    const float aspect = sky_camera.aspect;

    const Skybox::FrameState sky_frame = Skybox::EvaluateFrame(mp);
    const float sky_time = static_cast<float>(sky_frame.elapsed_time);
    const auto& render_sky_bottom = sky_frame.sky_bottom;
    const auto& render_weather = sky_frame.weather;
    const float render_mist = sky_frame.mist;
    const auto& render_fog_range = sky_frame.fog_range;
    const auto& render_fog_density = sky_frame.fog_density;
    const bool render_has_fog = sky_frame.has_fog;
    const XMFLOAT3 sun_dir_f{
        sky_frame.sun_direction[0],
        sky_frame.sun_direction[1],
        sky_frame.sun_direction[2]};

    float clear[4]{};
    Skybox::GetClearColour(mp, sky_frame, clear);
    ctx->OMSetRenderTargets(1,&mp.rtv, mp.dsv);
    ctx->ClearRenderTargetView(mp.rtv, clear);
    ctx->ClearDepthStencilView(mp.dsv, D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL, 1.0f, 0);

    Skybox::DrawSky(dev, ctx, mp, sky_camera, sky_frame);
    ctx->IASetInputLayout(mp.layout);
    ctx->VSSetShader(mp.vs,nullptr,0);
    ctx->PSSetShader(mp.ps,nullptr,0);
    ID3D11SamplerState* samplers[2] = { mp.sampler, mp.sampler_point };
    ctx->PSSetSamplers(0, 2, samplers);

    ctx->RSSetState((mp.wireframe && mp.rs_wire) ? mp.rs_wire : mp.rs);
    XMVECTOR eye = XMVectorSet(cam.pos[0], cam.pos[1], cam.pos[2], 1);
    XMVECTOR at = XMVectorSet(cam.pos[0] + forward[0], cam.pos[1] + forward[1], cam.pos[2] + forward[2], 1);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    XMMATRIX V = XMMatrixLookAtLH(eye, at, up);
    const float far_plane = mp.radius * 100.0f;
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

    Skybox::DrawClouds(
        ctx, mp, cam, sky_camera, sky_frame, V, P);

    XMMATRIX MVP = XMMatrixTranspose(W * V * P);
    XMMATRIX MV  = XMMatrixTranspose(W * V);
    XMVECTOR lightDirV = XMVector3Normalize(
        XMVectorSet(sun_dir_f.x,
                    std::max(sun_dir_f.y, 0.15f),
                    sun_dir_f.z,
                    0.0f));
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

    if (mp.cbuffer_fog) {
        const bool fog_enabled =
            mp.show_mist && mp.no_tilt &&
            (render_has_fog || render_mist > 0.0001f);
        float mist_base = mp.center[1];
        bool have_terrain = false;
        for (const auto& m : mp.meshes) {
            if (!m.is_terrain) continue;
            if (!have_terrain || m.center[1] < mist_base) {
                mist_base = m.center[1];
            }
            have_terrain = true;
        }
        struct FogCBData {
            XMFLOAT4 colour;
            XMFLOAT4 dist;
            XMFLOAT4 density;
            XMFLOAT4 misc;
        } fog_cb{};
        auto fin = [](float v, float fb) {
            return std::isfinite(v) ? v : fb;
        };
        // Engine fog curve (XEX sub_821FDF10): optical depth
        // OD(d) = od_far * pow((d - start)/span2, e), fog = 1 - exp(-OD*k).
        // span1/span2/densities and the exponent e are engine-exact; k is
        // calibrated so fog(FarDistance) == FarDensity.
        const float fog_start = std::max(fin(render_fog_range[0], 0.0f),
                                         0.0f);
        const float near_dist = fin(render_fog_range[1], 60.0f);
        const float far_dist = fin(render_fog_range[2], 400.0f);
        const float span1 = std::max(near_dist - fog_start, 1.0f);
        const float span2 = std::max(far_dist - fog_start, span1 + 9.0f);
        const float d1 =
            std::max(std::clamp(fin(render_fog_density[0], 0.0f),
                                0.0f, 1.0f), 0.01f);
        const float d2 =
            std::max(std::clamp(fin(render_fog_density[1], 0.0f),
                                0.0f, 1.0f), 0.01f);
        const float od_far = d2 * span2 + (d1 - d2) * span1;
        float fog_e = 1.0f;
        if (od_far > 0.0001f) {
            const float num = std::log10(d1 * span1 / od_far);
            const float den = std::log10(span1 / span2);
            if (std::isfinite(num) && std::isfinite(den) &&
                std::fabs(den) > 0.0001f) {
                fog_e = std::clamp(num / den, 0.05f, 8.0f);
            }
        }
        const float fog_k =
            -std::log(std::max(1.0f - d2, 0.02f)) /
            std::max(od_far, 0.0001f);
        const float fog_amp =
            (render_has_fog ? od_far * fog_k : 0.0f);
        fog_cb.colour = XMFLOAT4(
            std::clamp(fin(render_sky_bottom[0], 0.48f), 0.0f, 1.0f),
            std::clamp(fin(render_sky_bottom[1], 0.55f), 0.0f, 1.0f),
            std::clamp(fin(render_sky_bottom[2], 0.62f), 0.0f, 1.0f),
            fog_enabled ? 1.0f : 0.0f);
        fog_cb.dist = XMFLOAT4(
            fog_start,
            1.0f / span2,
            fog_e,
            fog_amp);
        fog_cb.density = XMFLOAT4(
            0.0f,
            0.0f,
            std::clamp(fin(render_mist, 0.0f), 0.0f, 1.0f) * 0.75f,
            25.0f);
        fog_cb.misc = XMFLOAT4(
            mist_base + 4.0f,
            4.0f,
            sky_time,
            0.0f);
        D3D11_MAPPED_SUBRESOURCE fms{};
        if (SUCCEEDED(ctx->Map(mp.cbuffer_fog, 0,
                               D3D11_MAP_WRITE_DISCARD, 0, &fms))) {
            std::memcpy(fms.pData, &fog_cb, sizeof(fog_cb));
            ctx->Unmap(mp.cbuffer_fog, 0);
        }
        ctx->PSSetConstantBuffers(5, 1, &mp.cbuffer_fog);
    }

    std::vector<XMFLOAT4X4> bone_mats(MP_MAX_BONES);
    for (uint32_t i = 0; i < MP_MAX_BONES; ++i){
        XMStoreFloat4x4(&bone_mats[i], XMMatrixIdentity());
    }
    if (mp.bone_count > 0) {
        const uint32_t n = mp.bone_count;

        std::vector<XMFLOAT4X4> local(n);
        bool have_deltas = (S.bone_rot_deltas.size() >= (size_t)n * 4);
        const bool have_anim_pose =
            S.bone_anim_pose_active &&
            S.bone_anim_rot_absolute.size() >= (size_t)n * 4 &&
            S.bone_anim_rot_present.size() >= (size_t)n;
        const bool have_anim_trans =
            S.bone_anim_trans_delta.size() >= (size_t)n * 3 &&
            S.bone_anim_trans_present.size() >= (size_t)n;
        for (uint32_t i = 0; i < n; ++i){
            const float* tf = &mp.local_rest[(size_t)i * 11];
            const float* dq = have_deltas ? &S.bone_rot_deltas[(size_t)i * 4] : nullptr;
            const float* aq =
                (have_anim_pose && S.bone_anim_rot_present[(size_t)i])
                    ? &S.bone_anim_rot_absolute[(size_t)i * 4]
                    : nullptr;
            const float* at =
                (have_anim_pose && have_anim_trans &&
                 S.bone_anim_trans_present[(size_t)i])
                    ? &S.bone_anim_trans_delta[(size_t)i * 3]
                    : nullptr;
            XMMATRIX L = have_anim_pose
                ? bone_local_matrix_anim_delta(tf, aq, at)
                : bone_local_matrix(tf, dq);
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

    for (auto& m : mp.meshes){
        if (!m.cloth_sim || !m.cloth || !m.vb) continue;
        mp_step_cloth(*m.cloth, bone_mats.data(), MP_MAX_BONES);
        D3D11_MAPPED_SUBRESOURCE cms{};
        if (SUCCEEDED(ctx->Map(m.vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &cms))){
            std::memcpy(cms.pData, m.cloth->vtx.data(),
                        m.cloth->vtx.size() * sizeof(MPVertex));
            ctx->Unmap(m.vb, 0);
        }
    }

    bool any_isolated = false;
    for (const auto& mm : mp.meshes) {
        if (mp_should_hide_mesh(mm)) continue;
        if (mm.isolated) { any_isolated = true; break; }
    }

    const float water_time = (float)ImGui::GetTime();
    auto upload_per_mesh_cb = [&](bool highlight, bool is_cloth, bool alpha_test,
                                  bool no_skin = false){

        cb.params = XMFLOAT4(alpha_test ? 1.0f : 0.0f, no_skin ? 1.0f : 0.0f,
                             is_cloth ? 2.0f : (highlight ? 1.0f : 0.0f),
                             water_time);
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
        const bool use_direct_terrain =
            !R.material_weight_array && mp.ps_terrain_direct;
        const bool use_terrain_shader =
            m.is_terrain && R.ok &&
            R.lod_diffuse_array && R.lod_detail_array &&
            R.chunk_idx_array && R.chunk_blend_array && R.chunk_uv_array &&
            R.splat_mask && R.lightmap &&
            (use_direct_terrain || R.material_weight_array) &&
            mp.vs_terrain && mp.ps_terrain && mp.cbuffer_terrain;

        if (use_terrain_shader) {
            static uint32_t s_logged_splat_generation = 0;
            if (s_logged_splat_generation != R.generation) {
                s_logged_splat_generation = R.generation;
                OutputLog::success(std::string(use_direct_terrain
                    ? "terrain direct layer fallback draw active: "
                    : "terrain SPLAT shader draw active: ")
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
                    float weight_size[4];
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
                    ? 32.0f / float(R.splat_w) : 0.0f;
                t.splat_params[3] = (R.splat_h > 0)
                    ? 32.0f / float(R.splat_h) : 0.0f;
                t.mesh_xform[0] = 1.0f;
                t.mesh_xform[1] = 1.0f;
                t.mesh_xform[2] = R.mesh_to_world_x;
                t.mesh_xform[3] = R.mesh_to_world_z;
                t.weight_size[0] = (float)R.weight_w;
                t.weight_size[1] = (float)R.weight_h;
                t.weight_size[2] = (R.weight_w > 0)
                    ? 1.0f / (float)R.weight_w : 1.0f;
                t.weight_size[3] = (R.weight_h > 0)
                    ? 1.0f / (float)R.weight_h : 1.0f;
                for (int mi = 0; mi < 32; ++mi) {
                    for (int mj = 0; mj < 4; ++mj) {
                        t.material_params[mi][mj] =
                            R.material_params[mi][mj];
                    }
                }
                std::memcpy(tms.pData, &t, sizeof(t));
                ctx->Unmap(mp.cbuffer_terrain, 0);
            }

            const bool use_landscape_blend =
                !use_direct_terrain && S.dev_mode &&
                S.terrain_landscape_blend && mp.ps_terrain_landscape;
            ctx->VSSetShader(mp.vs_terrain, nullptr, 0);
            ctx->PSSetShader(use_direct_terrain ? mp.ps_terrain_direct
                : (use_landscape_blend ? mp.ps_terrain_landscape
                                       : mp.ps_terrain), nullptr, 0);
            ctx->VSSetConstantBuffers(2, 1, &mp.cbuffer_terrain);
            ctx->PSSetConstantBuffers(2, 1, &mp.cbuffer_terrain);

            if (use_direct_terrain) {
                ID3D11ShaderResourceView* srvs[6] = {
                    R.lod_diffuse_array,
                    R.chunk_idx_array,
                    R.chunk_blend_array,
                    R.chunk_uv_array,
                    R.splat_mask,
                    R.lightmap
                };
                ctx->PSSetShaderResources(0, 6, srvs);
            } else {
                ID3D11ShaderResourceView* srvs[8] = {
                    R.lod_diffuse_array,
                    R.chunk_idx_array,
                    R.chunk_blend_array,
                    R.chunk_uv_array,
                    R.splat_mask,
                    R.lightmap,
                    R.lod_detail_array,
                    R.material_weight_array
                };
                ctx->PSSetShaderResources(0, 8, srvs);
            }

            ctx->DrawIndexed(m.index_count, 0, 0);

            ID3D11ShaderResourceView* nulls[8] = {
                nullptr, nullptr, nullptr, nullptr,
                nullptr, nullptr, nullptr, nullptr
            };
            ctx->PSSetShaderResources(0, 8, nulls);
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
                    float w_bias[4];
                    float w_ph01[4];
                    float w_sc01[4];
                    float w_surface[4];
                    float w_deep[4];
                    float w_reflp[4];
                    float w_glit[4];
                    float w_eye[4];
                    float w_sun[4];
                    float w_light[4];
                } w{};

                auto p = [&](int idx, float fallback) {
                    const float v = (idx >= 0 && idx + 1 < 38)
                        ? m.water_params[idx + 1] : fallback;
                    return std::isfinite(v) ? v : fallback;
                };
                auto phase = [&](float speed) {
                    const double ph = double(water_time) * double(speed);
                    return float(ph - std::floor(ph));
                };

                // indices per the XEX g_WaterConstants packer sub_82B2A008
                w.w_bias[0] = p(0, 0.2f);        // m_FresnelBias
                w.w_bias[1] = p(1, 0.0f);        // m_ReflectionBias
                w.w_bias[2] = p(24, 0.05f);      // m_NormalScale
                w.w_bias[3] = std::clamp(m.has_water_theme
                    ? m.water_opacity : 0.78f, 0.05f, 1.0f);

                w.w_ph01[0] = phase(p(2,  0.052f));
                w.w_ph01[1] = phase(p(3,  0.011f));
                w.w_ph01[2] = phase(p(4, -0.019f));
                w.w_ph01[3] = phase(p(5,  0.019f));
                w.w_sc01[0] = p(6, 0.188f);
                w.w_sc01[1] = p(7, 0.188f);
                w.w_sc01[2] = p(8, 0.220f);
                w.w_sc01[3] = p(9, 0.220f);

                w.w_surface[0] = m.water_shallow_colour[0];
                w.w_surface[1] = m.water_shallow_colour[1];
                w.w_surface[2] = m.water_shallow_colour[2];
                w.w_surface[3] = p(30, 0.1f);    // m_DiffuseAbsorption
                w.w_deep[0] = m.water_deep_colour[0];
                w.w_deep[1] = m.water_deep_colour[1];
                w.w_deep[2] = m.water_deep_colour[2];
                w.w_deep[3] = p(29, 0.75f);      // m_ReflectionStrength

                w.w_reflp[0] = p(25, 2.0f);      // m_ReflectionScale.x
                w.w_reflp[1] = p(27, 2.0f);      // m_RefractionScale.x
                w.w_reflp[2] = p(34, 0.3f);      // m_GlitteringNormalBendFactor
                w.w_reflp[3] = p(35, 5.0f);      // m_GlitteringStrength

                const bool sky_ok = mp.has_sky_theme && mp.show_sky &&
                                    mp.cbuffer_sky;
                // Exact dome reflection needs the sky pass's constant block
                // and in-scatter LUT (refreshed by DrawSky each frame).
                const bool dome_ok = sky_ok && mp.cbuffer_sky_dome &&
                                     mp.sky_lut_srv && mp.sampler_sky_clamp;
                w.w_glit[0] = p(36, 128.0f);     // m_GlitteringPower
                w.w_glit[1] = 0.0f;              // highlight flag
                w.w_glit[2] = sky_ok ? 1.0f : 0.0f;
                w.w_glit[3] = dome_ok ? 1.0f : 0.0f;

                w.w_eye[0] = cam.pos[0];
                w.w_eye[1] = cam.pos[1];
                w.w_eye[2] = cam.pos[2];
                w.w_eye[3] = water_time;

                // c143 light colour: the theme's evaluated MainLightColour
                // (time-of-day keyframed); the old sun ramp remains as the
                // no-theme fallback.
                if (mp.has_sky_theme) {
                    w.w_sun[0] = sky_frame.main_light_colour[0];
                    w.w_sun[1] = sky_frame.main_light_colour[1];
                    w.w_sun[2] = sky_frame.main_light_colour[2];
                } else {
                    const float sun_up =
                        std::clamp(sun_dir_f.y * 3.0f, 0.0f, 1.0f);
                    w.w_sun[0] = 1.0f;
                    w.w_sun[1] = 0.55f + 0.42f * sun_up;
                    w.w_sun[2] = 0.30f + 0.60f * sun_up;
                }
                w.w_sun[3] = sun_dir_f.y;

                // c142 light direction (direction the light travels): sun by
                // day; when the sun is below the horizon the engine's main
                // light is the moon. The mesh path's lightDir clamps the sun
                // above the horizon, which is wrong for glints.
                if (sun_dir_f.y < 0.0f && mp.has_moon_axis) {
                    w.w_light[0] = -sky_frame.moon_direction[0];
                    w.w_light[1] = -sky_frame.moon_direction[1];
                    w.w_light[2] = -sky_frame.moon_direction[2];
                } else {
                    w.w_light[0] = sun_dir_f.x;
                    w.w_light[1] = sun_dir_f.y;
                    w.w_light[2] = sun_dir_f.z;
                }
                w.w_light[3] = 0.0f;

                std::memcpy(wms.pData, &w, sizeof(w));
                ctx->Unmap(mp.cbuffer_water, 0);

                // Water flashing diagnosis: log every global input the water
                // shader sees. If a value alternates frame-to-frame the
                // culprit is visible immediately.
                UINT tex_mips = 0, tex_w = 0;
                if (m.srv_diffuse) {
                    ID3D11Resource* res = nullptr;
                    m.srv_diffuse->GetResource(&res);
                    ID3D11Texture2D* t2 = nullptr;
                    if (res && SUCCEEDED(res->QueryInterface(
                            __uuidof(ID3D11Texture2D), (void**)&t2))) {
                        D3D11_TEXTURE2D_DESC tdd{};
                        t2->GetDesc(&tdd);
                        tex_mips = tdd.MipLevels;
                        tex_w = tdd.Width;
                        t2->Release();
                    }
                    if (res) res->Release();
                }
                water_debug_line(
                    "W t=%.4f tex='%s' mips=%u w=%u "
                    "eye=(%.2f,%.2f,%.2f) sun=(%.5f,%.5f,%.5f) "
                    "L=(%.5f,%.5f,%.5f) suncol=(%.3f,%.3f,%.3f) "
                    "ph=(%.5f,%.5f,%.5f,%.5f) sc=(%.4f,%.4f,%.4f,%.4f) "
                    "bias=(%.3f,%.3f) ns=%.4f "
                    "reflstr=%.3f glit(bend=%.3f str=%.3f pow=%.1f)\n",
                    water_time, m.diffuse_tex_name.c_str(),
                    tex_mips, tex_w,
                    cam.pos[0], cam.pos[1], cam.pos[2],
                    sun_dir_f.x, sun_dir_f.y, sun_dir_f.z,
                    w.w_light[0], w.w_light[1], w.w_light[2],
                    w.w_sun[0], w.w_sun[1], w.w_sun[2],
                    w.w_ph01[0], w.w_ph01[1], w.w_ph01[2], w.w_ph01[3],
                    w.w_sc01[0], w.w_sc01[1], w.w_sc01[2], w.w_sc01[3],
                    w.w_bias[0], w.w_bias[1], w.w_bias[2],
                    w.w_deep[3], w.w_reflp[2], w.w_reflp[3],
                    w.w_glit[0]);
            }

            ctx->VSSetShader(mp.vs_water, nullptr, 0);
            ctx->PSSetShader(mp.ps_water, nullptr, 0);
            ctx->VSSetConstantBuffers(3, 1, &mp.cbuffer_water);
            ctx->PSSetConstantBuffers(3, 1, &mp.cbuffer_water);
            if (mp.cbuffer_sky) {
                ctx->PSSetConstantBuffers(4, 1, &mp.cbuffer_sky);
            }
            // Exact dome reflection inputs (see dome_reflect in the PS):
            // the sky pass's constant block, in-scatter LUT and clamp
            // sampler, refreshed by DrawSky earlier this frame.
            if (mp.cbuffer_sky_dome) {
                ctx->PSSetConstantBuffers(6, 1, &mp.cbuffer_sky_dome);
            }
            if (mp.sky_lut_srv) {
                ctx->PSSetShaderResources(4, 1, &mp.sky_lut_srv);
            }
            if (mp.sampler_sky_clamp) {
                ctx->PSSetSamplers(2, 1, &mp.sampler_sky_clamp);
            }

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

    auto draw_regular_range = [&](const MPPerMesh& m,
                                  uint32_t index_start,
                                  uint32_t index_count,
                                  ID3D11BlendState* bs) {
        if (index_count == 0 || index_start >= m.index_count) return;
        if (index_start + index_count > m.index_count) {
            index_count = m.index_count - index_start;
        }
        UINT stride=sizeof(MPVertex), offset=0;
        ctx->IASetInputLayout(mp.layout);
        ctx->VSSetShader(mp.vs, nullptr, 0);
        ctx->PSSetShader(mp.ps, nullptr, 0);
        ctx->IASetVertexBuffers(0,1,&m.vb,&stride,&offset);
        ctx->IASetIndexBuffer(m.ib, DXGI_FORMAT_R32_UINT, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->OMSetBlendState(bs, blend_factor, 0xFFFFFFFF);

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
        ID3D11ShaderResourceView* srvs[5] = {
            diffuse_to_use, normal_to_use, specular_to_use,
            metallic_to_use, extra_to_use
        };
        ctx->PSSetShaderResources(0, 5, srvs);
        ctx->DrawIndexed(index_count, index_start, 0);
        ID3D11ShaderResourceView* nullsrvs[5] = {
            nullptr, nullptr, nullptr, nullptr, nullptr
        };
        ctx->PSSetShaderResources(0, 5, nullsrvs);
    };

    ctx->OMSetDepthStencilState(mp.dssWrite, 0);
    for(const auto& m : mp.meshes){
        if (mp_should_hide_mesh(m)) continue;
        if(!m.vb || !m.ib || m.index_count==0 || (m.has_alpha && m.alpha_test)) continue;
        if (m.is_water) continue; // drawn after everything it refracts
        if (any_isolated && !m.isolated) continue;
        if (mp.selected_lod >= 0 &&
            m.lod_index != (uint32_t)mp.selected_lod) continue;
        upload_per_mesh_cb(m.highlight, m.is_cloth, m.alpha_test, m.cloth_sim);
        draw_one(m, mp.bs);
    }
    ctx->OMSetDepthStencilState(mp.dssNoWrite, 0);
    for(const auto& m : mp.meshes){
        if (mp_should_hide_mesh(m)) continue;
        if(!m.vb || !m.ib || m.index_count==0 || !m.has_alpha || !m.alpha_test) continue;
        if (any_isolated && !m.isolated) continue;
        if (mp.selected_lod >= 0 &&
            m.lod_index != (uint32_t)mp.selected_lod) continue;
        upload_per_mesh_cb(m.highlight, m.is_cloth, m.alpha_test, m.cloth_sim);
        draw_one(m, mp.bsAlpha);
    }
    // Water last: the ONE/SRC_ALPHA composite reads the scene behind the
    // surface out of the framebuffer as the retail refraction tile, so
    // everything it refracts must already be drawn. Depth writes stay on
    // (retail water z-writes); the stencil gate keeps overlapping coplanar
    // water copies (one .water per heightfield) from re-compositing.
    ctx->OMSetDepthStencilState(
        mp.dssWaterOnce ? mp.dssWaterOnce : mp.dssWrite, 0);
    for(const auto& m : mp.meshes){
        if (mp_should_hide_mesh(m)) continue;
        if(!m.vb || !m.ib || m.index_count==0 || !m.is_water) continue;
        if (any_isolated && !m.isolated) continue;
        if (mp.selected_lod >= 0 &&
            m.lod_index != (uint32_t)mp.selected_lod) continue;
        upload_per_mesh_cb(m.highlight, m.is_cloth, m.alpha_test, m.cloth_sim);
        const bool water_ok =
            mp.vs_water && mp.ps_water && mp.cbuffer_water && mp.bs_water;
        draw_one(m, water_ok ? mp.bs_water : mp.bs);
    }
    ctx->OMSetDepthStencilState(mp.dssNoWrite, 0);

    if (mp.show_weather && mp.no_tilt && mp.has_weather_theme &&
        mp.vs_weather && mp.ps_weather && mp.cbuffer_weather) {
        const float rain_density = std::clamp(
            render_weather[0] * std::max(mp.rain_intensity_mult, 0.0f),
            0.0f, 1.0f);
        const float rain_size = std::clamp(render_weather[1], 0.0f, 4.0f);
        const float snow_speed = std::clamp(render_weather[2], 0.0f, 6.0f);
        const float snow_size = std::clamp(render_weather[3], 0.0f, 4.0f);
        const bool has_snow = snow_speed > 0.001f && snow_size > 0.001f;
        int rain_count = (int)(rain_density * 15000.0f);
        rain_count = std::clamp(rain_count, 0, 20000);
        if (rain_size <= 0.001f) rain_count = 0;
        // Retail draws 4096 snow point sprites (WeatherPrim_RenderSnowFlakes
        // @ 0x821BE068, Draw_PointList(0, 4096)).
        int snow_count = has_snow
            ? (int)(4096.0f * std::max(mp.snow_intensity_mult, 0.0f))
            : 0;
        snow_count = std::clamp(snow_count, 0, 8000);

        if (rain_count > 0 || snow_count > 0) {
            struct WeatherCBData {
                XMFLOAT4X4 wvp;
                XMFLOAT4 cam_time;
                XMFLOAT4 wind_vec;
                XMFLOAT4 rain_p;
                XMFLOAT4 snow_p;
                XMFLOAT4 area;
                XMFLOAT4 cam_right;
                XMFLOAT4 cam_up;
            } wx{};
            XMMATRIX VP = XMMatrixTranspose(V * P);
            XMStoreFloat4x4(&wx.wvp, VP);
            wx.cam_time = XMFLOAT4(cam.pos[0], cam.pos[1], cam.pos[2],
                                   sky_time);
            const float wind_az = mp.weather_wind[0];
            const float wind_str = std::clamp(mp.weather_wind[1],
                                              0.0f, 40.0f);
            wx.wind_vec = XMFLOAT4(std::cos(wind_az) * wind_str,
                                   0.0f,
                                   std::sin(wind_az) * wind_str,
                                   0.0f);
            wx.rain_p = XMFLOAT4((float)rain_count,
                                 std::max(rain_size, 0.15f),
                                 14.0f,
                                 0.34f);
            wx.snow_p = XMFLOAT4((float)snow_count,
                                 std::max(snow_size, 0.15f),
                                 std::max(snow_speed, 0.15f),
                                 1.0f);
            wx.area = XMFLOAT4(30.0f, 24.0f, 22.0f, 18.0f);
            wx.cam_right = XMFLOAT4(sky_right_f.x, sky_right_f.y,
                                    sky_right_f.z,
                                    sky_camera.tan_half_x);
            wx.cam_up = XMFLOAT4(sky_up_f.x, sky_up_f.y,
                                 sky_up_f.z, sky_camera.tan_half_y);
            D3D11_MAPPED_SUBRESOURCE xms{};
            if (SUCCEEDED(ctx->Map(mp.cbuffer_weather, 0,
                                   D3D11_MAP_WRITE_DISCARD, 0, &xms))) {
                std::memcpy(xms.pData, &wx, sizeof(wx));
                ctx->Unmap(mp.cbuffer_weather, 0);
            }

            water_debug_line(
                "X t=%.4f rain=%d snow=%d rain_size=%.3f snow_size=%.3f "
                "wind=(%.2f,%.2f)\n",
                sky_time, (int)rain_count, (int)snow_count,
                rain_size, snow_size,
                mp.weather_wind[0], mp.weather_wind[1]);

            ctx->IASetInputLayout(nullptr);
            ctx->IASetPrimitiveTopology(
                D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ctx->RSSetState(mp.rs);
            ctx->OMSetDepthStencilState(mp.dssNoWrite, 0);
            ctx->OMSetBlendState(mp.bsAlpha, blend_factor, 0xFFFFFFFF);
            ctx->VSSetShader(mp.vs_weather, nullptr, 0);
            ctx->PSSetShader(mp.ps_weather, nullptr, 0);
            ctx->VSSetConstantBuffers(6, 1, &mp.cbuffer_weather);
            ctx->PSSetConstantBuffers(6, 1, &mp.cbuffer_weather);
            ctx->Draw((UINT)(rain_count + snow_count) * 6, 0);
            ID3D11Buffer* null_cb = nullptr;
            ctx->VSSetConstantBuffers(6, 1, &null_cb);
            ctx->PSSetConstantBuffers(6, 1, &null_cb);
            ctx->IASetInputLayout(mp.layout);
            ctx->VSSetShader(mp.vs, nullptr, 0);
            ctx->PSSetShader(mp.ps, nullptr, 0);
            ctx->RSSetState((mp.wireframe && mp.rs_wire) ? mp.rs_wire
                                                         : mp.rs);
        }
    }

    // ---- particle FX (chimney smoke, waterfalls, ...) ----
    if (mp.fx_show && mp.vs_fx && mp.ps_fx && mp.layout_fx && mp.cbuffer_fx &&
        !mp.fx_system.empty()) {
        double now = ImGui::GetTime();
        float dt = (float)(now - mp.fx_last_time);
        mp.fx_last_time = now;
        if (dt < 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;
        mp.fx_system.update(dt);

        const float cr[3] = { sky_right_f.x, sky_right_f.y, sky_right_f.z };
        const float cu[3] = { sky_up_f.x, sky_up_f.y, sky_up_f.z };
        const float ce[3] = { cam.pos[0], cam.pos[1], cam.pos[2] };
        std::vector<Fx::FxBatch> batches;
        mp.fx_system.build_batches(cr, cu, ce, batches);

        XMMATRIX VP = XMMatrixTranspose(V * P);
        D3D11_MAPPED_SUBRESOURCE fms{};
        if (SUCCEEDED(ctx->Map(mp.cbuffer_fx, 0,
                               D3D11_MAP_WRITE_DISCARD, 0, &fms))) {
            XMFLOAT4X4 m; XMStoreFloat4x4(&m, VP);
            std::memcpy(fms.pData, &m, sizeof(m));
            ctx->Unmap(mp.cbuffer_fx, 0);
        }
        ctx->IASetInputLayout(mp.layout_fx);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->RSSetState(mp.rs);
        ctx->OMSetDepthStencilState(mp.dssNoWrite, 0);
        ctx->VSSetShader(mp.vs_fx, nullptr, 0);
        ctx->PSSetShader(mp.ps_fx, nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, &mp.cbuffer_fx);
        ID3D11SamplerState* fx_smp = mp.sampler;
        ctx->PSSetSamplers(0, 1, &fx_smp);
        const UINT fstride = sizeof(Fx::FxVertex), foff = 0;
        for (auto& b : batches) {
            if (b.verts.empty()) continue;
            if (mp.fx_vb_capacity < b.verts.size()) {
                if (mp.fx_vb) { mp.fx_vb->Release(); mp.fx_vb = nullptr; }
                size_t cap = b.verts.size() + b.verts.size() / 2 + 2048;
                D3D11_BUFFER_DESC vd{};
                vd.ByteWidth = (UINT)(cap * sizeof(Fx::FxVertex));
                vd.Usage = D3D11_USAGE_DYNAMIC;
                vd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                vd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                if (FAILED(dev->CreateBuffer(&vd, nullptr, &mp.fx_vb))) {
                    mp.fx_vb_capacity = 0; continue;
                }
                mp.fx_vb_capacity = cap;
            }
            D3D11_MAPPED_SUBRESOURCE vms{};
            if (FAILED(ctx->Map(mp.fx_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &vms)))
                continue;
            std::memcpy(vms.pData, b.verts.data(),
                        b.verts.size() * sizeof(Fx::FxVertex));
            ctx->Unmap(mp.fx_vb, 0);
            ctx->IASetVertexBuffers(0, 1, &mp.fx_vb, &fstride, &foff);
            auto it = mp.fx_tex_srv.find(b.texture);
            ID3D11ShaderResourceView* srv =
                (it != mp.fx_tex_srv.end() && it->second) ? it->second
                                                          : mp.default_srv;
            ctx->PSSetShaderResources(0, 1, &srv);
            // Retail fixed-function factors from the material's
            // Src/DestBlendMode (batch carries D3D11_BLEND values; default
            // SrcAlpha/InvSrcAlpha). States are created on demand and cached.
            ID3D11BlendState* fx_bs = nullptr;
            {
                const int key = b.src_blend * 100 + b.dst_blend;
                auto bit = mp.fx_blend_states.find(key);
                if (bit != mp.fx_blend_states.end()) {
                    fx_bs = bit->second;
                } else {
                    D3D11_BLEND_DESC bdx{};
                    bdx.RenderTarget[0].BlendEnable = TRUE;
                    bdx.RenderTarget[0].SrcBlend =
                        (D3D11_BLEND)std::clamp(b.src_blend, 1, 19);
                    bdx.RenderTarget[0].DestBlend =
                        (D3D11_BLEND)std::clamp(b.dst_blend, 1, 19);
                    bdx.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
                    bdx.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
                    bdx.RenderTarget[0].DestBlendAlpha =
                        D3D11_BLEND_INV_SRC_ALPHA;
                    bdx.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
                    bdx.RenderTarget[0].RenderTargetWriteMask =
                        D3D11_COLOR_WRITE_ENABLE_ALL;
                    dev->CreateBlendState(&bdx, &fx_bs);
                    mp.fx_blend_states.emplace(key, fx_bs);
                }
            }
            ctx->OMSetBlendState(fx_bs ? fx_bs : mp.bs_fx_alpha,
                                 blend_factor, 0xFFFFFFFF);
            ctx->Draw((UINT)b.verts.size(), 0);
        }
        ID3D11Buffer* null_cb = nullptr;
        ctx->VSSetConstantBuffers(0, 1, &null_cb);
        ctx->IASetInputLayout(mp.layout);
        ctx->VSSetShader(mp.vs, nullptr, 0);
        ctx->PSSetShader(mp.ps, nullptr, 0);
        ctx->RSSetState((mp.wireframe && mp.rs_wire) ? mp.rs_wire : mp.rs);
    }

    if (mp.selected_pick_id != 0 && mp.dssNoWriteLEqual) {
        // The FX pass above nulls VS constant-buffer slot 0 on exit; rebind
        // the scene cbuffer or the highlight verts transform through garbage.
        ctx->VSSetConstantBuffers(0, 1, &mp.cbuffer);
        ctx->PSSetConstantBuffers(0, 1, &mp.cbuffer);
        ctx->OMSetDepthStencilState(mp.dssNoWriteLEqual, 0);
        upload_per_mesh_cb(true, false, false);
        for (const auto& m : mp.meshes) {
            if (mp_should_hide_mesh(m)) continue;
            if (!m.vb || !m.ib || m.index_count == 0) continue;
            if (m.is_terrain || m.is_water) continue;
            if (any_isolated && !m.isolated) continue;
            if (mp.selected_lod >= 0 &&
                m.lod_index != (uint32_t)mp.selected_lod) continue;
            ID3D11BlendState* bs = m.has_alpha ? mp.bsAlpha : mp.bs;
            for (const auto& pr : m.pick_ranges) {
                if (pr.selection_id != mp.selected_pick_id) continue;
                draw_regular_range(m, pr.index_start, pr.index_count, bs);
            }
        }
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
    const bool have_anim_pose =
        S.bone_anim_pose_active &&
        S.bone_anim_rot_absolute.size() >= (size_t)n * 4 &&
        S.bone_anim_rot_present.size() >= (size_t)n;
    const bool have_anim_trans =
        S.bone_anim_trans_delta.size() >= (size_t)n * 3 &&
        S.bone_anim_trans_present.size() >= (size_t)n;

    std::vector<XMFLOAT4X4> local(n);
    for (uint32_t i = 0; i < n; ++i){
        const float* tf = &mp.local_rest[(size_t)i * 11];
        const float* dq = have_deltas ? &deltas[(size_t)i * 4] : nullptr;
        const float* aq =
            (have_anim_pose && S.bone_anim_rot_present[(size_t)i])
                ? &S.bone_anim_rot_absolute[(size_t)i * 4]
                : nullptr;
        const float* at =
            (have_anim_pose && have_anim_trans &&
             S.bone_anim_trans_present[(size_t)i])
                ? &S.bone_anim_trans_delta[(size_t)i * 3]
                : nullptr;
        XMMATRIX L = have_anim_pose
            ? bone_local_matrix_anim_delta(tf, aq, at)
            : bone_local_matrix(tf, dq);
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
        m.is_cloth  = g.is_cloth;
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
        if (mp_should_hide_mesh(m)) continue;
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
        if (mp_should_hide_mesh(m)) continue;
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
