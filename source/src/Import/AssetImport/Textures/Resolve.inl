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

    auto nested_virtual_dir = [&](const std::string& path) -> std::string {
        const auto it = S.nested_bnk_virtual_paths.find(path);
        if (it == S.nested_bnk_virtual_paths.end()) return std::string();
        std::string v = normalized_path(it->second);
        const size_t slash = v.find_last_of('/');
        return slash == std::string::npos ? std::string()
                                          : v.substr(0, slash);
    };
    const std::string preferred_virtual_dir =
        preferred_is_nested ? nested_virtual_dir(preferred_bnk)
                            : std::string();

    auto same_scope = [&](const std::string& path) {
        const auto nested_it = S.nested_bnk_parents.find(path);
        if (preferred_is_nested) {
            return nested_it != S.nested_bnk_parents.end() &&
                   nested_it->second == preferred_nested_parent &&
                   nested_virtual_dir(path) == preferred_virtual_dir;
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
    if (out.header.path.empty()) {
        for (const std::string& path : paths) {
            if (texture_bank_role(path) != 1) continue;
            const int index = BnkCache::find_index(path, key);
            if (index < 0) continue;
            consider(out.header, path, index, 5);
        }
    }
    if (out.body.path.empty()) {
        for (const std::string& path : paths) {
            if (!is_shared_texture_bank(path)) continue;
            const int index = BnkCache::find_index(path, key);
            if (index < 0) continue;
            consider(out.body, path, index, 5);
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

bool resolve_all_texture_targets(const std::string& preferred_bnk,
                                 int preferred_index,
                                 std::vector<TextureTargets>& groups,
                                 std::vector<TexturePart>& shared_mip0,
                                 std::string& err) {
    groups.clear();
    shared_mip0.clear();

    TextureTargets clicked;
    if (!resolve_texture_targets(preferred_bnk, preferred_index, clicked,
                                 err)) {
        return false;
    }
    const std::string key = normalized_path(clicked.virtual_path);

    auto scope_key = [&](const std::string& path) -> std::string {
        const auto it = S.nested_bnk_parents.find(path);
        if (it != S.nested_bnk_parents.end()) {
            std::string vdir;
            const auto vit = S.nested_bnk_virtual_paths.find(path);
            if (vit != S.nested_bnk_virtual_paths.end()) {
                vdir = normalized_path(vit->second);
                const size_t slash = vdir.find_last_of('/');
                vdir = slash == std::string::npos ? std::string()
                                                  : vdir.substr(0, slash);
            }
            return "nested|" + it->second + "|" + vdir;
        }
        return "disk|" +
               std::filesystem::path(path).parent_path().string();
    };
    const std::string clicked_scope = scope_key(clicked.header.path);

    if (clicked.mip0.index >= 0) {
        shared_mip0.push_back(clicked.mip0);
    }
    clicked.mip0 = TexturePart{};
    groups.push_back(clicked);

    struct Extra { TexturePart header, body; };
    std::map<std::string, Extra> extra;

    std::vector<std::string> paths = S.bnk_paths;
    paths.insert(paths.end(), S.nested_bnk_paths.begin(),
                 S.nested_bnk_paths.end());
    for (const std::string& path : paths) {
        const int role = texture_bank_role(path);
        if (role != 1 && role != 2 && role != 3) continue;
        const int index = BnkCache::find_index(path, key);
        if (index < 0) continue;
        if (role == 2) {
            bool known = false;
            for (const auto& part : shared_mip0) {
                if (part.path == path && part.index == index) {
                    known = true;
                    break;
                }
            }
            if (!known) shared_mip0.push_back({path, index, 0});
            continue;
        }
        std::string sk = scope_key(path);
        if (is_shared_texture_bank(path)) {
            if (path == clicked.body.path) continue;
            sk += "|" + normalized_path(
                std::filesystem::path(path).filename().string());
        }
        if (sk == clicked_scope) continue;
        Extra& g = extra[sk];
        if (role == 1) {
            if (g.header.index < 0) g.header = {path, index, 0};
        } else if (g.body.index < 0) {
            g.body = {path, index, 0};
        }
    }

    for (auto& [sk, g] : extra) {
        if (g.header.index < 0 && g.body.index < 0) continue;
        TextureTargets t;
        t.virtual_path = groups.front().virtual_path;
        t.header = g.header;
        t.body = g.body;
        groups.push_back(std::move(t));
    }
    return true;
}
