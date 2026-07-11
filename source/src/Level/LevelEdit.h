#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct FlatAssetEntry;

namespace LevelEdit {

void OnLevelLoaded(const FlatAssetEntry& entry);

void SetGdbSource(const std::string& bnk_path,
                  int file_index,
                  const std::string& loose_file);

bool Available();

bool Enabled();

bool SetEnabled(bool on, std::string& msg);

const float* DeltaFor(uint32_t selection_id);

void AddDelta(uint32_t selection_id,
              const float step[3],
              const float orig[3],
              uint32_t lev_pos_offset,
              const uint32_t gdb_pos_off[3]);

bool   Dirty();
size_t EditedCount();

uint64_t Revision();

void CollectPreviewOffsets(
    std::unordered_map<uint32_t, std::array<float, 3>>& out);

void PushUndoSnapshot(const std::vector<uint32_t>& ids);
bool Undo();

bool Save(std::string& msg);

bool RestoreDefaults(std::string& msg);

void ClearEdits();

}
