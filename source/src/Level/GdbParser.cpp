#include "GdbParser.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <unordered_map>

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

constexpr uint32_t kFixedMarker  = 0x00004B40;
constexpr uint32_t kVarMarker    = 0x00004B80;
constexpr uint32_t kTagVec3      = 0x00000568;
constexpr uint32_t kTagFloat     = 0x00000540;
constexpr uint32_t kTagQuat      = 0x00004B10;
constexpr size_t   kFixedRecSize = 92;
constexpr size_t   kHeaderSize   = 0x18;

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
        pl.scale  = ReadBeF32(p + 0x58);
        if (!std::isfinite(pl.scale) || pl.scale <= 0.0f || pl.scale > 1000.0f) {
            pl.scale = 1.0f;
        }
        pl.marker = kFixedMarker;
        pl.hash_a = ReadBeU32(p + 0x04);

        if (Finite3(pl.x, pl.y, pl.z)) {
            out.placements.push_back(pl);
        }

        off += kFixedRecSize;
    }

    while (off + 4 <= bytes.size() &&
           ReadBeU32(bytes.data() + off) != kVarMarker) {
        off += 4;
    }

    std::vector<size_t> var_offs;
    {
        size_t s = off;
        while (s + 4 <= bytes.size()) {
            if (ReadBeU32(bytes.data() + s) == kVarMarker) {
                var_offs.push_back(s);
            }
            s += 4;
        }
        var_offs.push_back(bytes.size());
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

        int   pos_idx = -1;
        float pos_x = 0.f, pos_y = 0.f, pos_z = 0.f;
        float yaw_from_eulers = 0.0f;
        bool  saw_euler = false;

        for (size_t q = rs + 4; q + 16 <= re; q += 4) {
            uint32_t tag = ReadBeU32(bytes.data() + q);
            if (tag != kTagVec3) continue;

            float x = ReadBeF32(bytes.data() + q + 4);
            float y = ReadBeF32(bytes.data() + q + 8);
            float z = ReadBeF32(bytes.data() + q + 12);
            if (!Finite3(x, y, z)) continue;

            const bool any_large =
                std::fabs(x) > 5.0f ||
                std::fabs(y) > 5.0f ||
                std::fabs(z) > 5.0f;

            if (!any_large) {
                if (!saw_euler) {
                    yaw_from_eulers = z;
                    saw_euler = true;
                }
                continue;
            }

            if (std::fabs(x) > 600.0f || std::fabs(y) > 600.0f ||
                std::fabs(z) > 600.0f) {
                continue;
            }

            if (pos_idx < 0) {
                pos_idx = static_cast<int>(q - rs);
                pos_x = x;
                pos_y = y;
                pos_z = z;
                break;
            }
        }

        if (pos_idx < 0) continue;

        Placement pl;
        pl.x          = pos_x;
        pl.y          = pos_y;
        pl.z          = pos_z;
        pl.yaw        = std::isfinite(yaw_from_eulers) ? yaw_from_eulers : 0.0f;
        pl.scale      = 1.0f;
        pl.marker     = kVarMarker;
        pl.hash_a     = inst_hash;
        pl.entity_name = std::move(entity_name);
        out.placements.push_back(std::move(pl));
    }

    return out;
}

}
