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

int eye_bone(const MDLInfo& info, EyeSide side)
{
    if (side == EyeSide::None || info.Bones.empty()) return -1;
    const auto lookup = Anim::build_model_bone_lookup(
        info, static_cast<uint32_t>(info.Bones.size()));
    static constexpr const char* kLeftCandidates[] = {
        "L_Eye", "Left_Eye", "LeftEye", "Character.Focal.Eye.Left",
        "Focal_Eye_DummyObject1",
    };
    static constexpr const char* kRightCandidates[] = {
        "R_Eye", "Right_Eye", "RightEye", "Character.Focal.Eye.Right",
        "Focal_Eye_DummyObject",
    };
    const char* const* candidates = side == EyeSide::Left
        ? kLeftCandidates : kRightCandidates;
    const size_t count = side == EyeSide::Left
        ? std::size(kLeftCandidates) : std::size(kRightCandidates);
    for (size_t i = 0; i < count; ++i) {
        const int bone = Anim::model_bone_for_track_name(lookup,
                                                          candidates[i]);
        if (bone >= 0) return bone;
    }
    return -1;
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

bool attach_eye_mesh(MDLMeshGeom& mesh, const MDLInfo& rig, int bone)
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
    std::vector<uint16_t> remap(part.Bones.size(), 0);
    for (size_t i = 0; i < part.Bones.size(); ++i) {
        const int target = Anim::model_bone_for_track_name(
            lookup, part.Bones[i].Name);
        if (target >= 0) remap[i] = static_cast<uint16_t>(target);
    }
    for (MDLMeshGeom& mesh : meshes) {
        for (uint16_t& bone : mesh.bone_ids) {
            bone = static_cast<size_t>(bone) < remap.size()
                ? remap[bone] : uint16_t(0);
        }
    }
}

}

bool Resolve(const std::vector<std::uint32_t>& model_hashes,
             ResolvedModel& out,
             std::string* error)
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

    size_t primary = 0;
    for (size_t i = 1; i < parts.size(); ++i) {
        if (parts[i].geometry_score > parts[primary].geometry_score) {
            primary = i;
        }
    }
    const MDLInfo& rig = parts[primary].info;
    out.primary_model_path = parts[primary].path;
    out.primary_model_hash = parts[primary].hash;

    for (size_t i = 0; i < parts.size(); ++i) {
        LoadedPart& part = parts[i];
        const EyeSide side = eye_side_from_path(part.path);
        if (side != EyeSide::None) {
            const int bone = eye_bone(rig, side);
            for (MDLMeshGeom& mesh : part.meshes) {
                attach_eye_mesh(mesh, rig, bone);
            }
        } else if (i != primary) {
            remap_part_bones(part.meshes, part.info, rig);
        }
        out.meshes.insert(out.meshes.end(),
                          std::make_move_iterator(part.meshes.begin()),
                          std::make_move_iterator(part.meshes.end()));
    }
    out.info = std::move(parts[primary].info);
    return !out.meshes.empty();
}

}
