#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Anim {

struct AnimEvent {
    float        time;
    std::string  name;
    std::string  param;
};

struct AnimTrackBone {
    std::string name;
    int32_t     parent = -1;
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

    std::shared_ptr<const std::vector<AnimTrackBone>> track_map;
};

struct ModelAnimationBinding {
    uint32_t model_path_hash = 0;
    uint32_t skeleton_file_hash = 0;
    uint32_t retarget_skeleton_file_hash = 0;
    uint32_t animation_record_hash = 0;
    uint32_t animation_key = 0;
    uint32_t source_record_hash = 0;
    size_t   clip_index = 0;
    std::string animation_name;
    std::string source_name;
};

float clip_duration_seconds(const AnimClip& clip);

bool load_toc(const std::filesystem::path& toc_path,
              std::vector<AnimClip>& out_clips);

bool load_toc_bytes(const uint8_t* data, size_t size,
                    std::vector<AnimClip>& out_clips);

bool load_toc_for_root(const std::string& root,
                       std::vector<AnimClip>& out_clips);

size_t resolve_clip_names_from_luas(std::vector<AnimClip>& clips);

size_t resolve_clip_names_from_gdb_animation_fields_for_root(
    const std::string& root,
    std::vector<AnimClip>& clips);

uint32_t gdb_model_path_hash(std::string path);

uint64_t model_animation_binding_revision();

size_t build_model_animation_cache_for_hash(
    uint32_t model_path_hash,
    size_t clip_count,
    std::vector<uint8_t>& out_authored);

size_t model_animation_binding_count_for_hash(uint32_t model_path_hash);

const std::vector<ModelAnimationBinding>& model_animation_bindings();

}
