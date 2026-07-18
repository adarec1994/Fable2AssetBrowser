bool ends_with_lower_ci(const std::string& s, const char* tail) {
    const size_t n = std::strlen(tail);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c =
            static_cast<unsigned char>(s[s.size() - n + i]);
        if (std::tolower(c) != tail[i]) return false;
    }
    return true;
}

void scan_bnk_gdb_entries(
    BNKReader& reader,
    const std::unordered_map<uint32_t, size_t>& by_key0,
    std::vector<AnimClip>& clips,
    GdbAnimScanStats& stats,
    int depth) {
    const auto& files = reader.list_files();
    if (files.empty()) return;

    for (size_t i = 0; i < files.size(); ++i) {
        const FileEntry& entry = files[i];
        if (ends_with_lower_ci(entry.name, ".gdb")) {
            ++stats.bnk_gdb_entries;
            try {
                std::vector<uint8_t> bytes =
                    reader.extract_index_bytes(static_cast<int>(i));
                if (bytes.empty()) {
                    ++stats.files_seen;
                    continue;
                }
                scan_gdb_animation_fields(bytes, by_key0, clips, stats);
            } catch (...) {
                ++stats.files_seen;
            }
        } else if (depth < 4 && ends_with_lower_ci(entry.name, ".bnk")) {
            try {
                std::vector<uint8_t> bytes =
                    reader.extract_index_bytes(static_cast<int>(i));
                if (bytes.empty()) continue;
                ++stats.bnk_nested_seen;
                BNKReader nested(std::move(bytes));
                scan_bnk_gdb_entries(nested, by_key0, clips, stats,
                                     depth + 1);
            } catch (...) {
            }
        }
    }
}

void scan_bnk_path_for_gdbs(
    const std::string& bnk_path,
    const std::unordered_map<uint32_t, size_t>& by_key0,
    std::vector<AnimClip>& clips,
    GdbAnimScanStats& stats) {
    try {
        ++stats.bnks_seen;
        BNKReader reader(bnk_path);
        scan_bnk_gdb_entries(reader, by_key0, clips, stats, 0);
    } catch (...) {
    }
}
