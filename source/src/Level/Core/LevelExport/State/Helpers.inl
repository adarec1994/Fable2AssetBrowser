std::atomic<bool> g_level_exporting{false};

struct ExportOnlyLevelLoadGuard {
    bool previous = false;

    ExportOnlyLevelLoadGuard()
        : previous(g_level_export_only_load.exchange(true))
    {
        g_pending_terrain_load.store(false);
    }

    ~ExportOnlyLevelLoadGuard()
    {
        g_pending_terrain_load.store(false);
        g_level_export_only_load.store(previous);
    }
};

std::string json_escape(const std::string& s)
{
    std::ostringstream os;
    for (unsigned char c : s) {
        switch (c) {
            case '"':  os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\b': os << "\\b";  break;
            case '\f': os << "\\f";  break;
            case '\n': os << "\\n";  break;
            case '\r': os << "\\r";  break;
            case '\t': os << "\\t";  break;
            default:
                if (c < 0x20) {
                    os << "\\u" << std::hex << std::setw(4)
                       << std::setfill('0') << int(c)
                       << std::dec << std::setfill(' ');
                } else {
                    os << char(c);
                }
                break;
        }
    }
    return os.str();
}

std::string to_slash(std::string s)
{
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

std::string lower_copy(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

uint64_t fnv1a64(const std::string& s)
{
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= uint64_t(c);
        h *= 1099511628211ull;
    }
    return h;
}

std::string hex8(uint64_t v)
{
    std::ostringstream os;
    os << std::hex << std::setw(8) << std::setfill('0')
       << uint32_t(v & 0xffffffffu);
    return os.str();
}

std::string sanitize_name(std::string s)
{
    if (s.empty()) s = "export";
    for (char& c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 32 || c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') {
            c = '_';
        }
    }
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
    return s.empty() ? std::string("export") : s;
}

std::vector<std::string> path_parts(const std::string& p)
{
    std::vector<std::string> parts;
    std::string cur;
    for (char c : p) {
        if (c == '/' || c == '\\') {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

std::string stem_of(std::string p)
{
    std::replace(p.begin(), p.end(), '\\', '/');
    const size_t slash = p.find_last_of('/');
    std::string leaf = (slash == std::string::npos) ? p : p.substr(slash + 1);
    const size_t dot = leaf.find_last_of('.');
    if (dot != std::string::npos) leaf.resize(dot);
    return leaf.empty() ? std::string("asset") : leaf;
}

std::string level_folder_name(const FlatAssetEntry& entry)
{
    const auto parts = path_parts(entry.full_path);
    std::string stem = stem_of(entry.name.empty() ? entry.full_path
                                                  : entry.name);
    const std::string low = lower_copy(stem);
    if ((low == "defaultscenario" || low == "chapter1" ||
         low == "chapter2" || low == "chapter3" || low == "chapter4") &&
        parts.size() >= 3) {
        const std::string parent = lower_copy(parts[parts.size() - 2]);
        if (parent == low || parent == "defaultscenario") {
            stem = parts[parts.size() - 3];
        }
    }
    return sanitize_name(stem);
}
