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

struct FlatAssetEntry;   // forward — defined in Utilities/State.h

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
    size_t   offset = 0;   // byte offset of the entry inside the file
    size_t   size   = 0;   // best-effort entry byte length
    std::string str_a;     // first string field if applicable
    std::string str_b;     // second string field if applicable
};

struct EngineLevelInfo {
    bool                          ok        = false;
    std::string                   source_path;
    uint32_t                      version   = 0;
    uint32_t                      entry_count = 0;
    std::vector<EngineLevelEntry> entries;
    /* If parsing bailed mid-way, this is the human-readable reason. */
    std::string                   error;
};

/* Resources discovered for a level (so far: heightfield siblings).
   Populated by `Open` after it inspects the `.list` companion file
   and consults the global heightfield index. */
struct LevelResources {
    std::string ehf_path;   // .ehf  — heightfield graphics descriptor
    std::string ghf_path;   // .ghf  — gzipped raw heightmap
    std::string hdb_path;   // .hdb
    std::string genv_path;  // .genv
    std::string ama_path;   // .ama / .amm / .amr — ADMP triplet
    std::string amm_path;
    std::string amr_path;
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

}  // namespace Level

/* When `Level::Open` succeeds and the level has a usable heightfield,
   it stashes a built TerrainMesh here and sets `g_pending_terrain_load`
   so the renderer thread can pick it up next frame (mirroring the
   .mdl pending-load pattern). */
extern std::atomic<bool>  g_pending_terrain_load;
extern Level::TerrainMesh g_pending_terrain_mesh;
extern std::string        g_pending_terrain_label;
