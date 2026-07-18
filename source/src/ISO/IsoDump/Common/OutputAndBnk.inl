static bool write_buf_to_disk(const std::filesystem::path& out,
                              const std::vector<unsigned char>& bytes) {
    std::error_code ec;
    if (auto parent = out.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) return false;
    }
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (!bytes.empty()) {
        f.write(reinterpret_cast<const char*>(bytes.data()),
                (std::streamsize)bytes.size());
    }
    return f.good();
}

static std::filesystem::path build_asset_out_path(const FlatAssetEntry& e,
                                                  const char* expected_ext) {
    std::string rel = e.full_path;
    while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\'))
        rel.erase(rel.begin());
    std::filesystem::path root =
        S.export_dir.empty() ? std::filesystem::path("extracted")
                             : std::filesystem::path(S.export_dir);
    std::filesystem::path p(rel);
    p.replace_extension(expected_ext);
    return root / p;
}

static bool ends_with_ci(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char a = static_cast<unsigned char>(s[s.size() - n + i]);
        unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

static std::string nested_asset_prefix_for_bnk(const std::string& bnk_path) {
    auto it = S.nested_bnk_virtual_paths.find(bnk_path);
    if (it == S.nested_bnk_virtual_paths.end()) return {};

    std::string nested_path = it->second;
    std::replace(nested_path.begin(), nested_path.end(), '\\', '/');
    const size_t slash = nested_path.find_last_of('/');
    if (slash == std::string::npos) return {};
    return nested_path.substr(0, slash + 1);
}

struct BnkCacheEntry {
    std::unique_ptr<BNKReader> reader;

    std::unordered_map<std::string, int> by_leaf;
};

static BnkCacheEntry* get_or_open_bnk(
    std::unordered_map<std::string, BnkCacheEntry>& cache,
    const std::string& bnk_path)
{
    auto& ce = cache[bnk_path];
    if (ce.reader) return &ce;
    try {

        ce.reader = std::make_unique<BNKReader>(bnk_path);
        const auto& files = ce.reader->list_files();
        ce.by_leaf.reserve(files.size());
        for (size_t i = 0; i < files.size(); ++i) {
            std::string leaf = std::filesystem::path(files[i].name)
                                   .filename().string();
            std::transform(leaf.begin(), leaf.end(), leaf.begin(), ::tolower);
            ce.by_leaf.emplace(std::move(leaf), (int)i);
        }
        return &ce;
    } catch (...) {

        cache.erase(bnk_path);
        return nullptr;
    }
}

static std::optional<std::string> derive_paired_bnk(
    const std::string& body_bnk_path,
    const char* body_suffix,
    const char* header_suffix,
    const char* globals_fallback)
{
    std::string base = std::filesystem::path(body_bnk_path)
                           .filename().string();
    std::string base_lower = base;
    std::transform(base_lower.begin(), base_lower.end(),
                   base_lower.begin(), ::tolower);
    const std::string body_sfx(body_suffix);
    if (base_lower.size() >= body_sfx.size() &&
        base_lower.compare(base_lower.size() - body_sfx.size(),
                           body_sfx.size(), body_sfx) == 0) {
        std::string paired =
            base_lower.substr(0, base_lower.size() - body_sfx.size())
            + header_suffix;
        if (auto p = find_bnk_by_filename(paired)) return p;
    }

    if (globals_fallback) {
        if (auto p = find_bnk_by_filename(globals_fallback)) return p;
    }
    return std::nullopt;
}

static std::optional<std::string> derive_paired_model_headers_bnk(
    const std::string& body_bnk_path)
{
    return derive_paired_bnk(body_bnk_path, "_models.bnk",
                             "_model_headers.bnk",
                             "globals_model_headers.bnk");
}

static std::optional<std::string> derive_paired_texture_headers_bnk(
    const std::string& body_bnk_path)
{

    return derive_paired_bnk(body_bnk_path, "_textures.bnk",
                             "_texture_headers.bnk",
                             "globals_texture_headers.bnk");
}
