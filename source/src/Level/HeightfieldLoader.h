#pragma once

#include <string>
#include <vector>
#include <cstdint>

/* Low-level readers for Fable 2's heightfield triplet:

     `.ehf`  — HeightFieldGraphicsFile (terrain LODs / texture-blend
                refs, version 18 in the Bloodstone sample).  Raw,
                not compressed.
     `.ghf`  — Gzipped raw heightmap blob.  Decompresses to a tight
                big-endian binary with no string magic; first u32 is
                the BE float `64.0` (world-scale or tile-size constant).
     `.hdb`  — Heightfield database (terrain prop placements).
     `.genv` — Environment table (per-cell shader params).

   This module's job is just to extract bytes from BNKs and (for
   `.ghf`) gunzip them.  The actual byte-layout decoding is layered
   on top in a separate stage. */

struct FlatAssetEntry;

namespace Level {

struct HeightfieldHeader {
    /* Common metadata extracted from the .ehf header.

       Full 63-byte layout (big-endian, IDA-validated against the
       parser at `sub_82A855A8` — see docs/level_format.md § 9b.11):

         +0   23B  magic    "HeightFieldGraphicsFile" (no NUL)
         +23   4B  u32      version (= 18 in Bloodstone)
         +27   4B  f32      prefix_float  → renderer state[+68]
         +31   4B  f32      f1            → renderer state[+64]
         +35   4B  u32      u0            → renderer state[+72]
         +39   4B  u32      u1            → renderer state[+76]
         +43   4B  f32      f2            → renderer state[+80]
         +47   4B  f32      f3            → renderer state[+84]
         +51   4B  f32      f4            → renderer state[+88]
         +55   4B  u32      body_offset (offset into .ehf of body blob)
         +59   4B  u32      body_size   (size of body blob)

       The 5 floats are likely world-space origin/extent + max height
       for the heightfield; the 2 u32s look like per-cell resolution
       (verts-per-tile-row × verts-per-tile-col).  We name them
       generically until we've validated against multiple .ehfs.    */
    std::string magic;        
    bool        ok = false;   
    uint32_t    version = 0;
    float       prefix_float = 0.f;   
    float       f0 = 0.f;             
    float       f1 = 0.f;             
    uint32_t    u0 = 0;               
    uint32_t    u1 = 0;               
    float       f2 = 0.f;             
    float       f3 = 0.f;             
    float       f4 = 0.f;             
    uint32_t    body_offset = 0;      
    uint32_t    body_size   = 0;      
};

struct HeightfieldFiles {
    bool                  ok = false;
    std::string           error;

    HeightfieldHeader     ehf_header;
    std::vector<uint8_t>  ehf_bytes;            
    std::vector<uint8_t>  ghf_bytes_compressed; 
    std::vector<uint8_t>  ghf_bytes_raw;        
};

/* Decoded heightmap pulled out of a .ghf payload.

   Layout, empirically determined against the Bloodstone sample
   `new_heightfield_14_id_874c5ee2.ghf` (after gunzip):

     0x00 (f32 BE)  tile_size      — world units between adjacent
                                     vertices.  Sample = 64.0.
     0x04..0x0B     padding (zero)
     0x0C (u32 BE)  W               — column count.   Sample = 641.
     0x10 (u32 BE)  H               — row count.      Sample = 769.
     0x14 onwards   W*H cells, 14 bytes per cell:
                       +0  (f32 BE)  height (world units)
                       +4..+13       10 bytes per-cell metadata
                                     (apparently constant in our
                                     sample — same 10 bytes
                                     "00 00 00 00 02 3b 3f 6a 00 01"
                                     repeated for every cell —
                                     probably blend / material refs.
                                     Left untouched for now.)        */
struct GhfHeights {
    bool                  ok        = false;
    uint32_t              width     = 0;   
    uint32_t              height    = 0;   
    float                 tile_size = 64.f;
    float                 min_height = 0.f;
    float                 max_height = 0.f;
    std::vector<float>    heights;         
    std::string           error;
};

/* Find a file whose path (lower-cased, forward-slashed) ends with
   `relative_path` in any of S.all_heightfield_files.  Returns
   nullptr if no match.  This is how we resolve names from a .list
   sidecar against the actually-mounted BNK contents. */
const FlatAssetEntry* FindHeightfieldByPath(const std::string& relative_path);

/* Load and decompress the four primary heightfield files for a
   level.  Each argument may be empty (the level simply doesn't
   have that file) — missing files leave the corresponding slot
   in `out` blank but don't fail the overall load.  Errors that
   *do* fail the load (corrupt gzip, missing files we asked for
   that aren't indexed) are surfaced via `out.error`.            */
bool LoadHeightfieldFiles(const std::string& ehf_path,
                          const std::string& ghf_path,
                          const std::string& hdb_path,
                          const std::string& genv_path,
                          HeightfieldFiles&  out);

/* Decode the heights grid out of an already-decompressed .ghf
   payload.  Pulls W, H, tile_size from the header and the f32
   height field from each 14-byte cell record.  Updates
   out.min_height / out.max_height as it goes.                    */
bool DecodeGhfHeights(const std::vector<uint8_t>& ghf_bytes_raw,
                      GhfHeights&                 out);

/* CPU-side terrain mesh built from a decoded heightmap.  Plain
   vectors of floats / uint32s — same shape ModelPreview's
   MDLMeshGeom expects, but kept independent here so callers can
   also feed this to glTF exporters / dump-to-OBJ tools.

   Vertex count = W*H, triangle count = (W-1)*(H-1)*2.
   Positions are in world units (cell pitch = tile_size);
   normals are smooth-shaded from the height gradient.
   UVs span [0, 1] across the whole heightmap so a single atlas
   tile or a tiled texture can be applied uniformly.            */
struct TerrainMesh {
    bool                  ok = false;
    uint32_t              width  = 0;
    uint32_t              height = 0;

    std::vector<float>    positions;   
    std::vector<float>    normals;     
    std::vector<float>    uvs;         
    std::vector<uint32_t> indices;     

    float                 min_height = 0.f;
    float                 max_height = 0.f;
};

bool BuildTerrainMesh(const GhfHeights& heights, TerrainMesh& out);

}  
