#pragma once

#include <cstdint>
#include <string>
#include <vector>

/* GPU resources for the per-pixel terrain texture-splat shader.

   The terrain mesh is built from the .ghf height grid with world-space
   UVs (so detail textures tile naturally with GPU mipmapping).  At
   render time the pixel shader needs to know, for each world position,
   which LOD palette entries to blend.  That comes from the .ehf body's
   chunk grid: chunk_w × chunk_h chunks, each chunk has 1+ layers, each
   layer has 4 per-corner texture indices + 4 per-corner blend amounts.

   We package this for the GPU as:

     * `lod_diffuse_array` — Texture2DArray, N slices = LOD palette size.
       Each slice is one LOD's diffuse, resampled to a common size
       (kCommonLodSize × kCommonLodSize) with a full mip chain.

     * `chunk_idx_array`   — Texture2DArray, kMaxLayers slices.
       Each slice = chunk_w × chunk_h RGBA8 where each pixel encodes
       the 4 per-corner LOD-palette indices (0..N-1) for ONE layer
       of that chunk.  When a chunk has fewer than kMaxLayers layers,
       the unused slices store 0xFF as a sentinel.

     * `chunk_blend_array` — Texture2DArray, kMaxLayers slices.
       Each slice = chunk_w × chunk_h RGBA8 where each pixel encodes
       the 4 per-corner blend amounts for ONE layer.  Unused slices
       store 0.

     * `lightmap`          — Texture2D, the .ehf PF=24 lightmap.

   Plus the chunk grid origin + extent so the shader can map world
   XY → chunk coords. */

#ifdef _WIN32
struct ID3D11Device;
struct ID3D11ShaderResourceView;
struct ID3D11SamplerState;
#endif

namespace Level { struct EhfParsedBody; }
namespace EhfLodThumbnails { struct Entry; }

namespace TerrainSplat {

constexpr int kCommonLodSize = 512;   // every LOD diffuse resampled to this
/* Chapter3 chunks have up to 13 layers each; 16 gives headroom while
   staying well within typical D3D Texture2DArray slice limits. */
constexpr int kMaxLayers     = 16;

struct Resources {
#ifdef _WIN32
    ID3D11ShaderResourceView* lod_diffuse_array   = nullptr;
    ID3D11ShaderResourceView* chunk_idx_array     = nullptr;
    ID3D11ShaderResourceView* chunk_blend_array   = nullptr;
    ID3D11ShaderResourceView* lightmap            = nullptr;
#endif
    /* Chunk grid world-space placement.  Filled from chunk[0].origin
       and the per-chunk extent assumed uniform across the grid.     */
    float world_origin_x = 0.f, world_origin_z = 0.f;
    float chunk_extent_x = 80.f, chunk_extent_z = 80.f;
    int   chunk_w = 0,  chunk_h = 0;
    int   lod_count = 0;

    /* Offset from mesh-space (the .ghf-derived TerrainMesh is centered
       at (0,0,0) for camera framing) to world-space.  The shader does:
         world_xy = mesh_xy + mesh_to_world_xz
       so it can map vertex positions back into chunk-grid coords.   */
    float mesh_to_world_x = 0.f, mesh_to_world_z = 0.f;

    /* Per-material tile_scale for the detail textures.  All LODs share
       this for now (chapter3 uses 0.125 for everything).            */
    float tile_scale = 0.125f;

    /* True once Build() has produced valid SRVs. */
    bool  ok = false;
};

/* Build the splat resources from a parsed .ehf body + the already-
   decoded EhfLodThumbnails diffuse SRVs (we reuse those instead of
   re-decoding).  On Linux/macOS this is a no-op (returns ok=false).
   Replaces any previously-built resources. */
#ifdef _WIN32
bool Build(ID3D11Device*                                       device,
           const Level::EhfParsedBody&                          parsed,
           const std::vector<EhfLodThumbnails::Entry>&          lod_thumbs,
           const std::vector<uint8_t>&                          lightmap_rgba,
           int                                                  lightmap_w,
           int                                                  lightmap_h,
           float                                                mesh_to_world_x,
           float                                                mesh_to_world_z);
#endif

/* Live view of current resources. */
const Resources& Get();

/* Release every SRV; call on level change. */
void Clear();

}  // namespace TerrainSplat
