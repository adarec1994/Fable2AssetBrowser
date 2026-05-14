#include "TextureAtlasDecoder.h"

#include <cmath>
#include <cstring>
#include <sstream>
#include <zlib.h>

/* ----- self-contained helpers ------------------------------------ */
/* Everything this decoder needs is duplicated in this translation
   unit on purpose — the original `static` helpers in
   src/UI/ModelPreview.cpp live in an anonymous TU and the .tex
   parser in src/textures/TexParser.cpp already has subtle bugs
   that swallow our zlib variant.  Keeping the atlas pipeline
   isolated means a future change to either of those files can't
   break atlases by accident.                                       */

namespace {

inline bool rd32be(const uint8_t* p, size_t n, size_t off, uint32_t& out) {
    if (off + 4 > n) return false;
    out = (uint32_t(p[off+0]) << 24) | (uint32_t(p[off+1]) << 16) |
          (uint32_t(p[off+2]) <<  8) |  uint32_t(p[off+3]);
    return true;
}

/* BC1 / DXT1 block decode → 16 BGRA8 texels into `outRGBA`. */
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

/* Xbox 360 BC1: byte-swap endpoint pairs (c0/c1) BE→LE; also flip
   the index byte order so the BC1 decoder sees it the way D3D does. */
void swap_bc1_endian(uint8_t* data, size_t size) {
    for (size_t i = 0; i + 8 <= size; i += 8) {
        uint16_t c0 = (data[i+0] << 8) | data[i+1];
        uint16_t c1 = (data[i+2] << 8) | data[i+3];
        uint32_t idx = (uint32_t(data[i+4]) << 24) | (uint32_t(data[i+5]) << 16) |
                       (uint32_t(data[i+6]) <<  8) |  uint32_t(data[i+7]);
        data[i+0] = c0 & 0xFF;  data[i+1] = (c0 >> 8) & 0xFF;
        data[i+2] = c1 & 0xFF;  data[i+3] = (c1 >> 8) & 0xFF;
        data[i+4] = idx & 0xFF;        data[i+5] = (idx >> 8) & 0xFF;
        data[i+6] = (idx >> 16) & 0xFF; data[i+7] = (idx >> 24) & 0xFF;
    }
}

void swap_bc3_endian(uint8_t* data, size_t size) {
    for (size_t i = 0; i + 16 <= size; i += 16) {
        uint64_t alpha_bits = 0;
        for (int j = 0; j < 6; ++j)
            alpha_bits |= ((uint64_t)data[i+2+j]) << (j*8);
        uint64_t alpha_swapped = 0;
        for (int j = 0; j < 6; ++j)
            alpha_swapped |= ((alpha_bits >> (j*8)) & 0xFF) << ((5-j)*8);
        for (int j = 0; j < 6; ++j)
            data[i+2+j] = (alpha_swapped >> (j*8)) & 0xFF;
        swap_bc1_endian(data + i + 8, 8);
    }
}

void swap_bc5_endian(uint8_t* data, size_t size) {
    for (size_t i = 0; i + 16 <= size; i += 16) {
        for (int half = 0; half < 2; ++half) {
            uint8_t* blk = data + i + 8 * half;
            uint64_t bits = 0;
            for (int j = 0; j < 6; ++j) bits |= ((uint64_t)blk[2+j]) << (j*8);
            uint64_t sw = 0;
            for (int j = 0; j < 6; ++j) sw |= ((bits >> (j*8)) & 0xFF) << ((5-j)*8);
            for (int j = 0; j < 6; ++j) blk[2+j] = (sw >> (j*8)) & 0xFF;
        }
    }
}

/* Port of ReverseBox `xg_address_2d_tiled_x / y` (the source for
   ImageHeat's "XBOX 360 (block_pixel_size, texel_byte_pitch)" modes).
   Iterates sequential tiled-storage block offsets and scatters each
   block to its logical (x, y) — works for any
   (block_pixel_size, texel_byte_pitch) pair.  */
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

/* Simple "tile-major, linear-inside-tile" BC untile used by the
   Fable 2 .ehf baked-terrain pages.  Layout the engine writes:

     - image is split into 32×32-BC-block macro-tiles (= 128×128
       pixels for BC1, 128×128 for BC3/BC5 as well — block-count
       is what matters)
     - macro-tiles are stored row-major in storage order
     - WITHIN each macro-tile, blocks are stored row-major linearly,
       no further swizzling

   This is what the engine actually does for `.ehf` terrain pages
   — confirmed against Bloodstone defaultscenario (640×768 baked
   albedo) and Brightwood / Bowerlake (512×512 material atlas
   pages).  Standard Xbox 360 XGAddress2DTiledOffset / ImageHeat
   `xg_address_2d_tiled_*` formulas DON'T match this — they apply
   an extra within-tile swizzle the .ehf doesn't.  Atlas / .tex
   pages still need the full ImageHeat path (see below). */
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

/* ImageHeat-style untile.  Reorders a tiled block buffer into linear
   row-major block order.  block_pixel_size=4, texel_byte_pitch=8 for
   BC1; texel_byte_pitch=16 for BC3 / BC5; block_pixel_size=1,
   texel_byte_pitch=4 for raw 32-bpp ARGB (not used here but the
   formula handles it). */
void untile_xbox360_imageheat(const uint8_t* tiled,
                              size_t tiled_size,
                              std::vector<uint8_t>& linear_out,
                              int width_pixels, int height_pixels,
                              uint32_t block_pixel_size,
                              uint32_t texel_byte_pitch)
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
                    tiled + src, (size_t)texel_byte_pitch);
    }
}

/* Final BCn → RGBA8 blit. */
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

}  // namespace

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

    /* Parse header. */
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

    /* The mip_table field at 0x20 points to where the size-prefixed
       payload begins.  In every atlas we've seen this is 0x54 with
       all of 0x24..0x4F zero-padded; honour the field rather than
       assuming.                                                     */
    const size_t mt = (size_t)mip_table;
    if (mt + 8 > n) {
        r.error = "atlas: mip table offset past EOF";
        return r;
    }
    uint32_t raw_size  = 0;
    uint32_t comp_size = 0;
    rd32be(d, n, mt + 0, raw_size);
    rd32be(d, n, mt + 4, comp_size);

    /* Sanity-check raw_size against the format.
       BC1 (PF=35): raw = W*H/2
       BC3 (PF=39): raw = W*H
       BC5 (PF=40): raw = W*H
       L8A8 (PF=24, lightmap inside .ehf bodies): raw_size is the
         file-stated PADDED storage size (W and H both rounded up to
         32-texel boundaries, with an occasional +1 tile of padding
         the engine adds for textures larger than 16K texels).
         We trust the file's raw_size verbatim for PF=24.            */
    size_t   expected_raw = 0;
    uint32_t block_bytes  = 0;        // BC block size in bytes
    if      (PF == 35u) { expected_raw = (size_t)W * H / 2; block_bytes = 8;  }
    else if (PF == 24u) { expected_raw = (size_t)raw_size;  block_bytes = 2;  }
    else                { expected_raw = (size_t)W * H;     block_bytes = 16; }
    if (PF != 24u && (size_t)raw_size != expected_raw) {
        std::ostringstream os;
        os << "atlas: raw_size " << raw_size << " != expected "
           << expected_raw << " for " << W << "x" << H
           << " PF=" << PF;
        r.error = os.str();
        return r;
    }

    /* The zlib stream starts at mt+8.  comp_size names how many bytes
       it occupies; clamp it to what's actually in the blob. */
    const size_t zlib_off = mt + 8;
    if (zlib_off + 2 > n || d[zlib_off] != 0x78) {
        r.error = "atlas: no zlib magic at mip data offset";
        return r;
    }
    size_t avail_comp = n - zlib_off;
    if ((size_t)comp_size > avail_comp) comp_size = (uint32_t)avail_comp;

    /* Inflate. */
    std::vector<uint8_t> raw;
    if (!inflate_zlib(d + zlib_off, comp_size, raw, expected_raw)) {
        r.error = "atlas: zlib inflate failed";
        return r;
    }

    /* Untile.  For PF=24 (L8A8 lightmap inside .ehf bodies) the
       tiling is per-texel (block_pixel_size=1, texel_byte_pitch=2)
       rather than per-BC-block (4, block_bytes).  Validated in
       tools/ehf_body_decode.py against bl_chapter3 and autumn_1
       — the resulting image shows a recognisable terrain lightmap
       so the formulas are correct as-is.                              */
    std::vector<uint8_t> linear;
    if (PF == 24u) {
        untile_xbox360_imageheat(raw.data(), raw.size(), linear,
                                 (int)W, (int)H,
                                 /*block_pixel_size=*/1,
                                 /*texel_byte_pitch=*/2);
    } else {
        untile_xbox360_imageheat(raw.data(), raw.size(), linear,
                                 (int)W, (int)H,
                                 /*block_pixel_size=*/4,
                                 /*texel_byte_pitch=*/block_bytes);
    }

    /* Endian-swap + BCn → RGBA, or for PF=24 just splat the two
       channels into RGBA(R=high_byte, G=low_byte, B=0, A=255).      */
    r.width  = (int)W;
    r.height = (int)H;
    r.pixel_format = PF;
    if (PF == 24u) {
        /* Each output pixel = 2 bytes of linear, stored as (R,G,0,255).
           From the Python validation: ch0 (offset 0 inside each pair)
           is the smooth high byte that visualises as a baked terrain
           lightmap; ch1 (offset 1) is a noisier secondary channel
           (detail / dither / vignette — TBD).  Preserving both into
           R and G lets downstream code combine or pick whichever
           channel it needs.                                          */
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
    if (PF == 35u) {
        swap_bc1_endian(linear.data(), linear.size());
        blit_bc_to_rgba<8>(linear.data(), (int)W, (int)H, r.rgba,
                           decode_bc1_block);
    } else if (PF == 39u) {
        swap_bc3_endian(linear.data(), linear.size());
        blit_bc_to_rgba<16>(linear.data(), (int)W, (int)H, r.rgba,
                            decode_bc3_block);
    } else { /* PF == 40 */
        swap_bc5_endian(linear.data(), linear.size());
        blit_bc5_to_rgba(linear.data(), (int)W, (int)H, r.rgba);
    }
    r.ok = true;
    return r;
}

/* Generic "decode a zlib-deflated tiled BC{1,3,5} page" helper used
   by the .ehf baked-terrain-texture pipeline.  Same byte transforms
   as DecodeAtlas but with the caller controlling W/H/format and
   pointing directly at the deflate bitstream — no .texture_atlas
   container header in the way.                                    */
namespace {
bool decode_zlib_bc_page_generic(const uint8_t* zlib_stream,
                                 size_t          comp_size,
                                 size_t          expected_raw,
                                 int             width_pixels,
                                 int             height_pixels,
                                 std::vector<uint8_t>& rgba,
                                 int             which /* 1=BC1, 3=BC3, 5=BC5 */)
{
    if (!zlib_stream || comp_size < 2 ||
        width_pixels <= 0 || height_pixels <= 0 ||
        expected_raw == 0) return false;
    if (zlib_stream[0] != 0x78) return false;

    std::vector<uint8_t> raw;
    if (!inflate_zlib(zlib_stream, comp_size, raw, expected_raw)) return false;

    const uint32_t block_bytes = (which == 1) ? 8u : 16u;

    /* The actual .ehf BC1 storage layout is NOT fully understood
       yet — tile-major (32-block macro tiles, linear inside) gets
       us closer than the full ImageHeat swizzle for non-Bloodstone
       levels but still has tile-aligned black gaps on Bloodstone.
       Until we crack it via IDA, use the ImageHeat path so at least
       Bloodstone defaultscenario renders cleanly.                 */
    std::vector<uint8_t> linear;
    untile_xbox360_imageheat(raw.data(), raw.size(), linear,
                             width_pixels, height_pixels,
                             /*block_pixel_size=*/4,
                             /*texel_byte_pitch=*/block_bytes);

    if (which == 1) {
        swap_bc1_endian(linear.data(), linear.size());
        blit_bc_to_rgba<8>(linear.data(), width_pixels, height_pixels,
                           rgba, decode_bc1_block);
    } else if (which == 3) {
        swap_bc3_endian(linear.data(), linear.size());
        blit_bc_to_rgba<16>(linear.data(), width_pixels, height_pixels,
                            rgba, decode_bc3_block);
    } else { /* which == 5 */
        swap_bc5_endian(linear.data(), linear.size());
        blit_bc5_to_rgba(linear.data(), width_pixels, height_pixels, rgba);
    }
    return true;
}
}  // namespace

bool DecodeZlibBc1Page(const uint8_t* zlib_stream, size_t comp_size,
                       size_t expected_raw, int w, int h,
                       std::vector<uint8_t>& rgba)
{
    return decode_zlib_bc_page_generic(zlib_stream, comp_size, expected_raw,
                                       w, h, rgba, /*which=*/1);
}
bool DecodeZlibBc3Page(const uint8_t* zlib_stream, size_t comp_size,
                       size_t expected_raw, int w, int h,
                       std::vector<uint8_t>& rgba)
{
    return decode_zlib_bc_page_generic(zlib_stream, comp_size, expected_raw,
                                       w, h, rgba, /*which=*/3);
}
bool DecodeZlibBc5Page(const uint8_t* zlib_stream, size_t comp_size,
                       size_t expected_raw, int w, int h,
                       std::vector<uint8_t>& rgba)
{
    return decode_zlib_bc_page_generic(zlib_stream, comp_size, expected_raw,
                                       w, h, rgba, /*which=*/5);
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

    /* Parse .tex sub-header. */
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

    /* Inflate to raw_size bytes.  Same lenient handling as DecodeAtlas. */
    std::vector<uint8_t> raw;
    if (!inflate_zlib(pf99_blob + zlib_off, comp_size, raw, raw_size)) {
        out_err = "splat: zlib inflate failed";
        return false;
    }

    /* Derive padded block dimensions from raw_size.  raw_size =
       blocks_w * blocks_h * 8.  Padded pixel dims = blocks * 4.
       Search for plausible padding: padded_w must be a multiple of
       128 (= 32 BC-block macro tile × 4 pixels) and padded_w >= W.  */
    const size_t total_blocks = (size_t)raw_size / 8;
    int padded_w = 0, padded_h = 0;
    for (int pad_step : {128, 64, 32, 16, 8, 4}) {
        const int pw = ((int)W + pad_step - 1) / pad_step * pad_step;
        if (pw % 4 != 0) continue;
        const size_t blocks_w = (size_t)pw / 4;
        if (total_blocks % blocks_w != 0) continue;
        const size_t blocks_h = total_blocks / blocks_w;
        const int ph = (int)blocks_h * 4;
        if (ph >= (int)H) { padded_w = pw; padded_h = ph; break; }
    }
    if (padded_w == 0) {
        out_err = "splat: could not factor padded dims from raw_size";
        return false;
    }

    /* Untile: 32×32-block macro tiles, row-major between AND within.
       Mirrors the tile_major formula confirmed by
       tools/ehf_pf99_4bit.py.                                       */
    const int blocks_w = padded_w / 4;
    const int blocks_h = padded_h / 4;
    constexpr int TILE = 32;
    const int tiles_w = (blocks_w + TILE - 1) / TILE;
    const int tiles_h = (blocks_h + TILE - 1) / TILE;
    std::vector<uint8_t> linear((size_t)blocks_w * blocks_h * 8, 0);
    size_t src_off = 0;
    for (int ty = 0; ty < tiles_h; ++ty) {
        for (int tx = 0; tx < tiles_w; ++tx) {
            for (int ly = 0; ly < TILE; ++ly) {
                const int by = ty * TILE + ly;
                for (int lx = 0; lx < TILE; ++lx) {
                    const int bx = tx * TILE + lx;
                    if (by < blocks_h && bx < blocks_w &&
                        src_off + 8 <= raw.size())
                    {
                        const size_t dst_off =
                            ((size_t)by * blocks_w + bx) * 8;
                        std::memcpy(linear.data() + dst_off,
                                    raw.data() + src_off, 8);
                    }
                    src_off += 8;
                }
            }
        }
    }

    /* Unpack each 8-byte block into a 4×4 patch of 4-bit indices.
       HIGH nibble of byte[k] = pixel (k*2), LOW = pixel (k*2 + 1).
       Pixels within the patch are row-major: pixel 0 = top-left.   */
    out_indices.assign((size_t)W * (size_t)H, 0);
    for (int by = 0; by < blocks_h; ++by) {
        for (int bx = 0; bx < blocks_w; ++bx) {
            const uint8_t* b8 = linear.data() + ((size_t)by * blocks_w + bx) * 8;
            uint8_t nibs[16];
            for (int k = 0; k < 8; ++k) {
                nibs[k * 2]     = (b8[k] >> 4) & 0xF;
                nibs[k * 2 + 1] =  b8[k]       & 0xF;
            }
            for (int py = 0; py < 4; ++py) {
                const int yy = by * 4 + py;
                if (yy >= (int)H) break;
                for (int pxn = 0; pxn < 4; ++pxn) {
                    const int xx = bx * 4 + pxn;
                    if (xx >= (int)W) continue;
                    out_indices[(size_t)yy * W + xx] = nibs[py * 4 + pxn];
                }
            }
        }
    }

    out_w = (int)W;
    out_h = (int)H;
    return true;
}

}  // namespace TextureAtlas
