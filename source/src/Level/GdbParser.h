#pragma once

#include <cstdint>
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
    std::string entity_name;
};

struct GdbInfo {
    std::vector<Placement> placements;
};

GdbInfo Parse(const std::vector<uint8_t>& bytes);

GdbInfo ParseWithSaveMap(
    const std::vector<uint8_t>& bytes,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name);

}
