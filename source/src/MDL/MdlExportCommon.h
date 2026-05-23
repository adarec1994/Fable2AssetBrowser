#pragma once

#include "ModelParser.h"
#include "../animations/AnimBank.h"
#include "../animations/AnimDataFile.h"
#include "../animations/AnimDecoder.h"
#include "../animations/AnimRigMap.h"
#include "../Utilities/State.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace MdlExport {

inline bool parse_model_for_export(const std::vector<unsigned char>& data,
                                   const std::string& source_path,
                                   MDLInfo& info,
                                   std::vector<MDLMeshGeom>& geoms,
                                   std::string& err_msg) {
    geoms.clear();
    if (!parse_mdl_info(data, info, source_path)) {
        if (!reparse_mdl_missing_buffers_optstr(data, info) &&
            !reparse_mdl_as_foliage_48b(data, info)) {
            err_msg = "Failed to parse MDL info";
            return false;
        }
    }

    auto missing_buffers = [&]() -> size_t {
        size_t n = 0;
        if (info.MeshBuffers.size() < info.MeshCount) {
            n += (size_t)info.MeshCount - info.MeshBuffers.size();
        }
        for (const MDLMeshBufferInfo& mb : info.MeshBuffers) {
            if (mb.VertexCount == 0 || mb.FaceCount == 0) ++n;
        }
        return n;
    };

    auto all_buffers_empty = [&]() {
        if (info.MeshBuffers.empty()) return false;
        for (const MDLMeshBufferInfo& mb : info.MeshBuffers) {
            if (mb.VertexCount > 0 && mb.FaceCount > 0) return false;
        }
        return true;
    };

    if (all_buffers_empty()) {
        reparse_mdl_buffers_via_polymsh_scan(data, info);
    }
    if (missing_buffers() > 0) {
        reparse_mdl_missing_buffers_optstr(data, info);
    }
    if (missing_buffers() > 0) {
        reparse_mdl_as_foliage_48b(data, info);
    }

    std::string p = source_path;
    std::transform(p.begin(), p.end(), p.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    std::replace(p.begin(), p.end(), '\\', '/');
    const bool multi_instance_target =
        p.find("bs_townhouse_basic_snow_v2") != std::string::npos &&
        (p.find("/exterior.mdl") != std::string::npos ||
         p.find("/interior.mdl") != std::string::npos);
    if (multi_instance_target) {
        reparse_mdl_multi_instance_buffers(data, info);
    }

    if (!parse_mdl_geometry(data, info, geoms)) {
        err_msg = "Failed to parse MDL geometry";
        return false;
    }
    if (geoms.empty()) {
        err_msg = "No geometry found";
        return false;
    }
    return true;
}

inline uint32_t model_bone_count_for_export(const MDLInfo& info) {
    if (!info.HasBoneTransforms || info.BoneCount == 0) return 0;
    return std::min<uint32_t>(
        info.BoneCount,
        static_cast<uint32_t>(info.Bones.size()));
}

struct ExportAnimChannel {
    int original_bone = -1;
    int target_node = -1;
    bool is_root = false;
    bool has_rotation = false;
    bool has_translation = false;
    std::vector<float> rotations;
    std::vector<float> translations;
};

struct ExportAnimClip {
    std::string name;
    std::vector<float> times;
    std::vector<ExportAnimChannel> channels;
};

inline std::string safe_clip_name(const Anim::AnimClip& clip, size_t index) {
    if (!clip.name.empty()) return clip.name;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "clip_%zu_%08X_%08X",
                  index, clip.key0, clip.key1);
    return std::string(buf);
}

inline void normalise_quat(float* q) {
    const float len2 = q[0] * q[0] + q[1] * q[1] +
                       q[2] * q[2] + q[3] * q[3];
    if (!std::isfinite(len2) || len2 <= 1e-12f) {
        q[0] = q[1] = q[2] = 0.0f;
        q[3] = 1.0f;
        return;
    }
    const float inv = 1.0f / std::sqrt(len2);
    q[0] *= inv;
    q[1] *= inv;
    q[2] *= inv;
    q[3] *= inv;
}

inline std::vector<size_t> compatible_animation_indices(
    const MDLInfo& info,
    uint32_t model_bone_count,
    const std::string& model_path) {
    std::vector<size_t> out;
    if (model_bone_count == 0 ||
        S.anim_clips.empty() ||
        !Anim::global_data_file().is_open()) {
        return out;
    }

    std::vector<uint8_t> compatible;
    std::vector<uint16_t> matches;
    std::vector<uint16_t> named_tracks;
    Anim::build_rig_compatibility_cache(
        info, model_bone_count, S.anim_clips,
        compatible, matches, named_tracks);

    std::vector<uint8_t> authored;
    size_t authored_count = 0;
    std::string path = model_path.empty() ? S.current_mdl_path : model_path;
    if (!path.empty()) {
        const uint32_t model_hash = Anim::gdb_model_path_hash(path);
        authored_count = Anim::build_model_animation_cache_for_hash(
            model_hash, S.anim_clips.size(), authored);
    }

    out.reserve(S.anim_clips.size());
    for (size_t i = 0; i < S.anim_clips.size(); ++i) {
        if (i >= compatible.size() || !compatible[i]) continue;
        if (authored_count > 0 &&
            (i >= authored.size() || !authored[i])) {
            continue;
        }
        out.push_back(i);
    }
    return out;
}

inline std::vector<ExportAnimClip> collect_compatible_animations(
    const MDLInfo& info,
    uint32_t model_bone_count,
    const std::vector<int>& original_to_target_node,
    const std::string& model_path) {
    std::vector<ExportAnimClip> out;
    if (model_bone_count == 0) return out;

    const std::vector<size_t> candidates =
        compatible_animation_indices(info, model_bone_count, model_path);
    out.reserve(candidates.size());

    for (size_t clip_index : candidates) {
        if (clip_index >= S.anim_clips.size()) continue;
        const Anim::AnimClip& clip = S.anim_clips[clip_index];
        auto h = Anim::global_data_file().parse_clip_header(clip);
        if (!h.ok || h.bone_count == 0 || h.frame_count == 0 ||
            h.bone_count > 4096 || clip.fps <= 0.0f) {
            continue;
        }

        Anim::DecodedPose first_pose;
        if (!Anim::global_decoder().decode(clip, 0.0f, first_pose) ||
            !first_pose.ok) {
            continue;
        }

        const uint32_t decoded_tracks =
            static_cast<uint32_t>(first_pose.bone_quats.size() / 4);
        const uint32_t track_count =
            std::min<uint32_t>(h.bone_count, decoded_tracks);
        if (track_count == 0) continue;

        std::vector<int> track_to_model =
            Anim::resolve_clip_track_model_bones(
                clip, info, model_bone_count, track_count);

        std::vector<int> bone_to_track(model_bone_count, -1);
        for (uint32_t track = 0; track < track_count; ++track) {
            const int model_bone = (track < track_to_model.size())
                ? track_to_model[(size_t)track]
                : -1;
            if (model_bone < 0 ||
                model_bone >= (int)model_bone_count ||
                model_bone >= (int)original_to_target_node.size() ||
                original_to_target_node[(size_t)model_bone] < 0) {
                continue;
            }
            bone_to_track[(size_t)model_bone] = (int)track;
        }

        ExportAnimClip exported;
        exported.name = safe_clip_name(clip, clip_index);
        exported.times.resize(h.frame_count);
        for (uint32_t frame = 0; frame < h.frame_count; ++frame) {
            exported.times[(size_t)frame] = (float)frame / clip.fps;
        }

        for (uint32_t bone = 0; bone < model_bone_count; ++bone) {
            const int track = bone_to_track[(size_t)bone];
            if (track < 0) continue;

            ExportAnimChannel ch;
            ch.original_bone = (int)bone;
            ch.target_node = original_to_target_node[(size_t)bone];
            ch.is_root = (size_t)bone < info.Bones.size() &&
                         info.Bones[(size_t)bone].ParentID < 0;
            ch.has_rotation = true;
            ch.rotations.resize((size_t)h.frame_count * 4, 0.0f);
            if (ch.is_root) {
                ch.translations.resize((size_t)h.frame_count * 3, 0.0f);
            }
            exported.channels.push_back(std::move(ch));
        }
        if (exported.channels.empty()) continue;

        auto write_pose = [&](uint32_t frame, const Anim::DecodedPose& pose) {
            for (ExportAnimChannel& ch : exported.channels) {
                const int track = bone_to_track[(size_t)ch.original_bone];
                if (track < 0) continue;
                const size_t qsrc = (size_t)track * 4;
                const size_t qdst = (size_t)frame * 4;
                if (qsrc + 3 < pose.bone_quats.size()) {
                    float q[4] = {
                        pose.bone_quats[qsrc + 0],
                        pose.bone_quats[qsrc + 1],
                        pose.bone_quats[qsrc + 2],
                        pose.bone_quats[qsrc + 3],
                    };
                    normalise_quat(q);
                    ch.rotations[qdst + 0] = q[0];
                    ch.rotations[qdst + 1] = q[1];
                    ch.rotations[qdst + 2] = q[2];
                    ch.rotations[qdst + 3] = q[3];
                } else {
                    ch.rotations[qdst + 3] = 1.0f;
                }

                if (!ch.is_root || ch.translations.empty()) continue;
                float tx = 0.0f;
                float ty = 0.0f;
                float tz = 0.0f;
                if ((size_t)ch.original_bone < info.BoneTransforms.size() &&
                    info.BoneTransforms[(size_t)ch.original_bone].size() >= 7) {
                    const auto& tf =
                        info.BoneTransforms[(size_t)ch.original_bone];
                    tx = tf[4];
                    ty = tf[5];
                    tz = tf[6];
                }
                const size_t tsrc = (size_t)track * 3;
                bool moved = false;
                if (tsrc + 2 < pose.bone_trans.size()) {
                    const float dx = pose.bone_trans[tsrc + 0];
                    const float dy = pose.bone_trans[tsrc + 1];
                    const float dz = pose.bone_trans[tsrc + 2];
                    tx += dx;
                    ty += dy;
                    tz += dz;
                    moved = (dx * dx + dy * dy + dz * dz) > 1e-10f;
                }
                const size_t tdst = (size_t)frame * 3;
                ch.translations[tdst + 0] = tx;
                ch.translations[tdst + 1] = ty;
                ch.translations[tdst + 2] = tz;
                ch.has_translation = ch.has_translation || moved;
            }
        };

        write_pose(0, first_pose);
        bool ok = true;
        for (uint32_t frame = 1; frame < h.frame_count; ++frame) {
            Anim::DecodedPose pose;
            const float t = (float)frame / clip.fps;
            if (!Anim::global_decoder().decode(clip, t, pose) || !pose.ok) {
                ok = false;
                break;
            }
            write_pose(frame, pose);
        }
        if (!ok) continue;

        for (ExportAnimChannel& ch : exported.channels) {
            if (!ch.has_translation) ch.translations.clear();
        }
        out.push_back(std::move(exported));
    }
    return out;
}

}
