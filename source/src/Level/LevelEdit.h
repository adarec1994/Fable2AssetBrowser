#pragma once
// Level editing (v1: object movement). Owns the edit-mode state, the
// per-instance position deltas, and the file-side operations: backup on
// enable, Save Level (patch positions into the level's BNK slot), Restore
// Defaults (put the backup back). See LEVEL_EDITING_PLAN.md.

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

struct FlatAssetEntry;

namespace LevelEdit {

// Called by Level::Open after a successful parse: stashes the level's BNK
// identity and clears any previous edit state.
void OnLevelLoaded(const FlatAssetEntry& entry);

// A level is loaded and known to this module.
bool Available();

bool Enabled();
// Toggling on creates the backup (.bnk.bak beside a loose BNK, or a slot
// backup for ISO-hosted BNKs) if one doesn't exist. Returns false and leaves
// edit mode off if the backup couldn't be made.
bool SetEnabled(bool on, std::string& msg);

// Engine-space position delta for one baked instance (selection id), or
// nullptr when the instance is unmoved.
const float* DeltaFor(uint32_t selection_id);

// Accumulate a movement step for an instance. `orig` is the authored engine
// position, `pos_file_offset` the byte offset of its position in the level
// file (0 = not stored in the level file; the move is visual-only).
void AddDelta(uint32_t selection_id,
              const float step[3],
              const float orig[3],
              uint32_t pos_file_offset);

bool   Dirty();
size_t EditedCount();

// Bumped on every delta change; the render panel re-syncs its preview
// offsets only when this moves.
uint64_t Revision();

// All active deltas converted to PREVIEW space (viewer Y-up), keyed by
// selection id — the form the renderer and picker consume.
void CollectPreviewOffsets(
    std::unordered_map<uint32_t, std::array<float, 3>>& out);

// Patch every moved instance's position into the level file. Raw BNK
// entries are patched in place (loose file or mounted ISO); chunked
// entries fall back to exporting the patched level beside the app.
bool Save(std::string& msg);

// Overwrite the level's file(s) with the backup and reload the level.
bool RestoreDefaults(std::string& msg);

// Forget all deltas (used on level reload).
void ClearEdits();

}  // namespace LevelEdit
