#pragma once

#include <cstdint>
#include <string>
#include <vector>

/* Ground-texture PALETTE parsed out of a `.ehf` HeightFieldGraphicsFile.

   Each `.ehf` carries a list of (diffuse, normal) `.tex` path PAIRS
   plus per-entry metadata (tile_scale, intensity).  The terrain
   shader picks one entry per cell using per-cell index data that
   lives elsewhere in the `.ehf` (still being reverse-engineered).

   Layout of a palette entry, validated against Bloodstone, Brightwood,
   Bowerlake, BWS Cemetery, Brightwood ch3bigfarm dumps:

     <null-terminated ASCII string>   diffuse path  ("art\…\xxx_diffuse.tex")
     <null-terminated ASCII string>   normal  path  ("art\…\xxx_normal.tex")
     <13 bytes metadata>              `00 [f32 BE tile_scale] [f32 BE intensity] 00 00 00 00`
                                      where tile_scale ≈ 0.125 (1/8) and
                                      intensity = 1.0 are typical defaults.

   The palette starts at the first `art\` occurrence preceded by a
   small u32 BE count (which is roughly N/2 where N is the entry
   count my parser sees — looks like each "material" stores two
   (diff, normal) layer pairs).

   What this gives us right now:

     - Even without per-cell sampling, picking ANY palette entry
       and stretching its diffuse across the terrain (with the
       tile_scale UV factor) looks WAY more correct than the
       texture_atlas fallback (which shows source materials at
       1:1 stretch).

     - Long-term: once we find the per-cell index data we can
       sample the right palette entry per cell.                   */

namespace EhfPalette {

struct Entry {
    std::string diffuse_path;   // e.g. "art\themetextures\grass_diffuse.tex"
    std::string normal_path;
    float       tile_scale = 0.125f;  // f32 BE from metadata
    float       intensity  = 1.0f;
};

struct Palette {
    bool                 ok = false;
    std::vector<Entry>   entries;
    size_t               palette_offset = 0;  // byte offset in .ehf where palette starts
};

/* Parse the ground-texture palette out of an in-memory `.ehf` blob.
   Returns `ok = false` if the magic/structure doesn't match. */
Palette Parse(const std::vector<uint8_t>& ehf);

}  // namespace EhfPalette
