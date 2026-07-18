std::filesystem::path build_export_target(const std::string& asset_path) {
    std::string rel = asset_path;
    while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\'))
        rel.erase(rel.begin());
    std::filesystem::path root =
        S.export_dir.empty() ? std::filesystem::path("extracted")
                             : std::filesystem::path(S.export_dir);
    return root / rel;
}

bool ext_is(const std::string& name, const char* ext) {
    size_t n = std::strlen(ext);
    if (name.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        char a = name[name.size() - n + i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != ext[i]) return false;
    }
    return true;
}
