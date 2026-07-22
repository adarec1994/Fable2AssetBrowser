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
    Npc = 4,
};

struct Addition {
    std::string model_path;
    std::string entity_name;
    float pos[3] = {0, 0, 0};
    float yaw_deg = 0.0f;
    bool removed = false;
    AdditionEntityKind entity_kind = AdditionEntityKind::None;
    std::vector<uint32_t> chest_items;
    int silver_keys_needed = 0;
    bool is_dig_spot = false;

    uint32_t entity_template = 0;
    uint32_t entity_comp_field = 0;
    uint32_t entity_comp_template = 0;
    uint32_t entity_position_template = 0;
    uint32_t entity_rotation_template = 0;
    uint32_t physics_file_hash = 0;
    std::string creature_name;
    std::vector<std::string> asset_models;

    bool entity_has_text = false;
    std::string readable_text;
    uint32_t loot_table_record = 0;

    bool as_entity() const {
        return entity_kind != AdditionEntityKind::None;
    }
};

struct ContainerTemplateInfo {
    std::string entity_name;
    bool is_dig_spot = false;
    int silver_keys_needed = 0;
    uint32_t entity_template = 0;
    uint32_t transform_component_field = 0;
    uint32_t transform_component_template = 0;
    uint32_t physics_file_hash = 0;
    std::vector<uint32_t> initial_items;
    uint32_t potential_items_record = 0;
};

struct NpcPlacementInfo {
    std::string instance_name;
    std::string creature_name;
    uint32_t creature_entity = 0;
    uint32_t transform_component_field = 0;
    uint32_t transform_component_template = 0;
    uint32_t position_template = 0;
    uint32_t rotation_template = 0;
    std::vector<std::string> asset_models;
    bool valid() const {
        return !instance_name.empty() && !creature_name.empty() &&
               creature_entity != 0 && transform_component_field != 0;
    }
};

struct StaticPropPlacementInfo {
    std::string instance_name;
    uint32_t entity_template = 0;
    uint32_t transform_component_field = 0;
    uint32_t transform_component_template = 0;
    uint32_t position_template = 0;
    uint32_t rotation_template = 0;
    bool valid() const {
        return !instance_name.empty() && entity_template != 0 &&
               transform_component_field != 0 &&
               transform_component_template != 0 &&
               position_template != 0 && rotation_template != 0;
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
void ClearDeleted(uint32_t selection_id);
bool IsDeleted(uint32_t selection_id);
bool EntityRemovalPending(uint32_t entity_hash);

int AddPlacement(const std::string& model_path, const float pos[3]);
void NoteExternalEdit();
void GetAdditions(std::vector<Addition>& out);
void MoveAddition(int index, const float pos[3]);
void SetAdditionYaw(int index, float yaw_deg);
void RemoveAddition(int index);
int SilverKeyChestRequirement(const std::string& model_path);

void SetChestContents(uint32_t entity_hash,
                      const std::vector<uint32_t>& item_hashes);
bool GetChestContents(uint32_t entity_hash, std::vector<uint32_t>& out);
void ClearChestContents(uint32_t entity_hash);
size_t ChestContentsEditCount();

bool GetContainerLootTable(uint32_t entity_hash, uint32_t& out);
void SetContainerLootTable(uint32_t entity_hash, uint32_t loot_record);
void ClearContainerLootTable(uint32_t entity_hash);

bool AdditionIsChest(int index);
uint32_t GetAdditionLootTable(int index);
void SetAdditionLootTable(int index, uint32_t loot_record);
bool GetAdditionChestItems(int index, std::vector<uint32_t>& out);
void SetAdditionChestItems(int index, const std::vector<uint32_t>& items);
void MarkAdditionEntityKind(int index, AdditionEntityKind kind);
void MarkAdditionAsSilverKeyChest(int index, int silver_keys_needed);
int AddContainerPlacement(const std::string& model_path,
                          const float pos[3],
                          const ContainerTemplateInfo& info);
void MarkAdditionAsContainer(int index,
                             const ContainerTemplateInfo& info);
bool AdditionIsDigSpot(int index);
int AddNpcPlacement(const float pos[3], const NpcPlacementInfo& info);
bool AdditionIsNpc(int index);
bool AdditionIsNamedEntity(int index);

void MarkAdditionAsPropEntity(int index,
                              uint32_t template_hash,
                              uint32_t comp_field_hash,
                              uint32_t comp_template_hash,
                              uint32_t physics_file_hash,
                              bool has_text_tags = false);
void MarkAdditionAsStaticProp(int index,
                              const StaticPropPlacementInfo& info);

bool AdditionIsReadable(int index);
bool GetAdditionReadableText(int index, std::string& out);
void SetAdditionReadableText(int index, const std::string& text);

void SetEntityTextEdit(uint32_t tag_hash, const std::string& utf8);
bool GetEntityTextEdit(uint32_t tag_hash, std::string& out);
size_t TextEditCount();

struct GeneratorAddition {
    float pos[3] = {0, 0, 0};
    std::string creature_name;
    uint32_t creature_entity = 0;
    std::vector<std::array<float, 3>> spawn_points;
    std::vector<std::string> asset_models;
    bool removed = false;
};

int  AddGenerator(const float pos[3], const std::string& creature_name,
                  uint32_t creature_entity,
                  const std::vector<std::string>& asset_models = {});
void GetGenerators(std::vector<GeneratorAddition>& out);
void MovePendingGenerator(int index, const float pos[3]);
void RemoveGenerator(int index);
void AddGeneratorSpawnPoint(int index, const float pos[3]);
void RemoveGeneratorSpawnPoint(int index, int sp_index);

void AddSpawnPointToExisting(uint32_t generator_entity,
                             uint32_t spawn_points_record,
                             const float pos[3]);
void RemoveSpawnPointFromExisting(uint32_t generator_entity,
                                  uint32_t spawn_points_record,
                                  uint32_t spawn_point_entity);
bool SpawnPointRemovalPending(uint32_t spawn_point_entity);
size_t PendingSpawnPointCount();

struct PendingSpawnPoint {
    int id = 0;
    float pos[3] = {0, 0, 0};
    std::string label;
};
void GetPendingSpawnPoints(std::vector<PendingSpawnPoint>& out);
void MovePendingSpawnPoint(int id, const float pos[3]);
void RemovePendingSpawnPoint(int id);

bool   Dirty();
bool   Saving();
size_t EditedCount();

uint64_t Revision();

void CollectPreviewXforms(
    std::unordered_map<uint32_t, EditXform>& out);

void PushUndoSnapshot(const std::vector<uint32_t>& ids);
bool Undo();

bool Save(std::string& msg);





bool SaveWorkingCopy(std::string& msg);

void ClearEdits();

}
