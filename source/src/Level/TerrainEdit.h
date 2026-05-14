#pragma once

#include <cstdint>
#include <string>
#include <vector>

/* Live terrain-edit state.

   Populated by PendingLoads when a heightfield level is loaded, used
   by the Edit Terrain overlay to mutate heights and re-upload the
   terrain mesh's vertex buffer.  Cleared on level change.

   Saving back to the .iso lives in TerrainEdit::Save (Phase 3 — not
   yet implemented).  The snapshot of the ORIGINAL heights is kept so
   the Reset button restores the level to its pristine state without
   needing a reload.                                                 */

#ifdef _WIN32
struct ID3D11Device;
struct ID3D11Buffer;
#endif

namespace TerrainEdit {

struct State {
    bool                  loaded = false;
    bool                  dirty  = false;

    /* Grid + world placement of the heightfield mesh. */
    int                   width  = 0;
    int                   height = 0;
    float                 tile_size = 1.f;
    float                 center_x = 0.f;
    float                 center_z = 0.f;

    /* width * height floats — Y component per vertex. */
    std::vector<float>    heights_original;
    std::vector<float>    heights_current;

    /* All-axis CPU position buffer (3 floats per vertex), kept in
       sync with `heights_current` so we can re-upload the vertex
       buffer cheaply after any edit. */
    std::vector<float>    positions;     // width*height*3 floats

    /* Source .ghf bytes (uncompressed payload after gunzip).  Saving
       writes back into this buffer then back into the BNK / ISO.   */
    std::vector<uint8_t>  ghf_payload_original;

    /* Locator info so save can find the right slot in the .iso. */
    std::string           ghf_bnk_path;       // path (iso:// or disk) to the .bnk
    int                   ghf_file_index = -1;
    std::string           ghf_full_path;      // relative path inside BNK

    /* BNK entry record for the .ghf — captured at level-load time so
       Save doesn't need to re-open the BNK reader.                  */
    uint64_t              ghf_bnk_entry_offset = 0;     // byte offset within the BNK file
    uint32_t              ghf_bnk_entry_on_disk_size = 0;
    bool                  ghf_bnk_entry_is_compressed = false;

    /* Min/max bounds used by the mesh in world units (so brush
       picking can later transform mouse rays into grid space). */
    float                 min_x = 0.f, max_x = 0.f;
    float                 min_z = 0.f, max_z = 0.f;
};

/* Replace the stored state.  Heights are moved in.  After this, the
   per-tool operations can run. */
void Init(int width, int height, float tile_size,
          float center_x, float center_z,
          std::vector<float> heights,
          std::vector<float> positions,
          std::vector<uint8_t> ghf_payload,
          std::string ghf_bnk_path, int ghf_file_index,
          std::string ghf_full_path,
          uint64_t   ghf_bnk_entry_offset       = 0,
          uint32_t   ghf_bnk_entry_on_disk_size = 0,
          bool       ghf_bnk_entry_is_compressed= false);

const State& Get();

/* Returns true iff a heightfield is currently loaded and editable. */
bool IsLoaded();

/* True if the user has applied any tool since the last Save/Reset. */
bool IsDirty();

/* --- Tools (whole-terrain for now; brush-picking comes later). --- */

void RaiseAll(float delta);
void LowerAll(float delta);
void SmoothAll();            // single pass of 3x3 box filter
void FlattenAll(float target_height);
void Reset();                // restore heights_original → heights_current

/* --- Brush tools (operate on grid cells within a radius). --- */

enum class BrushTool : int {
    None    = 0,
    Raise   = 1,
    Lower   = 2,
    Smooth  = 3,
    Flatten = 4,
};

/* Apply a brush at world position (wx, wz).  `radius` and `strength`
   are in world units; smoothstep falloff is applied from radius edge
   to centre.  For Flatten, `target_h` is the height to converge to
   (typically sampled at the brush centre by the caller). */
void ApplyBrush(BrushTool tool,
                float wx, float wz,
                float radius, float strength,
                float target_h = 0.f);

/* Bilinear height sample at world XZ. Returns 0 if out of bounds. */
float SampleHeightAtWorldXZ(float wx, float wz);

/* Ray-march the loaded terrain.  Returns true if the ray (ox,oy,oz)
   + t*(dx,dy,dz) intersects the heightfield; out_hit_x/z get the
   world XZ of the first intersection, out_hit_y the terrain height
   there.                                                            */
bool Raycast(float ox, float oy, float oz,
             float dx, float dy, float dz,
             float& out_hit_x, float& out_hit_y, float& out_hit_z);

/* Re-uploads the modified positions to the GPU.  Drops the old VB on
   the supplied mesh (cast internally to ::MPPerMesh*) and creates a
   fresh one.  No-op on non-Windows.                                 */
#ifdef _WIN32
void ApplyToGpu(ID3D11Device* device, void* mesh);
#endif

/* Re-encode the current heights into the stored .ghf payload and
   gzip the result, writing the gzipped bytes to disk.

   Phase 1 destination: a sibling file alongside the originating .iso
   (path: `<iso_dir>/<level_name>.modified.ghf.gz`).  The user can
   then use external tooling to inject this back into the .iso.  A
   future iteration will do the BNK repack + ISO splice in-process.

   Returns true on success; fills `out_path_or_error` with either the
   written path or a diagnostic message.                              */
bool Save(std::string& out_path_or_error);

/* Drop everything (called on level change). */
void Clear();

}  // namespace TerrainEdit
