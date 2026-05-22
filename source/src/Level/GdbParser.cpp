#include "GdbParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
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
constexpr uint32_t kHashGraphicAppearanceAnimatedMeshComponent = 0x21D312CA;
constexpr uint32_t kHashStaticMeshComponent = 0x29CF50D1;
constexpr uint32_t kHashStaticMultipleMeshComponent = 0xCE642A15;
constexpr uint32_t kHashPhysicsSimulationKeyframedComponent = 0x6B177DD0;
constexpr uint32_t kHashPhysicsSimulationStaticComponent = 0x5883C406;
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
constexpr uint32_t kHashLevelData = 0x123F8AFD;
constexpr uint32_t kHashEnvThemeGlobal = 0xAE17B958;
constexpr uint32_t kHashEnvironmentThemeDaySet = 0x0843AB41;
constexpr uint32_t kHashTheme = 0xB57E3290;
constexpr uint32_t kHashTimeOfDay = 0x9723C2C9;
constexpr uint32_t kHashWater = 0x1E6889DA;
constexpr uint32_t kHashSky = 0x2420BFA4;
constexpr uint32_t kHashFogging = 0xDDF56C9A;
constexpr uint32_t kHashSunIntensity = 0xC868C0DC;
constexpr uint32_t kHashSkyBetaRayleighMultiplier = 0x59837340;
constexpr uint32_t kHashSkyBetaMieMultiplier = 0xD7FC122C;
constexpr uint32_t kHashSkyColour = 0xD78A6E40;
constexpr uint32_t kHashSkyComplementaryColour = 0x5CBE1462;
constexpr uint32_t kHashSkyComplementaryColourBias = 0x2DA0C989;
constexpr uint32_t kHashSunsetColour = 0x897262B7;
constexpr uint32_t kHashSkyOverlayTexture = 0xF8C0DD95;
constexpr uint32_t kHashMoonTexture = 0x20D88F43;
constexpr uint32_t kHashMoonGlareTexture = 0xF2C7518C;
constexpr uint32_t kHashDiscTexture = 0x6B29E8A3;
constexpr uint32_t kHashCloudsLayer1 = 0xF2CF57DD;
constexpr uint32_t kHashCloudsLayer2 = 0xF2CF57DE;
constexpr uint32_t kHashCloudsLayer3 = 0xF2CF57DF;
constexpr uint32_t kHashCloudsLayer4 = 0xF2CF57D8;
constexpr uint32_t kHashDensityMap = 0x13821B7F;
constexpr uint32_t kHashPositionX = 0x1E72B2E4;
constexpr uint32_t kHashPositionY = 0x1E72B2E5;
constexpr uint32_t kHashSizeX = 0x9C014CCE;
constexpr uint32_t kHashSizeY = 0x9C014CCF;
constexpr uint32_t kHashTextureScaleX = 0x0A8BA024;
constexpr uint32_t kHashTextureScaleY = 0x0A8BA025;
constexpr uint32_t kHashVelocityX = 0x5CE30740;
constexpr uint32_t kHashVelocityY = 0x5CE30741;
constexpr uint32_t kHashHeight = 0xF47DB020;
constexpr uint32_t kHashTransparency = 0x383FDB33;
constexpr uint32_t kHashNormalStrength = 0xB5B0AE93;
constexpr uint32_t kHashTranslucencyStrength = 0x114E67B1;
constexpr uint32_t kHashBrightness = 0xC452018C;
constexpr uint32_t kHashAmbientLight = 0x15DD1091;
constexpr uint32_t kHashNearDistance = 0xDA3F7AAA;
constexpr uint32_t kHashNearDensity = 0x91F00AC5;
constexpr uint32_t kHashFarDistance = 0xFF154645;
constexpr uint32_t kHashFarDensity = 0xE1DF20B4;
constexpr uint32_t kHashCloseFogColour = 0x66353755;
constexpr uint32_t kHashCloseFogMaxDistance = 0xCC4BDF70;
constexpr uint32_t kHashRed = 0x3A232172;
constexpr uint32_t kHashGreen = 0x608C9792;
constexpr uint32_t kHashBlue = 0xB1911CC9;
constexpr uint32_t kHashFactor = 0xBF21DA70;
constexpr uint32_t kHashShallowWaterColourRed = 0xB2ECBF11;
constexpr uint32_t kHashShallowWaterColourGreen = 0x25F0E705;
constexpr uint32_t kHashShallowWaterColourBlue = 0x8BF17608;
constexpr uint32_t kHashDeepWaterColourRed = 0x1750476D;
constexpr uint32_t kHashDeepWaterColourGreen = 0x61671F01;
constexpr uint32_t kHashDeepWaterColourBlue = 0x632A3D74;
constexpr uint32_t kHashEdgeBlendBias = 0x79F85F10;
constexpr uint32_t kHashEdgeBlendMin = 0x5A234079;
constexpr uint32_t kHashEdgeBlendMax = 0x52233307;
constexpr uint32_t kHashMaxRefractionDistance = 0x671D3125;
constexpr uint32_t kHashFresnelBias = 0x73C59519;
constexpr uint32_t kHashReflectionStrength = 0xAF315449;
constexpr uint32_t kHashRefractionScale = 0x623C0662;
constexpr uint32_t kHashReflectionScale = 0x9BA6BDE0;
constexpr uint32_t kHashReflectionBias = 0x4838AA55;
constexpr uint32_t kHashNormalScale = 0xA027B7EE;
constexpr uint32_t kHashNull = 0x811C9DC5;
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
        return lookup(hash, rec) && readVec3Record(rec, x, y, z);
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

bool TryComponentTransformField(const GdbView& view,
                                size_t record,
                                uint32_t component_field_hash,
                                float& x,
                                float& y,
                                float& z,
                                float& rot_x,
                                float& rot_y,
                                float& rot_z,
                                bool& has_rotation) {
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
                              rot_x, rot_y, rot_z, has_rotation);
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
    if (TryComponentTransformField(view, record, kHashTransformComponent,
                                   x, y, z, rot_x, rot_y, rot_z,
                                   has_rotation)) {
        return true;
    }
    if (TryComponentTransformField(
            view, record, kHashPhysicsSimulationKeyframedComponent,
            x, y, z, rot_x, rot_y, rot_z, has_rotation)) {
        return true;
    }
    return TryComponentTransformField(
        view, record, kHashPhysicsSimulationStaticComponent,
        x, y, z, rot_x, rot_y, rot_z, has_rotation);
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

struct WaterThemeRecordRef {
    size_t db = 0;
    size_t record = 0;
    bool valid = false;
};

struct WaterThemeFieldRef {
    WaterThemeRecordRef owner;
    size_t slot = 0;
    uint8_t type = 0;
    uint32_t raw = 0;
    float f32 = 0.0f;
};

inline float Clamp01(float v)
{
    return std::clamp(v, 0.0f, 1.0f);
}

inline float EnvColourComponentToLinearInput(float v)
{
    
    
    return Clamp01(v * (1.0f / 255.0f));
}

class WaterThemeExtractor {
public:
    explicit WaterThemeExtractor(
        const std::vector<const std::vector<uint8_t>*>& gdbs)
    {
        views_.reserve(gdbs.size());
        for (const auto* bytes : gdbs) {
            if (!bytes) continue;
            views_.emplace_back(*bytes);
        }
    }

    bool extract(WaterTheme& out_theme) const
    {
        out_theme = WaterTheme{};
        if (views_.empty()) return false;

        bool applied = false;

        WaterThemeRecordRef day_set = findDaySet(true);
        if (day_set.valid) {
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applyThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& level :
                 lookupAllInDb(kHashLevelData, 0)) {
                applied |= applyThemeField(level, kHashEnvThemeGlobal,
                                           out_theme);
            }
        }

        if (!applied) {
            WaterThemeRecordRef owner =
                firstRecordWithFieldInDb(kHashEnvThemeGlobal, 0);
            if (owner.valid) {
                applied |= applyThemeField(owner, kHashEnvThemeGlobal,
                                           out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAllInDb(kHashEnvThemeGlobal, 0)) {
                applied |= applyThemeRecord(global, out_theme);
            }
        }

        if (!applied) {
            day_set = findDaySet(false);
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (day_set.valid &&
                selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applyThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAll(kHashEnvThemeGlobal)) {
                applied |= applyThemeRecord(global, out_theme);
            }
        }

        if (!applied) {
            applied = applyFirstWaterLikeRecord(out_theme);
        }

        out_theme.has_any = applied;
        return applied;
    }

    bool extractSky(SkyTheme& out_theme) const
    {
        out_theme = SkyTheme{};
        if (views_.empty()) return false;

        bool applied = false;

        WaterThemeRecordRef day_set = findDaySet(true);
        if (day_set.valid) {
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applySkyThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& level :
                 lookupAllInDb(kHashLevelData, 0)) {
                applied |= applySkyThemeField(level, kHashEnvThemeGlobal,
                                              out_theme);
            }
        }

        if (!applied) {
            WaterThemeRecordRef owner =
                firstRecordWithFieldInDb(kHashEnvThemeGlobal, 0);
            if (owner.valid) {
                applied |= applySkyThemeField(owner, kHashEnvThemeGlobal,
                                              out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAllInDb(kHashEnvThemeGlobal, 0)) {
                applied |= applySkyThemeRecord(global, out_theme);
            }
        }

        if (!applied) {
            day_set = findDaySet(false);
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (day_set.valid &&
                selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applySkyThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAll(kHashEnvThemeGlobal)) {
                applied |= applySkyThemeRecord(global, out_theme);
            }
        }

        if (!applied) {
            applied = applyFirstSkyLikeRecord(out_theme);
        }

        out_theme.has_any = applied;
        return applied;
    }

    bool extractClouds(CloudTheme& out_theme) const
    {
        out_theme = CloudTheme{};
        if (views_.empty()) return false;

        bool applied = false;

        WaterThemeRecordRef day_set = findDaySet(true);
        if (day_set.valid) {
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applyCloudThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& level :
                 lookupAllInDb(kHashLevelData, 0)) {
                applied |= applyCloudThemeField(level, kHashEnvThemeGlobal,
                                                out_theme);
            }
        }

        if (!applied) {
            WaterThemeRecordRef owner =
                firstRecordWithFieldInDb(kHashEnvThemeGlobal, 0);
            if (owner.valid) {
                applied |= applyCloudThemeField(owner, kHashEnvThemeGlobal,
                                                out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAllInDb(kHashEnvThemeGlobal, 0)) {
                applied |= applyCloudThemeRecord(global, out_theme);
            }
        }

        if (!applied) {
            day_set = findDaySet(false);
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (day_set.valid &&
                selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applyCloudThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAll(kHashEnvThemeGlobal)) {
                applied |= applyCloudThemeRecord(global, out_theme);
            }
        }

        if (!applied) {
            applied = applyFirstCloudLikeRecords(out_theme);
        }

        finaliseCloudTheme(out_theme);
        out_theme.has_any = applied && out_theme.layer_count > 0;
        return out_theme.has_any;
    }

    bool extractEnvironmentTimeline(EnvironmentThemeTimeline& out_timeline)
        const
    {
        out_timeline = EnvironmentThemeTimeline{};
        if (views_.empty()) return false;

        WaterThemeRecordRef day_set = findDaySet(true);
        if (!day_set.valid) {
            day_set = findDaySet(false);
        }
        if (!day_set.valid) return false;

        const GdbView& v = view(day_set);
        size_t sch = 0;
        uint32_t n = 0;
        if (!v.schema(day_set.record, sch, n)) return false;

        const size_t descs = sch + 4 + size_t(n) * 4;
        if (descs + size_t(n) * 4 > v.body_end) return false;

        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t desc =
                ReadBeU32(v.bytes.data() + descs + size_t(i) * 4);
            const uint8_t type = uint8_t(desc >> 24);
            if (type != 4 && type != 6 && type != 7) continue;

            const size_t slot = day_set.record + 4 + size_t(i) * 4;
            if (slot + 4 > v.body_end) continue;

            WaterThemeFieldRef item_field;
            item_field.owner = day_set;
            item_field.slot = slot;
            item_field.type = type;
            item_field.raw = ReadBeU32(v.bytes.data() + slot);
            item_field.f32 = ReadBeF32(v.bytes.data() + slot);

            WaterThemeRecordRef entry = fieldToRecord(item_field);
            if (!entry.valid) continue;

            WaterThemeRecordRef theme =
                resolveRecordField(entry, kHashTheme);
            if (!theme.valid) continue;

            float time = 0.5f;
            float read_time = 0.0f;
            if (readFloat(entry, kHashTimeOfDay, read_time)) {
                time = normalizeTimeOfDay(read_time);
            }

            EnvironmentThemeKeyframe key;
            key.time_of_day = time;

            const bool water_applied = applyThemeRecord(theme, key.water);
            key.water.has_any = water_applied;
            key.water.source_time_of_day = time;

            const bool sky_applied = applySkyThemeRecord(theme, key.sky);
            key.sky.has_any = sky_applied;
            key.sky.source_time_of_day = time;

            const bool cloud_applied =
                applyCloudThemeRecord(theme, key.clouds);
            finaliseCloudTheme(key.clouds);
            key.clouds.has_any =
                cloud_applied && key.clouds.layer_count > 0;
            key.clouds.source_time_of_day = time;

            if (key.water.has_any || key.sky.has_any || key.clouds.has_any) {
                out_timeline.keyframes.push_back(key);
            }
        }

        std::sort(out_timeline.keyframes.begin(),
                  out_timeline.keyframes.end(),
                  [](const EnvironmentThemeKeyframe& a,
                     const EnvironmentThemeKeyframe& b) {
                      return a.time_of_day < b.time_of_day;
                  });
        out_timeline.keyframes.erase(
            std::unique(out_timeline.keyframes.begin(),
                        out_timeline.keyframes.end(),
                        [](const EnvironmentThemeKeyframe& a,
                           const EnvironmentThemeKeyframe& b) {
                            return std::fabs(a.time_of_day -
                                             b.time_of_day) < 0.0005f;
                        }),
            out_timeline.keyframes.end());

        out_timeline.has_any = out_timeline.keyframes.size() >= 2;
        return out_timeline.has_any;
    }

private:
    std::vector<GdbView> views_;

    static float normalizeTimeOfDay(float time)
    {
        if (!std::isfinite(time)) return 0.5f;
        if (time > 1.0f && time <= 24.0f) {
            time *= (1.0f / 24.0f);
        }
        time -= std::floor(time);
        if (time < 0.0f) time += 1.0f;
        return time;
    }

    const GdbView& view(const WaterThemeRecordRef& ref) const
    {
        return views_[ref.db];
    }

    std::vector<WaterThemeRecordRef> lookupAll(uint32_t hash) const
    {
        std::vector<WaterThemeRecordRef> out;
        if (hash == 0 || hash == kHashNull) return out;
        for (size_t db = 0; db < views_.size(); ++db) {
            const GdbView& v = views_[db];
            if (!v.ok) continue;
            size_t record = 0;
            if (v.lookup(hash, record)) {
                out.push_back(WaterThemeRecordRef{db, record, true});
            }
        }
        return out;
    }

    std::vector<WaterThemeRecordRef> lookupAllInDb(uint32_t hash,
                                                   size_t db) const
    {
        std::vector<WaterThemeRecordRef> out;
        if (hash == 0 || hash == kHashNull || db >= views_.size()) {
            return out;
        }
        const GdbView& v = views_[db];
        if (!v.ok) return out;
        size_t record = 0;
        if (v.lookup(hash, record)) {
            out.push_back(WaterThemeRecordRef{db, record, true});
        }
        return out;
    }

    WaterThemeRecordRef lookupFirst(uint32_t hash) const
    {
        const std::vector<WaterThemeRecordRef> all = lookupAll(hash);
        if (all.empty()) return {};
        return all.front();
    }

    WaterThemeRecordRef firstRecordWithField(uint32_t field_hash) const
    {
        for (size_t db = 0; db < views_.size(); ++db) {
            const GdbView& v = views_[db];
            if (!v.ok) continue;
            for (size_t record : v.record_data_offsets) {
                size_t slot = 0;
                if (v.findLocal(record, field_hash, 0xFF,
                                slot, nullptr)) {
                    return WaterThemeRecordRef{db, record, true};
                }
            }
        }
        return {};
    }

    WaterThemeRecordRef firstRecordWithFieldInDb(uint32_t field_hash,
                                                 size_t db) const
    {
        if (db >= views_.size()) return {};
        const GdbView& v = views_[db];
        if (!v.ok) return {};
        for (size_t record : v.record_data_offsets) {
            size_t slot = 0;
            if (v.findLocal(record, field_hash, 0xFF, slot, nullptr)) {
                return WaterThemeRecordRef{db, record, true};
            }
        }
        return {};
    }

    WaterThemeRecordRef findDaySet(bool level_only) const
    {
        const size_t db_count = level_only
            ? std::min<size_t>(views_.size(), 1)
            : views_.size();
        for (size_t db = 0; db < db_count; ++db) {
            for (const WaterThemeRecordRef& level :
                 lookupAllInDb(kHashLevelData, db)) {
                WaterThemeRecordRef day_set =
                    resolveRecordField(level, kHashEnvironmentThemeDaySet);
                if (day_set.valid) return day_set;
            }
        }

        for (size_t db = 0; db < db_count; ++db) {
            WaterThemeRecordRef owner =
                firstRecordWithFieldInDb(kHashEnvironmentThemeDaySet, db);
            if (!owner.valid) continue;
            WaterThemeRecordRef day_set =
                resolveRecordField(owner, kHashEnvironmentThemeDaySet);
            if (day_set.valid) return day_set;
        }

        return {};
    }

    bool findLocalField(WaterThemeRecordRef record,
                        uint32_t field_hash,
                        uint8_t expected_type,
                        WaterThemeFieldRef& out) const
    {
        if (!record.valid || record.db >= views_.size()) return false;
        const GdbView& v = view(record);
        size_t slot = 0;
        uint8_t type = 0;
        if (!v.findLocal(record.record, field_hash, expected_type,
                         slot, &type)) {
            return false;
        }
        out.owner = record;
        out.slot = slot;
        out.type = type;
        out.raw = ReadBeU32(v.bytes.data() + slot);
        out.f32 = ReadBeF32(v.bytes.data() + slot);
        return true;
    }

    bool findField(WaterThemeRecordRef record,
                   uint32_t field_hash,
                   uint8_t expected_type,
                   WaterThemeFieldRef& out) const
    {
        if (!record.valid || record.db >= views_.size()) return false;

        WaterThemeRecordRef cur = record;
        std::unordered_set<uint64_t> seen;
        for (int depth = 0; depth < 64; ++depth) {
            const uint64_t key =
                (uint64_t(cur.db) << 48) ^ uint64_t(cur.record);
            if (!seen.insert(key).second) return false;

            if (findLocalField(cur, field_hash, expected_type, out)) {
                return true;
            }

            WaterThemeFieldRef parent_field;
            if (!findLocalField(cur, kHashParent, 0xFF, parent_field)) {
                return false;
            }
            cur = fieldToRecord(parent_field);
            if (!cur.valid) return false;
        }
        return false;
    }

    WaterThemeRecordRef fieldToRecord(const WaterThemeFieldRef& field) const
    {
        if (field.raw == 0 || field.raw == kHashNull) return {};
        if (field.type != 4 && field.type != 6 && field.type != 7) return {};
        return lookupFirst(field.raw);
    }

    WaterThemeRecordRef resolveRecordField(WaterThemeRecordRef record,
                                           uint32_t field_hash) const
    {
        WaterThemeFieldRef field;
        if (!findField(record, field_hash, 0xFF, field)) return {};
        return fieldToRecord(field);
    }

    bool readFloat(WaterThemeRecordRef record,
                   uint32_t field_hash,
                   float& out) const
    {
        WaterThemeFieldRef field;
        if (!findField(record, field_hash, 3, field)) return false;
        if (!std::isfinite(field.f32)) return false;
        out = field.f32;
        return true;
    }

    bool readColour(WaterThemeRecordRef record,
                    uint32_t red_hash,
                    uint32_t green_hash,
                    uint32_t blue_hash,
                    float (&out)[3]) const
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        if (!readFloat(record, red_hash, r) ||
            !readFloat(record, green_hash, g) ||
            !readFloat(record, blue_hash, b)) {
            return false;
        }
        out[0] = EnvColourComponentToLinearInput(r);
        out[1] = EnvColourComponentToLinearInput(g);
        out[2] = EnvColourComponentToLinearInput(b);
        return true;
    }

    bool readColourRecordField(WaterThemeRecordRef record,
                               uint32_t field_hash,
                               float (&out)[3]) const
    {
        WaterThemeRecordRef colour = resolveRecordField(record, field_hash);
        if (!colour.valid) return false;
        if (!readColour(colour, kHashRed, kHashGreen, kHashBlue, out)) {
            return false;
        }
        float factor = 1.0f;
        if (readFloat(colour, kHashFactor, factor) &&
            std::isfinite(factor) && factor > 0.0f) {
            out[0] *= factor;
            out[1] *= factor;
            out[2] *= factor;
        }
        return true;
    }

    bool applyWaterRecord(WaterThemeRecordRef water,
                          WaterTheme& theme) const
    {
        if (!water.valid) return false;

        bool any = false;
        float colour[3] = {};
        if (readColour(water,
                       kHashShallowWaterColourRed,
                       kHashShallowWaterColourGreen,
                       kHashShallowWaterColourBlue,
                       colour)) {
            std::copy(std::begin(colour), std::end(colour),
                      std::begin(theme.shallow_colour));
            theme.has_shallow_colour = true;
            any = true;
        }
        if (readColour(water,
                       kHashDeepWaterColourRed,
                       kHashDeepWaterColourGreen,
                       kHashDeepWaterColourBlue,
                       colour)) {
            std::copy(std::begin(colour), std::end(colour),
                      std::begin(theme.deep_colour));
            theme.has_deep_colour = true;
            any = true;
        }

        auto read_param = [&](uint32_t hash, bool& flag, float& dst) {
            float v = 0.0f;
            if (readFloat(water, hash, v)) {
                flag = true;
                dst = v;
                any = true;
            }
        };

        read_param(kHashEdgeBlendMin, theme.has_edge_blend_min,
                   theme.edge_blend_min);
        read_param(kHashEdgeBlendMax, theme.has_edge_blend_max,
                   theme.edge_blend_max);
        read_param(kHashEdgeBlendBias, theme.has_edge_blend_bias,
                   theme.edge_blend_bias);
        read_param(kHashMaxRefractionDistance,
                   theme.has_max_refraction_distance,
                   theme.max_refraction_distance);
        read_param(kHashFresnelBias, theme.has_fresnel_bias,
                   theme.fresnel_bias);
        read_param(kHashReflectionStrength,
                   theme.has_reflection_strength,
                   theme.reflection_strength);
        read_param(kHashRefractionScale, theme.has_refraction_scale,
                   theme.refraction_scale);
        read_param(kHashReflectionScale, theme.has_reflection_scale,
                   theme.reflection_scale);
        read_param(kHashReflectionBias, theme.has_reflection_bias,
                   theme.reflection_bias);
        read_param(kHashNormalScale, theme.has_normal_scale,
                   theme.normal_scale);

        return any;
    }

    bool applyThemeRecord(WaterThemeRecordRef theme_record,
                          WaterTheme& theme) const
    {
        if (!theme_record.valid) return false;
        WaterThemeRecordRef water =
            resolveRecordField(theme_record, kHashWater);
        if (water.valid && applyWaterRecord(water, theme)) {
            return true;
        }
        return applyWaterRecord(theme_record, theme);
    }

    bool applyThemeField(WaterThemeRecordRef owner,
                         uint32_t field_hash,
                         WaterTheme& theme) const
    {
        WaterThemeRecordRef target = resolveRecordField(owner, field_hash);
        return target.valid && applyThemeRecord(target, theme);
    }

    bool applySkyRecord(WaterThemeRecordRef sky,
                        SkyTheme& theme) const
    {
        if (!sky.valid) return false;

        bool any = false;
        float colour[3] = {};
        if (readColourRecordField(sky, kHashSkyColour, colour)) {
            std::copy(std::begin(colour), std::end(colour),
                      std::begin(theme.sky_colour));
            theme.has_sky_colour = true;
            any = true;
        }
        if (readColourRecordField(sky, kHashSkyComplementaryColour, colour)) {
            std::copy(std::begin(colour), std::end(colour),
                      std::begin(theme.complementary_colour));
            theme.has_complementary_colour = true;
            any = true;
        }
        if (readColourRecordField(sky, kHashSunsetColour, colour)) {
            std::copy(std::begin(colour), std::end(colour),
                      std::begin(theme.sunset_colour));
            theme.has_sunset_colour = true;
            any = true;
        }

        auto read_param = [&](uint32_t hash, bool& flag, float& dst) {
            float v = 0.0f;
            if (readFloat(sky, hash, v)) {
                flag = true;
                dst = v;
                any = true;
            }
        };

        read_param(kHashSunIntensity, theme.has_sun_intensity,
                   theme.sun_intensity);
        read_param(kHashSkyBetaRayleighMultiplier, theme.has_rayleigh,
                   theme.rayleigh);
        read_param(kHashSkyBetaMieMultiplier, theme.has_mie,
                   theme.mie);
        read_param(kHashSkyComplementaryColourBias,
                   theme.has_complementary_bias,
                   theme.complementary_bias);

        auto read_texture = [&](uint32_t hash, bool& flag, uint32_t& dst) {
            WaterThemeFieldRef field;
            if (findField(sky, hash, 0xFF, field) &&
                field.raw != 0 && field.raw != kHashNull) {
                flag = true;
                dst = field.raw;
                any = true;
            }
        };
        read_texture(kHashSkyOverlayTexture,
                     theme.has_sky_overlay_texture,
                     theme.sky_overlay_texture_hash);
        read_texture(kHashMoonTexture,
                     theme.has_moon_texture,
                     theme.moon_texture_hash);
        read_texture(kHashMoonGlareTexture,
                     theme.has_moon_glare_texture,
                     theme.moon_glare_texture_hash);
        read_texture(kHashDiscTexture,
                     theme.has_sun_disc_texture,
                     theme.sun_disc_texture_hash);

        return any;
    }

    bool applyFogRecord(WaterThemeRecordRef fog,
                        SkyTheme& theme) const
    {
        if (!fog.valid) return false;

        bool any = false;
        float colour[3] = {};
        if (readColourRecordField(fog, kHashCloseFogColour, colour)) {
            std::copy(std::begin(colour), std::end(colour),
                      std::begin(theme.fog_colour));
            theme.has_fog_colour = true;
            any = true;
        }

        float v = 0.0f;
        if (readFloat(fog, kHashNearDistance, v)) {
            theme.near_distance = v;
            theme.has_near_fog = true;
            any = true;
        }
        if (readFloat(fog, kHashNearDensity, v)) {
            theme.near_density = v;
            theme.has_near_fog = true;
            any = true;
        }
        if (readFloat(fog, kHashFarDistance, v)) {
            theme.far_distance = v;
            theme.has_far_fog = true;
            any = true;
        }
        if (readFloat(fog, kHashFarDensity, v)) {
            theme.far_density = v;
            theme.has_far_fog = true;
            any = true;
        }
        if (readFloat(fog, kHashCloseFogMaxDistance, v)) {
            theme.close_fog_max_distance = v;
            any = true;
        }

        return any;
    }

    bool applySkyThemeRecord(WaterThemeRecordRef theme_record,
                             SkyTheme& theme) const
    {
        if (!theme_record.valid) return false;

        bool any = false;
        WaterThemeRecordRef sky =
            resolveRecordField(theme_record, kHashSky);
        if (sky.valid) {
            any |= applySkyRecord(sky, theme);
        } else {
            any |= applySkyRecord(theme_record, theme);
        }

        WaterThemeRecordRef fog =
            resolveRecordField(theme_record, kHashFogging);
        if (fog.valid) {
            any |= applyFogRecord(fog, theme);
        } else {
            any |= applyFogRecord(theme_record, theme);
        }
        return any;
    }

    bool applySkyThemeField(WaterThemeRecordRef owner,
                            uint32_t field_hash,
                            SkyTheme& theme) const
    {
        WaterThemeRecordRef target = resolveRecordField(owner, field_hash);
        return target.valid && applySkyThemeRecord(target, theme);
    }

    int readCloudLayerRecord(WaterThemeRecordRef layer,
                             CloudLayerTheme& out) const
    {
        if (!layer.valid) return 0;

        int fields = 0;
        auto read_param = [&](uint32_t hash, bool& flag, float& dst) {
            float v = 0.0f;
            if (readFloat(layer, hash, v)) {
                flag = true;
                dst = v;
                ++fields;
            }
        };

        WaterThemeFieldRef density;
        if (findField(layer, kHashDensityMap, 0xFF, density) &&
            density.raw != 0 && density.raw != kHashNull) {
            out.has_density_map = true;
            out.density_map_hash = density.raw;
            ++fields;
        }

        read_param(kHashPositionX, out.has_position, out.position_x);
        read_param(kHashPositionY, out.has_position, out.position_y);
        read_param(kHashSizeX, out.has_size, out.size_x);
        read_param(kHashSizeY, out.has_size, out.size_y);
        read_param(kHashTextureScaleX, out.has_texture_scale,
                   out.texture_scale_x);
        read_param(kHashTextureScaleY, out.has_texture_scale,
                   out.texture_scale_y);
        read_param(kHashVelocityX, out.has_velocity, out.velocity_x);
        read_param(kHashVelocityY, out.has_velocity, out.velocity_y);
        read_param(kHashHeight, out.has_height, out.height);
        read_param(kHashTransparency, out.has_transparency,
                   out.transparency);
        read_param(kHashBrightness, out.has_brightness, out.brightness);
        read_param(kHashAmbientLight, out.has_ambient,
                   out.ambient_light);
        read_param(kHashNormalStrength, out.has_normal_strength,
                   out.normal_strength);
        read_param(kHashTranslucencyStrength,
                   out.has_translucency_strength,
                   out.translucency_strength);

        if (fields > 0) {
            out.enabled = true;
        }
        return fields;
    }

    bool applyCloudLayerField(WaterThemeRecordRef theme_record,
                              uint32_t field_hash,
                              CloudLayerTheme& layer) const
    {
        WaterThemeRecordRef target = resolveRecordField(theme_record,
                                                        field_hash);
        return target.valid && readCloudLayerRecord(target, layer) > 0;
    }

    bool applyCloudThemeRecord(WaterThemeRecordRef theme_record,
                               CloudTheme& theme) const
    {
        if (!theme_record.valid) return false;

        bool any = false;
        constexpr uint32_t kLayerFields[4] = {
            kHashCloudsLayer1, kHashCloudsLayer2,
            kHashCloudsLayer3, kHashCloudsLayer4
        };
        for (int i = 0; i < 4; ++i) {
            CloudLayerTheme layer = theme.layers[i];
            if (applyCloudLayerField(theme_record, kLayerFields[i], layer)) {
                theme.layers[i] = layer;
                any = true;
            }
        }

        if (!any) {
            CloudLayerTheme layer = theme.layers[0];
            const int field_count = readCloudLayerRecord(theme_record, layer);
            if (field_count >= 2 || layer.has_density_map) {
                theme.layers[0] = layer;
                any = true;
            }
        }

        return any;
    }

    bool applyCloudThemeField(WaterThemeRecordRef owner,
                              uint32_t field_hash,
                              CloudTheme& theme) const
    {
        WaterThemeRecordRef target = resolveRecordField(owner, field_hash);
        return target.valid && applyCloudThemeRecord(target, theme);
    }

    bool selectThemeFromDaySet(WaterThemeRecordRef day_set,
                               WaterThemeRecordRef& out_theme,
                               float& out_time) const
    {
        if (!day_set.valid) return false;
        const GdbView& v = view(day_set);
        size_t sch = 0;
        uint32_t n = 0;
        if (!v.schema(day_set.record, sch, n)) return false;
        const size_t descs = sch + 4 + size_t(n) * 4;

        bool found = false;
        float best_dist = std::numeric_limits<float>::max();
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t desc =
                ReadBeU32(v.bytes.data() + descs + size_t(i) * 4);
            const uint8_t type = uint8_t(desc >> 24);
            if (type != 4 && type != 6 && type != 7) continue;

            const size_t slot = day_set.record + 4 + size_t(i) * 4;
            if (slot + 4 > v.body_end) continue;

            WaterThemeFieldRef item_field;
            item_field.owner = day_set;
            item_field.slot = slot;
            item_field.type = type;
            item_field.raw = ReadBeU32(v.bytes.data() + slot);
            item_field.f32 = ReadBeF32(v.bytes.data() + slot);

            WaterThemeRecordRef entry = fieldToRecord(item_field);
            if (!entry.valid) continue;

            WaterThemeRecordRef theme =
                resolveRecordField(entry, kHashTheme);
            if (!theme.valid) continue;

            float time = 0.5f;
            float read_time = 0.0f;
            if (readFloat(entry, kHashTimeOfDay, read_time)) {
                time = read_time;
            }

            const float dist = std::fabs(time - 0.5f);
            if (!found || dist < best_dist) {
                best_dist = dist;
                out_theme = theme;
                out_time = time;
                found = true;
            }
        }
        return found;
    }

    bool applyFirstWaterLikeRecord(WaterTheme& out_theme) const
    {
        for (size_t db = 0; db < views_.size(); ++db) {
            const GdbView& v = views_[db];
            if (!v.ok) continue;
            for (size_t record : v.record_data_offsets) {
                WaterTheme scratch = out_theme;
                WaterThemeRecordRef ref{db, record, true};
                if (applyThemeRecord(ref, scratch)) {
                    out_theme = scratch;
                    return true;
                }
            }
        }
        return false;
    }

    bool applyFirstSkyLikeRecord(SkyTheme& out_theme) const
    {
        for (size_t db = 0; db < views_.size(); ++db) {
            const GdbView& v = views_[db];
            if (!v.ok) continue;
            for (size_t record : v.record_data_offsets) {
                SkyTheme scratch = out_theme;
                WaterThemeRecordRef ref{db, record, true};
                if (applySkyThemeRecord(ref, scratch)) {
                    out_theme = scratch;
                    return true;
                }
            }
        }
        return false;
    }

    bool applyFirstCloudLikeRecords(CloudTheme& out_theme) const
    {
        int layer_index = 0;
        for (size_t db = 0; db < views_.size() && layer_index < 4; ++db) {
            const GdbView& v = views_[db];
            if (!v.ok) continue;
            for (size_t record : v.record_data_offsets) {
                CloudLayerTheme layer = out_theme.layers[layer_index];
                WaterThemeRecordRef ref{db, record, true};
                const int field_count = readCloudLayerRecord(ref, layer);
                if (field_count < 2 && !layer.has_density_map) continue;
                out_theme.layers[layer_index] = layer;
                ++layer_index;
                if (layer_index >= 4) break;
            }
        }
        out_theme.layer_count = layer_index;
        return layer_index > 0;
    }

    void finaliseCloudTheme(CloudTheme& theme) const
    {
        static constexpr float kDefaultVelocity[4][2] = {
            {0.010f,  0.004f},
            {-0.006f, 0.008f},
            {0.004f, -0.005f},
            {-0.012f, 0.003f}
        };

        CloudLayerTheme compact[4];
        int count = 0;
        for (int i = 0; i < 4; ++i) {
            CloudLayerTheme& layer = theme.layers[i];
            if (!layer.enabled) continue;

            layer.texture_scale_x =
                std::clamp(std::fabs(layer.texture_scale_x), 0.08f, 24.0f);
            layer.texture_scale_y =
                std::clamp(std::fabs(layer.texture_scale_y), 0.08f, 24.0f);
            layer.size_x = std::clamp(std::fabs(layer.size_x), 0.05f, 128.0f);
            layer.size_y = std::clamp(std::fabs(layer.size_y), 0.05f, 128.0f);
            layer.height = std::clamp(layer.height, 50.0f, 2500.0f);
            layer.transparency =
                std::clamp(layer.transparency, 0.0f, 1.0f);
            layer.brightness = std::clamp(layer.brightness, 0.0f, 4.0f);
            layer.ambient_light =
                std::clamp(layer.ambient_light, 0.0f, 4.0f);
            layer.normal_strength =
                std::clamp(layer.normal_strength, 0.0f, 4.0f);
            layer.translucency_strength =
                std::clamp(layer.translucency_strength, 0.0f, 4.0f);

            if (!layer.has_velocity) {
                layer.velocity_x = kDefaultVelocity[count][0];
                layer.velocity_y = kDefaultVelocity[count][1];
            } else {
                layer.velocity_x = std::clamp(layer.velocity_x * 0.0001f,
                                              -0.08f, 0.08f);
                layer.velocity_y = std::clamp(layer.velocity_y * 0.0001f,
                                              -0.08f, 0.08f);
                if (std::fabs(layer.velocity_x) +
                        std::fabs(layer.velocity_y) < 0.002f) {
                    layer.velocity_x = kDefaultVelocity[count][0];
                    layer.velocity_y = kDefaultVelocity[count][1];
                }
            }

            if (!layer.has_transparency) {
                layer.transparency = 0.18f + 0.10f * float(count);
            }
            if (!layer.has_brightness) {
                layer.brightness = 0.62f + 0.08f * float(count);
            }
            if (!layer.has_ambient) {
                layer.ambient_light = 0.45f;
            }
            compact[count] = layer;
            ++count;
        }
        for (int i = 0; i < 4; ++i) {
            theme.layers[i] = i < count ? compact[i] : CloudLayerTheme{};
        }
        theme.layer_count = count;
    }
};

}

GdbInfo Parse(const std::vector<uint8_t>& bytes) {
    return ParseWithSaveMap(bytes, {});
}

bool ExtractWaterTheme(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    WaterTheme& out_theme)
{
    WaterThemeExtractor extractor(gdbs);
    return extractor.extract(out_theme);
}

bool ExtractSkyTheme(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    SkyTheme& out_theme)
{
    WaterThemeExtractor extractor(gdbs);
    return extractor.extractSky(out_theme);
}

bool ExtractCloudTheme(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    CloudTheme& out_theme)
{
    WaterThemeExtractor extractor(gdbs);
    return extractor.extractClouds(out_theme);
}

bool ExtractEnvironmentThemeTimeline(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    EnvironmentThemeTimeline& out_timeline)
{
    WaterThemeExtractor extractor(gdbs);
    return extractor.extractEnvironmentTimeline(out_timeline);
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
