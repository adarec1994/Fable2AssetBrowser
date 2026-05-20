#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Anim {

struct LocomotionClipRecord {
    std::string name;
};

struct LocomotionStateRecord {
    uint32_t hash = 0;
    std::string primary_anim;
    std::string secondary_anim;
};

struct LocomotionSummary {
    uint32_t state_count = 0;
    uint32_t clip_count = 0;
    uint32_t ptr_count = 0;
    uint32_t string_size = 0;
    std::vector<LocomotionStateRecord> states;
    std::vector<LocomotionClipRecord> clips;
};

bool load_locomotion(const std::filesystem::path& path,
                     LocomotionSummary* out_summary = nullptr);

bool load_locomotion_bytes(const uint8_t* data, size_t size,
                           LocomotionSummary* out_summary = nullptr);

bool load_locomotion_for_root(const std::string& root,
                              LocomotionSummary* out_summary = nullptr);

}
