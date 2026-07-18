std::string gdb_lower_slash(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

uint32_t gdb_fnv1_model_path_hash(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    std::replace(s.begin(), s.end(), '/', '\\');

    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : s) {
        h *= 0x01000193u;
        h ^= uint32_t(c);
    }
    return h;
}

std::vector<std::pair<uint32_t, std::string>>
parse_save_hash_to_name(const std::vector<uint8_t>& save_bytes)
{
    std::vector<std::pair<uint32_t, std::string>> out;
    if (save_bytes.empty()) return out;
    std::string xml(reinterpret_cast<const char*>(save_bytes.data()),
                    save_bytes.size());
    const std::string tag_open = "<Entity name=\"";
    const std::string tag_close = "</Entity>";
    size_t pos = 0;
    while (true) {
        size_t a = xml.find(tag_open, pos);
        if (a == std::string::npos) break;
        a += tag_open.size();
        size_t name_end = xml.find('"', a);
        if (name_end == std::string::npos) break;
        std::string name = xml.substr(a, name_end - a);
        size_t hash_start = xml.find("0x", name_end);
        if (hash_start == std::string::npos) break;
        size_t hash_end = xml.find('<', hash_start);
        if (hash_end == std::string::npos) break;
        std::string hex =
            xml.substr(hash_start + 2, hash_end - hash_start - 2);
        size_t entity_close = xml.find(tag_close, hash_end);
        if (entity_close == std::string::npos) break;

        uint32_t h = 0;
        bool ok = true;
        for (char c : hex) {
            h <<= 4;
            if (c >= '0' && c <= '9') h |= uint32_t(c - '0');
            else if (c >= 'A' && c <= 'F') h |= uint32_t(c - 'A' + 10);
            else if (c >= 'a' && c <= 'f') h |= uint32_t(c - 'a' + 10);
            else {
                ok = false;
                break;
            }
        }
        if (ok) {
            out.emplace_back(h, std::move(name));
        }
        pos = entity_close + tag_close.size();
    }
    return out;
}

bool find_save_sibling_bytes(const std::string& bnk_path,
                             const std::string& gdb_file_name,
                             std::vector<uint8_t>& out)
{
    std::string save_key = gdb_lower_slash(gdb_file_name);
    const size_t dot = save_key.find_last_of('.');
    if (dot != std::string::npos) {
        save_key.resize(dot);
    }
    save_key += ".save";
    const std::string save_leaf =
        std::filesystem::path(save_key).filename().string();

    auto try_bnk = [&](const std::string& candidate_bnk) -> bool {
        if (candidate_bnk.empty()) return false;
        int idx = BnkCache::find_index(candidate_bnk, save_key);
        if (idx < 0) idx = BnkCache::find_index(candidate_bnk, save_leaf);
        if (idx < 0) return false;
        try {
            out = BnkCache::extract_bytes(candidate_bnk, idx);
        } catch (...) {
            out.clear();
            return false;
        }
        return !out.empty();
    };

    if (try_bnk(bnk_path)) return true;

    if (auto other_bnk = find_bnk_by_virtual_path(save_key)) {
        if (try_bnk(*other_bnk)) return true;
    }

    return false;
}
