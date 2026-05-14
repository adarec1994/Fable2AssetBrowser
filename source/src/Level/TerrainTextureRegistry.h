#pragma once

#include <cstdint>
#include <string>
#include <vector>

/* Tiny side-registry for terrain-generated texture RGBA buffers
   (the `.ehf` lightmap, the BC5 normal map, the baked composite —
   things that aren't backed by a `.tex` file in any BNK, so the
   normal `tex_export_menu_named` path can't find them).

   PendingLoads.cpp registers each generated terrain texture by name
   when it builds the terrain.  RenderPanel.cpp's right-click "Export
   to" menu queries this registry first; on a hit it uses
   `tex_export_menu_rgba` against the cached bytes, otherwise it
   falls through to `tex_export_menu_named` (which scans BNKs).

   The registry is single-threaded — it's only touched from the
   renderer / UI thread.  Cleared when a new level loads.            */

namespace TerrainTextureRegistry {

struct Entry {
    std::vector<uint8_t> rgba;       // tightly packed, 4 bytes per pixel
    int                  width  = 0;
    int                  height = 0;
};

/* Replace (or create) the entry for `name`.  `rgba` is moved in. */
void Register(const std::string&        name,
              std::vector<uint8_t>      rgba,
              int                       width,
              int                       height);

/* Return the entry for `name` or nullptr if not present. */
const Entry* Find(const std::string& name);

/* Drop every entry — called when the user navigates away from the
   level / loads a new one. */
void Clear();

/* Per-material entry from the .ehf LOD palette.  Each LOD has a
   BaseLayer and a DetailLayer; both reference a diffuse + normal
   `.tex` path inside the game's BNKs.  These are stored exactly as
   they appear in the .ehf (with backslash separators and the
   "art\..." prefix), so the UI can show them verbatim.

   Filled by PendingLoads when terrain mesh loads, consumed by the
   "Materials & Textures" window in HexView.cpp.                  */
struct LodPaletteEntry {
    std::string base_diffuse;     // strs[0]
    std::string base_normal;      // strs[1]
    std::string detail_diffuse;   // strs[3]
    std::string detail_normal;    // strs[4]
};

/* Replace the entire LOD palette list. Cleared with Clear(). */
void SetLodPalette(std::vector<LodPaletteEntry> entries);

/* Returns a pointer to the stored list (may be empty). */
const std::vector<LodPaletteEntry>& GetLodPalette();

}  // namespace TerrainTextureRegistry
