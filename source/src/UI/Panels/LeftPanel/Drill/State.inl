enum class DrillKind { None, Bnk, Adb, Lua };

struct DrillState {
    DrillKind kind = DrillKind::None;
    std::string title;
    std::string bnk_path;
    bool        from_nested = false;
    std::vector<BNKItemUI> items;
    std::string filter;

    float anim_t   = 0.0f;
    float target_t = 0.0f;
};

DrillState g_bnk_drill;
DrillState g_tree_drill;

void drill_step_anim(DrillState& d, float dt) {
    constexpr float kSpeed = 7.0f;
    if (d.target_t == d.anim_t) return;
    float dir = (d.target_t > d.anim_t) ? +1.0f : -1.0f;
    d.anim_t += dir * dt * kSpeed;
    if ((dir > 0 && d.anim_t > d.target_t) ||
        (dir < 0 && d.anim_t < d.target_t)) {
        d.anim_t = d.target_t;
    }
}

void drill_open_bnk(DrillState& d, const std::string& bnk_path,
                    bool from_nested) {
    d.kind        = DrillKind::Bnk;
    d.title       = std::filesystem::path(bnk_path).filename().string();
    d.bnk_path    = bnk_path;
    d.from_nested = from_nested;
    d.items.clear();
    d.filter.clear();
    try {
        BNKReader reader(bnk_path);
        const auto& files = reader.list_files();
        d.items.reserve(files.size());
        for (size_t i = 0; i < files.size(); ++i) {
            d.items.push_back({(int)i, files[i].name,
                               files[i].uncompressed_size});
        }
        std::sort(d.items.begin(), d.items.end(),
                  [](const BNKItemUI& a, const BNKItemUI& b) {
                      auto la = std::filesystem::path(a.name)
                                    .filename().string();
                      auto lb = std::filesystem::path(b.name)
                                    .filename().string();
                      std::transform(la.begin(), la.end(), la.begin(),
                                     ::tolower);
                      std::transform(lb.begin(), lb.end(), lb.begin(),
                                     ::tolower);
                      return la < lb;
                  });
    } catch (...) {

    }
    d.target_t = 1.0f;
}

void drill_open_adb(DrillState& d) {
    d.kind        = DrillKind::Adb;
    d.title       = "Audio Database";
    d.bnk_path.clear();
    d.from_nested = false;
    d.items.clear();
    d.filter.clear();
    for (size_t i = 0; i < S.adb_paths.size(); ++i) {
        std::error_code ec;
        auto sz = std::filesystem::file_size(S.adb_paths[i], ec);
        d.items.push_back({(int)i, S.adb_paths[i],
                           ec ? 0u : (uint32_t)sz});
    }
    d.target_t = 1.0f;
}

void drill_open_lua(DrillState& d) {
    d.kind        = DrillKind::Lua;
    d.title       = "Lua Scripts";
    d.bnk_path.clear();
    d.from_nested = false;
    d.items.clear();
    d.filter.clear();
    for (size_t i = 0; i < S.lua_files.size(); ++i) {
        d.items.push_back({(int)i, S.lua_files[i].filename,
                           S.lua_files[i].size});
    }
    d.target_t = 1.0f;
}

std::string lua_script_list_label(const LuaFileUI& e) {
    std::string label = e.path;
    if (!S.root_dir.empty() && label.rfind(S.root_dir, 0) == 0) {
        label.erase(0, S.root_dir.size());
        while (!label.empty() && (label.front() == '\\' || label.front() == '/')) {
            label.erase(label.begin());
        }
    } else if (label.rfind("iso://", 0) == 0) {
        label.erase(0, 6);
    }
    if (label.empty()) {
        label = e.filename.empty() ? e.path : e.filename;
    }
    return label;
}
