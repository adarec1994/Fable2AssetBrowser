#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace Gdb {

struct Placement {
    float    x, y, z;
    float    yaw;
    float    rot_x, rot_y, rot_z;
    float    scale;
    bool     has_rotation;
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

struct DebugNode {
    std::string label;
    std::string value;
    std::vector<DebugNode> children;
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
    std::vector<DebugNode> debug_tree;
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

std::string DebugDumpRecordChains(
    const std::vector<uint8_t>& bytes,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name,
    const std::vector<uint32_t>& target_hashes,
    size_t max_records_per_hash = 8,
    size_t max_parent_depth = 12);

std::vector<DebugNode> BuildDebugTreeForHash(
    const std::vector<uint8_t>& bytes,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name,
    uint32_t target_hash,
    size_t max_depth = 6,
    size_t max_fields_per_record = 80);

std::vector<RecordRow> Build010RecordRows(
    const std::vector<uint8_t>& bytes,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name,
    size_t max_depth = 6,
    size_t max_fields_per_record = 80);

}
