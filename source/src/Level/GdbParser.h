#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "Skybox/EnvironmentThemeParser.h"

namespace Gdb {

struct Placement {
    float    x, y, z;
    float    yaw;
    float    rot_x, rot_y, rot_z;
    float    scale;
    bool     has_rotation;
    uint32_t pos_value_off[3] = {0, 0, 0};
    uint32_t rot_value_off[3] = {0, 0, 0};
    uint32_t marker;
    uint32_t hash_a;
    uint32_t parent_hash;
    uint32_t model_path_hash;
    uint32_t skeleton_file_hash;
    uint32_t retarget_skeleton_file_hash;
    std::vector<uint32_t> model_path_hashes;
    bool     indexed_record;
    bool     transform_from_indexed_record;
    std::string entity_name;
};

struct GdbInfo {
    std::vector<Placement> placements;
};

struct RecordRow {
    uint32_t index = 0;
    uint32_t hash = 0;
    uint32_t parent_hash = 0;
    uint32_t model_path_hash = 0;
    uint32_t skeleton_file_hash = 0;
    uint32_t retarget_skeleton_file_hash = 0;
    std::vector<uint32_t> model_path_hashes;
    std::string name;
    std::string skeleton_file_name;
    std::string retarget_skeleton_file_name;
};

GdbInfo Parse(const std::vector<uint8_t>& bytes);

GdbInfo ParseWithSaveMap(
    const std::vector<uint8_t>& bytes,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name);

bool LookupModelPathHash(
    const std::vector<uint8_t>& bytes,
    uint32_t record_hash,
    uint32_t& out_model_path_hash,
    uint32_t* out_parent_hash = nullptr);

bool LookupModelPathHashes(
    const std::vector<uint8_t>& bytes,
    uint32_t record_hash,
    std::vector<uint32_t>& out_model_path_hashes,
    uint32_t* out_parent_hash = nullptr);

bool LookupPlacement(
    const std::vector<uint8_t>& bytes,
    uint32_t record_hash,
    const std::string& entity_name,
    Placement& out_placement);

std::vector<RecordRow> Build010RecordRows(
    const std::vector<uint8_t>& bytes,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name);

struct ParticleFxBinding {
    uint32_t fx_hash_lower = 0;
    std::string fx_name;
};

struct ParticleAttachmentBinding {
    uint32_t fx_hash_lower = 0;
    std::string fx_name;
    uint32_t component_hash = 0;
    uint32_t emitter_record_hash = 0;
    uint32_t dummy_hash = 0;
    std::string dummy_name;
    float offset[3] = {0.0f, 0.0f, 0.0f};
    float max_visibility_distance = 0.0f;
    bool override_max_visibility_distance = false;
    bool disable_when_parent_invisible = false;
    bool orient_to_attachment_point = false;
};

std::unordered_map<uint32_t, std::vector<ParticleFxBinding>>
ExtractParticleFxBindings(
    const std::vector<const std::vector<uint8_t>*>& gdbs);

std::unordered_map<uint32_t, std::vector<ParticleAttachmentBinding>>
ExtractParticleAttachmentBindings(
    const std::vector<const std::vector<uint8_t>*>& gdbs);

struct FxEntityPlacement {
    std::string name;
    uint32_t record_hash = 0;
    float x = 0, y = 0, z = 0;
    float rot_x = 0, rot_y = 0, rot_z = 0;
    bool  has_rotation = false;
    uint32_t fx_hash = 0;
};

std::vector<FxEntityPlacement> ExtractFxEntityPlacements(
    const std::vector<uint8_t>& level_bytes,
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name);

struct EntityContentsItem {
    uint32_t record_hash = 0;
    std::string entry_label;
    std::string name_tag;
    uint32_t name_tag_hash = 0;   // NameTag hash for book.babel resolution
    std::string display_name;     // localized name (resolved app-side)
    int money = -1;
    float weight = -1.0f;
    float min_chapter = -1.0f;
    float max_chapter = -1.0f;
};

struct EntityContents {
    std::string entity_name;
    bool has_chest_component = false;
    int  silver_keys_needed = -1;
    float chance_of_respawning = -1.0f;
    uint32_t potential_items_record = 0;
    std::vector<EntityContentsItem> initial_items;
    std::vector<EntityContentsItem> potential_items;
};

std::unordered_map<uint32_t, EntityContents> ExtractEntityContents(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name);

struct ItemCatalogEntry {
    uint32_t record_hash = 0;
    std::string label;
    int money = -1;
    bool from_level = false;
    bool unnamed = false;
};

std::vector<ItemCatalogEntry> BuildItemCatalog(
    const std::vector<const std::vector<uint8_t>*>& gdbs);

struct ItemDetail {
    uint32_t record_hash = 0;
    std::string label;          // tag-derived fallback label
    std::string display_name;   // localized name (resolved app-side)
    uint32_t name_tag = 0;
    uint32_t desc_tag = 0;
    uint32_t model_path_hash = 0;
    std::string model_path;     // resolved .mdl path string when known
    std::string icon_tex;
    int money = -1;
    bool unnamed = false;
    bool is_money = false;       // a preset gold/money-bag loot amount
    std::vector<std::pair<std::string, std::string>> stats;
};

std::vector<ItemDetail> BuildItemDetails(
    const std::vector<const std::vector<uint8_t>*>& gdbs);

std::unordered_map<uint32_t, std::string> LoadEmbeddedDict(
    const std::vector<uint8_t>& bytes);

struct PropTemplateInfo {
    uint32_t template_hash = 0;
    uint32_t comp_field_hash = 0;
    uint32_t comp_template_hash = 0;
    uint32_t physics_file_hash = 0;
    bool has_text_tags = false;
};

std::unordered_map<uint32_t, PropTemplateInfo> BuildPropTemplateIndex(
    const std::vector<const std::vector<uint8_t>*>& gdbs);

struct EntityTextTags {
    uint32_t tags_record_hash = 0;
    bool tags_record_in_level_gdb = false;
    bool has_pos = false;
    float x = 0, y = 0, z = 0;
    std::vector<uint32_t> tag_hashes;
};

std::unordered_map<uint32_t, EntityTextTags> ExtractEntityTextTags(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name);

struct SpawnEntityInfo {
    uint8_t kind = 0;
    bool has_pos = false;
    float x = 0, y = 0, z = 0;
    std::vector<uint32_t> model_hashes;
    uint32_t pos_off[3] = {0, 0, 0};
    uint32_t rot_off[3] = {0, 0, 0};
    uint32_t spawn_points_record = 0;
    std::vector<uint32_t> spawn_point_entities;
    std::string creature_name;
};

struct SpawnDonorInfo {
    uint32_t gen_template = 0;
    uint32_t gen_comp_field = 0;
    uint32_t gen_comp_parent = 0;
    uint32_t gen_transform_field = 0;
    uint32_t gen_transform_parent = 0;
    uint32_t gen_position_parent = 0;
    uint32_t gen_rotation_parent = 0;
    uint32_t sp_template = 0;
    uint32_t sp_comp_field = 0;
    uint32_t sp_comp_parent = 0;
    uint32_t sp_transform_field = 0;
    uint32_t sp_transform_parent = 0;
    uint32_t sp_position_parent = 0;
    uint32_t sp_rotation_parent = 0;
    uint32_t spawn_list_parent = 0;
    bool valid() const {
        return gen_template && gen_comp_field && sp_template &&
               sp_comp_field && gen_transform_field &&
               sp_transform_field;
    }
};
std::unordered_map<uint32_t, SpawnEntityInfo> CollectSpawnEntities(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name,
    SpawnDonorInfo* out_donor = nullptr);

struct CreatureCatalogEntry {
    std::string name;
    uint32_t entity_hash = 0;
    std::vector<uint32_t> model_hashes;
};
std::vector<CreatureCatalogEntry> CollectCreatureNames(
    const std::vector<const std::vector<uint8_t>*>& gdbs);

}
