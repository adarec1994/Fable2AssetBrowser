#pragma once

#include <cstdint>
#include <string>
#include <vector>

/* Parse the per-chunk material binding data out of a `.ehf` body.

   Reverse-engineered from `sub_82A855A8` in the Xbox 360 binary.  The
   parser walks the full body and reaches `body_end` exactly when run
   against the extracted Bloodstone-chapter3 `.ehf` (validated by
   `tools/ehf_body_walker.py`).

   Structure (after the 63-byte header):

     1. Lightmap (.tex, PF=24) and BC5 normal map (.tex, PF=40)
     2. One float (state[+176] in the engine)
     3. sub_82A850A0 vector: count + N × (16B header + w_sub*h_sub*160B
        + 24B trailer) — terrain mesh tiles
     4. sub_82A860E8 vector: 1 float + count + N × 18B entries
     5. Two more textures (typically PF=98 uncompressed)
     6. LOD vector: count + N × (3 strings + 12B + 3 strings + 12B)
        — these strings are the palette diffuse/normal `.tex` paths
     7. sub_82A85DB0 vector: count + N textures (the PF=99 blob lives here)
     8. u32 W, u32 H — the CHUNK GRID dimensions (typically small,
        e.g. 24×24 for chapter3)
     9. W*H chunk records, each:
          - vec4 origin (3 floats = 12B from stream)
          - vec4 extent (3 floats = 12B from stream)
          - u32 layer_count
          - layer_count × per-layer entry (24B each):
              - 4B sub_82B25850 result
              - 4B name_index (into the layer-name list — not parsed yet)
              - 8B vec2 (two floats — tile-scale parameters)
              - 4B (4 × u8 texture indices into the LOD vector)
              - 4B (4 × u8 blend weights for those textures)
    10. sub_82A854D8 final pass: u8 flag + N × (u32 sub_count + sub_count*8B)
        per entry of the 40B vector from step 4.

   For terrain texturing we only need the LOD vector (palette material
   paths) + the chunk grid (per-chunk texture indices and blends).    */

namespace Level {

struct EhfLodEntry {
    /* 6 strings per LOD; the BaseLayer / detail tags are interleaved.
       In practice only positions 0, 1, 3, 4 carry `art\...` `.tex`
       paths (diffuse + normal × 2 layer types).                     */
    std::string strs[6];
};

struct EhfChunkLayer {
    uint32_t name_idx;                  // index into a layer-name list
    float    tile_uv[2];                // (u-scale, v-scale)-like vec2
    uint8_t  texture_idx[4];            // indices into the LOD vector
    uint8_t  blend[4];                  // 0..255 weight for each texture
};

struct EhfChunk {
    float                       origin[3];  // world-space position
    float                       extent[3];  // world-space size
    std::vector<EhfChunkLayer>  layers;
};

struct EhfParsedBody {
    bool                         ok = false;
    std::string                  error;

    /* High-level layout markers. */
    uint32_t                     chunk_w = 0;
    uint32_t                     chunk_h = 0;

    /* Vectors. */
    std::vector<EhfLodEntry>     lods;
    std::vector<EhfChunk>        chunks;     // size = chunk_w * chunk_h

    /* Bytes consumed and remaining after the parse.  If
       `bytes_remaining == 0` we walked the body exactly.  Use this as
       a sanity check on whether the parser is right.                  */
    size_t                       bytes_consumed = 0;
    size_t                       bytes_remaining = 0;
};

/* Parse a `.ehf` blob's body section.  `ehf` is the full file
   (including the 63-byte header).  The parser opens the body via the
   header's `body_offset`/`body_size`, then walks step-by-step.

   On any structural error sets `error` and leaves `ok == false`.    */
bool ParseEhfBody(const std::vector<uint8_t>& ehf, EhfParsedBody& out);

}  // namespace Level
