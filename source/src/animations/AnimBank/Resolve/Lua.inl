size_t resolve_clip_names_from_luas(std::vector<AnimClip>& clips) {
    if (clips.empty() || S.lua_files.empty()) return 0;

    std::unordered_map<uint32_t, size_t> by_key0;
    by_key0.reserve(clips.size());
    for (size_t i = 0; i < clips.size(); ++i) {
        by_key0[clips[i].key0] = i;
    }

    auto scan_quoted = [](const std::string& body,
                          std::vector<std::string_view>& out) {
        const char* p = body.data();
        const size_t n = body.size();
        for (size_t i = 0; i + 1 < n; ++i) {
            char q = p[i];
            if (q != '"' && q != '\'') continue;

            size_t end = i + 1;
            while (end < n && p[end] != q && p[end] != '\n') ++end;
            if (end >= n || p[end] != q) continue;
            const size_t len = end - i - 1;
            if (len < 4 || len > 80) {
                i = end;
                continue;
            }

            const char* s = p + i + 1;
            char c0 = s[0];
            bool ok = (c0 == '_' ||
                       (c0 >= 'A' && c0 <= 'Z') ||
                       (c0 >= 'a' && c0 <= 'z'));
            if (ok) {
                for (size_t k = 0; k < len; ++k) {
                    char c = s[k];
                    bool valid =
                        (c == '_' || c == ' ' || c == '.' ||
                         c == '#' || c == '-' ||
                         (c >= '0' && c <= '9') ||
                         (c >= 'A' && c <= 'Z') ||
                         (c >= 'a' && c <= 'z'));
                    if (!valid) { ok = false; break; }
                }
            }
            if (ok) out.emplace_back(s, len);
            i = end;
        }
    };

    OutputLog::info("Scanning " + std::to_string(S.lua_files.size()) +
                    " Lua scripts for clip names...");

    size_t overridden = 0;
    size_t scripts_read = 0;
    size_t strings_seen = 0;
    size_t scripts_empty = 0;
    size_t scripts_bytecode_failed = 0;

    for (const auto& lua : S.lua_files) {

        std::string body = ::read_lua_file_content(lua.path);
        if (body.empty()) { scripts_empty++; continue; }

        if (body.size() >= 9 && body.compare(0, 9, "-- Error:") == 0) {
            scripts_bytecode_failed++;
            continue;
        }
        scripts_read++;

        std::vector<std::string_view> quoted;
        quoted.reserve(64);
        scan_quoted(body, quoted);
        strings_seen += quoted.size();

        for (const auto& sv : quoted) {
            uint32_t h = fnv1_32(sv.data(), sv.size());
            auto it = by_key0.find(h);
            if (it == by_key0.end()) continue;
            auto& clip = clips[it->second];

            if (clip.name.size() == 11 &&
                clip.name.compare(0, 3, "id_") == 0) {
                clip.name.assign(sv.data(), sv.size());
                overridden++;
            }
        }
    }

    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "Lua scan: %zu scripts read, %zu empty, %zu bytecode "
                  "failed, %zu strings hashed, %zu names resolved (of %zu).",
                  scripts_read, scripts_empty, scripts_bytecode_failed,
                  strings_seen, overridden, clips.size());
    OutputLog::success(buf);
    return overridden;
}
