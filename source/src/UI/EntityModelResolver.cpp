#include "EntityModelResolver.h"

#include "../Level/Core/LevelLoader.h"
#include "../Utilities/State.h"
#include "../animations/AnimRigMap.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace EntityModels {
namespace {

enum class EyeSide { None, Left, Right };

EyeSide eye_side_from_path(std::string path)
{
    std::transform(path.begin(), path.end(), path.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    std::replace(path.begin(), path.end(), '\\', '/');
    const size_t slash = path.find_last_of('/');
    std::string leaf = slash == std::string::npos
        ? path : path.substr(slash + 1);
    const size_t dot = leaf.rfind('.');
    if (dot != std::string::npos) leaf.resize(dot);
    if (leaf.find("eye") == std::string::npos) return EyeSide::None;
    if (leaf.find("left") != std::string::npos ||
        (leaf.size() >= 2 && leaf.compare(leaf.size() - 2, 2, "_l") == 0)) {
        return EyeSide::Left;
    }
    if (leaf.find("right") != std::string::npos ||
        (leaf.size() >= 2 && leaf.compare(leaf.size() - 2, 2, "_r") == 0)) {
        return EyeSide::Right;
    }
    return EyeSide::None;
}

int named_bone(const MDLInfo& info,
               const std::vector<std::string>& candidates)
{
    if (info.Bones.empty()) return -1;
    const auto lookup = Anim::build_model_bone_lookup(
        info, static_cast<uint32_t>(info.Bones.size()));
    for (const std::string& candidate : candidates) {
        const int bone = Anim::model_bone_for_track_name(lookup, candidate);
        if (bone >= 0) return bone;
    }
    return -1;
}

int eye_bone(const MDLInfo& info, EyeSide side)
{
    if (side == EyeSide::None || info.Bones.empty()) return -1;
    static constexpr const char* kLeftCandidates[] = {
        "L_Eye", "Left_Eye", "LeftEye", "Character.Focal.Eye.Left",
        "Focal_Eye_DummyObject1",
    };
    static constexpr const char* kRightCandidates[] = {
        "R_Eye", "Right_Eye", "RightEye", "Character.Focal.Eye.Right",
        "Focal_Eye_DummyObject",
    };
    std::vector<std::string> candidates;
    if (side == EyeSide::Left) {
        candidates.assign(std::begin(kLeftCandidates),
                          std::end(kLeftCandidates));
    } else {
        candidates.assign(std::begin(kRightCandidates),
                          std::end(kRightCandidates));
    }
    return named_bone(info, candidates);
}

void rotate_vector(float& x, float& y, float& z,
                   const std::vector<float>& transform)
{
    if (transform.size() < 4) return;
    float qx = transform[0];
    float qy = transform[1];
    float qz = transform[2];
    float qw = transform[3];
    const float qlen = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    if (qlen <= 1.0e-8f) return;
    qx /= qlen;
    qy /= qlen;
    qz /= qlen;
    qw /= qlen;

    const float cx = qy * z - qz * y;
    const float cy = qz * x - qx * z;
    const float cz = qx * y - qy * x;
    const float ccx = qy * cz - qz * cy;
    const float ccy = qz * cx - qx * cz;
    const float ccz = qx * cy - qy * cx;
    x += 2.0f * (qw * cx + ccx);
    y += 2.0f * (qw * cy + ccy);
    z += 2.0f * (qw * cz + ccz);
}

bool attach_rigid_mesh(MDLMeshGeom& mesh, const MDLInfo& rig, int bone,
                       bool apply_bind_orientation)
{
    if (bone < 0 || static_cast<size_t>(bone) >= rig.Bones.size() ||
        rig.BoneTransforms.size() != rig.Bones.size()) {
        return false;
    }
    std::vector<int> chain;
    std::unordered_set<int> visited;
    for (int current = bone;
         current >= 0 && static_cast<size_t>(current) < rig.Bones.size() &&
         visited.insert(current).second;
         current = rig.Bones[static_cast<size_t>(current)].ParentID) {
        chain.push_back(current);
    }
    if (chain.empty()) return false;



    if (apply_bind_orientation) {
        for (size_t i = 0; i + 2 < mesh.positions.size(); i += 3) {
            float& x = mesh.positions[i + 0];
            float& y = mesh.positions[i + 1];
            float& z = mesh.positions[i + 2];
            for (int current : chain) {
                const auto& tf = rig.BoneTransforms[static_cast<size_t>(current)];
                if (tf.size() < 10) continue;
                x *= tf[7];
                y *= tf[8];
                z *= tf[9];
                rotate_vector(x, y, z, tf);
                x += tf[4];
                y += tf[5];
                z += tf[6];
            }
        }
        for (size_t i = 0; i + 2 < mesh.normals.size(); i += 3) {
            float& x = mesh.normals[i + 0];
            float& y = mesh.normals[i + 1];
            float& z = mesh.normals[i + 2];
            for (int current : chain) {
                const auto& tf = rig.BoneTransforms[static_cast<size_t>(current)];
                if (tf.size() < 10) continue;
                if (std::fabs(tf[7]) > 1.0e-7f) x /= tf[7];
                if (std::fabs(tf[8]) > 1.0e-7f) y /= tf[8];
                if (std::fabs(tf[9]) > 1.0e-7f) z /= tf[9];
                rotate_vector(x, y, z, tf);
            }
            const float length = std::sqrt(x * x + y * y + z * z);
            if (length > 1.0e-7f) {
                x /= length;
                y /= length;
                z /= length;
            }
        }
    } else {
        float origin_x = 0.0f;
        float origin_y = 0.0f;
        float origin_z = 0.0f;
        for (int current : chain) {
            const auto& tf = rig.BoneTransforms[static_cast<size_t>(current)];
            if (tf.size() < 10) continue;
            origin_x *= tf[7];
            origin_y *= tf[8];
            origin_z *= tf[9];
            rotate_vector(origin_x, origin_y, origin_z, tf);
            origin_x += tf[4];
            origin_y += tf[5];
            origin_z += tf[6];
        }
        for (size_t i = 0; i + 2 < mesh.positions.size(); i += 3) {
            mesh.positions[i + 0] += origin_x;
            mesh.positions[i + 1] += origin_y;
            mesh.positions[i + 2] += origin_z;
        }
    }

    const size_t vertex_count = mesh.positions.size() / 3;
    mesh.bone_ids.assign(vertex_count * 4, 0);
    mesh.bone_weights.assign(vertex_count * 4, 0.0f);
    for (size_t vertex = 0; vertex < vertex_count; ++vertex) {
        mesh.bone_ids[vertex * 4] = static_cast<uint16_t>(bone);
        mesh.bone_weights[vertex * 4] = 1.0f;
    }
    return true;
}

void remap_part_bones(std::vector<MDLMeshGeom>& meshes,
                      const MDLInfo& part,
                      const MDLInfo& rig)
{
    if (part.Bones.empty() || rig.Bones.empty()) return;
    const auto lookup = Anim::build_model_bone_lookup(
        rig, static_cast<uint32_t>(rig.Bones.size()));
    std::vector<int> direct(part.Bones.size(), -1);
    for (size_t i = 0; i < part.Bones.size(); ++i) {
        const int target = Anim::model_bone_for_track_name(
            lookup, part.Bones[i].Name);
        if (target >= 0) direct[i] = target;
    }
    std::vector<uint16_t> remap(part.Bones.size(), 0);
    for (size_t i = 0; i < part.Bones.size(); ++i) {
        int target = direct[i];
        int ancestor = part.Bones[i].ParentID;
        std::unordered_set<int> visited;
        while (target < 0 && ancestor >= 0 &&
               ancestor < static_cast<int>(part.Bones.size()) &&
               visited.insert(ancestor).second) {
            target = direct[static_cast<size_t>(ancestor)];
            ancestor = part.Bones[static_cast<size_t>(ancestor)].ParentID;
        }
        if (target >= 0) remap[i] = static_cast<uint16_t>(target);
    }
    for (MDLMeshGeom& mesh : meshes) {
        for (uint16_t& bone : mesh.bone_ids) {
            bone = static_cast<size_t>(bone) < remap.size()
                ? remap[bone] : uint16_t(0);
        }
    }
}

void merge_part_skeleton(MDLInfo& rig, const MDLInfo& part)
{
    if (part.Bones.empty() ||
        part.BoneTransforms.size() != part.Bones.size() ||
        rig.BoneTransforms.size() != rig.Bones.size()) {
        return;
    }
    const auto lookup = Anim::build_model_bone_lookup(
        rig, static_cast<uint32_t>(rig.Bones.size()));
    std::vector<int> remap(part.Bones.size(), -1);
    std::vector<bool> appended(part.Bones.size(), false);
    for (size_t i = 0; i < part.Bones.size(); ++i) {
        remap[i] = Anim::model_bone_for_track_name(lookup,
                                                    part.Bones[i].Name);
        if (remap[i] >= 0) continue;
        remap[i] = static_cast<int>(rig.Bones.size());
        rig.Bones.push_back({part.Bones[i].Name, -1});
        rig.BoneTransforms.push_back(part.BoneTransforms[i]);
        appended[i] = true;
    }
    for (size_t i = 0; i < part.Bones.size(); ++i) {
        if (!appended[i]) continue;
        const int parent = part.Bones[i].ParentID;
        if (parent >= 0 && static_cast<size_t>(parent) < remap.size()) {
            rig.Bones[static_cast<size_t>(remap[i])].ParentID =
                remap[static_cast<size_t>(parent)];
        }
    }
    rig.BoneCount = static_cast<uint32_t>(rig.Bones.size());
    rig.BoneTransformCount =
        static_cast<uint32_t>(rig.BoneTransforms.size());
    rig.HasBoneTransforms = !rig.Bones.empty();
}

}

bool Resolve(const std::vector<std::uint32_t>& model_hashes,
             ResolvedModel& out,
             std::string* error,
             const ResolveOptions* options)
{
    out = ResolvedModel{};
    struct LoadedPart {
        MDLInfo info;
        std::vector<MDLMeshGeom> meshes;
        std::string path;
        uint32_t hash = 0;
        size_t geometry_score = 0;
    };

    std::vector<LoadedPart> parts;
    std::unordered_set<std::string> seen_paths;
    for (uint32_t hash : model_hashes) {
        const FlatAssetEntry* model = FindGlobalModelAssetByPathHash(hash);
        if (!model || model->full_path.empty()) continue;
        std::string key = model->full_path;
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        std::replace(key.begin(), key.end(), '/', '\\');
        if (!seen_paths.insert(key).second) continue;

        std::vector<unsigned char> bytes;
        bool loaded = false;
        try {
            loaded = build_mdl_buffer_for_name(model->full_path, bytes);
        } catch (...) {
            loaded = false;
        }
        if (!loaded || bytes.empty()) continue;

        MDLInfo info;
        bool parsed = parse_mdl_info(bytes, info, model->full_path);
        if (!parsed && info.MeshCount > 0) {
            parsed = reparse_mdl_missing_buffers_optstr(bytes, info);
        }
        if (!parsed) parsed = reparse_mdl_as_foliage_48b(bytes, info);
        if (!parsed) continue;

        std::vector<MDLMeshGeom> meshes;
        if (!build_mdl_engine_geometry(bytes, meshes) || meshes.empty()) {
            meshes.clear();
            if (!parse_mdl_geometry(bytes, info, meshes) || meshes.empty()) {
                continue;
            }
        }
        for (MDLMeshGeom& mesh : meshes) {
            mesh.source_model_hash = hash;
        }
        size_t geometry_score = 0;
        for (const MDLMeshGeom& mesh : meshes) {
            geometry_score += mesh.positions.size();
            geometry_score += mesh.indices.size();
        }
        parts.push_back({std::move(info), std::move(meshes),
                         model->full_path, hash, geometry_score});
    }

    if (parts.empty()) {
        if (error) *error = "no authored model parts resolved";
        return false;
    }

    size_t primary = parts.size();
    if (options && options->preferred_primary_hash) {
        for (size_t i = 0; i < parts.size(); ++i) {
            if (parts[i].hash == options->preferred_primary_hash) {
                primary = i;
                break;
            }
        }
    }
    if (primary >= parts.size()) {
        primary = 0;
        for (size_t i = 1; i < parts.size(); ++i) {
            if (parts[i].geometry_score > parts[primary].geometry_score) {
                primary = i;
            }
        }
    }
    MDLInfo rig = parts[primary].info;
    out.primary_model_path = parts[primary].path;
    out.primary_model_hash = parts[primary].hash;

    auto is_attachment = [&](const LoadedPart& part) {
        return options && std::any_of(
            options->attachments.begin(), options->attachments.end(),
            [&](const Attachment& item) {
                return item.model_hash == part.hash;
            });
    };
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i == primary ||
            eye_side_from_path(parts[i].path) != EyeSide::None ||
            is_attachment(parts[i])) {
            continue;
        }
        merge_part_skeleton(rig, parts[i].info);
    }

    for (size_t i = 0; i < parts.size(); ++i) {
        LoadedPart& part = parts[i];
        const EyeSide side = eye_side_from_path(part.path);
        if (side != EyeSide::None) {
            const int bone = eye_bone(rig, side);
            for (MDLMeshGeom& mesh : part.meshes) {
                attach_rigid_mesh(mesh, rig, bone, false);
            }
        } else if (options) {
            const auto attachment = std::find_if(
                options->attachments.begin(), options->attachments.end(),
                [&](const Attachment& item) {
                    return item.model_hash == part.hash;
                });
            if (attachment != options->attachments.end()) {
                const int bone = named_bone(rig,
                                             attachment->bone_candidates);
                for (MDLMeshGeom& mesh : part.meshes) {
                    attach_rigid_mesh(mesh, rig, bone, true);
                }
            } else if (i != primary) {
                remap_part_bones(part.meshes, part.info, rig);
            }
        } else if (i != primary) {
            remap_part_bones(part.meshes, part.info, rig);
        }
        out.meshes.insert(out.meshes.end(),
                          std::make_move_iterator(part.meshes.begin()),
                          std::make_move_iterator(part.meshes.end()));
    }
    out.info = std::move(rig);
    return !out.meshes.empty();
}

}
