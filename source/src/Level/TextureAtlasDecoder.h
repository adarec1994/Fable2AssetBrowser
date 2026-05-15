#pragma once

#include <cstdint>
#include <string>
#include <vector>

/* Dedicated decoder for Fable 2 `.texture_atlas` level files.

   Bypasses the generic `.tex` parser in src/textures/TexParser.cpp,
   which mis-routes atlas blobs through the old `untile_xbox360_bc`
   (the "32-block-collision" untile that produces scrambled output
   on 1024-wide BC1 textures).  Pipeline mirrors ImageHeat exactly:

     1. parse header (magic 0xFFFFFFFE, W, H, PixelFormat, mip-offset
        table)
     2. read [u32 raw_size BE][u32 comp_size BE] at the first mip
        offset
     3. inflate the zlib stream that immediately follows (78 DA / 78
        9C / 78 01 / 78 5E)
     4. untile via the ReverseBox / ImageHeat
        `xg_address_2d_tiled_*` formulas
        (XBOX 360 "(block_pixel_size, texel_byte_pitch)" modes)
     5. byte-swap BC1/BC3/BC5 endpoints (BE → LE) for D3D BC decode
     6. decode BCn → RGBA

   Layout of a .texture_atlas file (validated against
   defaultscenario.texture_atlas):

     +0x00  u32 BE   magic_version       (0xFFFFFFFE)
     +0x04  u32 BE   raw_data_size_hint
     +0x08  u32 BE   unknown_0           (0)
     +0x0C  u32 BE   header_count        (e.g. 6)
     +0x10  u32 BE   TextureWidth        (1024)
     +0x14  u32 BE   TextureHeight       (1024)
     +0x18  u32 BE   PixelFormat         (35 = BC1, 39 = BC3, 40 = BC5)
     +0x1C  u32 BE   unknown_1           (4)
     +0x20  u32 BE   mip_table_offset    (0x54)
     +0x24..0x50    zero padding
     +0x50  u32 BE   unknown_2           (13)
     +0x54  u32 BE   raw_size            (= W*H/2 for BC1 mip0, etc.)
     +0x58  u32 BE   comp_size           (size of zlib stream)
     +0x5C  ...      zlib stream of comp_size bytes, inflates to
                     raw_size bytes of tiled+endian-swapped BCn      */

namespace TextureAtlas {

/* Result of decoding a `.texture_atlas` blob: the first (largest)
   page as an RGBA8 buffer plus its dimensions and the source pixel
   format. */
struct DecodedAtlas {
    bool                  ok          = false;
    std::vector<uint8_t>  rgba;        
    int                   width       = 0;
    int                   height      = 0;
    uint32_t              pixel_format = 0;  
    std::string           error;       
};

/* Decode the largest page (mip 0) of a `.texture_atlas` blob.
   Returns `DecodedAtlas.ok == true` on success.  On failure
   `.error` is filled with a short reason string suitable for the
   OutputLog. */
DecodedAtlas DecodeAtlas(const std::vector<uint8_t>& blob);

/* Decode a raw Xbox-360 BC1 page that has been zlib-deflated and
   stored tiled.  `zlib_stream` is the deflate bitstream (starting
   with 0x78 ...) of `comp_size` bytes; the decompressed payload
   is expected to be `width_pixels * height_pixels / 2` bytes of
   tiled+endian-swapped BC1 (Xbox 360 "(4, 8)").  Used by the
   `.ehf` baked-terrain-texture decoder which finds the section
   matching W*H/2 inside the heightfield graphics file.

   On success, `rgba` is filled with width*height*4 bytes of
   RGBA8 and the function returns true.                          */
bool DecodeZlibBc1Page(const uint8_t* zlib_stream,
                       size_t          comp_size,
                       size_t          expected_raw,
                       int             width_pixels,
                       int             height_pixels,
                       std::vector<uint8_t>& rgba);

/* Same but for BC3 (zlib → tiled BC3 → RGBA8). */
bool DecodeZlibBc3Page(const uint8_t* zlib_stream,
                       size_t          comp_size,
                       size_t          expected_raw,
                       int             width_pixels,
                       int             height_pixels,
                       std::vector<uint8_t>& rgba);

/* Same but for BC5 (zlib → tiled BC5 → RGBA8 with reconstructed
   blue channel — what we use for X/Y normals).                  */
bool DecodeZlibBc5Page(const uint8_t* zlib_stream,
                       size_t          comp_size,
                       size_t          expected_raw,
                       int             width_pixels,
                       int             height_pixels,
                       std::vector<uint8_t>& rgba);

/* Decode a buffer of RAW LINEAR BC1 BLOCKS (8 bytes per 4×4 block,
   row-major, ALREADY untiled and in standard LE endianness) to
   RGBA8.  Used by the `.ehf` path after `lh_decode_compressed_mip`
   has produced raw BC1 from the Huffman-coded bitstream.       */
bool DecodeRawBc1ToRgba(const uint8_t* bc1, size_t bc1_size,
                        int W, int H, std::vector<uint8_t>& rgba);

/* Decode the `.ehf` body's PF=99 blob into a row-major plane of
   4-bit palette indices.  PF=99 is the per-cell material splat map
   the terrain shader samples (validated against
   bl_chapter3_heightfield_id_9501a1af.ehf — index 0 = grass,
   1 = cliff, etc., matching the palette order).

   Storage details (empirically derived in tools/ehf_pf99_4bit.py):

     - .tex header at `pf99_blob[+0]`: magic 0xFFFFFFFE, W=logical_w,
       H=logical_h, PF=99, mt=0x54.
     - Zlib-compressed body inflates to raw_size = padded_w *
       padded_h / 2 bytes, where padded_w and padded_h are the
       logical dims rounded up to the next 128-pixel boundary
       (= 32 BC-block macro tiles × 4 pixels).
     - Storage layout: 32×32-block macro tiles, row-major between
       tiles AND row-major within each tile.
     - Per 8-byte block: 16 nibbles encoding a 4×4 pixel patch of
       palette indices.  Within each byte, HIGH nibble first.

   On success `out_indices` is filled with `out_w * out_h` bytes,
   each in [0, 15], naming the palette index per pixel.  Caller is
   responsible for finding the PF=99 blob inside the `.ehf` body
   (it sits right after the palette).                              */
bool DecodePF99SplatMap(const uint8_t* pf99_blob, size_t blob_size,
                        std::vector<uint8_t>& out_indices,
                        int& out_w, int& out_h,
                        std::string& out_err);

}  
