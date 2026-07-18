void open_folder_logic(const std::string &sel) {
    if (sel.empty()) {
        show_error_box("No folder selected");
        return;
    }
    if (!std::filesystem::exists(sel)) {
        show_error_box(std::string("Folder does not exist: ") + sel);
        return;
    }
    if (!std::filesystem::is_directory(sel)) {
        show_error_box(std::string("Selected path is not a directory: ") + sel);
        return;
    }
    try {
        S.bnk_paths = scan_bnks_recursive(sel);
        if (S.bnk_paths.empty()) S.bnk_paths = find_bnks(sel);

        S.adb_paths = scan_adbs_recursive(sel);

        auto lua_paths = scan_luas_recursive(sel);
        S.lua_files.clear();
        S.lua_files.reserve(lua_paths.size());
        for (size_t i = 0; i < lua_paths.size(); ++i) {
            std::filesystem::path p(lua_paths[i]);
            std::error_code ec;
            auto fsize = std::filesystem::file_size(p, ec);
            uint32_t size = ec ? 0 : (uint32_t)fsize;
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
        show_error_box("Error searching for BNK files");
        return;
    }
    if (S.bnk_paths.empty()) {
        S.adb_paths.clear();
        S.lua_files.clear();
        show_error_box(
            std::string("No .bnk files found in:\n") + sel + std::string(
                "\n\nPlease select a folder containing Fable 2 BNK files."));
        return;
    }

    S.root_dir = sel;
    S.last_dir = sel;
    save_last_dir(sel);

    if (Anim::load_toc_for_root(sel, S.anim_clips)) {
        Anim::global_data_file().open_for_root(sel);

        Anim::resolve_clip_names_from_luas(S.anim_clips);
    }
    Anim::load_locomotion_for_root(sel);

    auto get_filename = [](const std::string& p) -> std::string {
        size_t pos = p.find_last_of("/\\");
        if (pos != std::string::npos) return p.substr(pos + 1);
        return p;
    };

    std::sort(S.bnk_paths.begin(), S.bnk_paths.end(), [&get_filename](const std::string &a, const std::string &b) {
        std::string A = get_filename(a);
        std::string B = get_filename(b);
        std::transform(A.begin(), A.end(), A.begin(), ::tolower);
        std::transform(B.begin(), B.end(), B.begin(), ::tolower);
        return A < B;
    });

    std::sort(S.adb_paths.begin(), S.adb_paths.end(), [&get_filename](const std::string &a, const std::string &b) {
        std::string A = get_filename(a);
        std::string B = get_filename(b);
        std::transform(A.begin(), A.end(), A.begin(), ::tolower);
        std::transform(B.begin(), B.end(), B.begin(), ::tolower);
        return A < B;
    });

    S.selected_bnk.clear();
    S.files.clear();
    refresh_file_table();

    start_tree_build_for_root(sel, S.bnk_paths);
}
