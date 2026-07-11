#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct FlatAssetEntry;

namespace LevelEdit {

struct EditXform {
    float off[3] = {0, 0, 0};
    float quat[4] = {0, 0, 0, 1};
    float pivot[3] = {0, 0, 0};
    float scale = 1.0f;
    bool has_rs = false;
    bool active() const {
        return has_rs || off[0] != 0.0f || off[1] != 0.0f ||
               off[2] != 0.0f;
    }
};

struct InstInfo {
    const float* orig_pos = nullptr;
    float orig_rot_deg[3] = {0, 0, 0};
    uint32_t lev_off = 0;
    uint8_t lev_kind = 0;
    const uint32_t* gdb_off = nullptr;
    const uint32_t* gdb_rot_off = nullptr;
};

struct Addition {
    std::string model_path;
    float pos[3] = {0, 0, 0};
    float yaw_deg = 0.0f;
};

void OnLevelLoaded(const FlatAssetEntry& entry);

void SetGdbSource(const std::string& bnk_path,
                  int file_index,
                  const std::string& loose_file);

bool Available();

bool Enabled();

bool SetEnabled(bool on, std::string& msg);

bool EditFor(uint32_t selection_id,
             float out_pos_delta[3],
             float out_rot_delta_deg[3]);

void AddMove(uint32_t selection_id, const float step[3],
             const InstInfo& info);
void AddRotate(uint32_t selection_id, const float step_deg[3],
               const InstInfo& info);

int AddPlacement(const std::string& model_path, const float pos[3]);
void GetAdditions(std::vector<Addition>& out);

bool   Dirty();
size_t EditedCount();

uint64_t Revision();

void CollectPreviewXforms(
    std::unordered_map<uint32_t, EditXform>& out);

void PushUndoSnapshot(const std::vector<uint32_t>& ids);
bool Undo();

bool Save(std::string& msg);

bool RestoreDefaults(std::string& msg);

void ClearEdits();

}
