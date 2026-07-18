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
