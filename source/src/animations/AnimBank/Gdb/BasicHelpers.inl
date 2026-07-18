uint32_t fnv1_32(const char* s, size_t n) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < n; ++i) {
        h = ((h * 0x01000193u) & 0xFFFFFFFFu) ^ (uint8_t)s[i];
    }
    return h;
}

uint32_t be_u32_at(const std::vector<uint8_t>& b, size_t off) {
    if (off + 4 > b.size()) return 0;
    const uint8_t* p = b.data() + off;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

std::string hex8(uint32_t v) {
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%08X", v);
    return buf;
}

bool is_default_clip_name(const AnimClip& clip) {
    if (clip.name.size() != 11 || clip.name.compare(0, 3, "id_") != 0) {
        return false;
    }
    return clip.name.substr(3) == hex8(clip.key0);
}

bool is_gdb_fallback_name(const AnimClip& clip) {
    if (clip.name.size() != 12 || clip.name.compare(0, 4, "gdb_") != 0) {
        return false;
    }
    return clip.name.substr(4) == hex8(clip.key0) ||
           std::all_of(clip.name.begin() + 4, clip.name.end(),
                       [](unsigned char c) {
                           return std::isxdigit(c) != 0;
                       });
}

std::string trim_enum_token(std::string s) {
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

std::string decode_010_enum_token(std::string s) {
    s = trim_enum_token(std::move(s));
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

const std::unordered_map<uint32_t, std::string>& external_gdb_enum_names() {
    static std::unordered_map<uint32_t, std::string> names;
    static bool loaded = false;
    if (loaded) return names;
    loaded = true;

    std::vector<std::string> candidates;
    candidates.emplace_back("F2GDBEnum.bt");
    if (const char* user_profile = std::getenv("USERPROFILE")) {
        candidates.emplace_back(
            std::string(user_profile) + "\\Downloads\\F2GDBEnum.bt");
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

        std::string token = decode_010_enum_token(line.substr(0, eq));
        if (token.empty()) continue;

        size_t end = hex + 2;
        while (end < line.size() &&
               std::isxdigit(static_cast<unsigned char>(line[end]))) {
            ++end;
        }
        if (end == hex + 2) continue;

        try {
            uint32_t h = static_cast<uint32_t>(
                std::stoul(line.substr(hex + 2, end - hex - 2), nullptr, 16));
            names.emplace(h, std::move(token));
        } catch (...) {
        }
    }
    return names;
}
