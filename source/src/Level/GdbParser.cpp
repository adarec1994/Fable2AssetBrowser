#include "GdbParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Gdb {

namespace {

inline uint32_t ReadBeU32(const uint8_t* p) {
    return  (uint32_t(p[0]) << 24) |
            (uint32_t(p[1]) << 16) |
            (uint32_t(p[2]) <<  8) |
             uint32_t(p[3]);
}

inline float ReadBeF32(const uint8_t* p) {
    uint32_t v = ReadBeU32(p);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

inline bool Finite3(float x, float y, float z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

constexpr uint32_t kVarMarker    = 0x00004B80;
constexpr uint32_t kInlineVec3SchemaRel = 0x00000568;
constexpr uint32_t kHashParent   = 0x5F6317D5;
constexpr uint32_t kHashPosition = 0xBD7C27D4;
constexpr uint32_t kHashRotation = 0x21EBC83B;
constexpr uint32_t kHashTransformComponent = 0xF73572C4;
constexpr uint32_t kHashGraphicAppearanceComponent = 0xA7B6EF56;
constexpr uint32_t kHashStaticMeshComponent = 0x29CF50D1;
constexpr uint32_t kHashStaticMultipleMeshComponent = 0xCE642A15;
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
constexpr uint32_t kHashVecZ = 0x050C5D45;
constexpr uint32_t kHashVecY = 0x050C5D46;
constexpr uint32_t kHashVecX = 0x050C5D47;
constexpr size_t   kHeaderSize   = 0x18;

struct GdbView {
    const std::vector<uint8_t>& bytes;
    uint32_t count = 0;
    uint32_t size_a = 0;
    uint32_t size_b = 0;
    size_t body_start = kHeaderSize;
    size_t body_end = 0;
    size_t schema_base = 0;
    size_t hash_base = 0;
    size_t offset_base = 0;
    bool ok = false;
    // 010 F2GDB.bt layout: Hashes[i] points to RecordData[i]. The first
    // dword in each RecordData entry is an offset into the Records block.
    std::vector<size_t> record_data_offsets;

    explicit GdbView(const std::vector<uint8_t>& b) : bytes(b) {
        if (bytes.size() < kHeaderSize) return;
        if (bytes[0] != 'G' || bytes[1] != 'D' || bytes[2] != 'B' ||
            bytes[3] != 0) {
            return;
        }
        count = ReadBeU32(bytes.data() + 0x04);
        size_a = ReadBeU32(bytes.data() + 0x08);
        size_b = ReadBeU32(bytes.data() + 0x0C);
        if (count == 0) return;
        schema_base = kHeaderSize + size_t(size_a);
        hash_base = schema_base + size_t(size_b);
        offset_base = hash_base + size_t(count) * 4;
        body_end = schema_base;
        if (body_end > bytes.size() ||
            offset_base + size_t(count) * 2 > bytes.size()) {
            return;
        }
        if (!buildRecordDataOffsets()) return;
        ok = true;
    }

    bool buildRecordDataOffsets() {
        record_data_offsets.clear();
        record_data_offsets.reserve(count);
        size_t cur = body_start;
        for (uint32_t i = 0; i < count; ++i) {
            if (cur + 4 > body_end) return false;
            record_data_offsets.push_back(cur);

            size_t schema_off = 0;
            uint32_t field_count = 0;
            if (!schemaAtRecordData(cur, schema_off, field_count)) {
                return false;
            }
            const size_t entry_size = 4 + size_t(field_count) * 4;
            if (entry_size < 4 || cur + entry_size > body_end) {
                return false;
            }
            cur += entry_size;
        }
        return true;
    }

    bool lookup(uint32_t hash, size_t& record) const {
        if (!ok) return false;
        size_t lo = 0;
        size_t hi = count;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            uint32_t v = ReadBeU32(bytes.data() + hash_base + mid * 4);
            if (v < hash) lo = mid + 1;
            else          hi = mid;
        }
        if (lo >= count) return false;
        uint32_t found = ReadBeU32(bytes.data() + hash_base + lo * 4);
        if (found != hash) return false;
        if (lo >= record_data_offsets.size()) return false;
        record = record_data_offsets[lo];
        return record + 4 <= body_end;
    }

    bool schemaAtRecordData(size_t record,
                            size_t& schema_off,
                            uint32_t& field_count) const {
        if (record + 4 > body_end) return false;
        uint32_t rel = ReadBeU32(bytes.data() + record);
        schema_off = schema_base + size_t(rel);
        if (schema_off + 4 > hash_base) return false;
        uint32_t header = ReadBeU32(bytes.data() + schema_off);
        field_count = header >> 8;
        if (field_count > 256) {
            const uint8_t* p = bytes.data() + schema_off;
            const uint32_t count1_le =
                uint32_t(p[0]) | (uint32_t(p[1]) << 8);
            const uint32_t count2 = uint32_t(p[2]);
            field_count = count1_le + count2;
            if (field_count > 1024) return false;
        }
        return schema_off + 4 + size_t(field_count) * 8 <= hash_base;
    }

    bool schema(size_t record, size_t& schema_off, uint32_t& field_count) const {
        if (!ok) return false;
        return schemaAtRecordData(record, schema_off, field_count);
    }

    bool findLocal(size_t record,
                   uint32_t field_hash,
                   uint8_t expected_type,
                   size_t& slot,
                   uint8_t* found_type = nullptr) const {
        size_t sch = 0;
        uint32_t n = 0;
        if (!schema(record, sch, n)) return false;
        const size_t hashes = sch + 4;
        const size_t descs = hashes + size_t(n) * 4;
        for (uint32_t i = 0; i < n; ++i) {
            if (ReadBeU32(bytes.data() + hashes + size_t(i) * 4) != field_hash) {
                continue;
            }
            const uint32_t desc = ReadBeU32(bytes.data() + descs + size_t(i) * 4);
            const uint8_t type = uint8_t(desc >> 24);
            if (expected_type != 0xFF && type != expected_type) return false;
            slot = record + 4 + size_t(i) * 4;
            if (slot + 4 > body_end) return false;
            if (found_type) *found_type = type;
            return true;
        }
        return false;
    }

    bool findField(size_t record,
                   uint32_t field_hash,
                   uint8_t expected_type,
                   size_t& slot,
                   uint8_t* found_type = nullptr) const {
        size_t owner = 0;
        return findFieldOwner(record, field_hash, expected_type, slot, owner,
                              found_type);
    }

    bool findFieldOwner(size_t record,
                        uint32_t field_hash,
                        uint8_t expected_type,
                        size_t& slot,
                        size_t& owner,
                        uint8_t* found_type = nullptr) const {
        size_t cur = record;
        for (int depth = 0; depth < 64; ++depth) {
            if (findLocal(cur, field_hash, expected_type, slot, found_type)) {
                owner = cur;
                return true;
            }
            size_t parent_slot = 0;
            if (!findLocal(cur, kHashParent, 6, parent_slot, nullptr)) {
                return false;
            }
            uint32_t parent_hash = ReadBeU32(bytes.data() + parent_slot);
            if (parent_hash == 0) return false;
            size_t parent_rec = 0;
            if (!lookup(parent_hash, parent_rec)) return false;
            if (parent_rec == cur) return false;
            cur = parent_rec;
        }
        return false;
    }

    bool readLocalFloat(size_t record, uint32_t field_hash, float& value) const {
        size_t slot = 0;
        if (!findLocal(record, field_hash, 3, slot, nullptr)) return false;
        if (slot + 4 > body_end) return false;
        value = ReadBeF32(bytes.data() + slot);
        return std::isfinite(value);
    }

    bool readVectorFields(size_t record,
                          float& vx,
                          float& vy,
                          float& vz) const {
        // The first dword of an indexed GDB record is a schema-relative
        // pointer.  Market's vector schema happens to live at 0x568, but other
        // levels place the same x/y/z schema elsewhere.
        return readLocalFloat(record, kHashVecX, vx) &&
               readLocalFloat(record, kHashVecY, vy) &&
               readLocalFloat(record, kHashVecZ, vz);
    }

    bool hasVectorSchema(size_t record) const {
        float v = 0.0f;
        return readLocalFloat(record, kHashVecZ, v) &&
               readLocalFloat(record, kHashVecY, v) &&
               readLocalFloat(record, kHashVecX, v);
    }

    bool readVec3Record(size_t record,
                        float& x,
                        float& y,
                        float& z,
                        float* raw_x = nullptr,
                        float* raw_y = nullptr,
                        float* raw_z = nullptr) const {
        float vx = 0.0f, vy = 0.0f, vz = 0.0f;
        if (!readVectorFields(record, vx, vy, vz)) return false;
        if (!Finite3(vx, vy, vz)) return false;
        x = vx;
        y = vy;
        z = vz;
        if (raw_x) *raw_x = vx;
        if (raw_y) *raw_y = vy;
        if (raw_z) *raw_z = vz;
        return true;
    }

    bool readVec3Ref(uint32_t hash,
                     float& x,
                     float& y,
                     float& z,
                     float* raw_x = nullptr,
                     float* raw_y = nullptr,
                     float* raw_z = nullptr) const {
        size_t rec = 0;
        return lookup(hash, rec) && readVec3Record(rec, x, y, z, raw_x, raw_y, raw_z);
    }

    bool readRotationVec3Ref(uint32_t hash,
                             float& x,
                             float& y,
                             float& z) const {
        size_t rec = 0;
        if (!lookup(hash, rec)) return false;
        if (!hasVectorSchema(rec) || rec + 16 > body_end) return false;
        x = ReadBeF32(bytes.data() + rec + 4);
        y = ReadBeF32(bytes.data() + rec + 8);
        z = ReadBeF32(bytes.data() + rec + 12);
        return Finite3(x, y, z);
    }

    bool payloadRange(size_t record, size_t& payload_start, size_t& payload_end) const {
        size_t sch = 0;
        uint32_t n = 0;
        if (!schema(record, sch, n)) return false;
        payload_start = record + 4 + size_t(n) * 4;
        if (payload_start > body_end) return false;

        payload_end = body_end;
        if (ok) {
            auto it = std::upper_bound(record_data_offsets.begin(),
                                       record_data_offsets.end(),
                                       record);
            if (it != record_data_offsets.end()) {
                payload_end = *it;
            }
        }
        payload_end = std::min(payload_end, payload_start + size_t(0x200));
        return payload_start + 4 <= payload_end;
    }
};

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
};

bool TryTransformRecord(const GdbView& view,
                        size_t record,
                        float& x,
                        float& y,
                        float& z,
                        float& rot_x,
                        float& rot_y,
                        float& rot_z,
                        bool& has_rotation) {
    size_t pos_slot = 0;
    size_t pos_owner = 0;
    if (!view.findFieldOwner(record, kHashPosition, 6,
                             pos_slot, pos_owner, nullptr)) {
        return false;
    }
    const uint32_t pos_hash = ReadBeU32(view.bytes.data() + pos_slot);
    if (!view.readVec3Ref(pos_hash, x, y, z)) return false;
    if (!PlausiblePosition(x, y, z)) return false;

    size_t rot_slot = 0;
    float rx = 0.0f, ry = 0.0f, rz = 0.0f;
    if (view.findLocal(pos_owner, kHashRotation, 6, rot_slot, nullptr)) {
        const uint32_t rot_hash = ReadBeU32(view.bytes.data() + rot_slot);
        if (view.readRotationVec3Ref(rot_hash, rx, ry, rz)) {
            if (Finite3(rx, ry, rz)) {
                rot_x = rx;
                rot_y = ry;
                rot_z = rz;
                has_rotation = true;
            }
        }
    }
    return true;
}

bool TryComponentTransformRecord(const GdbView& view,
                                 size_t record,
                                 float& x,
                                 float& y,
                                 float& z,
                                 float& rot_x,
                                 float& rot_y,
                                 float& rot_z,
                                 bool& has_rotation) {
    size_t transform_slot = 0;
    if (!view.findLocal(record, kHashTransformComponent, 6,
                        transform_slot, nullptr)) {
        return false;
    }

    const uint32_t transform_hash =
        ReadBeU32(view.bytes.data() + transform_slot);
    size_t transform_record = 0;
    if (!view.lookup(transform_hash, transform_record)) return false;
    return TryTransformRecord(view, transform_record, x, y, z,
                              rot_x, rot_y, rot_z, has_rotation);
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
        // Static-multiple mesh records often store the model path hash
        // directly in the Mesh field while also being a valid GDB record hash.
        // Keep the raw value so paths like BS_Market_Tavern exterior/interior
        // resolve even when the referenced record only carries flags.
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
    case kHashStaticMeshComponent: return "GraphicAppearanceStaticMeshComponent";
    case kHashStaticMultipleMeshComponent:
        return "GraphicAppearanceStaticMultipleMeshComponent";
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
    case 0x5883C406u: return "PhysicsSimulationStaticComponent";
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

std::string GdbFieldNameResolved(uint32_t hash)
{
    const char* known = GdbFieldName(hash);
    if (known && *known) return known;
    const auto& enum_names = ExternalEnumNames();
    auto it = enum_names.find(hash);
    if (it != enum_names.end()) return it->second;
    return {};
}

const char* GdbFieldTypeName(uint8_t type)
{
    switch (type) {
    case 3: return "f32/u32";
    case 4: return "u32";
    case 6: return "ref";
    default: return "";
    }
}

std::string CleanDebugName(std::string s)
{
    for (char& c : s) {
        if (c == '\t' || c == '\r' || c == '\n') c = ' ';
    }
    return s;
}

void DebugDumpLocalFields(
    const GdbView& view,
    const std::unordered_map<uint32_t, std::string>& name_by_hash,
    size_t record,
    const std::string& prefix,
    std::ostringstream& out)
{
    size_t sch = 0;
    uint32_t n = 0;
    if (!view.schema(record, sch, n)) {
        out << prefix << "schema\tinvalid\n";
        return;
    }

    const size_t hashes = sch + 4;
    const size_t descs = hashes + size_t(n) * 4;
    out << prefix << "schema\toff=" << HexOff(sch)
        << "\tfields=" << n << "\n";
    out << prefix
        << "field_index\tfield_hash\tfield_name\ttype\ttype_name\tslot"
        << "\traw_u32\tas_f32\tref_record\tref_name\n";

    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t field_hash =
            ReadBeU32(view.bytes.data() + hashes + size_t(i) * 4);
        const uint32_t desc =
            ReadBeU32(view.bytes.data() + descs + size_t(i) * 4);
        const uint8_t type = uint8_t(desc >> 24);
        const size_t slot = record + 4 + size_t(i) * 4;
        uint32_t raw = 0;
        float as_f32 = 0.0f;
        bool have_value = false;
        if (slot + 4 <= view.body_end) {
            raw = ReadBeU32(view.bytes.data() + slot);
            as_f32 = ReadBeF32(view.bytes.data() + slot);
            have_value = true;
        }

        size_t ref_record = 0;
        const bool has_ref = have_value && type == 6 &&
                             view.lookup(raw, ref_record);
        auto name_it = name_by_hash.find(raw);

        out << prefix << i
            << '\t' << Hex32(field_hash)
            << '\t' << GdbFieldNameResolved(field_hash)
            << '\t' << int(type)
            << '\t' << GdbFieldTypeName(type)
            << '\t' << HexOff(slot)
            << '\t' << (have_value ? Hex32(raw) : std::string())
            << '\t';
        if (have_value && type == 3 && std::isfinite(as_f32)) {
            out << std::setprecision(8) << as_f32;
        }
        out << '\t' << (has_ref ? HexOff(ref_record) : std::string())
            << '\t';
        if (name_it != name_by_hash.end()) {
            out << CleanDebugName(name_it->second);
        }
        out << '\n';
    }
}

void DebugDumpRecord(
    const GdbView& view,
    const std::unordered_map<uint32_t, std::string>& name_by_hash,
    uint32_t hash,
    size_t record,
    const std::string& relation,
    size_t depth,
    std::ostringstream& out)
{
    auto name_it = name_by_hash.find(hash);
    out << "record\t" << relation
        << "\tdepth=" << depth
        << "\thash=" << Hex32(hash)
        << "\toff=" << HexOff(record);
    if (name_it != name_by_hash.end()) {
        out << "\tname=" << CleanDebugName(name_it->second);
    }
    out << "\n";
    DebugDumpLocalFields(view, name_by_hash, record, "  ", out);
}

void DebugDumpComponentRecord(
    const GdbView& view,
    const std::unordered_map<uint32_t, std::string>& name_by_hash,
    size_t owner_record,
    uint32_t component_field_hash,
    const char* relation,
    std::ostringstream& out)
{
    size_t slot = 0;
    if (!view.findLocal(owner_record, component_field_hash, 6, slot, nullptr)) {
        return;
    }
    const uint32_t component_hash = ReadBeU32(view.bytes.data() + slot);
    size_t component_record = 0;
    if (!view.lookup(component_hash, component_record)) {
        out << "component\t" << relation
            << "\thash=" << Hex32(component_hash)
            << "\trecord=<not indexed>\n";
        return;
    }
    DebugDumpRecord(view, name_by_hash, component_hash, component_record,
                    relation, 0, out);

    if (component_field_hash == kHashStaticMultipleMeshComponent) {
        size_t list_slot = 0;
        if (view.findLocal(component_record, kHashStaticMultipleModelList, 6,
                           list_slot, nullptr))
        {
            const uint32_t list_hash =
                ReadBeU32(view.bytes.data() + list_slot);
            size_t list_record = 0;
            if (view.lookup(list_hash, list_record)) {
                DebugDumpRecord(view, name_by_hash, list_hash, list_record,
                                "component:StaticMultipleModelList", 0, out);
            }
        }
    }
}

void DebugDumpParentChain(
    const GdbView& view,
    const std::unordered_map<uint32_t, std::string>& name_by_hash,
    uint32_t start_hash,
    size_t start_record,
    size_t max_depth,
    std::ostringstream& out)
{
    uint32_t cur_hash = start_hash;
    size_t cur_record = start_record;
    std::unordered_set<uint32_t> seen;

    for (size_t depth = 0; depth <= max_depth; ++depth) {
        if (!seen.insert(cur_hash).second) {
            out << "parent_chain\tcycle\tat=" << Hex32(cur_hash) << "\n";
            return;
        }
        DebugDumpRecord(view, name_by_hash, cur_hash, cur_record,
                        depth == 0 ? "self" : "parent", depth, out);
        DebugDumpComponentRecord(view, name_by_hash, cur_record,
                                 kHashTransformComponent,
                                 "component:Transform", out);
        DebugDumpComponentRecord(view, name_by_hash, cur_record,
                                 kHashStaticMeshComponent,
                                 "component:StaticMesh", out);
        DebugDumpComponentRecord(view, name_by_hash, cur_record,
                                 kHashStaticMultipleMeshComponent,
                                 "component:StaticMultipleMesh", out);

        size_t parent_slot = 0;
        if (!view.findLocal(cur_record, kHashParent, 6,
                            parent_slot, nullptr)) {
            out << "parent_chain\tend\tno_parent_field\n";
            return;
        }
        const uint32_t parent_hash =
            ReadBeU32(view.bytes.data() + parent_slot);
        if (parent_hash == 0) {
            out << "parent_chain\tend\tzero_parent\n";
            return;
        }
        size_t parent_record = 0;
        if (!view.lookup(parent_hash, parent_record)) {
            out << "parent_chain\tend\tparent_not_indexed\t"
                << Hex32(parent_hash) << "\n";
            return;
        }
        cur_hash = parent_hash;
        cur_record = parent_record;
    }
    out << "parent_chain\tend\tmax_depth\n";
}

void DebugDumpBytes(const std::vector<uint8_t>& bytes,
                    size_t begin,
                    size_t end,
                    const std::string& prefix,
                    std::ostringstream& out)
{
    end = std::min(end, bytes.size());
    for (size_t row = begin; row < end; row += 16) {
        out << prefix << HexOff(row) << '\t';
        for (size_t k = 0; k < 16; ++k) {
            if (row + k < end) {
                out << std::uppercase << std::hex
                    << std::setw(2) << std::setfill('0')
                    << int(bytes[row + k]) << ' ';
            } else {
                out << "   ";
            }
        }
        out << "\t";
        for (size_t k = 0; k < 16 && row + k < end; ++k) {
            const unsigned char c = bytes[row + k];
            out << ((c >= 32 && c < 127) ? char(c) : '.');
        }
        out << std::dec << '\n';
    }
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
                        bool& has_rotation) {
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
                             bool& has_rotation) {
    if (begin >= end || end > bytes.size()) return false;
    const size_t shim = (begin >= 4) ? begin - 4 : begin;
    return TryInlineTransform(bytes, shim, end, x, y, z,
                              rot_x, rot_y, rot_z, has_rotation);
}

bool TryIndexedInlineTransform(const GdbView& view,
                               size_t record,
                               float& x,
                               float& y,
                               float& z,
                               float& rot_x,
                               float& rot_y,
                               float& rot_z,
                               bool& has_rotation) {
    size_t payload_start = 0;
    size_t payload_end = 0;
    if (!view.payloadRange(record, payload_start, payload_end)) return false;
    return TryInlineTransformRange(view.bytes, payload_start, payload_end,
                                   x, y, z, rot_x, rot_y, rot_z,
                                   has_rotation);
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
    // Unindexed 0x4B80 records have a 12-byte prefix:
    // marker, record/class id, entity hash, then the normal GDB record body.
    // Only accept them when the prefixed record schema exposes a real
    // transform/component chain; scanning arbitrary inline Vec3s here turns
    // embedded state/component records into duplicate world props.
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
    bool have_pos = TryComponentTransformRecord(view, record,
                                                pos_x, pos_y, pos_z,
                                                rot_x, rot_y, rot_z,
                                                has_rotation);
    if (!have_pos) {
        have_pos = TryTransformRecord(view, record,
                                      pos_x, pos_y, pos_z,
                                      rot_x, rot_y, rot_z,
                                      has_rotation);
    }
    if (!have_pos) {
        have_pos = TryIndexedInlineTransform(view, record,
                                             pos_x, pos_y, pos_z,
                                             rot_x, rot_y, rot_z,
                                             has_rotation);
    }
    if (!have_pos) return false;

    Placement pl;
    pl.x = pos_x;
    pl.y = pos_y;
    pl.z = pos_z;
    pl.rot_x = rot_x;
    pl.rot_y = rot_y;
    pl.rot_z = rot_z;
    pl.has_rotation = has_rotation;
    pl.yaw = has_rotation && std::isfinite(rot_z) ? rot_z : 0.0f;
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

std::string DebugDumpRecordChains(
    const std::vector<uint8_t>& bytes,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name,
    const std::vector<uint32_t>& target_hashes,
    size_t max_records_per_hash,
    size_t max_parent_depth)
{
    std::ostringstream out;
    GdbView view(bytes);
    if (!view.ok) {
        out << "GDB debug dump failed: invalid GDB view\n";
        return out.str();
    }

    std::unordered_map<uint32_t, std::string> name_by_hash;
    name_by_hash.reserve(hash_to_name.size() * 2);
    for (const auto& kv : hash_to_name) {
        name_by_hash.emplace(kv.first, kv.second);
    }

    (void)max_records_per_hash;
    out << "GDB_RECORD_DEBUG"
        << "\tcount=" << view.count
        << "\tsize_a=" << view.size_a
        << "\tsize_b=" << view.size_b
        << "\tbody_start=" << HexOff(view.body_start)
        << "\tbody_end=" << HexOff(view.body_end)
        << "\tschema_base=" << HexOff(view.schema_base)
        << "\thash_base=" << HexOff(view.hash_base)
        << "\toffset_base=" << HexOff(view.offset_base)
        << "\n";

    for (uint32_t target : target_hashes) {
        auto name_it = name_by_hash.find(target);
        out << "\nTARGET\t" << Hex32(target);
        if (name_it != name_by_hash.end()) {
            out << "\tname=" << CleanDebugName(name_it->second);
        }
        out << "\n";

        size_t direct_record = 0;
        if (view.lookup(target, direct_record)) {
            out << "DIRECT_RECORD\n";
            DebugDumpParentChain(view, name_by_hash, target, direct_record,
                                 max_parent_depth, out);
        } else {
            out << "DIRECT_RECORD\tmissing_from_index\n";
        }
    }

    return out.str();
}

namespace {

std::string DebugValueFloat(float v)
{
    std::ostringstream os;
    os << std::setprecision(7) << v;
    return os.str();
}

std::string DebugFieldLabel(uint32_t field_hash)
{
    std::string known = GdbFieldNameResolved(field_hash);
    if (!known.empty()) return known;
    return Hex32(field_hash);
}

DebugNode BuildDebugNodeRecursive(
    const GdbView& view,
    const std::unordered_map<uint32_t, std::string>& name_by_hash,
    uint32_t hash,
    size_t record,
    const std::string& relation,
    size_t depth,
    size_t max_depth,
    size_t max_fields_per_record,
    std::unordered_set<uint32_t>& active)
{
    DebugNode node;
    node.label = relation.empty() ? "Record" : relation;
    std::string resolved_name = GdbHashName(hash, name_by_hash);
    if (!resolved_name.empty()) {
        node.label = CleanDebugName(resolved_name);
    }
    node.value = Hex32(hash);
    node.value += " @ " + HexOff(record);

    if (!active.insert(hash).second) {
        node.children.push_back({"cycle", Hex32(hash), {}});
        return node;
    }

    size_t sch = 0;
    uint32_t n = 0;
    if (!view.schema(record, sch, n)) {
        node.children.push_back({"schema", "invalid", {}});
        active.erase(hash);
        return node;
    }

    const size_t hashes = sch + 4;
    const size_t descs = hashes + size_t(n) * 4;
    const size_t limit = std::min<size_t>(n, max_fields_per_record);
    for (size_t i = 0; i < limit; ++i) {
        const uint32_t field_hash =
            ReadBeU32(view.bytes.data() + hashes + i * 4);
        const uint32_t desc =
            ReadBeU32(view.bytes.data() + descs + i * 4);
        const uint8_t type = uint8_t(desc >> 24);
        const size_t slot = record + 4 + i * 4;

        DebugNode child;
        child.label = DebugFieldLabel(field_hash);
        if (slot + 4 > view.body_end) {
            child.value = "<out of range>";
            node.children.push_back(std::move(child));
            continue;
        }

        const uint32_t raw = ReadBeU32(view.bytes.data() + slot);
        const float as_f32 = ReadBeF32(view.bytes.data() + slot);
        const std::string raw_name = GdbHashName(raw, name_by_hash);

        if (type == 6) {
            child.value = Hex32(raw);
            if (!raw_name.empty()) {
                child.value += "  " + CleanDebugName(raw_name);
            }

            size_t ref_record = 0;
            if (depth + 1 <= max_depth && raw != 0 &&
                view.lookup(raw, ref_record))
            {
                child.children.push_back(
                    BuildDebugNodeRecursive(view, name_by_hash, raw,
                                            ref_record, child.label,
                                            depth + 1, max_depth,
                                            max_fields_per_record,
                                            active));
            }
        } else if (type == 3) {
            if (std::isfinite(as_f32)) {
                child.value = DebugValueFloat(as_f32);
                child.value += "  " + Hex32(raw);
            } else {
                child.value = Hex32(raw);
            }
        } else if (type == 4) {
            if (raw == 0 || raw == 1) {
                child.value = raw ? "True" : "False";
            } else {
                child.value = Hex32(raw);
            }
            if (!raw_name.empty()) {
                child.value += "  " + CleanDebugName(raw_name);
            }
        } else {
            child.value = Hex32(raw);
            if (!raw_name.empty()) {
                child.value += "  " + CleanDebugName(raw_name);
            }
        }

        node.children.push_back(std::move(child));
    }

    if (size_t(n) > limit) {
        node.children.push_back({
            "...",
            std::to_string(size_t(n) - limit) + " more field(s)",
            {}
        });
    }

    active.erase(hash);
    return node;
}

}

std::vector<DebugNode> BuildDebugTreeForHash(
    const std::vector<uint8_t>& bytes,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name,
    uint32_t target_hash,
    size_t max_depth,
    size_t max_fields_per_record)
{
    std::vector<DebugNode> roots;
    if (target_hash == 0) return roots;

    GdbView view(bytes);
    if (!view.ok) return roots;

    std::unordered_map<uint32_t, std::string> name_by_hash;
    name_by_hash.reserve(hash_to_name.size() * 2);
    for (const auto& kv : hash_to_name) {
        name_by_hash.emplace(kv.first, kv.second);
    }

    size_t record = 0;
    if (!view.lookup(target_hash, record)) return roots;

    std::unordered_set<uint32_t> active;
    roots.push_back(BuildDebugNodeRecursive(view, name_by_hash, target_hash,
                                            record, "Place", 0,
                                            max_depth,
                                            max_fields_per_record,
                                            active));
    return roots;
}

std::vector<RecordRow> Build010RecordRows(
    const std::vector<uint8_t>& bytes,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name,
    size_t max_depth,
    size_t max_fields_per_record)
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

        std::unordered_set<uint32_t> active;
        row.debug_tree.push_back(
            BuildDebugNodeRecursive(view, name_by_hash, hash, record,
                                    "RecordData", 0, max_depth,
                                    max_fields_per_record, active));
        rows.push_back(std::move(row));
    }

    return rows;
}

}
