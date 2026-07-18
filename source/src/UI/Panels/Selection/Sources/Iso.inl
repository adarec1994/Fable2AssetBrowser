void open_iso_logic(const std::string& iso_path) {
    if (iso_path.empty()) { show_error_box("No ISO selected"); return; }
    try {

        S.bnk_paths = scan_bnks_recursive(iso_path);
        if (S.bnk_paths.empty()) S.bnk_paths = find_bnks(iso_path);
        S.adb_paths = scan_adbs_recursive(iso_path);

        auto lua_paths = scan_luas_recursive(iso_path);
        S.lua_files.clear();
        S.lua_files.reserve(lua_paths.size());
        for (size_t i = 0; i < lua_paths.size(); ++i) {
            std::filesystem::path p(lua_paths[i]);

            uint32_t size = 0;
            if (ISO::IsoMount::is_iso_path(lua_paths[i])) {
                if (auto* mf = ISO::IsoMount::instance().find(
                        ISO::IsoMount::strip_iso_prefix(lua_paths[i]))) {
                    size = mf->size;
                }
            } else {
                std::error_code ec;
                auto fsize = std::filesystem::file_size(p, ec);
                size = ec ? 0 : (uint32_t)fsize;
            }
            S.lua_files.push_back({(int)i, lua_paths[i], p.filename().string(), size});
        }

        std::sort(S.lua_files.begin(), S.lua_files.end(), [](const LuaFileUI& a, const LuaFileUI& b) {
            std::string x = a.filename, y = b.filename;
            std::transform(x.begin(), x.end(), x.begin(), ::tolower);
            std::transform(y.begin(), y.end(), y.begin(), ::tolower);
            return x < y;
        });
    } catch (...) {
        S.bnk_paths.clear();
        S.adb_paths.clear();
        S.lua_files.clear();
        ISO::IsoMount::instance().unmount();
        show_error_box("Error indexing BNK files in the ISO");
        return;
    }
    if (S.bnk_paths.empty()) {
        S.adb_paths.clear();
        S.lua_files.clear();
        ISO::IsoMount::instance().unmount();
        show_error_box("No .bnk files found in the ISO.");
        return;
    }

    S.root_dir = iso_path;
    S.last_dir = std::filesystem::path(iso_path).parent_path().string();
    save_last_dir(S.last_dir);

    if (Anim::load_toc_for_root(iso_path, S.anim_clips)) {

        Anim::global_data_file().open_for_root(iso_path);

        Anim::resolve_clip_names_from_luas(S.anim_clips);
    }
    Anim::load_locomotion_for_root(iso_path);
    start_tree_build_for_root(iso_path, S.bnk_paths);
}
