std::string hex_u32(uint32_t v)
{
    std::ostringstream os;
    os << "0x" << std::uppercase << std::hex
       << std::setw(8) << std::setfill('0') << v;
    return os.str();
}

std::string gdb_shell_sample_text(
    const Gdb::Placement& p,
    const std::string& model_path)
{
    std::ostringstream os;
    os << (p.entity_name.empty() ? "<unnamed>" : p.entity_name)
       << " parent=" << hex_u32(p.parent_hash);
    if (p.model_path_hash != 0) {
        os << " modelHash=" << hex_u32(p.model_path_hash);
    }
    os << " pos=(" << p.x << ", " << p.y << ", " << p.z << ")"
       << " model=" << model_path;
    return os.str();
}

std::string gdb_instance_key(
    const Gdb::Placement& p,
    const std::string& model_path)
{
    auto q = [](float v) -> long long {
        if (!std::isfinite(v)) return 0;
        return static_cast<long long>(std::llround(v * 100.0f));
    };
    std::ostringstream os;
    os << lower_slash(model_path) << '|'
       << std::hex << p.hash_a << '|' << p.parent_hash << std::dec << '|'
       << p.entity_name << '|'
       << q(p.x) << ',' << q(p.y) << ',' << q(p.z);
    return os.str();
}

std::string prop_instance_transform_key(
    const Level::PropInstance& inst,
    const std::string& model_path)
{
    auto q = [](float v) -> long long {
        if (!std::isfinite(v)) return 0;
        return static_cast<long long>(std::llround(v * 100.0f));
    };
    std::ostringstream os;
    os << lower_slash(model_path);
    for (int i = 0; i < 12; ++i) {
        os << '|' << q(inst.values[i]);
    }
    return os.str();
}

std::string companion_interior_path(const std::string& model_path)
{
    std::string p = lower_slash(model_path);
    const std::string suffix = "/exterior.mdl";
    if (p.size() < suffix.size() ||
        p.compare(p.size() - suffix.size(), suffix.size(), suffix) != 0)
    {
        return {};
    }
    p.replace(p.size() - suffix.size(), suffix.size(), "/interior.mdl");
    return p;
}

std::string companion_exterior_path(const std::string& model_path)
{
    std::string p = lower_slash(model_path);
    const std::string suffix = "/interior.mdl";
    if (p.size() < suffix.size() ||
        p.compare(p.size() - suffix.size(), suffix.size(), suffix) != 0)
    {
        return {};
    }
    p.replace(p.size() - suffix.size(), suffix.size(), "/exterior.mdl");
    return p;
}

std::string house_facade_companion_exterior_path(const std::string& model_path)
{
    std::string p = lower_slash(model_path);
    struct Map {
        const char* facade;
        const char* shell;
    };
    static const Map maps[] = {
        { "bs_townhouse_basic_facade_mid", "bs_townhouse_basic" },
        { "bs_townhouse_basic_facade",     "bs_townhouse_basic" },
        { "bs_townhouse_basic_facade_snow_v2", "bs_townhouse_basic_snow_v2" },
        { "bs_townhouse_v1_facade_mid",    "bs_townhouse_v1" },
        { "bs_townhouse_v1_facade",        "bs_townhouse_v1" },
        { "bs_townhouse_v1_facade_snow",   "bs_townhouse_v1_snow" },
        { "bs_townhouse_v2_facade_mid",    "bs_townhouse_v2" },
        { "bs_townhouse_v2_facade",        "bs_townhouse_v2" },
        { "bs_townhouse_v2_facade_snow",   "bs_townhouse_v2_snow" },
        { "bs_townhouse_v3_facade_snow",   "bs_townhouse_v3_snow" },
        { "bs_townhouse_v1_snow",           "bs_townhouse_v1_snow" },
        { "bs_townhouse_v2_snow",           "bs_townhouse_v2_snow" },
        { "bs_townhouse_v3_snow",           "bs_townhouse_v3_snow" },
    };
    for (const Map& map : maps) {
        const std::string needle =
            std::string("/buildings/dotxsi/") + map.facade + "/" +
            map.facade + ".mdl";
        const size_t pos = p.find(needle);
        if (pos == std::string::npos) continue;

        const std::string exterior =
            std::string("/buildings/dotxsi/") + map.shell + "/" +
            map.shell + "/exterior.mdl";
        p.replace(pos, needle.size(), exterior);
        return p;
    }
    return {};
}

std::string shop_facade_companion_exterior_path(const std::string& model_path)
{
    std::string p = lower_slash(model_path);
    struct Map {
        const char* facade;
        const char* shell;
    };
    static const Map maps[] = {
        {"bs_market_largeshop_facade_mid", "bs_market_largeshop"},
        {"bs_market_largeshop_facade",     "bs_market_largeshop"},
        {"bs_market_smallshop_facade_mid", "bs_market_smallshop"},
        {"bs_market_smallshop_facade",     "bs_market_smallshop"},
        {"bs_market_generalshop_facade_mid", "bs_market_generalshop"},
        {"bs_market_generalshop_facade",     "bs_market_generalshop"},
        {"bs_market_tavern_facade_mid", "bs_market_tavern"},
        {"bs_market_tavern_facade",     "bs_market_tavern"},
    };
    for (const Map& map : maps) {
        const std::string needle =
            std::string("/buildings/dotxsi/") + map.facade + "/" +
            map.facade + ".mdl";
        const size_t pos = p.find(needle);
        if (pos == std::string::npos) continue;

        const std::string exterior =
            std::string("/buildings/dotxsi/") + map.shell + "/" +
            map.shell + "/exterior.mdl";
        p.replace(pos, needle.size(), exterior);
        return p;
    }
    return {};
}

std::string gdb_entity_key(std::string s)
{
    static const char* prefixes[] = {
        "NewObjectBuilding", "ObjectBuilding",
        "NewObjectFurniture", "ObjectFurniture",
        "NewObjectStatic", "ObjectStatic",
        "NewObject", "Object",
        "New"
    };
    for (const char* pfx : prefixes) {
        const size_t n = std::strlen(pfx);
        if (s.size() > n && s.compare(0, n, pfx) == 0) {
            s = s.substr(n);
            break;
        }
    }
    return compact_match_key(s);
}

bool is_gdb_landmark_name(const std::string& entity_name)
{
    const std::string key = gdb_entity_key(entity_name);
    if (key.empty()) return false;
    const char* needles[] = {
        "bridge",
        "clocktower",
        "grandfatherclock",
        "wallclock",
        "dockarch",
        "gatehouse",
        "lockgate",
        "walltower",
        "wallgate",
        "archway",
        "guardpost",
        "marketstairs",
        "scaffoldingstairs",
        "castlearch",
        "dockswall",
        "oilamp",
        "oillantern",
        "statue",
    };
    for (const char* needle : needles) {
        if (key.find(needle) != std::string::npos) return true;
    }
    return false;
}

bool bytes_contain_be_u32(const std::vector<uint8_t>& bytes, uint32_t value)
{
    const uint8_t a = uint8_t(value >> 24);
    const uint8_t b = uint8_t(value >> 16);
    const uint8_t c = uint8_t(value >> 8);
    const uint8_t d = uint8_t(value);
    for (size_t i = 0; i + 4 <= bytes.size(); ++i) {
        if (bytes[i] == a && bytes[i + 1] == b &&
            bytes[i + 2] == c && bytes[i + 3] == d) {
            return true;
        }
    }
    return false;
}

std::string hex32_for_log(uint32_t value)
{
    std::ostringstream os;
    os << "0x" << std::hex << std::uppercase
       << std::setw(8) << std::setfill('0') << value;
    return os.str();
}

void log_curated_hashlist_miss(const std::string& entity_name,
                               uint32_t parent_hash,
                               const char* target_model_path)
{
    static std::mutex logged_mutex;
    static std::unordered_set<std::string> logged_keys;

    std::string key = hex32_for_log(parent_hash) + "|" +
                      gdb_entity_key(entity_name) + "|" +
                      (target_model_path ? target_model_path : "");
    {
        std::lock_guard<std::mutex> lock(logged_mutex);
        if (!logged_keys.insert(key).second) return;
    }

    OutputLog::warn(
        "GDB hashlist: curated model target missing in streaming candidates; "
        "parent=" + hex32_for_log(parent_hash) +
        " entity='" + entity_name +
        "' target='" + (target_model_path ? target_model_path : "") + "'");
}
