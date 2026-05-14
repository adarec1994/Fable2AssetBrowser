#pragma once

#include <string>
#include <vector>

/* Per-LOD thumbnail SRVs for the .ehf LOD palette.

   When a level is loaded, PendingLoads decodes every diffuse + normal
   .tex referenced by the .ehf's LOD vector (typically 20 entries × 2
   maps = 40 textures), creates D3D11 SRVs from them, and stores them
   here so RenderPanel can show one row of clickable thumbnails per
   LOD entry in the "Materials" overlay.

   Cleared on level change.  SRVs are owned by this module — Clear()
   Release()s them all. */

#ifdef _WIN32
struct ID3D11ShaderResourceView;
#endif

namespace EhfLodThumbnails {

struct Entry {
#ifdef _WIN32
    /* Per-map SRVs.  Any of these may be null if the texture failed
       to resolve / decode (UI shows a placeholder in that case).    */
    ID3D11ShaderResourceView* srv_base_diffuse   = nullptr;
    ID3D11ShaderResourceView* srv_base_normal    = nullptr;
    ID3D11ShaderResourceView* srv_detail_diffuse = nullptr;
    ID3D11ShaderResourceView* srv_detail_normal  = nullptr;
#endif
    /* Original .tex paths from the .ehf — kept so we can hover-tooltip
       the full path and right-click-export by name. */
    std::string base_diffuse_path;
    std::string base_normal_path;
    std::string detail_diffuse_path;
    std::string detail_normal_path;

    int base_diffuse_w = 0,   base_diffuse_h = 0;
    int base_normal_w = 0,    base_normal_h = 0;
    int detail_diffuse_w = 0, detail_diffuse_h = 0;
    int detail_normal_w = 0,  detail_normal_h = 0;
};

/* Replace the stored set.  Clears any old SRVs first. */
void Set(std::vector<Entry> entries);

/* Live view of currently-stored entries. */
const std::vector<Entry>& Get();

/* Release every SRV and drop the entries (called on level change). */
void Clear();

}  // namespace EhfLodThumbnails
