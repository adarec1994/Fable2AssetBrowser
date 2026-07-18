bool resolve_texture_targets(const std::string& preferred_bnk,
                             int preferred_index, TextureTargets& out,
                             std::string& err) {
    const auto preferred_nested_it =
        S.nested_bnk_parents.find(preferred_bnk);
    const bool preferred_is_nested =
        preferred_nested_it != S.nested_bnk_parents.end();
    const std::string preferred_nested_parent =
        preferred_is_nested ? preferred_nested_it->second : std::string();
    BnkCache::Entry selected;
    try {
        selected = BnkCache::get(preferred_bnk);
    } catch (...) {
        err = "Could not open the selected texture bank.";
        return false;
    }
    const auto& selected_files = selected.reader->list_files();
    if (preferred_index < 0 ||
        preferred_index >= (int)selected_files.size()) {
        err = "Selected texture entry is out of range.";
        return false;
    }
    out.virtual_path = selected_files[(size_t)preferred_index].name;
    const std::string key = normalized_path(out.virtual_path);
    const std::filesystem::path preferred_parent =
        std::filesystem::path(preferred_bnk).parent_path();
    const std::string preferred_family =
        texture_bank_family(preferred_bnk);

    auto consider = [&](TexturePart& part, const std::string& path,
                        int index, int rank) {
        if (rank < part.rank) part = {path, index, rank};
    };

    auto same_scope = [&](const std::string& path) {
        const auto nested_it = S.nested_bnk_parents.find(path);
        if (preferred_is_nested) {
            return nested_it != S.nested_bnk_parents.end() &&
                   nested_it->second == preferred_nested_parent;
        }
        return nested_it == S.nested_bnk_parents.end() &&
               std::filesystem::path(path).parent_path() ==
                   preferred_parent;
    };

    std::vector<std::string> paths = S.bnk_paths;
    paths.insert(paths.end(), S.nested_bnk_paths.begin(),
                 S.nested_bnk_paths.end());
    if (std::find(paths.begin(), paths.end(), preferred_bnk) == paths.end()) {
        paths.push_back(preferred_bnk);
    }
    for (const std::string& path : paths) {
        const bool path_is_nested = S.nested_bnk_parents.count(path) != 0;
        if (preferred_is_nested != path_is_nested) continue;
        if (preferred_is_nested && !same_scope(path)) continue;
        const int index = BnkCache::find_index(path, key);
        if (index < 0) continue;
        int rank = path == preferred_bnk ? 0 : 3;
        if (rank != 0 && same_scope(path)) {
            rank = 1;
        } else if (rank != 0 &&
                   texture_bank_family(path) == preferred_family) {
            rank = 2;
        }
        const int role = texture_bank_role(path);
        if (role == 1) consider(out.header, path, index, rank);
        else if (role == 2 && same_scope(path)) {
            consider(out.mip0, path, index, rank);
        } else if (role == 3) {
            consider(out.body, path, index, rank);
        } else if (role == 4 &&
                   (path == preferred_bnk || out.body.path.empty())) {
            consider(out.body, path, index,
                     path == preferred_bnk ? 0 : rank + 10);
        }
    }
    if (out.header.path.empty() || out.body.path.empty()) {
        err = "Could not resolve both the header and body banks for '" +
              out.virtual_path + "'.";
        return false;
    }
    if (out.mip0.path.empty()) {
        for (const std::string& path : paths) {
            if (texture_bank_role(path) == 2 && same_scope(path)) {
                out.mip0.path = path;
                out.mip0.index = -1;
                out.mip0.rank = 1;
                break;
            }
        }
    }
    return true;
}
