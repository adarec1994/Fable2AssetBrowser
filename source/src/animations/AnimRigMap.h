#pragma once

#include "AnimBank.h"
#include "AnimDataFile.h"
#include "../MDL/ModelParser.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Anim {

using BoneNameLookup = std::unordered_map<std::string, uint32_t>;
using BoneNameCandidateLookup =
    std::unordered_map<std::string, std::vector<uint32_t>>;

inline uint64_t hash_mix_u64(uint64_t h, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (i * 8)) & 0xFFu;
        h *= 1099511628211ull;
    }
    return h;
}

inline uint64_t hash_mix_string(uint64_t h, std::string_view s) {
    for (char ch : s) {
        h ^= static_cast<uint8_t>(ch);
        h *= 1099511628211ull;
    }
    h ^= 0xFFu;
    h *= 1099511628211ull;
    return h;
}

inline std::string normalise_bone_name(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (char ch : name) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (c == ' ' || c == '-' || c == '.') c = '_';
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    while (!out.empty() && out.front() == '_') out.erase(out.begin());
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

inline std::string without_shadow_prefix(const std::string& name) {
    constexpr const char* kPrefix = "shadow_";
    constexpr size_t kPrefixLen = 7;
    if (name.size() > kPrefixLen &&
        name.compare(0, kPrefixLen, kPrefix) == 0) {
        return name.substr(kPrefixLen);
    }
    return name;
}

inline void add_bone_alias(BoneNameLookup& lookup,
                           const std::string& alias,
                           uint32_t index) {
    if (alias.empty()) return;
    lookup.emplace(alias, index);
}

inline void add_bone_candidate(BoneNameCandidateLookup& lookup,
                               const std::string& alias,
                               uint32_t index) {
    if (alias.empty()) return;
    auto& v = lookup[alias];
    if (std::find(v.begin(), v.end(), index) == v.end()) {
        v.push_back(index);
    }
}

inline BoneNameLookup build_model_bone_lookup(const MDLInfo& info,
                                              uint32_t max_bones) {
    BoneNameLookup lookup;
    const uint32_t n = std::min<uint32_t>(
        max_bones,
        static_cast<uint32_t>(info.Bones.size()));
    lookup.reserve((size_t)n * 3);
    for (uint32_t i = 0; i < n; ++i) {
        std::string norm = normalise_bone_name(info.Bones[i].Name);
        add_bone_alias(lookup, norm, i);
        add_bone_alias(lookup, without_shadow_prefix(norm), i);
        add_bone_alias(lookup, "shadow_" + without_shadow_prefix(norm), i);
    }
    return lookup;
}

inline BoneNameCandidateLookup build_model_bone_candidate_lookup(
    const MDLInfo& info,
    uint32_t max_bones) {
    BoneNameCandidateLookup lookup;
    const uint32_t n = std::min<uint32_t>(
        max_bones,
        static_cast<uint32_t>(info.Bones.size()));
    lookup.reserve((size_t)n * 3);
    for (uint32_t i = 0; i < n; ++i) {
        std::string norm = normalise_bone_name(info.Bones[i].Name);
        const std::string base = without_shadow_prefix(norm);
        add_bone_candidate(lookup, norm, i);
        add_bone_candidate(lookup, base, i);
        add_bone_candidate(lookup, "shadow_" + base, i);
    }
    return lookup;
}

inline bool is_locator_track_name(std::string_view track_name) {
    const std::string norm = normalise_bone_name(track_name);
    return norm.empty() || norm == "locator";
}

inline bool track_candidate_already_used(const std::vector<int>& resolved,
                                         uint32_t track_index,
                                         uint32_t candidate);

inline int first_model_root_bone(const MDLInfo& info,
                                 uint32_t model_bone_count,
                                 const std::vector<int>& resolved,
                                 uint32_t track_index) {
    const uint32_t n = std::min<uint32_t>(
        model_bone_count,
        static_cast<uint32_t>(info.Bones.size()));
    for (uint32_t i = 0; i < n; ++i) {
        if (info.Bones[(size_t)i].ParentID >= 0) continue;
        if (!track_candidate_already_used(resolved, track_index, i)) {
            return (int)i;
        }
    }
    for (uint32_t i = 0; i < n; ++i) {
        if (info.Bones[(size_t)i].ParentID < 0) return (int)i;
    }
    return -1;
}

inline std::vector<uint32_t> model_bone_candidates_for_track_name(
    const BoneNameCandidateLookup& lookup,
    std::string_view track_name) {
    std::vector<uint32_t> out;
    auto append = [&](const std::string& key) {
        auto it = lookup.find(key);
        if (it == lookup.end()) return;
        for (uint32_t index : it->second) {
            if (std::find(out.begin(), out.end(), index) == out.end()) {
                out.push_back(index);
            }
        }
    };

    std::string norm = normalise_bone_name(track_name);
    append(norm);
    norm = without_shadow_prefix(norm);
    append(norm);
    append("shadow_" + norm);
    return out;
}

inline int model_bone_for_track_name(const BoneNameLookup& lookup,
                                     std::string_view track_name) {
    std::string norm = normalise_bone_name(track_name);
    auto it = lookup.find(norm);
    if (it != lookup.end()) return static_cast<int>(it->second);

    norm = without_shadow_prefix(norm);
    it = lookup.find(norm);
    if (it != lookup.end()) return static_cast<int>(it->second);

    it = lookup.find("shadow_" + norm);
    if (it != lookup.end()) return static_cast<int>(it->second);
    return -1;
}

inline bool track_candidate_already_used(const std::vector<int>& resolved,
                                         uint32_t track_index,
                                         uint32_t candidate) {
    for (uint32_t i = 0; i < resolved.size(); ++i) {
        if (i != track_index && resolved[i] == (int)candidate) return true;
    }
    return false;
}

inline int choose_model_bone_for_track(
    const AnimTrackBone& track,
    uint32_t track_index,
    const MDLInfo& info,
    uint32_t model_bone_count,
    const BoneNameCandidateLookup& lookup,
    const std::vector<int>& resolved) {
    if (is_locator_track_name(track.name)) {
        return track.parent < 0
            ? first_model_root_bone(info, model_bone_count,
                                    resolved, track_index)
            : -1;
    }

    std::vector<uint32_t> candidates =
        model_bone_candidates_for_track_name(lookup, track.name);
    if (candidates.empty()) return -1;
    if (candidates.size() == 1) return (int)candidates[0];

    int expected_parent = -2;
    if (track.parent < 0) {
        expected_parent = -1;
    } else if ((uint32_t)track.parent < resolved.size() &&
               resolved[(size_t)track.parent] >= 0) {
        expected_parent = resolved[(size_t)track.parent];
    }

    auto parent_matches = [&](uint32_t candidate) {
        if (expected_parent == -2 ||
            (size_t)candidate >= info.Bones.size()) {
            return false;
        }
        return info.Bones[(size_t)candidate].ParentID == expected_parent;
    };

    for (uint32_t candidate : candidates) {
        if (parent_matches(candidate) &&
            !track_candidate_already_used(resolved, track_index, candidate)) {
            return (int)candidate;
        }
    }
    for (uint32_t candidate : candidates) {
        if (parent_matches(candidate)) return (int)candidate;
    }
    for (uint32_t candidate : candidates) {
        if (!track_candidate_already_used(resolved, track_index, candidate)) {
            return (int)candidate;
        }
    }
    return (int)candidates.front();
}

inline std::vector<int> resolve_clip_track_model_bones(
    const AnimClip& clip,
    const MDLInfo& info,
    uint32_t model_bone_count,
    uint32_t track_count) {
    std::vector<int> resolved(track_count, -1);
    if (track_count == 0 || model_bone_count == 0) return resolved;

    const bool has_track_map = clip.track_map && !clip.track_map->empty();
    if (!has_track_map) {
        for (uint32_t i = 0; i < track_count && i < model_bone_count; ++i) {
            resolved[(size_t)i] = (int)i;
        }
        return resolved;
    }

    BoneNameCandidateLookup lookup =
        build_model_bone_candidate_lookup(info, model_bone_count);
    const uint32_t mapped_tracks = std::min<uint32_t>(
        track_count,
        static_cast<uint32_t>(clip.track_map->size()));
    for (uint32_t pass = 0; pass < 3; ++pass) {
        bool changed = false;
        for (uint32_t i = 0; i < mapped_tracks; ++i) {
            const int picked = choose_model_bone_for_track(
                (*clip.track_map)[(size_t)i], i, info, model_bone_count,
                lookup, resolved);
            if (resolved[(size_t)i] != picked) {
                resolved[(size_t)i] = picked;
                changed = true;
            }
        }
        if (!changed) break;
    }
    return resolved;
}

inline size_t count_track_model_matches(const AnimClip& clip,
                                        const BoneNameLookup& lookup,
                                        size_t* out_named_tracks = nullptr) {
    if (out_named_tracks) *out_named_tracks = 0;
    if (!clip.track_map) return 0;

    size_t named = 0;
    size_t matched = 0;
    for (const AnimTrackBone& track : *clip.track_map) {
        std::string norm = normalise_bone_name(track.name);
        if (norm.empty() || norm == "locator") continue;
        ++named;
        if (model_bone_for_track_name(lookup, track.name) >= 0) ++matched;
    }
    if (out_named_tracks) *out_named_tracks = named;
    return matched;
}

inline bool track_map_matches_model(const AnimClip& clip,
                                    const BoneNameLookup& lookup,
                                    uint32_t model_bone_count,
                                    size_t* out_matched = nullptr,
                                    size_t* out_named_tracks = nullptr) {
    size_t named = 0;
    size_t matched = count_track_model_matches(clip, lookup, &named);
    if (out_matched) *out_matched = matched;
    if (out_named_tracks) *out_named_tracks = named;
    if (named == 0 || model_bone_count == 0) return false;

    const size_t comparable = std::min<size_t>(named, model_bone_count);
    const size_t required = std::max<size_t>(3, (comparable * 2 + 2) / 3);
    return matched >= required;
}

inline uint64_t rig_compatibility_signature(
    const MDLInfo& info,
    uint32_t model_bone_count,
    const std::vector<AnimClip>& clips,
    bool data_file_open) {
    uint64_t h = 1469598103934665603ull;
    h = hash_mix_u64(h, model_bone_count);
    h = hash_mix_u64(h, clips.size());
    h = hash_mix_u64(h, data_file_open ? 1u : 0u);

    const uint32_t n = std::min<uint32_t>(
        model_bone_count,
        static_cast<uint32_t>(info.Bones.size()));
    for (uint32_t i = 0; i < n; ++i) {
        h = hash_mix_string(h, info.Bones[i].Name);
        h = hash_mix_u64(h, static_cast<uint32_t>(info.Bones[i].ParentID));
    }

    for (const AnimClip& clip : clips) {
        h = hash_mix_u64(h, clip.key0);
        h = hash_mix_u64(h, clip.key1);
        h = hash_mix_u64(h, clip.track_map ? clip.track_map->size() : 0u);
        h = hash_mix_u64(h, reinterpret_cast<uintptr_t>(clip.track_map.get()));
    }
    return h ? h : 1ull;
}

inline void build_rig_compatibility_cache(
    const MDLInfo& info,
    uint32_t model_bone_count,
    const std::vector<AnimClip>& clips,
    std::vector<uint8_t>& out_compatible,
    std::vector<uint16_t>& out_matches,
    std::vector<uint16_t>& out_named_tracks) {
    out_compatible.assign(clips.size(), 0);
    out_matches.assign(clips.size(), 0);
    out_named_tracks.assign(clips.size(), 0);
    if (model_bone_count == 0 || clips.empty()) return;

    BoneNameLookup lookup = build_model_bone_lookup(info, model_bone_count);
    const bool have_data_file = global_data_file().is_open();

    for (size_t i = 0; i < clips.size(); ++i) {
        const AnimClip& clip = clips[i];
        size_t named = 0;
        size_t matched = 0;
        const bool rig_match = track_map_matches_model(
            clip, lookup, model_bone_count, &matched, &named);

        bool count_match = false;
        if (have_data_file) {
            auto h = global_data_file().parse_clip_header(clip);
            count_match = h.ok && h.bone_count == model_bone_count;
        }

        out_compatible[i] = (rig_match || count_match) ? 1u : 0u;
        out_matches[i] = static_cast<uint16_t>(
            std::min<size_t>(matched, UINT16_MAX));
        out_named_tracks[i] = static_cast<uint16_t>(
            std::min<size_t>(named, UINT16_MAX));
    }
}

}
