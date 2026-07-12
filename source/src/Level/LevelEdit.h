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
    bool deleted = false;
    bool active() const {
        return deleted || has_rs || off[0] != 0.0f || off[1] != 0.0f ||
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
    uint32_t gdb_entity_hash = 0;
};

enum class AdditionEntityKind : uint8_t {
    None = 0,
    Chest = 1,
    SilverKey = 2,
    GenericProp = 3,
};

struct Addition {
    std::string model_path;
    float pos[3] = {0, 0, 0};
    float yaw_deg = 0.0f;
    bool removed = false;
    AdditionEntityKind entity_kind = AdditionEntityKind::None;
    std::vector<uint32_t> chest_items;

    uint32_t entity_template = 0;
    uint32_t entity_comp_field = 0;
    uint32_t entity_comp_template = 0;
    uint32_t physics_file_hash = 0;

    bool as_entity() const {
        return entity_kind != AdditionEntityKind::None;
    }
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
void SetDeleted(uint32_t selection_id, const InstInfo& info);

int AddPlacement(const std::string& model_path, const float pos[3]);
void GetAdditions(std::vector<Addition>& out);

void SetChestContents(uint32_t entity_hash,
                      const std::vector<uint32_t>& item_hashes);
bool GetChestContents(uint32_t entity_hash, std::vector<uint32_t>& out);
void ClearChestContents(uint32_t entity_hash);
size_t ChestContentsEditCount();

bool AdditionIsChest(int index);
bool GetAdditionChestItems(int index, std::vector<uint32_t>& out);
void SetAdditionChestItems(int index, const std::vector<uint32_t>& items);
void MarkAdditionEntityKind(int index, AdditionEntityKind kind);

void MarkAdditionAsPropEntity(int index,
                              uint32_t template_hash,
                              uint32_t comp_field_hash,
                              uint32_t comp_template_hash,
                              uint32_t physics_file_hash);

bool   Dirty();
bool   Saving();
size_t EditedCount();

uint64_t Revision();

void CollectPreviewXforms(
    std::unordered_map<uint32_t, EditXform>& out);

void PushUndoSnapshot(const std::vector<uint32_t>& ids);
bool Undo();

bool Save(std::string& msg);

bool RestoreDefaults(std::string& msg);

void ClearEdits();

bool RunLevProbe(const std::string& bnk_path, std::string& msg);

bool RunLevProbeMode(const std::string& bnk_path, bool float_only,
                     std::string& msg);

bool RunStreamFix(const std::string& streaming_path, std::string& msg);

}
