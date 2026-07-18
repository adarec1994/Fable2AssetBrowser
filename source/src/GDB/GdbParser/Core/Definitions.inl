
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
