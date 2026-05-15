#pragma once

#include <atomic>
#include <string>
#include <vector>
#include <cstdint>

#include "HeightfieldLoader.h"

/* High-level entry points for loading a Fable 2 level.

   First milestone (current): read the `.engine_level` file, parse its
   17-char "LevelGraphicsFile" header + entry list, and report what
   resources the level references via the Output Log.

   Eventual goal: resolve every BNK named in the corresponding
   `level.vfsconfig`, decode the terrain heightfield + texture atlas,
   and surface the result so the model preview can show the terrain.

   The format itself is documented in docs/level_format.md.  The IDA
   parser sits at `level_parse_LevelGraphicsFile @ 0x82AAAC20`.       */

struct FlatAssetEntry;   

namespace Level {

/* One typed entry from a LevelGraphicsFile, captured roughly enough
   that we can iterate the list without losing structural info.
   `type` matches the engine's switch values:
     2  → instance placements (props at world transforms)
     4  → string + 8-byte payload (engine resource ref)
     5  → texture composite registration
     21 → renderable reference (2 strings + flags)
     32 → external streaming index (array of u64 pairs)              */
struct EngineLevelEntry {
    uint32_t type   = 0;
    size_t   offset = 0;   
    size_t   size   = 0;   
    std::string str_a;     
    std::string str_b;     
};

struct PropInstance {
    uint64_t hash = 0;
    uint8_t flags[3] = {0, 0, 0};
    float values[20] = {};
};

struct PropBlock {
    size_t offset = 0;
    std::string model_path;
    std::string shadow_model_path;
    std::string lod_model_path;
    std::string extra_model_path;
    std::vector<PropInstance> instances;
};

struct EngineLevelInfo {
    bool                          ok        = false;
    std::string                   source_path;
    uint32_t                      version   = 0;
    uint32_t                      entry_count = 0;
    std::vector<EngineLevelEntry> entries;
    std::vector<PropBlock>        prop_blocks;
    /* If parsing bailed mid-way, this is the human-readable reason. */
    std::string                   error;
};

/* Resources discovered for a level (so far: heightfield siblings).
   Populated by `Open` after it inspects the `.list` companion file
   and consults the global heightfield index. */
struct LevelResources {
    std::string ehf_path;   
    std::string ghf_path;   
    std::string hdb_path;   
    std::string genv_path;  
    std::string ama_path;   
    std::string amm_path;
    std::string amr_path;
    std::string model_body_bnk;
};

/* Open the level the user clicked.  `entry` points at the
   `.engine_level` file inside a BNK.  Posts a summary line to the
   Output Log on success or a single error line on failure.          */
bool Open(const FlatAssetEntry& entry);

/* Lower-level: parse an already-extracted blob.  Exposed so the
   future terrain pipeline can re-use the parser without going
   through the Output Log path. */
bool ParseEngineLevel(const std::vector<uint8_t>& bytes,
                      EngineLevelInfo&            out);

/* Build a normalised grayscale RGBA buffer of the level's heightmap
   suitable for the texture-preview window + tex_export_menu_rgba /
   PNG / JPG / TIFF / DDS pipelines.  Buffer is width*height*4 bytes
   (RGBA8) with R=G=B=normalized height and A=255.  Returns false +
   posts an OutputLog message on failure. */
bool RenderHeightmapToRGBA(const FlatAssetEntry& entry,
                           std::vector<uint8_t>& out_rgba,
                           int&                  out_w,
                           int&                  out_h);

/* Look up the `.texture_atlas` sibling of the given .engine_level
   (same BNK + same basename, different extension) and decode it
   into an RGBA buffer via the existing texture pipeline.  Returns
   false if there's no atlas file or the decode fails.            */
bool DecodeLevelTextureAtlas(const FlatAssetEntry& level_entry,
                             std::vector<uint8_t>& out_rgba,
                             int&                  out_w,
                             int&                  out_h);

/* Walk every zlib section in the level's `.ehf` and decode the one
   that matches a BC1 page sized to roughly the heightfield grid
   (W rounded down to multiple of 4 × H rounded down to multiple of
   4, BC1 = 0.5 bytes/pixel).  This is the BAKED TERRAIN ALBEDO
   that the Fable 2 engine renders for distant terrain LODs — at
   ~one texel per heightfield cell so the trivial UV mapping
   `(cx / (W-1), cy / (H-1))` lands each cell on its own pixel.

   `cells_w` / `cells_h` are the heightfield dimensions from the
   `.ghf` (typically 641 × 769).  On success returns the decoded
   RGBA8 buffer plus the BC1 page's own pixel dimensions (typically
   640 × 768 = cells rounded down to a multiple of 4).            */
bool DecodeEhfTerrainAlbedo(const FlatAssetEntry& level_entry,
                            uint32_t              cells_w,
                            uint32_t              cells_h,
                            std::vector<uint8_t>& out_rgba,
                            int&                  out_w,
                            int&                  out_h);

/* Variant of DecodeEhfTerrainAlbedo that works directly on the
   already-extracted `.ehf` bytes — useful when the caller has just
   pulled them out of a BNK and doesn't want a second lookup.  This
   is what PendingLoads uses on the terrain-load path, where the
   level loader has already extracted the .ehf during the mesh
   build.                                                          */
bool DecodeEhfTerrainAlbedoFromBytes(const std::vector<uint8_t>& ehf,
                                     uint32_t              cells_w,
                                     uint32_t              cells_h,
                                     std::vector<uint8_t>& out_rgba,
                                     int&                  out_w,
                                     int&                  out_h);

/* Parse the ground-texture palette from the .ehf, look up the FIRST
   diffuse `.tex` reference in any loaded BNK, decode it to RGBA,
   and return that buffer + the entry's tile_scale (so the renderer
   knows how many times to repeat the texture across the terrain).

   This is the "honest" terrain texturing fallback — it shows the
   level's actual primary ground texture (grass / dirt / cobbles /
   etc.) tiled across the terrain, with the per-entry tile_scale
   factor controlling repeat frequency.  Far more representative
   than stretching `.texture_atlas` source materials across the
   whole landscape.

   On success, `out_tile_scale` carries the f32 BE `tile_scale`
   metadata from the palette entry (typically 0.125 — meaning the
   texture repeats every 8 world units).  Multiply mesh UVs by
   `tile_scale * cells_per_axis` to get sensible repeats.

   Falls back to the SECOND, THIRD, etc. palette entry if the
   first .tex can't be located in any BNK.                       */
bool DecodeEhfPaletteFirstDiffuse(const std::vector<uint8_t>& ehf,
                                  std::vector<uint8_t>& out_rgba,
                                  int&                  out_w,
                                  int&                  out_h,
                                  float&                out_tile_scale,
                                  std::string&          out_picked_name);

/* Like `BakeEhfTerrainComposite` below, but with an explicit
   "preferred" BNK path passed through to `build_any_tex_buffer_for_name`
   so it can find level-specific texture pairs (e.g.
   `chapter3_texture_headers.bnk` + `chapter3_textures.bnk`).         */
bool BakeEhfTerrainCompositeWithBnk(const std::vector<uint8_t>& ehf,
                                    const std::string& preferred_bnk,
                                    std::vector<uint8_t>& out_rgba,
                                    int& out_w, int& out_h,
                                    std::string& out_picked_name);

/* Debug-only variant that additionally emits a pseudocolour
   visualisation of the PF=99 splat map (the per-cell palette index
   plane).  Useful when the bake's output looks wrong — comparing the
   splat map's spatial layout against the heightfield tells us whether
   the bug is in the splat decode, sampling, or texture blending.
   `splat_rgba` is sized `splat_w * splat_h * 4`.                    */
bool BakeEhfTerrainCompositeAndSplatDebug(
    const std::vector<uint8_t>& ehf,
    const std::string& preferred_bnk,
    std::vector<uint8_t>& out_rgba,
    int& out_w, int& out_h,
    std::string& out_picked_name,
    std::vector<uint8_t>& out_splat_rgba,
    int& out_splat_w, int& out_splat_h);

/* Bake a composite terrain albedo: tile the first decodable palette
   diffuse across the heightfield and modulate by the .ehf body's
   PF=24 lightmap (channel R = baked ambient-occlusion + shadow).
   The output is sized to the lightmap (typically W=u0 × H=u1 from the
   .ehf header), so the caller binds it with UVs spanning [0,1]
   across the whole terrain — uv_scale = 1.0.

   This is the visible Option-B-with-no-per-cell-indices: terrain
   shows the level's primary ground material PROPERLY DARKENED by
   the baked shadow/AO pass.  Looks dramatically better than the
   plain stretched-single-texture rendering even before we wire up
   per-cell material switching.

   On success returns the composite RGBA buffer + dimensions + the
   picked palette entry's name (for the OutputLog).             */
bool BakeEhfTerrainComposite(const std::vector<uint8_t>& ehf,
                             std::vector<uint8_t>&  out_rgba,
                             int&                   out_w,
                             int&                   out_h,
                             std::string&           out_picked_name);

}  

/* When `Level::Open` succeeds and the level has a usable heightfield,
   it stashes a built TerrainMesh here and sets `g_pending_terrain_load`
   so the renderer thread can pick it up next frame (mirroring the
   .mdl pending-load pattern).  The original level entry rides
   along so PendingLoads can resolve the .texture_atlas sibling.  The
   raw `.ehf` bytes the level loader pulled out of the BNK are
   stashed here too so the terrain-texture decode doesn't have to
   re-walk the BNK to find them.                                   */
extern std::atomic<bool>  g_pending_terrain_load;
extern Level::TerrainMesh g_pending_terrain_mesh;
extern std::string        g_pending_terrain_label;
extern FlatAssetEntry     g_pending_terrain_level_entry;
extern std::vector<uint8_t> g_pending_terrain_ehf_bytes;

/* Companion .ghf data + locator (so TerrainEdit can edit heights
   and save back into the BNK). */
extern std::vector<uint8_t>  g_pending_terrain_ghf_payload;
extern std::vector<float>    g_pending_terrain_ghf_heights;
extern float                 g_pending_terrain_ghf_tile_size;
extern int                   g_pending_terrain_ghf_width;
extern int                   g_pending_terrain_ghf_height;
extern FlatAssetEntry        g_pending_terrain_ghf_entry;

extern std::vector<Level::PropBlock> g_pending_level_prop_blocks;
extern std::string                   g_pending_level_model_body_bnk;
