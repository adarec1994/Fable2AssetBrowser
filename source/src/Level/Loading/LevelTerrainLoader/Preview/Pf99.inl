bool RenderPf99ToRGBA(const FlatAssetEntry& entry,
                      std::vector<uint8_t>& out_rgba,
                      int&                  out_w,
                      int&                  out_h)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;

    std::vector<uint8_t> level_bytes;
    try {
        auto v = BnkCache::extract_bytes(entry.bnk_path, entry.file_index);
        level_bytes.assign(v.begin(), v.end());
    } catch (...) {
        OutputLog::error("Open PF99: failed to extract level");
        return false;
    }

    EngineLevelInfo info;
    if (!ParseEngineLevel(level_bytes, info)) {
        OutputLog::error("Open PF99: level parse failed: " + info.error);
        return false;
    }

    auto ends_with_ci = [](const std::string& s, const char* suffix) {
        size_t n = std::strlen(suffix);
        if (s.size() < n) return false;
        for (size_t i = 0; i < n; ++i) {
            char a = s[s.size() - n + i];
            char b = suffix[i];
            if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
            if (a != b) return false;
        }
        return true;
    };
    auto basename_no_ext = [](const std::string& p) {
        size_t slash = p.find_last_of("/\\");
        std::string s = slash == std::string::npos ? p : p.substr(slash + 1);
        size_t dot = s.find_last_of('.');
        if (dot != std::string::npos) s.resize(dot);
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return s;
    };

    std::vector<std::string> ehf_refs;
    for (const auto& e : info.entries) {
        if (!e.str_a.empty() && ends_with_ci(e.str_a, ".ehf")) {
            ehf_refs.push_back(e.str_a);
        }
    }

    std::string list_ehf;
    std::string list_ghf;
    {
        std::filesystem::path lp = entry.full_path;
        lp.replace_extension(".list");
        std::string list_key = lp.string();
        std::transform(list_key.begin(), list_key.end(), list_key.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        std::replace(list_key.begin(), list_key.end(), '\\', '/');
        const int list_idx = BnkCache::find_index(entry.bnk_path, list_key);
        if (list_idx >= 0) {
            try {
                auto bytes = BnkCache::extract_bytes(entry.bnk_path, list_idx);
                std::string list_str(
                    reinterpret_cast<const char*>(bytes.data()),
                    bytes.size());
                size_t pos = 0;
                while (pos < list_str.size()) {
                    size_t eol = list_str.find_first_of("\r\n", pos);
                    std::string line = (eol == std::string::npos)
                        ? list_str.substr(pos)
                        : list_str.substr(pos, eol - pos);
                    pos = (eol == std::string::npos)
                        ? list_str.size()
                        : list_str.find_first_not_of("\r\n", eol);
                    if (pos == std::string::npos) pos = list_str.size();
                    if (ends_with_ci(line, ".ehf")) list_ehf = line;
                    else if (ends_with_ci(line, ".ghf")) list_ghf = line;
                }
            } catch (...) {}
        }
    }

    std::string ehf_path = list_ehf;
    if (ehf_path.empty() && !ehf_refs.empty()) {
        const std::string ghf_base = basename_no_ext(list_ghf);
        for (const auto& candidate : ehf_refs) {
            if (!ghf_base.empty() &&
                basename_no_ext(candidate) == ghf_base) {
                ehf_path = candidate;
                break;
            }
        }
        if (ehf_path.empty()) ehf_path = ehf_refs.front();
    }
    if (ehf_path.empty()) {
        OutputLog::error("Open PF99: no .ehf reference found");
        return false;
    }

    HeightfieldFiles hf;
    if (!LoadHeightfieldFiles(ehf_path, {}, {}, {}, hf)) {
        OutputLog::error("Open PF99: .ehf load failed: " + hf.error);
        return false;
    }

    EhfParsedBody parsed;
    if (!ParseEhfBody(hf.ehf_bytes, parsed)) {
        OutputLog::error("Open PF99: EHF parse failed: " + parsed.error);
        return false;
    }
    if (parsed.splat_indices.empty() ||
        parsed.splat_w == 0 || parsed.splat_h == 0 ||
        parsed.splat_indices.size() !=
            size_t(parsed.splat_w) * size_t(parsed.splat_h)) {
        OutputLog::error("Open PF99: no PF99 layer mask atlas");
        return false;
    }

    out_w = int(parsed.splat_w);
    out_h = int(parsed.splat_h);
    out_rgba.resize(size_t(out_w) * size_t(out_h) * 4);
    for (size_t i = 0; i < parsed.splat_indices.size(); ++i) {
        const uint8_t v = parsed.splat_indices[i];
        out_rgba[i * 4 + 0] = v;
        out_rgba[i * 4 + 1] = v;
        out_rgba[i * 4 + 2] = v;
        out_rgba[i * 4 + 3] = 255;
    }
    return true;
}
