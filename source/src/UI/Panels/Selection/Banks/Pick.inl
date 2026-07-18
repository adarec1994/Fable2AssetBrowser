void pick_bnk(const std::string &path) {
    S.selected_bnk = path;
    S.viewing_lua = false;
    S.lua_preview_content.clear();
    S.lua_preview_title.clear();
    S.lua_preview_selected = -1;
    ++S.lua_preview_request;
    S.lua_preview_loading = false;
    S.lua_preview_is_quest = false;
    S.quest_preview_select_nodes = false;
    S.show_gdb_render = false;
    S.gdb_view_rows.clear();
    S.gdb_view_title.clear();
    S.gdb_view_filter.clear();
    S.selected_nested_temp_path.clear();
    S.files.clear();
    S.file_filter.clear();
    S.ext_filter.clear();
    BNKReader reader(path);
    const auto &fe = reader.list_files();
    S.files.reserve(fe.size());
    for (size_t i = 0; i < fe.size(); ++i) S.files.push_back({(int) i, fe[i].name, fe[i].uncompressed_size});

    auto get_filename = [](const std::string& p) -> std::string {
        size_t pos = p.find_last_of("/\\");
        if (pos != std::string::npos) return p.substr(pos + 1);
        return p;
    };

    std::sort(S.files.begin(), S.files.end(), [&get_filename](const BNKItemUI &a, const BNKItemUI &b) {
        std::string x = get_filename(a.name);
        std::string y = get_filename(b.name);
        std::transform(x.begin(), x.end(), x.begin(), ::tolower);
        std::transform(y.begin(), y.end(), y.begin(), ::tolower);
        return x < y;
    });

    refresh_file_table();
}
