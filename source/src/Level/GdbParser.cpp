#include "GdbParser.h"
#include "Skybox/GdbReaderInternal.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Gdb {

namespace {

using detail::GdbView;
using detail::ReadBeF32;
using detail::ReadBeU32;
using detail::kHashParent;
using detail::kHashVecX;
using detail::kHashVecY;
using detail::kHashVecZ;
using detail::kHeaderSize;

inline bool Finite3(float x, float y, float z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

constexpr uint32_t kVarMarker    = 0x00004B80;
constexpr uint32_t kInlineVec3SchemaRel = 0x00000568;
constexpr uint32_t kHashPosition = 0xBD7C27D4;
constexpr uint32_t kHashRotation = 0x21EBC83B;
constexpr uint32_t kHashTransformComponent = 0xF73572C4;

constexpr uint32_t kHashSimpleTransformComponent = 0x619F96CF;

constexpr uint32_t kHashParticleSystemNameHash = 0x4EDC9083;
constexpr uint32_t kHashGraphicAppearanceComponent = 0xA7B6EF56;
constexpr uint32_t kHashGraphicAppearanceAnimatedMeshComponent = 0x21D312CA;
constexpr uint32_t kHashStaticMeshComponent = 0x29CF50D1;
constexpr uint32_t kHashStaticMultipleMeshComponent = 0xCE642A15;
constexpr uint32_t kHashPhysicsSimulationKeyframedComponent = 0x6B177DD0;
constexpr uint32_t kHashPhysicsSimulationStaticComponent = 0x5883C406;

constexpr uint32_t kHashPhysicsSimulationDynamicComponent = 0xFC8A57C5;
constexpr uint32_t kHashModelFile = 0x0C17DB4E;
constexpr uint32_t kHashModelFile1 = 0x578E3BFB;
constexpr uint32_t kHashModelFile2 = 0x578E3BF8;
constexpr uint32_t kHashStaticMultipleModelList = 0x77679B84;
constexpr uint32_t kHashStaticMultipleModelSlotA = 0x03C915A0;
constexpr uint32_t kHashStaticMultipleModelSlotD = 0x03C915A3;
constexpr uint32_t kHashStaticMultipleModelFile = 0x1372D766;
constexpr uint32_t kHashSkeletonFile = 0xC3D06E3A;
constexpr uint32_t kHashRetargetSkeletonFile = 0x64234AF2;
constexpr uint32_t kHashAnimationName = 0x78B1F79C;
constexpr uint32_t kHashAnimName = 0x49BD6FC7;
constexpr uint32_t kHashAnimation = 0x8F32748D;
constexpr uint32_t kHashAnimationList = 0x63565E85;
constexpr uint32_t kHashAnimations = 0xF96D7984;
constexpr uint32_t kHashAnimationSet = 0xE227399F;
constexpr uint32_t kHashNull = 0x811C9DC5;

inline bool PlausiblePosition(float x, float y, float z) {
    return Finite3(x, y, z) &&
           x >= -256.0f && x <= 768.0f &&
           y >= -256.0f && y <= 768.0f &&
           z >= -256.0f && z <= 512.0f &&
           (std::fabs(x) + std::fabs(y) + std::fabs(z)) > 1.0f;
}

inline bool LooksLikeRotationTriplet(float x, float y, float z)
{
    constexpr float kPi = 3.14159265358979323846f;
    if (!Finite3(x, y, z)) return false;
    if (std::fabs(x) > kPi + 0.25f ||
        std::fabs(y) > kPi + 0.25f ||
        std::fabs(z) > kPi + 0.25f)
    {
        return false;
    }
    if ((std::fabs(x) + std::fabs(y) + std::fabs(z)) < 1.0e-5f) {
        return true;
    }
    return (std::fabs(y) < 0.02f && std::fabs(z) < 0.02f) ||
           (std::fabs(y + kPi) < 0.02f &&
            std::fabs(z + kPi) < 0.02f) ||
           (std::fabs(y - kPi) < 0.02f &&
            std::fabs(z - kPi) < 0.02f);
}

std::string CompactEntityName(std::string s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (std::isalnum(c)) {
            out.push_back(char(std::tolower(c)));
        }
    }
    return out;
}

bool IsNoHashMarketShellEntity(const std::string& entity_name)
{
    const std::string key = CompactEntityName(entity_name);
    return key == "newobjectbuildingbsmarkettavern" ||
           key == "objectbuildinggeneralstore" ||
           key == "newobjectbuildinggeneralstore1" ||
           key.find("objectbuildingbsopenstall") != std::string::npos ||
           key.find("objectbuildingbsmarketstall") != std::string::npos ||
           key.find("newobjectbuildingbsmarketstall") != std::string::npos ||
           key == "objectbuildingbstarotstall";
}

struct InlineTransformCandidate {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float rot_x = 0.0f;
    float rot_y = 0.0f;
    float rot_z = 0.0f;
    bool has_rotation = false;
    size_t pos_slots[3] = {0, 0, 0};
};

bool TryTransformRecord(const GdbView& view,
                        size_t record,
                        float& x,
                        float& y,
                        float& z,
                        float& rot_x,
                        float& rot_y,
                        float& rot_z,
                        bool& has_rotation,
                        size_t* out_pos_slots = nullptr,
                        size_t* out_rot_slots = nullptr) {
    size_t pos_slot = 0;
    size_t pos_owner = 0;
    if (!view.findFieldOwner(record, kHashPosition, 6,
                             pos_slot, pos_owner, nullptr)) {
        return false;
    }
    const uint32_t pos_hash = ReadBeU32(view.bytes.data() + pos_slot);
    if (!view.readVec3Ref(pos_hash, x, y, z, nullptr, nullptr, nullptr,
                          out_pos_slots)) return false;
    if (!PlausiblePosition(x, y, z)) return false;

    size_t rot_slot = 0;
    float rx = 0.0f, ry = 0.0f, rz = 0.0f;
    if (view.findLocal(pos_owner, kHashRotation, 6, rot_slot, nullptr)) {
        const uint32_t rot_hash = ReadBeU32(view.bytes.data() + rot_slot);
        size_t rslots[3] = {0, 0, 0};
        if (view.readRotationVec3Ref(rot_hash, rx, ry, rz, rslots)) {
            if (Finite3(rx, ry, rz)) {
                rot_x = rx;
                rot_y = ry;
                rot_z = rz;
                has_rotation = true;
                if (out_rot_slots) {
                    out_rot_slots[0] = rslots[0];
                    out_rot_slots[1] = rslots[1];
                    out_rot_slots[2] = rslots[2];
                }
            }
        }
    }
    return true;
}

bool TryComponentTransformField(const GdbView& view,
                                size_t record,
                                uint32_t component_field_hash,
                                float& x,
                                float& y,
                                float& z,
                                float& rot_x,
                                float& rot_y,
                                float& rot_z,
                                bool& has_rotation,
                                size_t* out_pos_slots = nullptr,
                                size_t* out_rot_slots = nullptr) {
    size_t transform_slot = 0;
    if (!view.findLocal(record, component_field_hash, 6,
                        transform_slot, nullptr)) {
        return false;
    }

    const uint32_t transform_hash =
        ReadBeU32(view.bytes.data() + transform_slot);
    size_t transform_record = 0;
    if (!view.lookup(transform_hash, transform_record)) return false;
    return TryTransformRecord(view, transform_record, x, y, z,
                              rot_x, rot_y, rot_z, has_rotation,
                              out_pos_slots, out_rot_slots);
}

bool TryComponentTransformRecord(const GdbView& view,
                                 size_t record,
                                 float& x,
                                 float& y,
                                 float& z,
                                 float& rot_x,
                                 float& rot_y,
                                 float& rot_z,
                                 bool& has_rotation,
                                 size_t* out_pos_slots = nullptr,
                                 size_t* out_rot_slots = nullptr) {
    if (TryComponentTransformField(view, record, kHashTransformComponent,
                                   x, y, z, rot_x, rot_y, rot_z,
                                   has_rotation, out_pos_slots,
                                   out_rot_slots)) {
        return true;
    }
    if (TryComponentTransformField(
            view, record, kHashPhysicsSimulationKeyframedComponent,
            x, y, z, rot_x, rot_y, rot_z, has_rotation, out_pos_slots,
            out_rot_slots)) {
        return true;
    }
    if (TryComponentTransformField(
            view, record, kHashPhysicsSimulationStaticComponent,
            x, y, z, rot_x, rot_y, rot_z, has_rotation, out_pos_slots,
            out_rot_slots)) {
        return true;
    }
    return TryComponentTransformField(
        view, record, kHashPhysicsSimulationDynamicComponent,
        x, y, z, rot_x, rot_y, rot_z, has_rotation, out_pos_slots,
        out_rot_slots);

}

bool TryReadModelPathHashField(const GdbView& view,
                               size_t record,
                               uint32_t field_hash,
                               uint32_t& out_hash)
{
    out_hash = 0;
    size_t path_hash_slot = 0;
    uint8_t path_hash_type = 0;
    if (!view.findLocal(record, field_hash, 0xFF,
                        path_hash_slot, &path_hash_type)) {
        return false;
    }
    if (path_hash_type != 4 && path_hash_type != 3) return false;

    out_hash = ReadBeU32(view.bytes.data() + path_hash_slot);
    return out_hash != 0 && out_hash != 0x811C9DC5u;
}

bool TryReadInheritedHashField(const GdbView& view,
                               size_t record,
                               uint32_t field_hash,
                               uint32_t& out_hash)
{
    out_hash = 0;
    size_t slot = 0;
    uint8_t type = 0;
    if (!view.findField(record, field_hash, 0xFF, slot, &type)) {
        return false;
    }

    if (type != 3 && type != 4 && type != 6) return false;
    out_hash = ReadBeU32(view.bytes.data() + slot);
    return out_hash != 0 && out_hash != 0x811C9DC5u;
}

bool TryReadStaticMeshModelPathHash(const GdbView& view,
                                    size_t record,
                                    uint32_t& out_hash)
{
    out_hash = 0;
    size_t model_slot = 0;
    if (!view.findField(record, kHashStaticMeshComponent, 6,
                        model_slot, nullptr)) {
        return false;
    }

    const uint32_t model_resource_hash =
        ReadBeU32(view.bytes.data() + model_slot);
    size_t model_resource_record = 0;
    if (!view.lookup(model_resource_hash, model_resource_record)) return false;

    return TryReadModelPathHashField(view, model_resource_record,
                                     kHashModelFile, out_hash);
}

bool TryReadStaticMultipleSlotModelPathHash(const GdbView& view,
                                            size_t list_record,
                                            uint32_t slot_hash,
                                            uint32_t& out_hash)
{
    out_hash = 0;
    size_t sch = 0;
    uint32_t n = 0;
    if (!view.schema(list_record, sch, n)) return false;

    const size_t hashes = sch + 4;
    const size_t descs = hashes + size_t(n) * 4;
    for (uint32_t i = 0; i < n; ++i) {
        if (ReadBeU32(view.bytes.data() + hashes + size_t(i) * 4) !=
            slot_hash) {
            continue;
        }

        const uint32_t desc =
            ReadBeU32(view.bytes.data() + descs + size_t(i) * 4);
        if (uint8_t(desc >> 24) != 6) continue;

        const size_t item_slot = list_record + 4 + size_t(i) * 4;
        if (item_slot + 4 > view.body_end) return false;

        const uint32_t item_hash = ReadBeU32(view.bytes.data() + item_slot);
        if (item_hash == 0) continue;

        size_t item_record = 0;
        if (!view.lookup(item_hash, item_record)) continue;

        if (TryReadModelPathHashField(view, item_record,
                                      kHashStaticMultipleModelFile,
                                      out_hash)) {
            return true;
        }
    }

    return false;
}

bool TryReadStaticMultipleFallbackModelPathHash(const GdbView& view,
                                                size_t list_record,
                                                uint32_t& out_hash)
{
    out_hash = 0;
    size_t sch = 0;
    uint32_t n = 0;
    if (!view.schema(list_record, sch, n)) return false;

    const size_t hashes = sch + 4;
    const size_t descs = hashes + size_t(n) * 4;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t field_hash =
            ReadBeU32(view.bytes.data() + hashes + size_t(i) * 4);
        if (field_hash == kHashParent ||
            field_hash == kHashStaticMultipleModelSlotA ||
            field_hash == kHashStaticMultipleModelSlotD) {
            continue;
        }

        const uint32_t desc =
            ReadBeU32(view.bytes.data() + descs + size_t(i) * 4);
        if (uint8_t(desc >> 24) != 6) continue;

        const size_t item_slot = list_record + 4 + size_t(i) * 4;
        if (item_slot + 4 > view.body_end) return false;

        const uint32_t item_hash = ReadBeU32(view.bytes.data() + item_slot);
        if (item_hash == 0) continue;

        size_t item_record = 0;
        if (!view.lookup(item_hash, item_record)) continue;

        if (TryReadModelPathHashField(view, item_record,
                                      kHashStaticMultipleModelFile,
                                      out_hash)) {
            return true;
        }
    }

    return false;
}

bool TryReadStaticMultipleModelPathHash(const GdbView& view,
                                        size_t record,
                                        uint32_t& out_hash)
{
    out_hash = 0;
    size_t component_slot = 0;
    if (!view.findField(record, kHashStaticMultipleMeshComponent, 6,
                        component_slot, nullptr)) {
        return false;
    }

    const uint32_t component_hash =
        ReadBeU32(view.bytes.data() + component_slot);
    size_t component_record = 0;
    if (!view.lookup(component_hash, component_record)) return false;

    size_t list_slot = 0;
    if (!view.findField(component_record, kHashStaticMultipleModelList, 6,
                        list_slot, nullptr)) {
        return false;
    }

    const uint32_t list_hash = ReadBeU32(view.bytes.data() + list_slot);
    size_t list_record = 0;
    if (!view.lookup(list_hash, list_record)) return false;

    if (TryReadStaticMultipleSlotModelPathHash(view, list_record,
                                               kHashStaticMultipleModelSlotA,
                                               out_hash)) {
        return true;
    }
    if (TryReadStaticMultipleSlotModelPathHash(view, list_record,
                                               kHashStaticMultipleModelSlotD,
                                               out_hash)) {
        return true;
    }
    return TryReadStaticMultipleFallbackModelPathHash(view, list_record,
                                                     out_hash);
}

void AppendUniqueModelPathHash(std::vector<uint32_t>& out, uint32_t h)
{
    if (h == 0 || h == 0x811C9DC5u) return;
    if (std::find(out.begin(), out.end(), h) == out.end()) {
        out.push_back(h);
    }
}

template <typename Fn>
void VisitRecordAndParents(const GdbView& view, size_t record, Fn&& fn)
{
    std::unordered_set<size_t> visited;
    size_t cur = record;
    for (int depth = 0; depth < 64; ++depth) {
        if (!visited.insert(cur).second) return;
        fn(cur);

        size_t parent_slot = 0;
        if (!view.findLocal(cur, kHashParent, 6, parent_slot, nullptr)) {
            return;
        }
        const uint32_t parent_hash =
            ReadBeU32(view.bytes.data() + parent_slot);
        if (parent_hash == 0) return;

        size_t parent_record = 0;
        if (!view.lookup(parent_hash, parent_record)) return;
        cur = parent_record;
    }
}

void CollectModelFileFields(const GdbView& view,
                            size_t record,
                            std::vector<uint32_t>& out)
{
    uint32_t h = 0;
    if (TryReadModelPathHashField(view, record, kHashModelFile, h)) {
        AppendUniqueModelPathHash(out, h);
    }
    if (TryReadModelPathHashField(view, record, kHashModelFile1, h)) {
        AppendUniqueModelPathHash(out, h);
    }
    if (TryReadModelPathHashField(view, record, kHashModelFile2, h)) {
        AppendUniqueModelPathHash(out, h);
    }
}

void CollectModelFileFieldsAndParents(const GdbView& view,
                                      size_t record,
                                      std::vector<uint32_t>& out)
{
    std::unordered_set<size_t> visited;
    size_t cur = record;
    for (int depth = 0; depth < 64; ++depth) {
        if (!visited.insert(cur).second) return;

        const size_t before = out.size();
        bool has_model_file_field = false;
        size_t slot = 0;
        uint8_t field_type = 0;
        has_model_file_field =
            view.findLocal(cur, kHashModelFile, 0xFF, slot, &field_type) ||
            view.findLocal(cur, kHashModelFile1, 0xFF, slot, &field_type) ||
            view.findLocal(cur, kHashModelFile2, 0xFF, slot, &field_type);
        CollectModelFileFields(view, cur, out);
        if (has_model_file_field || out.size() != before) return;

        size_t parent_slot = 0;
        if (!view.findLocal(cur, kHashParent, 6, parent_slot, nullptr)) {
            return;
        }
        const uint32_t parent_hash =
            ReadBeU32(view.bytes.data() + parent_slot);
        if (parent_hash == 0) return;

        size_t parent_record = 0;
        if (!view.lookup(parent_hash, parent_record)) return;
        cur = parent_record;
    }
}

void CollectMeshRecordModelPathHashes(const GdbView& view,
                                      size_t mesh_record,
                                      std::vector<uint32_t>& out)
{
    CollectModelFileFieldsAndParents(view, mesh_record, out);

    size_t mesh_slot = 0;
    uint8_t mesh_type = 0;
    if (!view.findField(mesh_record, kHashStaticMultipleModelFile, 0xFF,
                        mesh_slot, &mesh_type)) {
        return;
    }
    if (mesh_slot + 4 > view.body_end) return;

    const uint32_t raw = ReadBeU32(view.bytes.data() + mesh_slot);
    if (raw == 0) return;

    if (mesh_type == 6) {
        size_t model_record = 0;
        if (view.lookup(raw, model_record)) {
            CollectModelFileFieldsAndParents(view, model_record, out);
        }

        AppendUniqueModelPathHash(out, raw);
    } else if (mesh_type == 3 || mesh_type == 4) {
        AppendUniqueModelPathHash(out, raw);
    }
}

void CollectStaticMultipleSlotModelPathHashes(const GdbView& view,
                                              size_t list_record,
                                              uint32_t slot_hash,
                                              std::vector<uint32_t>& out)
{
    size_t sch = 0;
    uint32_t n = 0;
    if (!view.schema(list_record, sch, n)) return;

    const size_t hashes = sch + 4;
    const size_t descs = hashes + size_t(n) * 4;
    for (uint32_t i = 0; i < n; ++i) {
        if (ReadBeU32(view.bytes.data() + hashes + size_t(i) * 4) !=
            slot_hash) {
            continue;
        }

        const uint32_t desc =
            ReadBeU32(view.bytes.data() + descs + size_t(i) * 4);
        if (uint8_t(desc >> 24) != 6) continue;

        const size_t item_slot = list_record + 4 + size_t(i) * 4;
        if (item_slot + 4 > view.body_end) return;

        const uint32_t item_hash = ReadBeU32(view.bytes.data() + item_slot);
        if (item_hash == 0) continue;

        size_t item_record = 0;
        if (!view.lookup(item_hash, item_record)) continue;

        CollectMeshRecordModelPathHashes(view, item_record, out);
    }
}

void CollectStaticMultipleFallbackModelPathHashes(const GdbView& view,
                                                  size_t list_record,
                                                  std::vector<uint32_t>& out)
{
    size_t sch = 0;
    uint32_t n = 0;
    if (!view.schema(list_record, sch, n)) return;

    const size_t hashes = sch + 4;
    const size_t descs = hashes + size_t(n) * 4;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t field_hash =
            ReadBeU32(view.bytes.data() + hashes + size_t(i) * 4);
        if (field_hash == kHashParent) continue;

        const uint32_t desc =
            ReadBeU32(view.bytes.data() + descs + size_t(i) * 4);
        if (uint8_t(desc >> 24) != 6) continue;

        const size_t item_slot = list_record + 4 + size_t(i) * 4;
        if (item_slot + 4 > view.body_end) return;

        const uint32_t item_hash = ReadBeU32(view.bytes.data() + item_slot);
        if (item_hash == 0) continue;

        size_t item_record = 0;
        if (!view.lookup(item_hash, item_record)) continue;

        CollectMeshRecordModelPathHashes(view, item_record, out);
    }
}

void CollectStaticMeshModelPathHashes(const GdbView& view,
                                      size_t record,
                                      std::vector<uint32_t>& out)
{
    size_t model_slot = 0;
    if (!view.findField(record, kHashStaticMeshComponent, 6,
                        model_slot, nullptr)) {
        return;
    }

    const uint32_t model_resource_hash =
        ReadBeU32(view.bytes.data() + model_slot);
    size_t model_resource_record = 0;
    if (!view.lookup(model_resource_hash, model_resource_record)) return;

    CollectModelFileFieldsAndParents(view, model_resource_record, out);
}

void CollectAnimatedMeshModelPathHashes(const GdbView& view,
                                        size_t record,
                                        std::vector<uint32_t>& out)
{
    size_t model_slot = 0;
    if (!view.findField(record, kHashGraphicAppearanceAnimatedMeshComponent, 6,
                        model_slot, nullptr)) {
        return;
    }

    const uint32_t model_resource_hash =
        ReadBeU32(view.bytes.data() + model_slot);
    size_t model_resource_record = 0;
    if (!view.lookup(model_resource_hash, model_resource_record)) return;

    CollectModelFileFieldsAndParents(view, model_resource_record, out);
}

void CollectStaticMultipleComponentModelPathHashes(const GdbView& view,
                                                   size_t component_record,
                                                   std::vector<uint32_t>& out)
{
    size_t list_slot = 0;
    if (!view.findField(component_record, kHashStaticMultipleModelList, 6,
                        list_slot, nullptr)) {
        return;
    }

    const uint32_t list_hash = ReadBeU32(view.bytes.data() + list_slot);
    size_t list_record = 0;
    if (!view.lookup(list_hash, list_record)) return;

    CollectStaticMultipleSlotModelPathHashes(
        view, list_record, kHashStaticMultipleModelSlotA, out);
    CollectStaticMultipleSlotModelPathHashes(
        view, list_record, kHashStaticMultipleModelSlotD, out);
    CollectStaticMultipleFallbackModelPathHashes(view, list_record, out);
}

void CollectStaticMultipleModelPathHashes(const GdbView& view,
                                          size_t record,
                                          std::vector<uint32_t>& out)
{
    size_t component_slot = 0;
    if (!view.findField(record, kHashStaticMultipleMeshComponent, 6,
                        component_slot, nullptr)) {
        return;
    }

    const uint32_t component_hash =
        ReadBeU32(view.bytes.data() + component_slot);
    size_t component_record = 0;
    if (!view.lookup(component_hash, component_record)) return;

    CollectStaticMultipleComponentModelPathHashes(view, component_record, out);
}

void CollectGraphicAppearanceModelPathHashes(const GdbView& view,
                                             size_t record,
                                             std::vector<uint32_t>& out)
{
    size_t appearance_slot = 0;
    if (!view.findLocal(record, kHashGraphicAppearanceComponent, 6,
                        appearance_slot, nullptr)) {
        return;
    }

    const uint32_t appearance_hash =
        ReadBeU32(view.bytes.data() + appearance_slot);
    size_t appearance_record = 0;
    if (!view.lookup(appearance_hash, appearance_record)) return;

    VisitRecordAndParents(view, appearance_record, [&](size_t r) {
        CollectStaticMeshModelPathHashes(view, r, out);
        CollectStaticMultipleModelPathHashes(view, r, out);
        CollectStaticMultipleComponentModelPathHashes(view, r, out);
    });
}

std::vector<uint32_t> CollectModelPathHashesForRecord(const GdbView& view,
                                                      size_t record)
{
    std::vector<uint32_t> out;
    if (!view.ok) return out;
    VisitRecordAndParents(view, record, [&](size_t r) {
        CollectAnimatedMeshModelPathHashes(view, r, out);
        CollectStaticMeshModelPathHashes(view, r, out);
        CollectStaticMultipleModelPathHashes(view, r, out);
        CollectGraphicAppearanceModelPathHashes(view, r, out);
    });
    return out;
}

bool TryReadModelPathHashForRecord(const GdbView& view,
                                   size_t record,
                                   uint32_t& out_hash)
{
    out_hash = 0;
    if (!view.ok) return false;
    std::vector<uint32_t> hashes = CollectModelPathHashesForRecord(view, record);
    if (hashes.empty()) return false;
    out_hash = hashes.front();
    return true;
}

bool TryReadModelPathHashForHash(const GdbView& view,
                                 uint32_t record_hash,
                                 uint32_t& out_hash)
{
    out_hash = 0;
    if (!view.ok || record_hash == 0) return false;

    size_t record = 0;
    if (!view.lookup(record_hash, record)) return false;
    return TryReadModelPathHashForRecord(view, record, out_hash);
}

std::string Hex32(uint32_t v)
{
    std::ostringstream os;
    os << "0x" << std::uppercase << std::hex
       << std::setw(8) << std::setfill('0') << v;
    return os.str();
}

std::string HexOff(size_t v)
{
    std::ostringstream os;
    os << "0x" << std::uppercase << std::hex << v;
    return os.str();
}

std::string TrimEnumToken(std::string s)
{
    while (!s.empty() &&
           std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() &&
           std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

std::string Decode010EnumToken(std::string s)
{
    s = TrimEnumToken(std::move(s));
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s.compare(i, 2, "__") == 0) {
            out.push_back('\\');
            i += 2;
        } else if (s.compare(i, 3, "DOT") == 0) {
            out.push_back('.');
            i += 3;
        } else {
            out.push_back(s[i++]);
        }
    }
    return out;
}

const std::unordered_map<uint32_t, std::string>& ExternalEnumNames()
{
    static std::unordered_map<uint32_t, std::string> names;
    static bool loaded = false;
    if (loaded) return names;
    loaded = true;

    std::vector<std::string> candidates;
    candidates.emplace_back("F2GDBEnum.bt");
    if (const char* user_profile = std::getenv("USERPROFILE")) {
        std::string path = user_profile;
        path += "\\Downloads\\F2GDBEnum.bt";
        candidates.push_back(std::move(path));
    }

    std::ifstream in;
    for (const std::string& path : candidates) {
        in.open(path);
        if (in) break;
        in.clear();
    }
    if (!in) return names;

    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        const size_t hex = line.find("0x", eq == std::string::npos ? 0 : eq);
        if (eq == std::string::npos || hex == std::string::npos) continue;

        const std::string token = Decode010EnumToken(line.substr(0, eq));
        if (token.empty()) continue;

        size_t end = hex + 2;
        while (end < line.size() &&
               std::isxdigit(static_cast<unsigned char>(line[end]))) {
            ++end;
        }
        if (end == hex + 2) continue;

        try {
            const uint32_t h = static_cast<uint32_t>(
                std::stoul(line.substr(hex + 2, end - hex - 2), nullptr, 16));
            names.emplace(h, token);
        } catch (...) {
        }
    }

    return names;
}

const char* GdbFieldName(uint32_t hash)
{
    switch (hash) {
    case kHashParent: return "parent";
    case kHashPosition: return "Position";
    case kHashRotation: return "Rotation";
    case kHashTransformComponent: return "TransformComponent";
    case kHashGraphicAppearanceComponent: return "GraphicAppearanceComponent";
    case kHashGraphicAppearanceAnimatedMeshComponent:
        return "GraphicAppearanceAnimatedMeshComponent";
    case kHashStaticMeshComponent: return "GraphicAppearanceStaticMeshComponent";
    case kHashStaticMultipleMeshComponent:
        return "GraphicAppearanceStaticMultipleMeshComponent";
    case kHashPhysicsSimulationKeyframedComponent:
        return "PhysicsSimulationKeyframedComponent";
    case kHashPhysicsSimulationStaticComponent:
        return "PhysicsSimulationStaticComponent";
    case kHashPhysicsSimulationDynamicComponent:
        return "PhysicsSimulationDynamicComponent";
    case kHashModelFile: return "ModelFile";
    case kHashStaticMultipleModelList: return "StaticMultipleModelList";
    case kHashStaticMultipleModelSlotA: return "StaticMultipleModelSlotA";
    case kHashStaticMultipleModelSlotD: return "StaticMultipleModelSlotD";
    case kHashStaticMultipleModelFile: return "StaticMultipleModelFile";
    case kHashSkeletonFile: return "SkeletonFile";
    case kHashRetargetSkeletonFile: return "RetargetSkeletonFile";
    case kHashAnimationName: return "AnimationName";
    case kHashAnimName: return "AnimName";
    case kHashAnimation: return "Animation";
    case kHashAnimationList: return "AnimationList";
    case kHashAnimations: return "Animations";
    case kHashAnimationSet: return "AnimationSet";
    case kHashModelFile1: return "ModelFile1";
    case kHashModelFile2: return "ModelFile2";
    case kHashVecX: return "VecX";
    case kHashVecY: return "VecY";
    case kHashVecZ: return "VecZ";
    case 0x73AB8B6Au: return "InventoryComponent";
    case 0x112935F8u: return "BuildingComponent";
    case 0x31FF8FCFu: return "GraphicAppearanceComponent";
    case 0x515A75DAu: return "GraphicAppearanceStaticMultipleMeshComponent";
    case 0x5330EEEFu: return "MeshRecords";
    case 0x9FE058A5u: return "Mesh2";
    case 0xE6431038u: return "Mesh1";
    case 0xAF3BB51Du: return "Mesh";
    case 0x333E7213u: return "Mesh";
    case 0xE47DDFF6u: return "MaxDrawDistanceOverride";
    case 0xE47DDDF6u: return "DrawDistanceRangeFactorOverride";
    case 0x63041E10u: return "GroupMindComponent";
    case 0xBA8BFF21u: return "VillageMemberComponent";
    case 0xBB128083u: return "SubgameControllerComponent";
    case 0x3D9D8E92u: return "WorkplaceComponent";
    case 0x8F506EFCu: return "ShopComponent";
    case 0x5D3F8069u: return "parent";
    default: return "";
    }
}

std::string GdbHashName(
    uint32_t hash,
    const std::unordered_map<uint32_t, std::string>& name_by_hash)
{
    auto it = name_by_hash.find(hash);
    if (it != name_by_hash.end()) return it->second;
    const auto& enum_names = ExternalEnumNames();
    auto enum_it = enum_names.find(hash);
    if (enum_it != enum_names.end()) return enum_it->second;
    return {};
}

bool TryReadInlineVec3(const std::vector<uint8_t>& bytes,
                       size_t q,
                       size_t re,
                       float& x,
                       float& y,
                       float& z)
{
    if (q + 16 > re || q + 16 > bytes.size()) return false;
    if (ReadBeU32(bytes.data() + q) != kInlineVec3SchemaRel) return false;
    const float rz = ReadBeF32(bytes.data() + q + 4);
    const float ry = ReadBeF32(bytes.data() + q + 8);
    const float rx = ReadBeF32(bytes.data() + q + 12);
    if (!Finite3(rx, ry, rz)) return false;
    x = rx;
    y = ry;
    z = rz;
    return true;
}

bool FindNearbyInlineRotation(const std::vector<uint8_t>& bytes,
                              size_t rs,
                              size_t re,
                              size_t pos_offset,
                              float& rot_x,
                              float& rot_y,
                              float& rot_z)
{
    const size_t back_begin =
        (pos_offset > rs + 0x60) ? pos_offset - 0x60 : rs;
    for (size_t q = pos_offset; q >= back_begin + 4;) {
        q -= 4;
        float cx = 0.0f, cy = 0.0f, cz = 0.0f;
        if (TryReadInlineVec3(bytes, q, re, cx, cy, cz) &&
            LooksLikeRotationTriplet(cx, cy, cz))
        {
            rot_x = cx;
            rot_y = cy;
            rot_z = cz;
            return true;
        }
        if (q == 0) break;
    }

    const size_t fwd_end = std::min(re, pos_offset + 0x60);
    for (size_t q = pos_offset + 4; q + 16 <= fwd_end; q += 4) {
        float cx = 0.0f, cy = 0.0f, cz = 0.0f;
        if (TryReadInlineVec3(bytes, q, re, cx, cy, cz) &&
            LooksLikeRotationTriplet(cx, cy, cz))
        {
            rot_x = cx;
            rot_y = cy;
            rot_z = cz;
            return true;
        }
    }
    return false;
}

std::vector<InlineTransformCandidate> CollectInlineTransforms(
    const std::vector<uint8_t>& bytes,
    size_t rs,
    size_t re)
{
    std::vector<InlineTransformCandidate> out;
    for (size_t q = rs + 4; q + 16 <= re; q += 4) {
        InlineTransformCandidate c;
        if (!TryReadInlineVec3(bytes, q, re, c.x, c.y, c.z)) continue;
        if (!PlausiblePosition(c.x, c.y, c.z)) continue;
        c.pos_slots[0] = q + 12;
        c.pos_slots[1] = q + 8;
        c.pos_slots[2] = q + 4;
        if (LooksLikeRotationTriplet(c.x, c.y, c.z)) continue;
        c.has_rotation = FindNearbyInlineRotation(
            bytes, rs, re, q, c.rot_x, c.rot_y, c.rot_z);
        bool duplicate = false;
        for (const InlineTransformCandidate& existing : out) {
            const float dx = existing.x - c.x;
            const float dy = existing.y - c.y;
            const float dz = existing.z - c.z;
            if ((dx * dx + dy * dy + dz * dz) < 0.01f) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) out.push_back(c);
    }
    return out;
}

bool TryInlineTransform(const std::vector<uint8_t>& bytes,
                        size_t rs,
                        size_t re,
                        float& x,
                        float& y,
                        float& z,
                        float& rot_x,
                        float& rot_y,
                        float& rot_z,
                        bool& has_rotation,
                        size_t* out_pos_slots = nullptr) {
    std::vector<InlineTransformCandidate> candidates =
        CollectInlineTransforms(bytes, rs, re);
    if (candidates.empty()) return false;
    const InlineTransformCandidate& c = candidates.front();
    x = c.x;
    y = c.y;
    z = c.z;
    rot_x = c.rot_x;
    rot_y = c.rot_y;
    rot_z = c.rot_z;
    has_rotation = c.has_rotation;
    if (out_pos_slots) {
        out_pos_slots[0] = c.pos_slots[0];
        out_pos_slots[1] = c.pos_slots[1];
        out_pos_slots[2] = c.pos_slots[2];
    }
    return true;
}

bool SameInlineTransformPosition(const InlineTransformCandidate& c,
                                 const Placement& p)
{
    const float dx = c.x - p.x;
    const float dy = c.y - p.y;
    const float dz = c.z - p.z;
    return (dx * dx + dy * dy + dz * dz) < 0.01f;
}

bool TryInlineTransformRange(const std::vector<uint8_t>& bytes,
                             size_t begin,
                             size_t end,
                             float& x,
                             float& y,
                             float& z,
                             float& rot_x,
                             float& rot_y,
                             float& rot_z,
                             bool& has_rotation,
                             size_t* out_pos_slots = nullptr) {
    if (begin >= end || end > bytes.size()) return false;
    const size_t shim = (begin >= 4) ? begin - 4 : begin;
    return TryInlineTransform(bytes, shim, end, x, y, z,
                              rot_x, rot_y, rot_z, has_rotation,
                              out_pos_slots);
}

bool TryIndexedInlineTransform(const GdbView& view,
                               size_t record,
                               float& x,
                               float& y,
                               float& z,
                               float& rot_x,
                               float& rot_y,
                               float& rot_z,
                               bool& has_rotation,
                               size_t* out_pos_slots = nullptr) {
    size_t payload_start = 0;
    size_t payload_end = 0;
    if (!view.payloadRange(record, payload_start, payload_end)) return false;
    return TryInlineTransformRange(view.bytes, payload_start, payload_end,
                                   x, y, z, rot_x, rot_y, rot_z,
                                   has_rotation, out_pos_slots);
}

bool TryEmbeddedTransformRecords(const GdbView& view,
                                 size_t rs,
                                 size_t re,
                                 float& x,
                                 float& y,
                                 float& z,
                                 float& rot_x,
                                 float& rot_y,
                                 float& rot_z,
                                 bool& has_rotation) {
    for (size_t q = rs + 4; q + 12 <= re && q + 4 <= view.body_end; q += 4) {
        size_t sch = 0;
        uint32_t n = 0;
        if (!view.schema(q, sch, n)) continue;
        size_t slot = 0;
        if (!view.findLocal(q, kHashPosition, 6, slot, nullptr)) continue;
        if (TryTransformRecord(view, q, x, y, z,
                               rot_x, rot_y, rot_z, has_rotation)) {
            return true;
        }
    }
    return false;
}

bool TryPrefixedPlacementRecord(const GdbView& view,
                                size_t rs,
                                size_t re,
                                size_t& record,
                                float& x,
                                float& y,
                                float& z,
                                float& rot_x,
                                float& rot_y,
                                float& rot_z,
                                bool& has_rotation) {

    if (!view.ok || rs + 16 > re) return false;
    record = rs + 12;

    size_t schema_off = 0;
    uint32_t field_count = 0;
    if (!view.schema(record, schema_off, field_count)) return false;
    if (view.hasVectorSchema(record)) return false;

    if (TryComponentTransformRecord(view, record, x, y, z,
                                    rot_x, rot_y, rot_z, has_rotation)) {
        return true;
    }
    return TryTransformRecord(view, record, x, y, z,
                              rot_x, rot_y, rot_z, has_rotation);
}

}

GdbInfo Parse(const std::vector<uint8_t>& bytes) {
    return ParseWithSaveMap(bytes, {});
}

bool LookupPlacement(
    const std::vector<uint8_t>& bytes,
    uint32_t record_hash,
    const std::string& entity_name,
    Placement& out_placement)
{
    if (record_hash == 0) return false;

    GdbView view(bytes);
    if (!view.ok) return false;

    size_t record = 0;
    if (!view.lookup(record_hash, record)) return false;

    float pos_x = 0.0f, pos_y = 0.0f, pos_z = 0.0f;
    float rot_x = 0.0f, rot_y = 0.0f, rot_z = 0.0f;
    bool has_rotation = false;
    size_t pos_slots[3] = {0, 0, 0};
    size_t rot_slots[3] = {0, 0, 0};
    bool have_pos = TryComponentTransformRecord(view, record,
                                                pos_x, pos_y, pos_z,
                                                rot_x, rot_y, rot_z,
                                                has_rotation, pos_slots,
                                                rot_slots);
    if (!have_pos) {
        have_pos = TryTransformRecord(view, record,
                                      pos_x, pos_y, pos_z,
                                      rot_x, rot_y, rot_z,
                                      has_rotation, pos_slots, rot_slots);
    }
    if (!have_pos) {
        have_pos = TryIndexedInlineTransform(view, record,
                                             pos_x, pos_y, pos_z,
                                             rot_x, rot_y, rot_z,
                                             has_rotation, pos_slots);
    }
    if (!have_pos) return false;

    Placement pl;
    pl.x = pos_x;
    pl.y = pos_y;
    pl.z = pos_z;
    pl.pos_value_off[0] = (uint32_t)pos_slots[0];
    pl.pos_value_off[1] = (uint32_t)pos_slots[1];
    pl.pos_value_off[2] = (uint32_t)pos_slots[2];
    pl.rot_value_off[0] = (uint32_t)rot_slots[0];
    pl.rot_value_off[1] = (uint32_t)rot_slots[1];
    pl.rot_value_off[2] = (uint32_t)rot_slots[2];
    pl.rot_x = rot_x;
    pl.rot_y = rot_y;
    pl.rot_z = rot_z;
    pl.has_rotation = has_rotation;
    pl.yaw = has_rotation && std::isfinite(rot_x) ? rot_x : 0.0f;
    pl.scale = 1.0f;
    pl.marker = kVarMarker;
    pl.hash_a = record_hash;
    pl.parent_hash = 0;
    pl.model_path_hash = 0;
    pl.skeleton_file_hash = 0;
    pl.retarget_skeleton_file_hash = 0;
    pl.model_path_hashes.clear();
    pl.indexed_record = true;
    pl.transform_from_indexed_record = true;
    pl.entity_name = entity_name;

    size_t parent_slot = 0;
    if (view.findLocal(record, kHashParent, 6, parent_slot, nullptr)) {
        pl.parent_hash = ReadBeU32(bytes.data() + parent_slot);
    }

    pl.model_path_hashes = CollectModelPathHashesForRecord(view, record);
    if (pl.model_path_hashes.empty() && pl.parent_hash != 0) {
        size_t parent_record = 0;
        if (view.lookup(pl.parent_hash, parent_record)) {
            pl.model_path_hashes =
                CollectModelPathHashesForRecord(view, parent_record);
        }
    }
    if (!pl.model_path_hashes.empty()) {
        pl.model_path_hash = pl.model_path_hashes.front();
    }

    TryReadInheritedHashField(view, record, kHashSkeletonFile,
                              pl.skeleton_file_hash);
    TryReadInheritedHashField(view, record, kHashRetargetSkeletonFile,
                              pl.retarget_skeleton_file_hash);

    out_placement = std::move(pl);
    return true;
}

GdbInfo ParseWithSaveMap(
    const std::vector<uint8_t>& bytes,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name)
{
    GdbInfo out;
    if (bytes.size() < kHeaderSize) return out;

    if (bytes[0] != 'G' || bytes[1] != 'D' || bytes[2] != 'B' || bytes[3] != 0) {
        return out;
    }

    std::unordered_map<uint32_t, std::string> name_by_hash;
    name_by_hash.reserve(hash_to_name.size() * 2);
    for (const auto& kv : hash_to_name) {
        name_by_hash.emplace(kv.first, kv.second);
    }

    GdbView view(bytes);
    if (!view.ok) return out;

    std::unordered_set<uint32_t> emitted;
    emitted.reserve(hash_to_name.empty()
                        ? size_t(view.count)
                        : hash_to_name.size() * 2);

    auto emit_for_hash = [&](uint32_t inst_hash,
                             const std::string& entity_name) {
        if (inst_hash == 0 || emitted.find(inst_hash) != emitted.end()) {
            return;
        }

        Placement pl;
        if (!LookupPlacement(bytes, inst_hash, entity_name, pl)) return;

        if (pl.model_path_hashes.size() > 1) {
            const std::vector<uint32_t> hashes = pl.model_path_hashes;
            for (uint32_t h : hashes) {
                Placement part = pl;
                part.model_path_hash = h;
                part.model_path_hashes.clear();
                part.model_path_hashes.push_back(h);
                out.placements.push_back(std::move(part));
            }
        } else {
            out.placements.push_back(std::move(pl));
        }
        emitted.insert(inst_hash);
    };

    if (!hash_to_name.empty()) {
        for (const auto& kv : hash_to_name) {
            emit_for_hash(kv.first, kv.second);
        }
    } else {
        for (uint32_t i = 0; i < view.count; ++i) {
            const uint32_t h =
                ReadBeU32(bytes.data() + view.hash_base + size_t(i) * 4);
            emit_for_hash(h, {});
        }
    }

    return out;
}

bool LookupModelPathHash(
    const std::vector<uint8_t>& bytes,
    uint32_t record_hash,
    uint32_t& out_model_path_hash,
    uint32_t* out_parent_hash)
{
    out_model_path_hash = 0;
    std::vector<uint32_t> hashes;
    if (!LookupModelPathHashes(bytes, record_hash, hashes, out_parent_hash) ||
        hashes.empty())
    {
        return false;
    }
    out_model_path_hash = hashes.front();
    return true;
}

bool LookupModelPathHashes(
    const std::vector<uint8_t>& bytes,
    uint32_t record_hash,
    std::vector<uint32_t>& out_model_path_hashes,
    uint32_t* out_parent_hash)
{
    out_model_path_hashes.clear();
    if (out_parent_hash) *out_parent_hash = 0;
    if (record_hash == 0) return false;

    GdbView view(bytes);
    if (!view.ok) return false;

    size_t record = 0;
    if (!view.lookup(record_hash, record)) return false;

    uint32_t parent_hash = 0;
    size_t parent_slot = 0;
    if (view.findLocal(record, kHashParent, 6, parent_slot, nullptr)) {
        parent_hash = ReadBeU32(bytes.data() + parent_slot);
        if (out_parent_hash) *out_parent_hash = parent_hash;
    }

    out_model_path_hashes = CollectModelPathHashesForRecord(view, record);
    if (out_model_path_hashes.empty() && parent_hash != 0) {
        size_t parent_record = 0;
        if (view.lookup(parent_hash, parent_record)) {
            out_model_path_hashes =
                CollectModelPathHashesForRecord(view, parent_record);
        }
    }
    return !out_model_path_hashes.empty();
}

std::vector<RecordRow> Build010RecordRows(
    const std::vector<uint8_t>& bytes,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name)
{
    std::vector<RecordRow> rows;
    GdbView view(bytes);
    if (!view.ok) return rows;

    std::unordered_map<uint32_t, std::string> name_by_hash;
    name_by_hash.reserve(hash_to_name.size() * 2);
    for (const auto& kv : hash_to_name) {
        name_by_hash.emplace(kv.first, kv.second);
    }

    rows.reserve(view.count);
    for (uint32_t i = 0; i < view.count; ++i) {
        const uint32_t hash =
            ReadBeU32(bytes.data() + view.hash_base + size_t(i) * 4);
        if (i >= view.record_data_offsets.size()) break;
        const size_t record = view.record_data_offsets[i];

        RecordRow row;
        row.index = i;
        row.hash = hash;
        row.name = GdbHashName(hash, name_by_hash);

        size_t parent_slot = 0;
        if (view.findLocal(record, kHashParent, 6, parent_slot, nullptr)) {
            row.parent_hash = ReadBeU32(bytes.data() + parent_slot);
        }

        row.model_path_hashes = CollectModelPathHashesForRecord(view, record);
        if (row.model_path_hashes.empty() && row.parent_hash != 0) {
            size_t parent_record = 0;
            if (view.lookup(row.parent_hash, parent_record)) {
                row.model_path_hashes =
                    CollectModelPathHashesForRecord(view, parent_record);
            }
        }
        if (!row.model_path_hashes.empty()) {
            row.model_path_hash = row.model_path_hashes.front();
        }

        TryReadInheritedHashField(view, record, kHashSkeletonFile,
                                  row.skeleton_file_hash);
        TryReadInheritedHashField(view, record, kHashRetargetSkeletonFile,
                                  row.retarget_skeleton_file_hash);
        if (row.skeleton_file_hash != 0) {
            row.skeleton_file_name =
                GdbHashName(row.skeleton_file_hash, name_by_hash);
        }
        if (row.retarget_skeleton_file_hash != 0) {
            row.retarget_skeleton_file_name =
                GdbHashName(row.retarget_skeleton_file_hash, name_by_hash);
        }

        rows.push_back(std::move(row));
    }

    return rows;
}

namespace {

constexpr uint32_t kHashParticleEffect = 0x5B009F68;
constexpr uint32_t kHashParticleEmitter = 0x4EDC9083;
constexpr uint32_t kHashDummyObject = 0xF4DF0382;
constexpr uint32_t kHashOffsetFromDummy = 0xE867B8A0;
constexpr uint32_t kHashMaxVisibilityDistance = 0x59029324;
constexpr uint32_t kHashOverrideMaxVisibilityDistance = 0xC835A9F4;
constexpr uint32_t kHashDisableWhenParentIsInvisible = 0xD27705D0;
constexpr uint32_t kHashOrientParticleToAttachmentPoint = 0xF825B96A;

inline uint32_t Fnv1LowerStr(const uint8_t* p, size_t n) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < n; ++i) {
        h = uint32_t(h * 0x01000193u);
        h ^= uint8_t(std::tolower(p[i]));
    }
    return h;
}

void CollectFxNameStrings(const std::vector<uint8_t>& bytes,
                          std::unordered_map<uint32_t, std::string>& out) {
    const uint8_t* p = bytes.data();
    const size_t n = bytes.size();
    size_t i = 4;
    while (i + 1 < n) {

        if (p[i] < 0x20 || p[i] >= 0x7f) { ++i; continue; }
        size_t j = i;
        while (j < n && p[j] >= 0x20 && p[j] < 0x7f) ++j;
        const size_t len = j - i;
        if (len >= 4 && j < n && p[j] == 0 && i >= 4) {
            const uint32_t stored = ReadBeU32(p + i - 4);
            uint32_t exact = 0x811C9DC5u;
            for (size_t k = i; k < j; ++k) { exact = uint32_t(exact * 0x01000193u); exact ^= p[k]; }
            if (exact == stored) {
                const uint32_t lower = Fnv1LowerStr(p + i, len);
                out.emplace(lower, std::string(reinterpret_cast<const char*>(p + i), len));
            }
        }
        i = j + 1;
    }
}

void CollectGdbNameStrings(
    const std::vector<uint8_t>& bytes,
    std::unordered_map<uint32_t, std::string>& exact,
    std::unordered_map<uint32_t, std::string>& lower) {
    const uint8_t* p = bytes.data();
    const size_t n = bytes.size();
    size_t i = 4;
    while (i + 1 < n) {
        if (p[i] < 0x20 || p[i] >= 0x7f) { ++i; continue; }
        size_t j = i;
        while (j < n && p[j] >= 0x20 && p[j] < 0x7f) ++j;
        const size_t len = j - i;
        if (len >= 1 && j < n && p[j] == 0 && i >= 4) {
            const uint32_t stored = ReadBeU32(p + i - 4);
            uint32_t h = 0x811C9DC5u;
            for (size_t k = i; k < j; ++k) {
                h = uint32_t(h * 0x01000193u);
                h ^= p[k];
            }
            if (h == stored) {
                std::string s(reinterpret_cast<const char*>(p + i), len);
                exact.emplace(stored, s);
                lower.emplace(Fnv1LowerStr(p + i, len), std::move(s));
            }
        }
        i = j + 1;
    }
}

}

std::unordered_map<uint32_t, std::vector<ParticleFxBinding>>
ExtractParticleFxBindings(
    const std::vector<const std::vector<uint8_t>*>& gdbs)
{
    std::unordered_map<uint32_t, std::vector<ParticleFxBinding>> out;

    std::unordered_map<uint32_t, std::string> name_by_lower;
    for (const auto* g : gdbs) {
        if (g && !g->empty()) CollectFxNameStrings(*g, name_by_lower);
    }

    std::unordered_map<uint32_t, uint32_t> parent_of;

    for (const auto* gp : gdbs) {
        if (!gp || gp->empty()) continue;
        const std::vector<uint8_t>& bytes = *gp;
        GdbView view(bytes);
        if (!view.ok) continue;

        auto for_each_field = [&](size_t record, auto&& fn) {
            size_t sch = 0; uint32_t fc = 0;
            if (!view.schema(record, sch, fc)) return;
            const size_t hashes = sch + 4;
            const size_t descs = hashes + size_t(fc) * 4;
            for (uint32_t i = 0; i < fc; ++i) {
                const uint32_t fh = ReadBeU32(bytes.data() + hashes + size_t(i) * 4);
                const uint32_t desc = ReadBeU32(bytes.data() + descs + size_t(i) * 4);
                const uint8_t type = uint8_t(desc >> 24);
                const size_t slot = record + 4 + size_t(i) * 4;
                if (slot + 4 > view.body_end) break;
                const uint32_t val = ReadBeU32(bytes.data() + slot);
                fn(fh, type, val);
            }
        };

        for (uint32_t i = 0; i < view.count; ++i) {
            if (i >= view.record_data_offsets.size()) break;
            const uint32_t rh = ReadBeU32(bytes.data() + view.hash_base + size_t(i) * 4);
            const size_t rec = view.record_data_offsets[i];
            for_each_field(rec, [&](uint32_t fh, uint8_t type, uint32_t val) {
                if (fh == kHashParent && (type == 6 || type == 7) && val != 0)
                    parent_of[rh] = val;
            });
        }

        for (uint32_t i = 0; i < view.count; ++i) {
            if (i >= view.record_data_offsets.size()) break;
            const uint32_t rh = ReadBeU32(bytes.data() + view.hash_base + size_t(i) * 4);
            const size_t rec = view.record_data_offsets[i];

            uint32_t chain_root = 0;
            for_each_field(rec, [&](uint32_t fh, uint8_t type, uint32_t val) {
                if (fh == kHashParticleEffect && (type == 6 || type == 7) && val != 0)
                    chain_root = val;
            });
            if (chain_root == 0) continue;

            std::vector<uint32_t> hashes_found;
            std::unordered_set<uint32_t> seen;
            std::vector<uint32_t> stack{ chain_root };
            int budget = 4096;
            while (!stack.empty() && budget-- > 0) {
                uint32_t rref = stack.back(); stack.pop_back();
                if (rref == 0 || seen.count(rref)) continue;
                seen.insert(rref);
                size_t rr = 0;
                if (!view.lookup(rref, rr)) continue;
                for_each_field(rr, [&](uint32_t fh, uint8_t type, uint32_t val) {
                    if (type == 4 && val != kHashNull) {
                        hashes_found.push_back(val);
                    } else if ((type == 6 || type == 7) && val != 0) {
                        stack.push_back(val);
                    }
                });
            }
            if (hashes_found.empty()) continue;

            std::vector<ParticleFxBinding>& binds = out[rh];
            for (uint32_t h : hashes_found) {
                bool dup = false;
                for (const auto& b : binds) if (b.fx_hash_lower == h) { dup = true; break; }
                if (dup) continue;
                ParticleFxBinding b;
                b.fx_hash_lower = h;
                auto it = name_by_lower.find(h);
                if (it != name_by_lower.end()) b.fx_name = it->second;
                binds.push_back(std::move(b));
            }
        }
    }

    if (!out.empty() && !parent_of.empty()) {
        std::unordered_map<uint32_t, std::vector<ParticleFxBinding>> resolved = out;
        for (const auto& kv : parent_of) {
            const uint32_t start = kv.first;
            if (resolved.count(start)) continue;
            uint32_t cur = start;
            int guard = 64;
            const std::vector<ParticleFxBinding>* hit = nullptr;
            std::unordered_set<uint32_t> path;
            while (guard-- > 0 && cur != 0 && !path.count(cur)) {
                path.insert(cur);
                auto pit = parent_of.find(cur);
                if (pit == parent_of.end()) break;
                cur = pit->second;
                auto rit = out.find(cur);
                if (rit != out.end()) { hit = &rit->second; break; }
            }
            if (hit) resolved[start] = *hit;
        }
        out.swap(resolved);
    }

    return out;
}

std::unordered_map<uint32_t, std::vector<ParticleAttachmentBinding>>
ExtractParticleAttachmentBindings(
    const std::vector<const std::vector<uint8_t>*>& gdbs)
{
    std::unordered_map<uint32_t, std::vector<ParticleAttachmentBinding>> out;
    const std::unordered_map<uint32_t, std::vector<ParticleFxBinding>>
        effects_by_record = ExtractParticleFxBindings(gdbs);

    std::unordered_map<uint32_t, std::string> name_by_hash;
    std::unordered_map<uint32_t, std::string> name_by_lower;
    for (const auto* g : gdbs) {
        if (g && !g->empty())
            CollectGdbNameStrings(*g, name_by_hash, name_by_lower);
    }

    std::unordered_map<uint32_t, uint32_t> parent_of;
    std::unordered_map<uint32_t, std::vector<uint32_t>> referrers;

    auto add_unique = [](std::vector<ParticleAttachmentBinding>& dst,
                         const ParticleAttachmentBinding& b) {
        for (const auto& e : dst) {
            if (e.fx_hash_lower == b.fx_hash_lower &&
                e.component_hash == b.component_hash &&
                e.dummy_hash == b.dummy_hash &&
                std::fabs(e.offset[0] - b.offset[0]) < 1e-6f &&
                std::fabs(e.offset[1] - b.offset[1]) < 1e-6f &&
                std::fabs(e.offset[2] - b.offset[2]) < 1e-6f) {
                return;
            }
        }
        dst.push_back(b);
    };

    for (const auto* gp : gdbs) {
        if (!gp || gp->empty()) continue;
        const std::vector<uint8_t>& bytes = *gp;
        GdbView view(bytes);
        if (!view.ok) continue;

        auto for_each_field = [&](size_t record, auto&& fn) {
            size_t sch = 0;
            uint32_t fc = 0;
            if (!view.schema(record, sch, fc)) return;
            const size_t hashes = sch + 4;
            const size_t descs = hashes + size_t(fc) * 4;
            for (uint32_t i = 0; i < fc; ++i) {
                const uint32_t fh =
                    ReadBeU32(bytes.data() + hashes + size_t(i) * 4);
                const uint32_t desc =
                    ReadBeU32(bytes.data() + descs + size_t(i) * 4);
                const uint8_t type = uint8_t(desc >> 24);
                const size_t slot = record + 4 + size_t(i) * 4;
                if (slot + 4 > view.body_end) break;
                const uint32_t val = ReadBeU32(bytes.data() + slot);
                fn(fh, type, val);
            }
        };

        for (uint32_t i = 0; i < view.count; ++i) {
            if (i >= view.record_data_offsets.size()) break;
            const uint32_t rh =
                ReadBeU32(bytes.data() + view.hash_base + size_t(i) * 4);
            const size_t rec = view.record_data_offsets[i];
            for_each_field(rec, [&](uint32_t fh, uint8_t type, uint32_t val) {
                if ((type == 6 || type == 7) && val != 0) {
                    referrers[val].push_back(rh);
                    if (fh == kHashParent) parent_of[rh] = val;
                }
            });
        }
    }

    for (const auto* gp : gdbs) {
        if (!gp || gp->empty()) continue;
        const std::vector<uint8_t>& bytes = *gp;
        GdbView view(bytes);
        if (!view.ok) continue;

        auto read_bool = [&](size_t record, uint32_t hash, bool& out_bool) {
            size_t slot = 0;
            if (!view.findField(record, hash, 0, slot, nullptr)) return false;
            out_bool = ReadBeU32(bytes.data() + slot) != 0;
            return true;
        };
        auto read_float = [&](size_t record, uint32_t hash, float& out_float) {
            size_t slot = 0;
            if (!view.findField(record, hash, 3, slot, nullptr)) return false;
            out_float = ReadBeF32(bytes.data() + slot);
            return std::isfinite(out_float);
        };

        auto resolve_effects = [&](uint32_t emitter_hash) {
            std::vector<ParticleFxBinding> found;
            auto eit = effects_by_record.find(emitter_hash);
            if (eit != effects_by_record.end()) {
                found = eit->second;
            }
            return found;
        };

        for (uint32_t i = 0; i < view.count; ++i) {
            if (i >= view.record_data_offsets.size()) break;
            const uint32_t component_hash =
                ReadBeU32(bytes.data() + view.hash_base + size_t(i) * 4);
            const size_t rec = view.record_data_offsets[i];

            size_t emitter_slot = 0;
            uint8_t emitter_type = 0;
            if (!view.findField(rec, kHashParticleEmitter, 0xFF,
                                emitter_slot, &emitter_type) ||
                (emitter_type != 6 && emitter_type != 7)) {
                continue;
            }

            size_t dummy_slot = 0;
            uint8_t dummy_type = 0;
            const bool has_dummy =
                view.findField(rec, kHashDummyObject, 4, dummy_slot,
                               &dummy_type);
            size_t offset_slot = 0;
            uint8_t offset_type = 0;
            const bool has_offset =
                view.findField(rec, kHashOffsetFromDummy, 0xFF,
                               offset_slot, &offset_type) &&
                (offset_type == 6 || offset_type == 7);
            size_t orient_slot = 0;
            const bool has_orient =
                view.findField(rec, kHashOrientParticleToAttachmentPoint, 0,
                               orient_slot, nullptr);
            if (!has_dummy && !has_offset && !has_orient) {
                continue;
            }

            const uint32_t emitter_hash = ReadBeU32(bytes.data() + emitter_slot);
            std::vector<ParticleFxBinding> effects = resolve_effects(emitter_hash);
            if (effects.empty()) continue;

            ParticleAttachmentBinding base;
            base.component_hash = component_hash;
            base.emitter_record_hash = emitter_hash;

            if (has_dummy) {
                base.dummy_hash = ReadBeU32(bytes.data() + dummy_slot);
                auto it = name_by_hash.find(base.dummy_hash);
                if (it != name_by_hash.end()) base.dummy_name = it->second;
            }
            if (has_offset) {
                const uint32_t off_hash = ReadBeU32(bytes.data() + offset_slot);
                float x = 0.0f, y = 0.0f, z = 0.0f;
                if (view.readVec3Ref(off_hash, x, y, z)) {
                    base.offset[0] = x;
                    base.offset[1] = y;
                    base.offset[2] = z;
                }
            }
            read_float(rec, kHashMaxVisibilityDistance,
                       base.max_visibility_distance);
            read_bool(rec, kHashOverrideMaxVisibilityDistance,
                      base.override_max_visibility_distance);
            read_bool(rec, kHashDisableWhenParentIsInvisible,
                      base.disable_when_parent_invisible);
            read_bool(rec, kHashOrientParticleToAttachmentPoint,
                      base.orient_to_attachment_point);

            std::vector<uint32_t> owners;
            owners.push_back(component_hash);
            std::vector<uint32_t> queue;
            if (auto it = referrers.find(component_hash);
                it != referrers.end()) {
                queue = it->second;
            }
            std::unordered_set<uint32_t> seen_owners;
            seen_owners.insert(component_hash);
            for (size_t qi = 0; qi < queue.size() && qi < 4096; ++qi) {
                const uint32_t owner = queue[qi];
                if (!seen_owners.insert(owner).second) continue;
                owners.push_back(owner);
                if (auto it = referrers.find(owner);
                    it != referrers.end() && qi < 1024) {
                    for (uint32_t p : it->second) queue.push_back(p);
                }
            }

            for (const ParticleFxBinding& fx : effects) {
                ParticleAttachmentBinding b = base;
                b.fx_hash_lower = fx.fx_hash_lower;
                b.fx_name = fx.fx_name;
                for (uint32_t owner : owners) {
                    add_unique(out[owner], b);
                }
            }
        }
    }

    if (!out.empty() && !parent_of.empty()) {
        std::unordered_map<uint32_t, std::vector<ParticleAttachmentBinding>>
            resolved = out;
        for (const auto& kv : parent_of) {
            const uint32_t start = kv.first;
            if (resolved.count(start)) continue;
            uint32_t cur = start;
            int guard = 64;
            const std::vector<ParticleAttachmentBinding>* hit = nullptr;
            std::unordered_set<uint32_t> path;
            while (guard-- > 0 && cur != 0 && !path.count(cur)) {
                path.insert(cur);
                auto pit = parent_of.find(cur);
                if (pit == parent_of.end()) break;
                cur = pit->second;
                auto rit = out.find(cur);
                if (rit != out.end()) {
                    hit = &rit->second;
                    break;
                }
            }
            if (hit) resolved[start] = *hit;
        }
        out.swap(resolved);
    }

    return out;
}

std::vector<FxEntityPlacement> ExtractFxEntityPlacements(
    const std::vector<uint8_t>& level_bytes,
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name)
{
    std::vector<FxEntityPlacement> out;
    if (level_bytes.empty()) return out;
    GdbView level(level_bytes);
    if (!level.ok) return out;

    std::vector<std::unique_ptr<GdbView>> views;
    for (const auto* g : gdbs) {
        if (!g || g->empty()) continue;
        auto v = std::make_unique<GdbView>(*g);
        if (v->ok) views.push_back(std::move(v));
    }

    auto for_each_field = [](const GdbView& view, size_t record, auto&& fn) {
        size_t sch = 0; uint32_t fc = 0;
        if (!view.schema(record, sch, fc)) return;
        const size_t hashes = sch + 4;
        const size_t descs = hashes + size_t(fc) * 4;
        for (uint32_t i = 0; i < fc; ++i) {
            const uint32_t fh =
                ReadBeU32(view.bytes.data() + hashes + size_t(i) * 4);
            const uint32_t desc =
                ReadBeU32(view.bytes.data() + descs + size_t(i) * 4);
            const uint8_t type = uint8_t(desc >> 24);
            const size_t slot = record + 4 + size_t(i) * 4;
            if (slot + 4 > view.body_end) break;
            fn(fh, type, ReadBeU32(view.bytes.data() + slot));
        }
    };

    auto hunt_fx_hash = [&](uint32_t root) -> uint32_t {
        uint32_t found = 0;
        std::unordered_set<uint32_t> seen;
        std::vector<std::pair<uint32_t, int>> stack{ { root, 0 } };
        int budget = 2048;
        while (!stack.empty() && budget-- > 0 && !found) {
            auto [h, depth] = stack.back();
            stack.pop_back();
            if (!h || depth > 4 || seen.count(h)) continue;
            seen.insert(h);
            for (const auto& v : views) {
                size_t rec = 0;
                if (!v->lookup(h, rec)) continue;
                for_each_field(*v, rec,
                               [&](uint32_t fh, uint8_t type, uint32_t val) {
                    if (fh == kHashParticleSystemNameHash && val != 0 &&
                        val != kHashNull) {
                        found = val;
                    } else if ((type == 6 || type == 7) && val != 0) {
                        stack.emplace_back(val, depth + 1);
                    }
                });
                break;
            }
        }
        return found;
    };

    for (const auto& [rec_hash, name] : hash_to_name) {
        size_t rec = 0;
        if (!level.lookup(rec_hash, rec)) continue;
        FxEntityPlacement p;

        if (!TryComponentTransformField(level, rec,
                                        kHashSimpleTransformComponent,
                                        p.x, p.y, p.z,
                                        p.rot_x, p.rot_y, p.rot_z,
                                        p.has_rotation) &&
            !TryComponentTransformRecord(level, rec, p.x, p.y, p.z,
                                         p.rot_x, p.rot_y, p.rot_z,
                                         p.has_rotation)) {
            continue;
        }

        p.fx_hash = hunt_fx_hash(rec_hash);
        p.record_hash = rec_hash;
        p.name = name;
        out.push_back(std::move(p));
    }
    return out;
}

namespace {

constexpr uint32_t kHashChestComponent        = 0x379C25A9;
constexpr uint32_t kHashSilverKeysNeeded      = 0xB208E419;
constexpr uint32_t kHashInventoryComponentA   = 0x1C7D7B74;
constexpr uint32_t kHashInventoryComponentB   = 0x73AB8B6A;
constexpr uint32_t kHashInitialItems          = 0x9C24A50D;
constexpr uint32_t kHashItemRepopulationData  = 0xFDF2E63A;
constexpr uint32_t kHashPotentialItems        = 0x4FB47937;
constexpr uint32_t kHashChanceOfRespawning    = 0x993B9AA2;
constexpr uint32_t kHashItemReference         = 0x32A71597;
constexpr uint32_t kHashLootWeight            = 0x04C07DF9;
constexpr uint32_t kHashMinChapterProgress    = 0xABD2D73B;
constexpr uint32_t kHashMaxChapterProgress    = 0x7CC73E81;
constexpr uint32_t kHashInventoryItemComponent = 0xC3318103;
constexpr uint32_t kHashNameTag               = 0x9555A6FC;
constexpr uint32_t kHashMoneyComponent        = 0xE21AB7A0;
constexpr uint32_t kHashMoney                 = 0x941C7FA7;

constexpr uint32_t kHashRandomlyGeneratedItem = 0x9121DCA1;
constexpr uint32_t kHashRandomTableEntry      = 0x951C5AA5;

struct MultiGdbCursor {
    const GdbView* view = nullptr;
    size_t record = 0;
};

bool MultiLookup(const std::vector<const GdbView*>& views,
                 uint32_t hash,
                 MultiGdbCursor& out)
{
    if (hash == 0 || hash == kHashNull) return false;
    for (const GdbView* v : views) {
        size_t rec = 0;
        if (v->lookup(hash, rec)) {
            out.view = v;
            out.record = rec;
            return true;
        }
    }
    return false;
}

bool MultiFindInherited(const std::vector<const GdbView*>& views,
                        MultiGdbCursor cur,
                        uint32_t field_hash,
                        uint8_t expected_type,
                        MultiGdbCursor& out_owner,
                        uint32_t& out_value,
                        uint8_t* out_type = nullptr)
{
    for (int depth = 0; depth < 64; ++depth) {
        size_t slot = 0;
        uint8_t type = 0;
        if (cur.view->findLocal(cur.record, field_hash, expected_type,
                                slot, &type)) {
            out_owner = cur;
            out_value = ReadBeU32(cur.view->bytes.data() + slot);
            if (out_type) *out_type = type;
            return true;
        }
        size_t parent_slot = 0;
        if (!cur.view->findLocal(cur.record, kHashParent, 6,
                                 parent_slot, nullptr)) {
            return false;
        }
        const uint32_t parent_hash =
            ReadBeU32(cur.view->bytes.data() + parent_slot);
        MultiGdbCursor next;
        if (!MultiLookup(views, parent_hash, next)) return false;
        if (next.view == cur.view && next.record == cur.record) return false;
        cur = next;
    }
    return false;
}

bool MultiReadInheritedFloat(const std::vector<const GdbView*>& views,
                             const MultiGdbCursor& cur,
                             uint32_t field_hash,
                             float& out_value)
{
    MultiGdbCursor owner;
    uint32_t raw = 0;
    if (!MultiFindInherited(views, cur, field_hash, 3, owner, raw)) {
        return false;
    }

    std::memcpy(&out_value, &raw, 4);
    return std::isfinite(out_value);
}

void ReadContentsItemInfo(const std::vector<const GdbView*>& views,
                          uint32_t item_hash,
                          EntityContentsItem& item,
                          const std::unordered_map<uint32_t, std::string>*
                              dict = nullptr)
{
    auto lookup_name = [&](uint32_t h) -> std::string {
        if (dict) {
            auto it = dict->find(h);
            if (it != dict->end() && !it->second.empty()) {
                return it->second;
            }
        }
        return GdbHashName(h, {});
    };

    item.record_hash = item_hash;
    MultiGdbCursor rec;
    if (!MultiLookup(views, item_hash, rec)) return;

    MultiGdbCursor comp_field_owner;
    uint32_t comp_hash = 0;
    if (MultiFindInherited(views, rec, kHashInventoryItemComponent, 6,
                           comp_field_owner, comp_hash)) {
        MultiGdbCursor comp;
        if (MultiLookup(views, comp_hash, comp)) {
            MultiGdbCursor tag_owner;
            uint32_t tag_hash = 0;
            uint8_t tag_type = 0;
            if (MultiFindInherited(views, comp, kHashNameTag, 0xFF,
                                   tag_owner, tag_hash, &tag_type) &&
                (tag_type == 4 || tag_type == 7) &&
                tag_hash != 0 && tag_hash != kHashNull) {
                item.name_tag = lookup_name(tag_hash);
                item.name_tag_hash = tag_hash;
            }
        }
    }
    if (MultiFindInherited(views, rec, kHashMoneyComponent, 6,
                           comp_field_owner, comp_hash)) {
        MultiGdbCursor comp;
        if (MultiLookup(views, comp_hash, comp)) {
            MultiGdbCursor money_owner;
            uint32_t money_raw = 0;
            uint8_t money_type = 0;
            if (MultiFindInherited(views, comp, kHashMoney, 0xFF,
                                   money_owner, money_raw, &money_type) &&
                (money_type == 1 || money_type == 5)) {
                item.money = int(money_raw);
            }
        }
    }

    if (item.name_tag.empty() &&
        MultiFindInherited(views, rec, kHashRandomlyGeneratedItem, 6,
                           comp_field_owner, comp_hash)) {
        MultiGdbCursor comp;
        if (MultiLookup(views, comp_hash, comp)) {
            MultiGdbCursor table_owner;
            uint32_t table_hash = 0;
            uint8_t table_type = 0;
            if (MultiFindInherited(views, comp, kHashRandomTableEntry, 0xFF,
                                   table_owner, table_hash, &table_type) &&
                (table_type == 4 || table_type == 7) &&
                table_hash != 0 && table_hash != kHashNull) {
                const std::string table_name = lookup_name(table_hash);
                if (!table_name.empty()) {
                    item.name_tag = "Random (" + table_name + ")";
                }
            }
        }
    }

    if (item.name_tag.empty()) {
        item.name_tag = lookup_name(item_hash);
    }
}

}

std::unordered_map<uint32_t, EntityContents> ExtractEntityContents(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name)
{
    std::unordered_map<uint32_t, EntityContents> out;

    std::vector<std::unique_ptr<GdbView>> owned;
    std::vector<const GdbView*> views;
    for (const auto* g : gdbs) {
        if (!g || g->empty()) continue;
        auto v = std::make_unique<GdbView>(*g);
        if (v->ok) {
            views.push_back(v.get());
            owned.push_back(std::move(v));
        }
    }
    if (views.empty()) return out;

    std::unordered_map<uint32_t, std::string> dict;
    for (const auto* g : gdbs) {
        if (!g || g->empty()) continue;
        auto d = LoadEmbeddedDict(*g);
        dict.insert(d.begin(), d.end());
    }
    auto dict_name = [&](uint32_t h) -> std::string {
        auto it = dict.find(h);
        if (it != dict.end() && !it->second.empty()) return it->second;
        return GdbHashName(h, {});
    };

    auto read_items_list = [&](uint32_t list_hash,
                               std::vector<EntityContentsItem>& items) {
        MultiGdbCursor list;
        if (!MultiLookup(views, list_hash, list)) return;
        size_t sch = 0;
        uint32_t n = 0;
        if (!list.view->schema(list.record, sch, n)) return;
        const size_t hashes = sch + 4;
        const size_t descs = hashes + size_t(n) * 4;
        for (uint32_t i = 0; i < n && items.size() < 64; ++i) {
            const uint32_t fh =
                ReadBeU32(list.view->bytes.data() + hashes + size_t(i) * 4);
            if (fh == kHashParent) continue;
            const uint32_t desc =
                ReadBeU32(list.view->bytes.data() + descs + size_t(i) * 4);
            const uint8_t type = uint8_t(desc >> 24);
            if (type != 4 && type != 6 && type != 7) continue;
            const size_t slot = list.record + 4 + size_t(i) * 4;
            if (slot + 4 > list.view->body_end) break;
            const uint32_t item_hash =
                ReadBeU32(list.view->bytes.data() + slot);
            if (item_hash == 0 || item_hash == kHashNull) continue;
            EntityContentsItem item;
            item.entry_label = dict_name(fh);
            ReadContentsItemInfo(views, item_hash, item, &dict);
            items.push_back(std::move(item));
        }
    };

    auto read_loot_table = [&](uint32_t table_hash,
                               std::vector<EntityContentsItem>& items) {
        MultiGdbCursor table;
        if (!MultiLookup(views, table_hash, table)) return;
        size_t sch = 0;
        uint32_t n = 0;
        if (!table.view->schema(table.record, sch, n)) return;
        const size_t hashes = sch + 4;
        const size_t descs = hashes + size_t(n) * 4;
        for (uint32_t i = 0; i < n && items.size() < 96; ++i) {
            const uint32_t fh =
                ReadBeU32(table.view->bytes.data() + hashes + size_t(i) * 4);
            if (fh == kHashParent) continue;
            const uint32_t desc =
                ReadBeU32(table.view->bytes.data() + descs + size_t(i) * 4);
            if (uint8_t(desc >> 24) != 6) continue;
            const size_t slot = table.record + 4 + size_t(i) * 4;
            if (slot + 4 > table.view->body_end) break;
            const uint32_t entry_hash =
                ReadBeU32(table.view->bytes.data() + slot);
            MultiGdbCursor entry;
            if (!MultiLookup(views, entry_hash, entry)) continue;

            MultiGdbCursor owner;
            uint32_t item_ref = 0;
            uint8_t ref_type = 0;
            if (!MultiFindInherited(views, entry, kHashItemReference, 0xFF,
                                    owner, item_ref, &ref_type) ||
                (ref_type != 4 && ref_type != 7) ||
                item_ref == 0 || item_ref == kHashNull) {
                continue;
            }

            EntityContentsItem item;
            item.entry_label = dict_name(fh);
            ReadContentsItemInfo(views, item_ref, item, &dict);
            MultiReadInheritedFloat(views, entry, kHashLootWeight,
                                    item.weight);
            MultiReadInheritedFloat(views, entry, kHashMinChapterProgress,
                                    item.min_chapter);
            MultiReadInheritedFloat(views, entry, kHashMaxChapterProgress,
                                    item.max_chapter);
            items.push_back(std::move(item));
        }
    };

    for (const auto& [entity_hash, entity_name] : hash_to_name) {
        MultiGdbCursor rec;
        if (!MultiLookup(views, entity_hash, rec)) continue;

        EntityContents ec;
        ec.entity_name = entity_name;

        MultiGdbCursor owner;
        uint32_t comp_hash = 0;
        if (MultiFindInherited(views, rec, kHashChestComponent, 6,
                               owner, comp_hash)) {
            ec.has_chest_component = true;
            MultiGdbCursor comp;
            if (MultiLookup(views, comp_hash, comp)) {
                MultiGdbCursor keys_owner;
                uint32_t keys_raw = 0;
                uint8_t keys_type = 0;
                if (MultiFindInherited(views, comp, kHashSilverKeysNeeded,
                                       0xFF, keys_owner, keys_raw,
                                       &keys_type) &&
                    (keys_type == 1 || keys_type == 5)) {
                    ec.silver_keys_needed = int(keys_raw);
                }
            }
        }

        uint32_t inv_hash = 0;
        if (!MultiFindInherited(views, rec, kHashInventoryComponentA, 6,
                                owner, inv_hash)) {
            MultiFindInherited(views, rec, kHashInventoryComponentB, 6,
                               owner, inv_hash);
        }
        if (inv_hash != 0 && inv_hash != kHashNull) {
            MultiGdbCursor inv;
            if (MultiLookup(views, inv_hash, inv)) {
                MultiGdbCursor items_owner;
                uint32_t items_hash = 0;
                if (MultiFindInherited(views, inv, kHashInitialItems, 6,
                                       items_owner, items_hash) &&
                    items_hash != 0 && items_hash != kHashNull) {
                    read_items_list(items_hash, ec.initial_items);
                }

                MultiGdbCursor repop_owner;
                uint32_t repop_hash = 0;
                if (MultiFindInherited(views, inv, kHashItemRepopulationData,
                                       6, repop_owner, repop_hash) &&
                    repop_hash != 0 && repop_hash != kHashNull) {
                    MultiGdbCursor repop;
                    if (MultiLookup(views, repop_hash, repop)) {
                        MultiReadInheritedFloat(views, repop,
                                                kHashChanceOfRespawning,
                                                ec.chance_of_respawning);
                        MultiGdbCursor pot_owner;
                        uint32_t pot_hash = 0;
                        uint8_t pot_type = 0;
                        if (MultiFindInherited(views, repop,
                                               kHashPotentialItems, 0xFF,
                                               pot_owner, pot_hash,
                                               &pot_type) &&
                            (pot_type == 6 || pot_type == 7) &&
                            pot_hash != 0 && pot_hash != kHashNull) {
                            ec.potential_items_record = pot_hash;
                            read_loot_table(pot_hash, ec.potential_items);
                        }
                    }
                }
            }
        }

        if (ec.has_chest_component || !ec.initial_items.empty() ||
            !ec.potential_items.empty()) {
            out.emplace(entity_hash, std::move(ec));
        }
    }

    return out;
}

std::unordered_map<uint32_t, std::string> LoadEmbeddedDict(
    const std::vector<uint8_t>& bytes)
{
    std::unordered_map<uint32_t, std::string> out;
    GdbView view(bytes);
    if (!view.ok) return out;
    if (bytes.size() < 0x18) return out;
    const uint32_t name_pairs = ReadBeU32(bytes.data() + 0x10);
    const size_t meta_end =
        view.offset_base + size_t(view.count) * 2;
    const size_t name_base = (meta_end + 3) & ~size_t(3);
    const size_t dict_base = name_base + size_t(name_pairs) * 8;
    if (dict_base + 12 > bytes.size()) return out;
    if (ReadBeU32(bytes.data() + dict_base) != 0x00010000u) return out;
    const uint32_t data_bytes = ReadBeU32(bytes.data() + dict_base + 4);
    const uint32_t str_count = ReadBeU32(bytes.data() + dict_base + 8);
    const size_t data_start = dict_base + 12;
    if (data_start + data_bytes > bytes.size()) return out;
    size_t off = data_start;
    const size_t data_end = data_start + data_bytes;
    out.reserve(str_count);
    for (uint32_t i = 0; i < str_count && off + 5 <= data_end; ++i) {
        const uint32_t h = ReadBeU32(bytes.data() + off);
        off += 4;
        size_t term = off;
        while (term < data_end && bytes[term] != 0) ++term;
        out.emplace(h, std::string(bytes.begin() + off,
                                   bytes.begin() + term));
        off = term + 1;
    }
    return out;
}

std::unordered_map<uint32_t, PropTemplateInfo> BuildPropTemplateIndex(
    const std::vector<const std::vector<uint8_t>*>& gdbs)
{
    std::unordered_map<uint32_t, PropTemplateInfo> out;

    std::vector<std::unique_ptr<GdbView>> owned;
    std::vector<const GdbView*> views;
    for (const auto* g : gdbs) {
        if (!g || g->empty()) continue;
        auto v = std::make_unique<GdbView>(*g);
        if (v->ok) {
            views.push_back(v.get());
            owned.push_back(std::move(v));
        }
    }
    if (views.empty()) return out;

    constexpr uint32_t kGraphicsComponents[] = {
        kHashGraphicAppearanceComponent,
        kHashGraphicAppearanceAnimatedMeshComponent,
        kHashStaticMeshComponent,
        kHashStaticMultipleMeshComponent,
        0x31FF8FCFu,
        0x515A75DAu,
    };

    constexpr uint32_t kPhysicsComponents[] = {
        kHashTransformComponent,
        kHashPhysicsSimulationKeyframedComponent,
        kHashPhysicsSimulationStaticComponent,
        0xFC8A57C5u,
    };
    constexpr uint32_t kHashPhysicsFile = 0x92F5FEEEu;
    constexpr uint32_t kInventoryComponents[] = {
        0x1C7D7B74u,
        0x73AB8B6Au,
    };

    for (const GdbView* vw : views) {
        const GdbView& view = *vw;
        for (uint32_t i = 0; i < view.count; ++i) {
            if (i >= view.record_data_offsets.size()) break;
            const size_t rec = view.record_data_offsets[i];

            // Placed entities can override graphics locally, but they are not
            // reusable templates.  Treating one as a donor makes later
            // placements inherit its inventory and world transform.
            bool is_placed_entity = false;
            for (uint32_t ic : kInventoryComponents) {
                size_t slot = 0;
                if (view.findLocal(rec, ic, 6, slot, nullptr)) {
                    is_placed_entity = true;
                    break;
                }
            }
            if (is_placed_entity) continue;

            bool has_graphics = false;
            for (uint32_t gc : kGraphicsComponents) {
                size_t slot = 0;
                if (view.findLocal(rec, gc, 6, slot, nullptr)) {
                    has_graphics = true;
                    break;
                }
            }
            if (!has_graphics) {
                bool has_local_physics = false;
                for (uint32_t pc : kPhysicsComponents) {
                    size_t slot = 0;
                    if (view.findLocal(rec, pc, 6, slot, nullptr)) {
                        has_local_physics = true;
                        break;
                    }
                }
                if (!has_local_physics) continue;
                float ix = 0, iy = 0, iz = 0;
                float rx = 0, ry = 0, rz = 0;
                bool has_rot = false;
                if (TryComponentTransformRecord(view, rec, ix, iy, iz,
                                                rx, ry, rz, has_rot)) {
                    continue;
                }
                for (uint32_t gc : kGraphicsComponents) {
                    size_t slot = 0;
                    if (view.findField(rec, gc, 6, slot, nullptr)) {
                        has_graphics = true;
                        break;
                    }
                }
            }
            if (!has_graphics) continue;

            const std::vector<uint32_t> model_hashes =
                CollectModelPathHashesForRecord(view, rec);
            if (model_hashes.empty()) continue;

            PropTemplateInfo info;
            info.template_hash =
                ReadBeU32(view.bytes.data() + view.hash_base +
                          size_t(i) * 4);

            MultiGdbCursor cur{vw, rec};
            for (uint32_t pc : kPhysicsComponents) {
                MultiGdbCursor owner;
                uint32_t comp_hash = 0;
                if (!MultiFindInherited(views, cur, pc, 6, owner,
                                        comp_hash) ||
                    comp_hash == 0 || comp_hash == kHashNull) {
                    continue;
                }
                MultiGdbCursor comp;
                if (!MultiLookup(views, comp_hash, comp)) continue;
                info.comp_field_hash = pc;
                info.comp_template_hash = comp_hash;
                MultiGdbCursor pf_owner;
                uint32_t pf_hash = 0;
                uint8_t pf_type = 0;
                if (MultiFindInherited(views, comp, kHashPhysicsFile, 0xFF,
                                       pf_owner, pf_hash, &pf_type) &&
                    (pf_type == 4 || pf_type == 7) &&
                    pf_hash != 0 && pf_hash != kHashNull) {
                    info.physics_file_hash = pf_hash;
                }
                break;
            }
            if (info.comp_field_hash == 0) continue;

            {
                constexpr uint32_t kHashTextTags = 0x709D872Bu;
                constexpr uint32_t kHashReadableComponent = 0x89ABB47Eu;
                MultiGdbCursor tt_owner;
                uint32_t tt_hash = 0;
                MultiGdbCursor cur2{vw, rec};
                if ((MultiFindInherited(views, cur2,
                                        kHashReadableComponent, 6,
                                        tt_owner, tt_hash) ||
                     MultiFindInherited(views, cur2, kHashTextTags, 6,
                                        tt_owner, tt_hash)) &&
                    tt_hash != 0 && tt_hash != kHashNull) {
                    info.has_text_tags = true;
                }
            }

            for (uint32_t mh : model_hashes) {
                auto it = out.find(mh);
                if (it == out.end()) {
                    out.emplace(mh, info);
                } else if (it->second.physics_file_hash == 0 &&
                           info.physics_file_hash != 0) {
                    it->second = info;
                }
            }
        }
    }
    return out;
}

namespace {

std::vector<uint32_t> ModelsForEntityMulti(
    const std::vector<const GdbView*>& views, uint32_t entity_hash)
{
    std::vector<uint32_t> out_hashes;
    MultiGdbCursor ent;
    if (!MultiLookup(views, entity_hash, ent)) return out_hashes;
    MultiGdbCursor cur = ent;
    for (int depth = 0; depth < 16; ++depth) {
        const auto hashes =
            CollectModelPathHashesForRecord(*cur.view, cur.record);
        if (!hashes.empty()) {
            out_hashes.push_back(hashes.front());
            return out_hashes;
        }
        size_t slot = 0;
        if (!cur.view->findLocal(cur.record, kHashParent, 6, slot,
                                 nullptr)) {
            break;
        }
        const uint32_t ph = ReadBeU32(cur.view->bytes.data() + slot);
        MultiGdbCursor nxt;
        if (!MultiLookup(views, ph, nxt)) break;
        cur = nxt;
    }
    constexpr uint32_t kMorphComponent = 0x0D4ADA1Au;
    constexpr uint32_t kCompositeModelRecord = 0x7CFF5EE2u;
    constexpr uint32_t kModel = 0x90347E14u;
    constexpr uint32_t kPartFields[] = {
        0x05294B89u,
        0x547DDA3Eu,
        0xD622E5ADu,
    };
    MultiGdbCursor mo;
    uint32_t morph = 0;
    if (!MultiFindInherited(views, ent, kMorphComponent, 6, mo, morph) ||
        morph == 0 || morph == kHashNull) {
        return out_hashes;
    }
    MultiGdbCursor mc;
    if (!MultiLookup(views, morph, mc)) return out_hashes;
    MultiGdbCursor co;
    uint32_t comp_rec = 0;
    uint8_t cty = 0;
    if (!MultiFindInherited(views, mc, kCompositeModelRecord, 0xFF, co,
                            comp_rec, &cty) ||
        comp_rec == 0 || comp_rec == kHashNull) {
        return out_hashes;
    }
    MultiGdbCursor comp;
    if (!MultiLookup(views, comp_rec, comp)) return out_hashes;
    for (uint32_t pf : kPartFields) {
        MultiGdbCursor po;
        uint32_t part = 0;
        if (!MultiFindInherited(views, comp, pf, 6, po, part) ||
            part == 0 || part == kHashNull) {
            continue;
        }
        MultiGdbCursor prec;
        if (!MultiLookup(views, part, prec)) continue;
        MultiGdbCursor mo2;
        uint32_t mh = 0;
        uint8_t mty = 0;
        if (MultiFindInherited(views, prec, kModel, 0xFF, mo2, mh,
                               &mty) &&
            (mty == 4 || mty == 7) && mh != 0 && mh != kHashNull) {
            out_hashes.push_back(mh);
        }
    }
    return out_hashes;
}

}

std::vector<CreatureCatalogEntry> CollectCreatureNames(
    const std::vector<const std::vector<uint8_t>*>& gdbs)
{
    std::vector<CreatureCatalogEntry> out;
    constexpr uint32_t kCreatureComponent = 0xC3B90D4Fu;

    std::vector<std::unique_ptr<GdbView>> owned;
    std::vector<const GdbView*> views;
    std::vector<std::unordered_map<uint32_t, std::string>> dicts;
    for (const auto* g : gdbs) {
        if (!g || g->empty()) continue;
        auto v = std::make_unique<GdbView>(*g);
        if (v->ok) {
            views.push_back(v.get());
            owned.push_back(std::move(v));
            dicts.push_back(LoadEmbeddedDict(*g));
        }
    }
    if (views.empty()) return out;

    std::unordered_set<std::string> seen;
    for (size_t vi = 0; vi < views.size(); ++vi) {
        const GdbView* vw = views[vi];
        const auto& dict = dicts[vi];
        if (vw->bytes.size() < 0x18) continue;
        const uint32_t pairs = ReadBeU32(vw->bytes.data() + 0x10);
        const size_t meta_end =
            vw->offset_base + size_t(vw->count) * 2;
        const size_t name_base = (meta_end + 3) & ~size_t(3);
        if (name_base + size_t(pairs) * 8 > vw->bytes.size()) continue;
        for (uint32_t i = 0; i < pairs; ++i) {
            const uint32_t a =
                ReadBeU32(vw->bytes.data() + name_base + size_t(i) * 8);
            const uint32_t b = ReadBeU32(vw->bytes.data() + name_base +
                                         size_t(i) * 8 + 4);
            const std::pair<uint32_t, uint32_t> combos[2] = {{a, b},
                                                             {b, a}};
            for (const auto& c : combos) {
                auto dit = dict.find(c.first);
                if (dit == dict.end()) continue;
                MultiGdbCursor ent;
                if (!MultiLookup(views, c.second, ent)) continue;
                MultiGdbCursor owner;
                uint32_t comp = 0;
                if (!MultiFindInherited(views, ent,
                                        kCreatureComponent, 6, owner,
                                        comp) ||
                    comp == 0 || comp == kHashNull) {
                    continue;
                }
                if (seen.insert(dit->second).second) {
                    CreatureCatalogEntry e;
                    e.name = dit->second;
                    e.entity_hash = c.second;
                    e.model_hashes =
                        ModelsForEntityMulti(views, c.second);
                    out.push_back(std::move(e));
                }
                break;
            }
        }
    }
    std::sort(out.begin(), out.end(),
              [](const CreatureCatalogEntry& a,
                 const CreatureCatalogEntry& b) {
                  return a.name < b.name;
              });
    return out;
}

std::unordered_map<uint32_t, EntityTextTags> ExtractEntityTextTags(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name)
{
    std::unordered_map<uint32_t, EntityTextTags> out;
    constexpr uint32_t kHashTextTags = 0x709D872Bu;
    constexpr uint32_t kTagFields[5] = {
        0x1D280BA4u,
        0x1D280BA7u,
        0x1D280BA6u,
        0x1D280BA1u,
        0x1D280BA0u,
    };

    std::vector<std::unique_ptr<GdbView>> owned;
    std::vector<const GdbView*> views;
    for (const auto* g : gdbs) {
        if (!g || g->empty()) continue;
        auto v = std::make_unique<GdbView>(*g);
        if (v->ok) {
            views.push_back(v.get());
            owned.push_back(std::move(v));
        }
    }
    if (views.empty() || hash_to_name.empty()) return out;
    const GdbView* level_view = views.front();

    constexpr uint32_t kHashReadableComponent = 0x89ABB47Eu;
    constexpr uint32_t kHashTextTag = 0xB8F45248u;

    for (const auto& kv : hash_to_name) {
        const uint32_t entity_hash = kv.first;
        MultiGdbCursor ent;
        if (!MultiLookup(views, entity_hash, ent)) continue;

        EntityTextTags et;

        {
            MultiGdbCursor rc_owner;
            uint32_t rc_hash = 0;
            if (MultiFindInherited(views, ent, kHashReadableComponent, 6,
                                   rc_owner, rc_hash) &&
                rc_hash != 0 && rc_hash != kHashNull) {
                MultiGdbCursor rc;
                if (MultiLookup(views, rc_hash, rc)) {
                    MultiGdbCursor owner;
                    uint32_t th = 0;
                    uint8_t ty = 0;
                    if (MultiFindInherited(views, rc, kHashTextTag, 0xFF,
                                           owner, th, &ty) &&
                        (ty == 4 || ty == 7) && th != 0 &&
                        th != kHashNull) {
                        et.tags_record_hash = rc_hash;
                        et.tags_record_in_level_gdb =
                            (rc.view == level_view);
                        et.tag_hashes.push_back(th);
                    }
                }
            }
        }

        if (et.tag_hashes.empty()) {
            MultiGdbCursor tt_owner;
            uint32_t tags_hash = 0;
            if (MultiFindInherited(views, ent, kHashTextTags, 6,
                                   tt_owner, tags_hash) &&
                tags_hash != 0 && tags_hash != kHashNull) {
                MultiGdbCursor tags;
                if (MultiLookup(views, tags_hash, tags)) {
                    et.tags_record_hash = tags_hash;
                    et.tags_record_in_level_gdb =
                        (tags.view == level_view);
                    for (uint32_t tf : kTagFields) {
                        MultiGdbCursor owner;
                        uint32_t th = 0;
                        uint8_t ty = 0;
                        if (MultiFindInherited(views, tags, tf, 0xFF,
                                               owner, th, &ty) &&
                            (ty == 4 || ty == 7) && th != 0 &&
                            th != kHashNull) {
                            et.tag_hashes.push_back(th);
                        }
                    }
                }
            }
        }

        if (et.tag_hashes.empty()) {
            constexpr uint32_t kHashNameTag = 0x9555A6FCu;
            constexpr uint32_t kHashDescriptionTag = 0xD823B12Bu;
            constexpr uint32_t kHashInventoryItemComponent = 0xC3318103u;
            constexpr uint32_t kHashCreatureComponent = 0xC3B90D4Fu;
            MultiGdbCursor tag_scopes[3];
            int n_scopes = 0;
            tag_scopes[n_scopes++] = ent;
            for (uint32_t cf : {kHashInventoryItemComponent,
                                kHashCreatureComponent}) {
                MultiGdbCursor owner;
                uint32_t ch = 0;
                if (MultiFindInherited(views, ent, cf, 6, owner, ch) &&
                    ch != 0 && ch != kHashNull) {
                    MultiGdbCursor comp;
                    if (MultiLookup(views, ch, comp) && n_scopes < 3) {
                        tag_scopes[n_scopes++] = comp;
                    }
                }
            }
            for (int si = 0; si < n_scopes && et.tag_hashes.empty();
                 ++si) {
                for (uint32_t tf : {kHashNameTag, kHashDescriptionTag}) {
                    MultiGdbCursor owner;
                    uint32_t th = 0;
                    uint8_t ty = 0;
                    if (MultiFindInherited(views, tag_scopes[si], tf,
                                           0xFF, owner, th, &ty) &&
                        (ty == 4 || ty == 7) && th != 0 &&
                        th != kHashNull) {
                        et.tag_hashes.push_back(th);
                    }
                }
            }
        }
        if (!et.tag_hashes.empty()) {
            float x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0;
            bool hr = false;
            if (ent.view &&
                (TryComponentTransformRecord(*ent.view, ent.record, x, y,
                                             z, rx, ry, rz, hr) ||
                 TryTransformRecord(*ent.view, ent.record, x, y, z, rx,
                                    ry, rz, hr))) {
                et.has_pos = true;
                et.x = x;
                et.y = y;
                et.z = z;
            }
            out.emplace(entity_hash, std::move(et));
        }
    }

    for (uint32_t i = 0; i < level_view->count; ++i) {
        if (i >= level_view->record_data_offsets.size()) break;
        const uint32_t rh = ReadBeU32(level_view->bytes.data() +
                                      level_view->hash_base +
                                      size_t(i) * 4);
        if (out.count(rh)) continue;
        const size_t rec = level_view->record_data_offsets[i];

        constexpr uint32_t kSimpleTransform = 0x619F96CFu;
        float x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0;
        bool hr = false;
        const bool got_pos =
            TryComponentTransformRecord(*level_view, rec, x, y, z, rx,
                                        ry, rz, hr) ||
            TryComponentTransformField(*level_view, rec,
                                       kSimpleTransform, x, y, z, rx,
                                       ry, rz, hr) ||
            TryTransformRecord(*level_view, rec, x, y, z, rx, ry, rz,
                               hr);

        MultiGdbCursor cur{level_view, rec};
        EntityTextTags et;
        MultiGdbCursor rc_owner;
        uint32_t rc_hash = 0;
        if (MultiFindInherited(views, cur, kHashReadableComponent, 6,
                               rc_owner, rc_hash) &&
            rc_hash != 0 && rc_hash != kHashNull) {
            MultiGdbCursor rc;
            MultiGdbCursor owner;
            uint32_t th = 0;
            uint8_t ty = 0;
            if (MultiLookup(views, rc_hash, rc) &&
                MultiFindInherited(views, rc, kHashTextTag, 0xFF, owner,
                                   th, &ty) &&
                (ty == 4 || ty == 7) && th != 0 && th != kHashNull) {
                et.tags_record_hash = rc_hash;
                et.tag_hashes.push_back(th);
            }
        }
        if (et.tag_hashes.empty()) {
            constexpr uint32_t kHashInventoryItemComponent = 0xC3318103u;
            constexpr uint32_t kHashNameTag = 0x9555A6FCu;
            constexpr uint32_t kHashDescriptionTag = 0xD823B12Bu;
            MultiGdbCursor io;
            uint32_t ih = 0;
            if (MultiFindInherited(views, cur,
                                   kHashInventoryItemComponent, 6, io,
                                   ih) &&
                ih != 0 && ih != kHashNull) {
                MultiGdbCursor item;
                if (MultiLookup(views, ih, item)) {
                    for (uint32_t tf : {kHashNameTag,
                                        kHashDescriptionTag}) {
                        MultiGdbCursor owner;
                        uint32_t th = 0;
                        uint8_t ty = 0;
                        if (MultiFindInherited(views, item, tf, 0xFF,
                                               owner, th, &ty) &&
                            (ty == 4 || ty == 7) && th != 0 &&
                            th != kHashNull) {
                            et.tags_record_hash = ih;
                            et.tag_hashes.push_back(th);
                        }
                    }
                }
            }
        }
        if (et.tag_hashes.empty()) continue;
        et.tags_record_in_level_gdb = true;
        et.has_pos = got_pos;
        et.x = x;
        et.y = y;
        et.z = z;
        out.emplace(rh, std::move(et));
    }
    return out;
}

std::unordered_map<uint32_t, SpawnEntityInfo> CollectSpawnEntities(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name,
    SpawnDonorInfo* out_donor)
{
    std::unordered_map<uint32_t, SpawnEntityInfo> out;
    constexpr uint32_t kCreatureGenerator = 0xA2371C5Au;
    constexpr uint32_t kCreatureGeneratorSpawnPoint = 0x110071ADu;
    constexpr uint32_t kCreatureSpawnPoint = 0x1054A35Cu;
    constexpr uint32_t kSimpleTransformComponent = 0x619F96CFu;

    std::vector<std::unique_ptr<GdbView>> owned;
    std::vector<const GdbView*> views;
    for (const auto* g : gdbs) {
        if (!g || g->empty()) continue;
        auto v = std::make_unique<GdbView>(*g);
        if (v->ok) {
            views.push_back(v.get());
            owned.push_back(std::move(v));
        }
    }
    if (views.empty() || hash_to_name.empty()) return out;

    constexpr uint32_t kCharNavComponent = 0xC5A11B7Au;
    const GdbView* level_view = views.front();
    auto resolve_pos = [&](const GdbView& view, size_t rec,
                           SpawnEntityInfo& info) {
        float x = 0, y = 0, z = 0;
        float rx = 0, ry = 0, rz = 0;
        bool hr = false;
        size_t ps[3] = {0, 0, 0};
        size_t rs[3] = {0, 0, 0};
        if (TryComponentTransformRecord(view, rec, x, y, z, rx, ry, rz,
                                        hr, ps, rs) ||
            TryComponentTransformField(view, rec,
                                       kSimpleTransformComponent, x, y,
                                       z, rx, ry, rz, hr, ps, rs) ||
            TryComponentTransformField(view, rec, kCharNavComponent, x,
                                       y, z, rx, ry, rz, hr, ps, rs) ||
            TryTransformRecord(view, rec, x, y, z, rx, ry, rz, hr, ps,
                               rs)) {
            info.has_pos = true;
            info.x = x;
            info.y = y;
            info.z = z;
            if (&view == level_view) {
                for (int k = 0; k < 3; ++k) {
                    info.pos_off[k] = uint32_t(ps[k]);
                    info.rot_off[k] = uint32_t(rs[k]);
                }
            }
        }
    };

    auto fill_donor_entity = [&](const GdbView& view, size_t rec,
                                 uint32_t& out_template,
                                 uint32_t& out_comp_field,
                                 uint32_t& out_comp_parent,
                                 uint32_t& out_tf_field,
                                 uint32_t& out_tf_parent,
                                 uint32_t& out_pos_parent,
                                 uint32_t& out_rot_parent,
                                 uint32_t comp_field_hash) {
        size_t slot = 0;
        if (view.findLocal(rec, kHashParent, 6, slot, nullptr)) {
            out_template = ReadBeU32(view.bytes.data() + slot);
        }
        if (view.findLocal(rec, comp_field_hash, 6, slot, nullptr)) {
            out_comp_field = comp_field_hash;
            const uint32_t ch = ReadBeU32(view.bytes.data() + slot);
            size_t crec = 0;
            if (view.lookup(ch, crec)) {
                size_t ps2 = 0;
                if (view.findLocal(crec, kHashParent, 6, ps2, nullptr)) {
                    out_comp_parent =
                        ReadBeU32(view.bytes.data() + ps2);
                }
            }
        }
        const uint32_t tf_candidates[] = {
            kHashTransformComponent,
            kSimpleTransformComponent,
            kHashPhysicsSimulationKeyframedComponent,
            kHashPhysicsSimulationStaticComponent,
            0xFC8A57C5u,
            kCharNavComponent,
        };
        for (uint32_t tf : tf_candidates) {
            size_t s2 = 0;
            if (!view.findLocal(rec, tf, 6, s2, nullptr)) continue;
            const uint32_t ch = ReadBeU32(view.bytes.data() + s2);
            size_t crec = 0;
            if (!view.lookup(ch, crec)) continue;
            size_t pslot = 0, powner = 0;
            if (!view.findFieldOwner(crec, kHashPosition, 6, pslot,
                                     powner, nullptr)) {
                continue;
            }
            out_tf_field = tf;
            size_t ps2 = 0;
            if (view.findLocal(crec, kHashParent, 6, ps2, nullptr)) {
                out_tf_parent = ReadBeU32(view.bytes.data() + ps2);
            }
            auto capture_vector_parent = [&](uint32_t field_hash,
                                             uint32_t& out_parent) {
                size_t fslot = 0, fowner = 0;
                if (!view.findFieldOwner(crec, field_hash, 6, fslot,
                                         fowner, nullptr)) {
                    return;
                }
                const uint32_t vec_hash =
                    ReadBeU32(view.bytes.data() + fslot);
                size_t vec_rec = 0, vec_parent_slot = 0;
                if (view.lookup(vec_hash, vec_rec) &&
                    view.findLocal(vec_rec, kHashParent, 6,
                                   vec_parent_slot, nullptr)) {
                    out_parent = ReadBeU32(
                        view.bytes.data() + vec_parent_slot);
                }
            };
            capture_vector_parent(kHashPosition, out_pos_parent);
            capture_vector_parent(kHashRotation, out_rot_parent);
            break;
        }
    };

    constexpr uint32_t kSpawnedCreatureName = 0x2A80DD7Bu;
    constexpr uint32_t kSpawnedCreatureDefault = 0x9FFE461Eu;
    constexpr uint32_t kCreatureComponent = 0xC3B90D4Fu;

    std::unordered_map<uint32_t, uint32_t> name_fnv_to_entity;
    name_fnv_to_entity.reserve(hash_to_name.size() * 2);
    for (const auto& kv : hash_to_name) {
        uint32_t h = 0x811C9DC5u;
        for (unsigned char c : kv.second) {
            h *= 0x01000193u;
            h ^= c;
        }
        name_fnv_to_entity.emplace(h, kv.first);
    }
    for (const GdbView* vw : views) {
        if (vw->bytes.size() < 0x18) continue;
        const uint32_t pairs = ReadBeU32(vw->bytes.data() + 0x10);
        const size_t meta_end =
            vw->offset_base + size_t(vw->count) * 2;
        const size_t name_base = (meta_end + 3) & ~size_t(3);
        if (name_base + size_t(pairs) * 8 > vw->bytes.size()) continue;
        for (uint32_t i = 0; i < pairs; ++i) {
            const uint32_t a =
                ReadBeU32(vw->bytes.data() + name_base + size_t(i) * 8);
            const uint32_t b = ReadBeU32(vw->bytes.data() + name_base +
                                         size_t(i) * 8 + 4);
            name_fnv_to_entity.emplace(a, b);
            name_fnv_to_entity.emplace(b, a);
        }
    }

    auto models_for_entity = [&](uint32_t entity_hash) {
        return ModelsForEntityMulti(views, entity_hash);
    };

    std::unordered_map<uint32_t, std::string> fnv_to_name;
    fnv_to_name.reserve(hash_to_name.size() * 2);
    for (const auto& kv : hash_to_name) {
        uint32_t h = 0x811C9DC5u;
        for (unsigned char c : kv.second) {
            h *= 0x01000193u;
            h ^= c;
        }
        fnv_to_name.emplace(h, kv.second);
    }

    for (const auto& kv : hash_to_name) {
        const uint32_t entity_hash = kv.first;
        MultiGdbCursor ent;
        if (!MultiLookup(views, entity_hash, ent)) continue;
        SpawnEntityInfo info;
        MultiGdbCursor owner;
        uint32_t comp = 0;
        if (MultiFindInherited(views, ent, kCreatureGenerator, 6, owner,
                               comp) &&
            comp != 0 && comp != kHashNull) {
            info.kind = 1;
            MultiGdbCursor gen;
            if (MultiLookup(views, comp, gen)) {
                constexpr uint32_t kSpawnPointsField = 0x559B5DBFu;
                {
                    MultiGdbCursor spo;
                    uint32_t sp_list = 0;
                    if (MultiFindInherited(views, gen,
                                           kSpawnPointsField, 6, spo,
                                           sp_list) &&
                        sp_list != 0 && sp_list != kHashNull) {
                        info.spawn_points_record = sp_list;
                        MultiGdbCursor sl;
                        if (MultiLookup(views, sp_list, sl)) {
                            for (int depth = 0; depth < 8; ++depth) {
                                size_t sch = 0;
                                uint32_t nf = 0;
                                if (!sl.view->schema(sl.record, sch,
                                                     nf)) {
                                    break;
                                }
                                const size_t fh0 = sch + 4;
                                for (uint32_t i2 = 0; i2 < nf; ++i2) {
                                    const uint32_t fh = ReadBeU32(
                                        sl.view->bytes.data() + fh0 +
                                        size_t(i2) * 4);
                                    if (fh == kHashParent) continue;
                                    const uint32_t vh = ReadBeU32(
                                        sl.view->bytes.data() +
                                        sl.record + 4 +
                                        size_t(i2) * 4);
                                    if (vh && vh != kHashNull) {
                                        info.spawn_point_entities
                                            .push_back(vh);
                                    }
                                }
                                size_t pslot = 0;
                                if (!sl.view->findLocal(sl.record,
                                                        kHashParent, 6,
                                                        pslot,
                                                        nullptr)) {
                                    break;
                                }
                                const uint32_t ph = ReadBeU32(
                                    sl.view->bytes.data() + pslot);
                                MultiGdbCursor nxt;
                                if (!MultiLookup(views, ph, nxt)) break;
                                sl = nxt;
                            }
                        }
                    }
                }
                if (out_donor && !out_donor->gen_template &&
                    ent.view == level_view) {
                    fill_donor_entity(*ent.view, ent.record,
                                      out_donor->gen_template,
                                      out_donor->gen_comp_field,
                                      out_donor->gen_comp_parent,
                                      out_donor->gen_transform_field,
                                      out_donor->gen_transform_parent,
                                      out_donor->gen_position_parent,
                                      out_donor->gen_rotation_parent,
                                      kCreatureGenerator);
                    if (!out_donor->gen_template ||
                        !out_donor->gen_comp_field ||
                        !out_donor->gen_transform_field) {
                        out_donor->gen_template = 0;
                        out_donor->gen_comp_field = 0;
                        out_donor->gen_comp_parent = 0;
                        out_donor->gen_transform_field = 0;
                        out_donor->gen_transform_parent = 0;
                        out_donor->gen_position_parent = 0;
                        out_donor->gen_rotation_parent = 0;
                    }
                    if (info.spawn_points_record) {
                        size_t lrec = 0;
                        if (level_view->lookup(info.spawn_points_record,
                                               lrec)) {
                            size_t ps2 = 0;
                            if (level_view->findLocal(lrec, kHashParent,
                                                      6, ps2,
                                                      nullptr)) {
                                out_donor->spawn_list_parent = ReadBeU32(
                                    level_view->bytes.data() + ps2);
                            }
                        }
                    }
                }
                MultiGdbCursor no;
                uint32_t name_fnv = 0;
                uint8_t ty = 0;
                if (MultiFindInherited(views, gen, kSpawnedCreatureName,
                                       0xFF, no, name_fnv, &ty) &&
                    (ty == 4 || ty == 7) &&
                    name_fnv != 0 && name_fnv != kHashNull &&
                    name_fnv != kSpawnedCreatureDefault) {
                    auto fit = fnv_to_name.find(name_fnv);
                    if (fit != fnv_to_name.end()) {
                        info.creature_name = fit->second;
                    }
                    auto nit = name_fnv_to_entity.find(name_fnv);
                    if (nit != name_fnv_to_entity.end()) {
                        info.model_hashes =
                            models_for_entity(nit->second);
                    }
                }
                if (info.model_hashes.empty()) {
                    constexpr uint32_t kFamilies = 0xF44CE155u;
                    constexpr uint32_t kCreatures = 0xA1F7A17Du;
                    MultiGdbCursor fo;
                    uint32_t fam_list = 0;
                    if (MultiFindInherited(views, gen, kFamilies, 6, fo,
                                           fam_list) &&
                        fam_list != 0 && fam_list != kHashNull) {
                        MultiGdbCursor fl;
                        if (MultiLookup(views, fam_list, fl)) {
                            for (const GdbView* fv = fl.view; fv;
                                 fv = nullptr) {
                                size_t sch = 0;
                                uint32_t nf = 0;
                                if (!fv->schema(fl.record, sch, nf)) {
                                    break;
                                }
                                const size_t fhashes = sch + 4;
                                const size_t descs =
                                    fhashes + size_t(nf) * 4;
                                for (uint32_t i = 0;
                                     i < nf &&
                                     info.model_hashes.empty();
                                     ++i) {
                                    const uint32_t fh = ReadBeU32(
                                        fv->bytes.data() + fhashes +
                                        size_t(i) * 4);
                                    const uint8_t fty = uint8_t(
                                        ReadBeU32(fv->bytes.data() +
                                                  descs +
                                                  size_t(i) * 4) >>
                                        24);
                                    if (fh == kHashParent || fty != 6) {
                                        continue;
                                    }
                                    const uint32_t fam_hash = ReadBeU32(
                                        fv->bytes.data() + fl.record +
                                        4 + size_t(i) * 4);
                                    MultiGdbCursor fam;
                                    if (!MultiLookup(views, fam_hash,
                                                     fam)) {
                                        continue;
                                    }
                                    MultiGdbCursor co2;
                                    uint32_t creatures = 0;
                                    if (!MultiFindInherited(
                                            views, fam, kCreatures, 6,
                                            co2, creatures) ||
                                        creatures == 0 ||
                                        creatures == kHashNull) {
                                        continue;
                                    }
                                    MultiGdbCursor cl;
                                    if (!MultiLookup(views, creatures,
                                                     cl)) {
                                        continue;
                                    }
                                    for (const auto pr :
                                         {uint8_t(7), uint8_t(4)}) {
                                        size_t sch2 = 0;
                                        uint32_t nf2 = 0;
                                        if (!cl.view->schema(cl.record,
                                                             sch2,
                                                             nf2)) {
                                            break;
                                        }
                                        const size_t h2 = sch2 + 4;
                                        const size_t d2 =
                                            h2 + size_t(nf2) * 4;
                                        for (uint32_t j = 0; j < nf2;
                                             ++j) {
                                            const uint32_t cfh =
                                                ReadBeU32(
                                                    cl.view->bytes
                                                        .data() +
                                                    h2 +
                                                    size_t(j) * 4);
                                            const uint8_t cty2 =
                                                uint8_t(ReadBeU32(
                                                    cl.view->bytes
                                                        .data() +
                                                    d2 +
                                                    size_t(j) * 4) >>
                                                    24);
                                            if (cfh == kHashParent ||
                                                cty2 != pr) {
                                                continue;
                                            }
                                            const uint32_t ch2 =
                                                ReadBeU32(
                                                    cl.view->bytes
                                                        .data() +
                                                    cl.record + 4 +
                                                    size_t(j) * 4);
                                            if (ch2 == 0 ||
                                                ch2 == kHashNull) {
                                                continue;
                                            }
                                            info.model_hashes =
                                                models_for_entity(ch2);
                                            if (!info.model_hashes
                                                     .empty()) {
                                                break;
                                            }
                                        }
                                        if (!info.model_hashes
                                                 .empty()) {
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else if ((MultiFindInherited(views, ent,
                                       kCreatureGeneratorSpawnPoint, 6,
                                       owner, comp) ||
                    MultiFindInherited(views, ent, kCreatureSpawnPoint,
                                       6, owner, comp)) &&
                   comp != 0 && comp != kHashNull) {
            info.kind = 2;
            if (out_donor && !out_donor->sp_template &&
                ent.view == level_view) {
                for (uint32_t cf : {kCreatureGeneratorSpawnPoint,
                                    kCreatureSpawnPoint}) {
                    size_t s3 = 0;
                    if (!ent.view->findLocal(ent.record, cf, 6, s3,
                                             nullptr)) {
                        continue;
                    }
                    fill_donor_entity(*ent.view, ent.record,
                                      out_donor->sp_template,
                                      out_donor->sp_comp_field,
                                      out_donor->sp_comp_parent,
                                      out_donor->sp_transform_field,
                                      out_donor->sp_transform_parent,
                                      out_donor->sp_position_parent,
                                      out_donor->sp_rotation_parent,
                                      cf);
                    if (!out_donor->sp_template ||
                        !out_donor->sp_comp_field ||
                        !out_donor->sp_transform_field) {
                        out_donor->sp_template = 0;
                        out_donor->sp_comp_field = 0;
                        out_donor->sp_comp_parent = 0;
                        out_donor->sp_transform_field = 0;
                        out_donor->sp_transform_parent = 0;
                        out_donor->sp_position_parent = 0;
                        out_donor->sp_rotation_parent = 0;
                        continue;
                    }
                    break;
                }
            }
        } else {
            MultiGdbCursor so;
            uint32_t skel = 0;
            uint8_t sty = 0;
            if ((MultiFindInherited(views, ent, kHashSkeletonFile, 0xFF,
                                    so, skel, &sty) &&
                 (sty == 4 || sty == 7) && skel != 0 &&
                 skel != kHashNull) ||
                (MultiFindInherited(views, ent, kCreatureComponent, 6,
                                    so, skel) &&
                 skel != 0 && skel != kHashNull)) {
                info.kind = 3;
                info.model_hashes = models_for_entity(entity_hash);
            }
        }
        if (!info.kind) continue;
        resolve_pos(*ent.view, ent.record, info);
        out.emplace(entity_hash, info);
    }
    return out;
}

namespace {

std::string PrettifyTagLabel(std::string tag)
{
    for (const char* pfx : { "INV_ITEM_", "OBJECT_", "TEXT_" }) {
        const size_t n = std::strlen(pfx);
        if (tag.size() > n && tag.compare(0, n, pfx) == 0) {
            tag = tag.substr(n);
            break;
        }
    }
    if (tag.size() > 5 && tag.compare(tag.size() - 5, 5, "_NAME") == 0) {
        tag.resize(tag.size() - 5);
    }
    const bool has_lower = std::any_of(
        tag.begin(), tag.end(),
        [](unsigned char c) { return std::islower(c) != 0; });
    if (tag.find('_') != std::string::npos || !has_lower) {
        bool word_start = true;
        for (auto& c : tag) {
            if (c == '_') {
                c = ' ';
                word_start = true;
            } else {
                c = word_start ? char(std::toupper((unsigned char)c))
                               : char(std::tolower((unsigned char)c));
                word_start = false;
            }
        }
    }
    return tag;
}

}

std::vector<ItemCatalogEntry> BuildItemCatalog(
    const std::vector<const std::vector<uint8_t>*>& gdbs)
{
    std::vector<ItemCatalogEntry> out;

    std::vector<std::unique_ptr<GdbView>> owned;
    std::vector<const GdbView*> views;
    std::vector<bool> view_is_level;
    for (size_t gi = 0; gi < gdbs.size(); ++gi) {
        const auto* g = gdbs[gi];
        if (!g || g->empty()) continue;
        auto v = std::make_unique<GdbView>(*g);
        if (v->ok) {
            views.push_back(v.get());
            view_is_level.push_back(gi == 0);
            owned.push_back(std::move(v));
        }
    }
    if (views.empty()) return out;

    std::unordered_map<uint32_t, std::string> dict;
    for (const auto* g : gdbs) {
        if (!g || g->empty()) continue;
        auto d = LoadEmbeddedDict(*g);
        dict.insert(d.begin(), d.end());
    }

    std::unordered_set<uint32_t> seen;
    for (size_t vi = 0; vi < views.size(); ++vi) {
        const GdbView& view = *views[vi];
        for (uint32_t i = 0; i < view.count; ++i) {
            if (i >= view.record_data_offsets.size()) break;
            const size_t rec = view.record_data_offsets[i];
            size_t slot = 0;
            if (!view.findLocal(rec, kHashInventoryItemComponent, 6,
                                slot, nullptr)) {
                continue;
            }
            const uint32_t rec_hash =
                ReadBeU32(view.bytes.data() + view.hash_base +
                          size_t(i) * 4);
            if (!seen.insert(rec_hash).second) continue;

            EntityContentsItem info;
            ReadContentsItemInfo(views, rec_hash, info, &dict);

            ItemCatalogEntry e;
            e.record_hash = rec_hash;
            e.money = info.money;
            e.from_level = view_is_level[vi];
            std::string raw;
            auto dit = dict.find(rec_hash);
            if (dit != dict.end() && !dit->second.empty()) {
                raw = dit->second;
            } else if (!info.name_tag.empty()) {
                raw = info.name_tag;
            }
            if (!raw.empty()) {
                e.label = PrettifyTagLabel(std::move(raw));
            } else {
                char buf[24];
                std::snprintf(buf, sizeof(buf), "unnamed 0x%08X",
                              rec_hash);
                e.label = buf;
                e.unnamed = true;
            }
            out.push_back(std::move(e));
        }
    }

    std::sort(out.begin(), out.end(),
              [](const ItemCatalogEntry& a, const ItemCatalogEntry& b) {
                  if (a.unnamed != b.unnamed) return b.unnamed;
                  const size_t n = std::min(a.label.size(),
                                            b.label.size());
                  for (size_t i = 0; i < n; ++i) {
                      const int ca = std::tolower(
                          (unsigned char)a.label[i]);
                      const int cb = std::tolower(
                          (unsigned char)b.label[i]);
                      if (ca != cb) return ca < cb;
                  }
                  return a.label.size() < b.label.size();
              });

    out.erase(std::unique(out.begin(), out.end(),
                          [](const ItemCatalogEntry& a,
                             const ItemCatalogEntry& b) {
                              return !a.unnamed && !b.unnamed &&
                                     a.money == b.money &&
                                     a.label == b.label;
                          }),
              out.end());
    return out;
}

std::vector<ItemDetail> BuildItemDetails(
    const std::vector<const std::vector<uint8_t>*>& gdbs)
{
    std::vector<ItemDetail> out;
    constexpr uint32_t kNameTag = 0x9555A6FCu;
    constexpr uint32_t kDescTag = 0xD823B12Bu;
    constexpr uint32_t kIconGraphic = 0x46F0F9CEu;

    std::vector<std::unique_ptr<GdbView>> owned;
    std::vector<const GdbView*> views;
    for (const auto* g : gdbs) {
        if (!g || g->empty()) continue;
        auto v = std::make_unique<GdbView>(*g);
        if (v->ok) {
            views.push_back(v.get());
            owned.push_back(std::move(v));
        }
    }
    if (views.empty()) return out;

    std::unordered_map<uint32_t, std::string> dict;
    for (const auto* g : gdbs) {
        if (!g || g->empty()) continue;
        auto d = LoadEmbeddedDict(*g);
        dict.insert(d.begin(), d.end());
    }
    auto dict_name = [&](uint32_t h) -> std::string {
        auto it = dict.find(h);
        return it != dict.end() ? it->second : std::string();
    };

    auto is_stat_field = [&](const std::string& nm) {
        static const char* kSkip[] = {
            "GUIScreen", "Icon", "parent", "Component", "Tag",
            "Offset", "Rotation", "Translation", "Mesh", "Model",
            "Texture", "ScriptName", "SoundEvent", "Expression",
        };
        for (const char* s : kSkip) {
            if (nm.find(s) != std::string::npos) return false;
        }
        return true;
    };
    constexpr uint32_t kAugmentable = 0x9DC93C5Bu;
    constexpr uint32_t kAugSlots[] = {
        0xFCF3EB8Eu, 0xFCF3EB8Du, 0xFCF3EB8Cu, 0xFCF3EB8Bu,
        0xFCF3EB8Au, 0xFCF3EB89u, 0xFCF3EB88u, 0xFCF3EB87u,
        0xFCF3EB86u,
    };

    std::unordered_set<uint32_t> seen;
    for (const GdbView* vw : views) {
        const GdbView& view = *vw;
        for (uint32_t i = 0; i < view.count; ++i) {
            if (i >= view.record_data_offsets.size()) break;
            const size_t rec = view.record_data_offsets[i];

            // an item entry: a record that locally names/describes/owns
            // an item AND resolves an InventoryItemComponent through its
            // chain. This catches base items (local component) and
            // legendary variants (local name, inherited component).
            // item if it locally owns an InventoryItemComponent, OR
            // locally names an "INV_ITEM_*" tag (base items + named
            // variants like The Chopper)
            size_t name_slot = 0;
            uint8_t name_ty = 0;
            const bool has_local_comp = view.findLocal(
                rec, kHashInventoryItemComponent, 6, name_slot, nullptr);
            bool is_inv_named = false;
            if (view.findLocal(rec, kNameTag, 0xFF, name_slot,
                               &name_ty) &&
                (name_ty == 4 || name_ty == 7)) {
                const uint32_t nh =
                    ReadBeU32(view.bytes.data() + name_slot);
                const std::string ns = dict_name(nh);
                is_inv_named = ns.compare(0, 9, "INV_ITEM_") == 0;
            }
            if (!has_local_comp && !is_inv_named) continue;

            const uint32_t rec_hash =
                ReadBeU32(view.bytes.data() + view.hash_base +
                          size_t(i) * 4);
            if (!seen.insert(rec_hash).second) continue;

            ItemDetail d;
            d.record_hash = rec_hash;

            EntityContentsItem info;
            ReadContentsItemInfo(views, rec_hash, info, &dict);
            d.money = info.money;

            // preset money-bag loot amounts carry a MoneyComponent but
            // no item name; flag them so the UI can special-case them
            {
                constexpr uint32_t kMoneyComponent = 0xE21AB7A0u;
                MultiGdbCursor mo;
                uint32_t mh = 0;
                MultiGdbCursor mc0{vw, rec};
                if (MultiFindInherited(views, mc0, kMoneyComponent, 6,
                                       mo, mh) &&
                    mh != 0 && mh != kHashNull) {
                    d.is_money = true;
                }
            }

            MultiGdbCursor cur{vw, rec};
            // the item's InventoryItemComponent (some items keep their
            // NameTag/DescTag/Icon on the component, not the record)
            MultiGdbCursor inv_comp;
            bool have_inv_comp = false;
            {
                MultiGdbCursor io;
                uint32_t ih = 0;
                if (MultiFindInherited(views, cur,
                                       kHashInventoryItemComponent, 6,
                                       io, ih) &&
                    ih != 0 && ih != kHashNull) {
                    have_inv_comp = MultiLookup(views, ih, inv_comp);
                }
            }
            // resolve a t4/t7 tag from the record chain, then the
            // component chain
            auto find_tag = [&](uint32_t field) -> uint32_t {
                MultiGdbCursor o;
                uint32_t v = 0;
                uint8_t t = 0;
                if (MultiFindInherited(views, cur, field, 0xFF, o, v,
                                       &t) &&
                    (t == 4 || t == 7) && v != kHashNull) {
                    return v;
                }
                if (have_inv_comp &&
                    MultiFindInherited(views, inv_comp, field, 0xFF, o,
                                       v, &t) &&
                    (t == 4 || t == 7) && v != kHashNull) {
                    return v;
                }
                return 0;
            };
            d.name_tag = find_tag(kNameTag);
            d.desc_tag = find_tag(kDescTag);
            const uint32_t icon = find_tag(kIconGraphic);
            if (icon) d.icon_tex = dict_name(icon);

            // model: check the item record, then walk its parent chain
            // across views (legendary variants share their base's mesh)
            {
                MultiGdbCursor mw{vw, rec};
                for (int md = 0; md < 24 && !d.model_path_hash; ++md) {
                    const auto mh = CollectModelPathHashesForRecord(
                        *mw.view, mw.record);
                    if (!mh.empty()) {
                        d.model_path_hash = mh.front();
                        d.model_path = dict_name(mh.front());
                        break;
                    }
                    size_t pslot = 0;
                    if (!mw.view->findLocal(mw.record, kHashParent, 6,
                                            pslot, nullptr)) {
                        break;
                    }
                    const uint32_t ph =
                        ReadBeU32(mw.view->bytes.data() + pslot);
                    MultiGdbCursor nxt;
                    if (ph == 0 || ph == kHashNull ||
                        !MultiLookup(views, ph, nxt)) {
                        break;
                    }
                    mw = nxt;
                }
            }

            // weapons keep no ModelFile in the GDB, but their meshes
            // exist as art\inventory\weapons\<type>_<quality>.mdl named
            // exactly after the item tag (KATANA_IRONBASE -> katana_iron,
            // KATANA_LEGENDARY -> katana_legendary). Derive the leaf so
            // the model resolver can find it.
            if (d.model_path.empty() && d.name_tag) {
                const std::string tag = dict_name(d.name_tag);
                static const std::string kWp = "INV_ITEM_WEAPON_";
                if (tag.compare(0, kWp.size(), kWp) == 0) {
                    std::string core = tag.substr(kWp.size());
                    const size_t np = core.rfind("_NAME");
                    if (np != std::string::npos) core.resize(np);
                    size_t bp;
                    while ((bp = core.find("BASE")) !=
                           std::string::npos) {
                        core.erase(bp, 4);
                    }
                    for (char& c : core) {
                        c = char(std::tolower((unsigned char)c));
                    }
                    // "katana" alone (generic base) has no mesh; only
                    // qualified variants do
                    if (core.find('_') != std::string::npos) {
                        d.model_path = core + ".mdl";
                    }
                }
            }

            std::string raw = dict_name(d.name_tag);
            if (raw.empty()) raw = dict_name(rec_hash);
            if (raw.empty()) raw = info.name_tag;
            if (!raw.empty()) {
                d.label = PrettifyTagLabel(raw);
            } else {
                char buf[24];
                std::snprintf(buf, sizeof(buf), "unnamed 0x%08X",
                              rec_hash);
                d.label = buf;
                d.unnamed = true;
            }

            std::unordered_set<uint32_t> stat_seen;
            // collect scalar fields (float/int/enum/bool) from a record
            // and its parent chain, skipping component pointers
            auto collect_scalars = [&](MultiGdbCursor start) {
                MultiGdbCursor walk = start;
                for (int depth = 0;
                     depth < 24 && d.stats.size() < 40; ++depth) {
                    size_t sch = 0;
                    uint32_t nf = 0;
                    if (!walk.view->schema(walk.record, sch, nf)) break;
                    const size_t h0 = sch + 4;
                    const size_t d0 = h0 + size_t(nf) * 4;
                    for (uint32_t f = 0;
                         f < nf && d.stats.size() < 40; ++f) {
                        const uint32_t fh =
                            ReadBeU32(walk.view->bytes.data() + h0 +
                                      size_t(f) * 4);
                        if (fh == kHashParent) continue;
                        if (!stat_seen.insert(fh).second) continue;
                        const uint32_t desc =
                            ReadBeU32(walk.view->bytes.data() + d0 +
                                      size_t(f) * 4);
                        const uint8_t ft = uint8_t(desc >> 24);
                        if (ft != 0 && ft != 1 && ft != 3 && ft != 5) {
                            continue;
                        }
                        const std::string fn = dict_name(fh);
                        if (fn.empty() || !is_stat_field(fn)) continue;
                        const size_t vslot =
                            walk.record + 4 + size_t(f) * 4;
                        if (vslot + 4 > walk.view->body_end) continue;
                        const uint8_t* vp =
                            walk.view->bytes.data() + vslot;
                        char vb[48];
                        if (ft == 3) {
                            std::snprintf(vb, sizeof(vb), "%.3g",
                                          ReadBeF32(vp));
                        } else if (ft == 0) {
                            std::snprintf(vb, sizeof(vb), "%s",
                                          ReadBeU32(vp) ? "yes" : "no");
                        } else {
                            std::snprintf(vb, sizeof(vb), "%u",
                                          ReadBeU32(vp));
                        }
                        d.stats.emplace_back(fn, vb);
                    }
                    size_t pslot = 0;
                    if (!walk.view->findLocal(walk.record, kHashParent,
                                              6, pslot, nullptr)) {
                        break;
                    }
                    const uint32_t ph =
                        ReadBeU32(walk.view->bytes.data() + pslot);
                    MultiGdbCursor nxt;
                    if (ph == 0 || ph == kHashNull ||
                        !MultiLookup(views, ph, nxt)) {
                        break;
                    }
                    walk = nxt;
                }
            };

            // top-level item scalars (Rating, Category, flags)
            collect_scalars(cur);
            // stat-bearing components (weapon damage, firearm, armour,
            // money value, etc.)
            constexpr uint32_t kStatComps[] = {
                0x2C34431Eu,  // WeaponComponent
                0x0A644D42u,  // FirearmComponent
                0xE21AB7A0u,  // MoneyComponent
                0x6D04D9A2u,  // Material (armour rating lives near here)
            };
            for (uint32_t sc_hash : kStatComps) {
                MultiGdbCursor so;
                uint32_t comp = 0;
                if (MultiFindInherited(views, cur, sc_hash, 6, so,
                                       comp) &&
                    comp != 0 && comp != kHashNull) {
                    MultiGdbCursor cc;
                    if (MultiLookup(views, comp, cc)) {
                        collect_scalars(cc);
                    }
                }
            }

            // pre-installed augments (AugmentableComponent slots)
            {
                MultiGdbCursor ao;
                uint32_t aug_comp = 0;
                if (MultiFindInherited(views, cur, kAugmentable, 6, ao,
                                       aug_comp) &&
                    aug_comp != 0 && aug_comp != kHashNull) {
                    MultiGdbCursor ac;
                    if (MultiLookup(views, aug_comp, ac)) {
                        int an = 0;
                        for (uint32_t asl : kAugSlots) {
                            MultiGdbCursor so;
                            uint32_t ah = 0;
                            uint8_t aty = 0;
                            if (!MultiFindInherited(views, ac, asl, 0xFF,
                                                    so, ah, &aty) ||
                                ah == 0 || ah == kHashNull) {
                                continue;
                            }
                            std::string an_name = dict_name(ah);
                            if (an_name.empty()) {
                                MultiGdbCursor ar;
                                if (MultiLookup(views, ah, ar)) {
                                    MultiGdbCursor no;
                                    uint32_t nt = 0;
                                    uint8_t nty = 0;
                                    if (MultiFindInherited(
                                            views, ar, kNameTag, 0xFF,
                                            no, nt, &nty) &&
                                        (nty == 4 || nty == 7)) {
                                        an_name = dict_name(nt);
                                    }
                                }
                            }
                            if (an_name.empty()) continue;
                            char lbl[24];
                            std::snprintf(lbl, sizeof(lbl),
                                          "Augment %d", ++an);
                            d.stats.emplace_back(
                                lbl, PrettifyTagLabel(an_name));
                        }
                    }
                }
            }

            out.push_back(std::move(d));
        }
    }

    std::sort(out.begin(), out.end(),
              [](const ItemDetail& a, const ItemDetail& b) {
                  if (a.unnamed != b.unnamed) return b.unnamed;
                  const size_t n =
                      std::min(a.label.size(), b.label.size());
                  for (size_t i = 0; i < n; ++i) {
                      const int ca =
                          std::tolower((unsigned char)a.label[i]);
                      const int cb =
                          std::tolower((unsigned char)b.label[i]);
                      if (ca != cb) return ca < cb;
                  }
                  return a.label.size() < b.label.size();
              });
    return out;
}

}
