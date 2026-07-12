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

    for (const GdbView* vw : views) {
        const GdbView& view = *vw;
        for (uint32_t i = 0; i < view.count; ++i) {
            if (i >= view.record_data_offsets.size()) break;
            const size_t rec = view.record_data_offsets[i];

            bool has_graphics = false;
            for (uint32_t gc : kGraphicsComponents) {
                size_t slot = 0;
                if (view.findLocal(rec, gc, 6, slot, nullptr)) {
                    has_graphics = true;
                    break;
                }
            }
            if (!has_graphics) {
                // derived templates inherit their graphics but override
                // the physics component locally; accept those too, but
                // reject placed instances (their transform component
                // resolves to a world Position)
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

}
