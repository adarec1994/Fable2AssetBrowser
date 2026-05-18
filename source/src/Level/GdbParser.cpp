#include "GdbParser.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace Gdb {

namespace {

inline uint32_t ReadBeU32(const uint8_t* p) {
    return  (uint32_t(p[0]) << 24) |
            (uint32_t(p[1]) << 16) |
            (uint32_t(p[2]) <<  8) |
             uint32_t(p[3]);
}

inline int16_t ReadBeS16(const uint8_t* p) {
    return static_cast<int16_t>((uint16_t(p[0]) << 8) | uint16_t(p[1]));
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

constexpr uint32_t kFixedMarker  = 0x00004B40;
constexpr uint32_t kVarMarker    = 0x00004B80;
constexpr uint32_t kTagVec3      = 0x00000568;
constexpr uint32_t kHashParent   = 0x5F6317D5;
constexpr uint32_t kHashPosition = 0xBD7C27D4;
constexpr uint32_t kHashRotation = 0x21EBC83B;
constexpr uint32_t kHashTransformComponent = 0xF73572C4;
constexpr uint32_t kHashModelResource = 0x29CF50D1;
constexpr uint32_t kHashModelPathHash = 0x0C17DB4E;
constexpr size_t   kFixedRecSize = 92;
constexpr size_t   kHeaderSize   = 0x18;

struct GdbView {
    const std::vector<uint8_t>& bytes;
    uint32_t count = 0;
    uint32_t size_a = 0;
    uint32_t size_b = 0;
    uint32_t stride = 0;
    size_t body_start = kHeaderSize;
    size_t body_end = 0;
    size_t schema_base = 0;
    size_t hash_base = 0;
    size_t offset_base = 0;
    bool ok = false;

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
        stride = static_cast<uint32_t>((uint64_t(size_a) << 8) / count);
        ok = true;
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
        const int64_t rel_words =
            int64_t(ReadBeS16(bytes.data() + offset_base + lo * 2)) +
            int64_t((uint64_t(stride) * uint64_t(lo)) >> 10);
        if (rel_words < 0) return false;
        record = body_start + size_t(rel_words) * 4;
        return record + 4 <= body_end;
    }

    bool schema(size_t record, size_t& schema_off, uint32_t& field_count) const {
        if (!ok || record + 4 > body_end) return false;
        uint32_t rel = ReadBeU32(bytes.data() + record);
        schema_off = schema_base + size_t(rel);
        if (schema_off + 4 > hash_base) return false;
        uint32_t header = ReadBeU32(bytes.data() + schema_off);
        field_count = header >> 8;
        if (field_count > 256) return false;
        return schema_off + 4 + size_t(field_count) * 8 <= hash_base;
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

    bool readVec3Record(size_t record,
                        float& x,
                        float& y,
                        float& z,
                        float* raw_x = nullptr,
                        float* raw_y = nullptr,
                        float* raw_z = nullptr) const {
        if (record + 16 > body_end) return false;
        if (ReadBeU32(bytes.data() + record) != kTagVec3) return false;
        const float rz = ReadBeF32(bytes.data() + record + 4);
        const float ry = ReadBeF32(bytes.data() + record + 8);
        const float rx = ReadBeF32(bytes.data() + record + 12);
        if (!Finite3(rx, ry, rz)) return false;
        x = rx;
        y = ry;
        z = rz;
        if (raw_x) *raw_x = rx;
        if (raw_y) *raw_y = ry;
        if (raw_z) *raw_z = rz;
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
        if (!lookup(hash, rec) || rec + 16 > body_end) return false;
        if (ReadBeU32(bytes.data() + rec) != kTagVec3) return false;
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
        if (ok && record >= body_start) {
            const size_t rel_words = (record - body_start) / 4;
            for (uint32_t i = 0; i < count; ++i) {
                const int64_t candidate_words =
                    int64_t(ReadBeS16(bytes.data() + offset_base + size_t(i) * 2)) +
                    int64_t((uint64_t(stride) * uint64_t(i)) >> 10);
                if (candidate_words <= int64_t(rel_words)) continue;
                const size_t candidate = body_start + size_t(candidate_words) * 4;
                if (candidate > record && candidate < payload_end) {
                    payload_end = candidate;
                }
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

bool TryReadModelPathHashForParent(const GdbView& view,
                                   uint32_t parent_hash,
                                   uint32_t& out_hash)
{
    out_hash = 0;
    if (!view.ok || parent_hash == 0) return false;

    size_t parent_record = 0;
    if (!view.lookup(parent_hash, parent_record)) return false;

    size_t model_slot = 0;
    if (!view.findField(parent_record, kHashModelResource, 6,
                        model_slot, nullptr)) {
        return false;
    }

    const uint32_t model_resource_hash =
        ReadBeU32(view.bytes.data() + model_slot);
    size_t model_resource_record = 0;
    if (!view.lookup(model_resource_hash, model_resource_record)) return false;

    size_t path_hash_slot = 0;
    uint8_t path_hash_type = 0;
    if (!view.findLocal(model_resource_record, kHashModelPathHash, 0xFF,
                        path_hash_slot, &path_hash_type)) {
        return false;
    }
    if (path_hash_type != 4 && path_hash_type != 3) return false;

    out_hash = ReadBeU32(view.bytes.data() + path_hash_slot);
    return out_hash != 0;
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
    for (size_t q = rs + 4; q + 16 <= re; q += 4) {
        if (ReadBeU32(bytes.data() + q) != kTagVec3) continue;

        const float rz = ReadBeF32(bytes.data() + q + 4);
        const float ry = ReadBeF32(bytes.data() + q + 8);
        const float rx = ReadBeF32(bytes.data() + q + 12);
        if (!Finite3(rx, ry, rz)) continue;

        if (!PlausiblePosition(rx, ry, rz)) continue;
        x = rx;
        y = ry;
        z = rz;
        return true;
    }
    return false;
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

}

GdbInfo Parse(const std::vector<uint8_t>& bytes) {
    return ParseWithSaveMap(bytes, {});
}

GdbInfo ParseWithSaveMap(
    const std::vector<uint8_t>& bytes,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name)
{
    GdbInfo out;
    if (bytes.size() < kHeaderSize + kFixedRecSize) return out;

    if (bytes[0] != 'G' || bytes[1] != 'D' || bytes[2] != 'B' || bytes[3] != 0) {
        return out;
    }

    std::unordered_map<uint32_t, std::string> name_by_hash;
    name_by_hash.reserve(hash_to_name.size() * 2);
    for (const auto& kv : hash_to_name) {
        name_by_hash.emplace(kv.first, kv.second);
    }

    GdbView view(bytes);

    size_t off = kHeaderSize;
    while (off + kFixedRecSize <= bytes.size()) {
        const uint8_t* p = bytes.data() + off;
        if (ReadBeU32(p + 0x00) != kFixedMarker) break;

        Placement pl;
        pl.x      = ReadBeF32(p + 0x38);
        pl.y      = ReadBeF32(p + 0x34);
        pl.z      = ReadBeF32(p + 0x30);
        pl.yaw    = ReadBeF32(p + 0x4C);
        if (!std::isfinite(pl.yaw)) pl.yaw = 0.0f;
        pl.rot_x  = 0.0f;
        pl.rot_y  = 0.0f;
        pl.rot_z  = pl.yaw;
        pl.has_rotation = true;
        pl.scale  = ReadBeF32(p + 0x58);
        if (!std::isfinite(pl.scale) || pl.scale <= 0.0f || pl.scale > 1000.0f) {
            pl.scale = 1.0f;
        }
        pl.marker = kFixedMarker;
        pl.hash_a = ReadBeU32(p + 0x04);
        pl.parent_hash = 0;
        pl.model_path_hash = 0;

        if (Finite3(pl.x, pl.y, pl.z)) {
            out.placements.push_back(pl);
        }

        off += kFixedRecSize;
    }

    const size_t body_end = view.ok ? view.body_end : bytes.size();

    while (off + 4 <= body_end &&
           ReadBeU32(bytes.data() + off) != kVarMarker) {
        off += 4;
    }

    std::vector<size_t> var_offs;
    {
        size_t s = off;
        while (s + 4 <= body_end) {
            if (ReadBeU32(bytes.data() + s) == kVarMarker) {
                var_offs.push_back(s);
            }
            s += 4;
        }
        var_offs.push_back(body_end);
    }

    size_t master_dict_size = 0;
    for (size_t i = 0; i + 1 < var_offs.size(); ++i) {
        size_t sz = var_offs[i + 1] - var_offs[i];
        if (sz > master_dict_size) master_dict_size = sz;
    }

    for (size_t i = 0; i + 1 < var_offs.size(); ++i) {
        const size_t rs = var_offs[i];
        const size_t re = var_offs[i + 1];
        const size_t rec_size = re - rs;

        if (rec_size == master_dict_size && rec_size > 100000) continue;

        uint32_t inst_hash = 0;
        if (rs + 12 <= re) {
            inst_hash = ReadBeU32(bytes.data() + rs + 8);
        }

        std::string entity_name;
        if (!name_by_hash.empty()) {
            auto it = name_by_hash.find(inst_hash);
            if (it != name_by_hash.end()) entity_name = it->second;
        }

        float pos_x = 0.f, pos_y = 0.f, pos_z = 0.f;
        float rot_x = 0.0f, rot_y = 0.0f, rot_z = 0.0f;
        bool has_rotation = false;
        bool have_pos = false;

        if (view.ok && inst_hash != 0) {
            size_t direct = 0;
            if (view.lookup(inst_hash, direct)) {
                have_pos = TryComponentTransformRecord(view, direct,
                                                       pos_x, pos_y, pos_z,
                                                       rot_x, rot_y, rot_z,
                                                       has_rotation);
                if (!have_pos) {
                    have_pos = TryTransformRecord(view, direct,
                                                  pos_x, pos_y, pos_z,
                                                  rot_x, rot_y, rot_z,
                                                  has_rotation);
                }
                if (!have_pos) {
                    have_pos = TryIndexedInlineTransform(view, direct,
                                                         pos_x, pos_y, pos_z,
                                                         rot_x, rot_y, rot_z,
                                                         has_rotation);
                }
            }
        }
        if (!have_pos) {
            have_pos = TryInlineTransform(bytes, rs, re,
                                          pos_x, pos_y, pos_z,
                                          rot_x, rot_y, rot_z,
                                          has_rotation);
        }
        if (!have_pos && view.ok) {
            have_pos = TryEmbeddedTransformRecords(view, rs, re,
                                                   pos_x, pos_y, pos_z,
                                                   rot_x, rot_y, rot_z,
                                                   has_rotation);
        }

        if (!have_pos) continue;

        Placement pl;
        pl.x          = pos_x;
        pl.y          = pos_y;
        pl.z          = pos_z;
        pl.rot_x      = rot_x;
        pl.rot_y      = rot_y;
        pl.rot_z      = rot_z;
        pl.has_rotation = has_rotation;
        pl.yaw        = has_rotation && std::isfinite(rot_z) ? rot_z : 0.0f;
        pl.scale      = 1.0f;
        pl.marker     = kVarMarker;
        pl.hash_a     = inst_hash;
        pl.parent_hash = 0;
        pl.model_path_hash = 0;
        if (view.ok && inst_hash != 0) {
            size_t direct = 0;
            size_t parent_slot = 0;
            if (view.lookup(inst_hash, direct) &&
                view.findLocal(direct, kHashParent, 6, parent_slot, nullptr)) {
                pl.parent_hash = ReadBeU32(bytes.data() + parent_slot);
                TryReadModelPathHashForParent(view, pl.parent_hash,
                                              pl.model_path_hash);
            }
        }
        pl.entity_name = std::move(entity_name);
        out.placements.push_back(std::move(pl));
    }

    if (view.ok && !name_by_hash.empty()) {
        std::unordered_map<uint32_t, bool> emitted;
        emitted.reserve(out.placements.size() * 2);
        for (const auto& p : out.placements) {
            if (p.hash_a != 0) emitted.emplace(p.hash_a, true);
        }

        for (const auto& kv : hash_to_name) {
            const uint32_t inst_hash = kv.first;
            if (inst_hash == 0 || emitted.find(inst_hash) != emitted.end()) {
                continue;
            }

            size_t direct = 0;
            if (!view.lookup(inst_hash, direct)) continue;

            float pos_x = 0.0f, pos_y = 0.0f, pos_z = 0.0f;
            float rot_x = 0.0f, rot_y = 0.0f, rot_z = 0.0f;
            bool has_rotation = false;
            bool have_pos = TryComponentTransformRecord(view, direct,
                                                        pos_x, pos_y, pos_z,
                                                        rot_x, rot_y, rot_z,
                                                        has_rotation);
            if (!have_pos) {
                have_pos = TryTransformRecord(view, direct,
                                              pos_x, pos_y, pos_z,
                                              rot_x, rot_y, rot_z,
                                              has_rotation);
            }
            if (!have_pos) {
                have_pos = TryIndexedInlineTransform(view, direct,
                                                     pos_x, pos_y, pos_z,
                                                     rot_x, rot_y, rot_z,
                                                     has_rotation);
            }
            if (!have_pos) {
                continue;
            }

            Placement pl;
            pl.x          = pos_x;
            pl.y          = pos_y;
            pl.z          = pos_z;
            pl.rot_x      = rot_x;
            pl.rot_y      = rot_y;
            pl.rot_z      = rot_z;
            pl.has_rotation = has_rotation;
            pl.yaw        = has_rotation && std::isfinite(rot_z) ? rot_z : 0.0f;
            pl.scale      = 1.0f;
            pl.marker     = kVarMarker;
            pl.hash_a     = inst_hash;
            pl.parent_hash = 0;
            pl.model_path_hash = 0;

            size_t parent_slot = 0;
            if (view.findLocal(direct, kHashParent, 6, parent_slot, nullptr)) {
                pl.parent_hash = ReadBeU32(bytes.data() + parent_slot);
                TryReadModelPathHashForParent(view, pl.parent_hash,
                                              pl.model_path_hash);
            }
            pl.entity_name = kv.second;
            out.placements.push_back(std::move(pl));
            emitted.emplace(inst_hash, true);
        }
    }

    return out;
}

}
