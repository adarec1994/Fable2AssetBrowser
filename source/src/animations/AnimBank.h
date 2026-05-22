#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Anim {

struct AnimEvent {
    float        time;
    std::string  name;
    std::string  param;
};

struct AnimClip {

    uint32_t key0           = 0;
    uint32_t key1           = 0;

    uint32_t data_offset    = 0;
    uint32_t toc_frame_count = 0;
    uint32_t data_size_bytes = 0;

    
    uint32_t data_length    = 0;

    float    fps            = 0.0f;

    std::string name;

    std::vector<AnimEvent> events;
};

float clip_duration_seconds(const AnimClip& clip);

bool load_toc(const std::filesystem::path& toc_path,
              std::vector<AnimClip>& out_clips);

bool load_toc_bytes(const uint8_t* data, size_t size,
                    std::vector<AnimClip>& out_clips);

bool load_toc_for_root(const std::string& root,
                       std::vector<AnimClip>& out_clips);

size_t resolve_clip_names_from_luas(std::vector<AnimClip>& clips);

}
