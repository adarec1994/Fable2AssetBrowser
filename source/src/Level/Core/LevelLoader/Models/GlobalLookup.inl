const FlatAssetEntry* FindGlobalModelAssetByPathHash(uint32_t path_hash)
{
    if (path_hash == 0 || S.all_mdl_files.empty()) return nullptr;

    static std::string cached_root;
    static const FlatAssetEntry* cached_data = nullptr;
    static size_t cached_size = size_t(-1);
    static std::unordered_map<uint32_t, size_t> model_index;
    const FlatAssetEntry* current_data = S.all_mdl_files.data();
    if (cached_root != S.root_dir || cached_data != current_data ||
        cached_size != S.all_mdl_files.size()) {
        model_index.clear();
        model_index.reserve(S.all_mdl_files.size() * 2 + 1);
        for (size_t i = 0; i < S.all_mdl_files.size(); ++i) {
            uint32_t hash = 0x811C9DC5u;
            for (unsigned char c : S.all_mdl_files[i].full_path) {
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<unsigned char>(c - 'A' + 'a');
                }
                if (c == '/') c = '\\';
                hash *= 0x01000193u;
                hash ^= uint32_t(c);
            }
            model_index.try_emplace(hash, i);
        }
        cached_root = S.root_dir;
        cached_data = current_data;
        cached_size = S.all_mdl_files.size();
    }

    const auto found = model_index.find(path_hash);
    if (found == model_index.end() ||
        found->second >= S.all_mdl_files.size()) {
        return nullptr;
    }
    return &S.all_mdl_files[found->second];
}
std::unordered_map<uint32_t, Gdb::EntityGameplayDetails>
    g_global_entity_gameplay;
Gdb::EntityGameplayOptions g_global_entity_gameplay_options;
static std::atomic<bool>      g_level_async_loading{false};
