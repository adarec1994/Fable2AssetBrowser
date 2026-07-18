void select_lua_script(size_t idx) {
    S.viewing_lua = true;
    S.viewing_adb = false;
    S.show_gdb_render = false;
    S.selected_bnk.clear();
    S.global_search.clear();
    S.files.clear();
    S.files.reserve(S.lua_files.size());
    S.selected_file_index = -1;

    for (size_t i = 0; i < S.lua_files.size(); ++i) {
        S.files.push_back({(int)i, S.lua_files[i].filename,
                           S.lua_files[i].size});
    }

    if (idx >= S.lua_files.size()) {
        return;
    }

    S.selected_file_index = (int)idx;

    const std::string lua_path = S.lua_files[idx].path;
    const std::string lua_title = S.lua_files[idx].filename;
    ContentTabs::OpenLua(lua_path, lua_title, false);

    g_pending_mdl_load = false;
    g_pending_tex_load = false;
    g_pending_mdl_index = -1;
    g_pending_tex_index = -1;
    g_pending_mdl_full_path.clear();

#ifdef _WIN32
    if (g_mp.has_model) MP_Release(g_mp);
    g_mp.has_model = false;
    if (S.texture_window_srv) {
        S.texture_window_srv->Release();
        S.texture_window_srv = nullptr;
    }
    S.texture_window_width  = 0;
    S.texture_window_height = 0;
#else
    g_mp.has_model = false;
#endif

    S.lua_preview_selected = (int)idx;
    S.lua_preview_title    = lua_title;
    S.lua_preview_content.clear();
    S.lua_preview_loading  = true;
    S.lua_preview_is_quest = false;
    S.quest_preview_select_nodes = false;
    const uint64_t preview_request = ++S.lua_preview_request;
    S.show_lua_render      = true;
    S.show_gdb_render      = false;

    OutputLog::info("Decompiling Lua: " + lua_title);
    progress_open(0, "Decompiling " + lua_title + "...");
    std::thread([lua_path, preview_request]() {
        std::string content = read_lua_file_content(lua_path);
        ContentTabs::CompleteLua(lua_path, content);
        if (S.lua_preview_request.load() == preview_request) {
            S.lua_preview_content = std::move(content);
            S.lua_preview_loading = false;
        }
        progress_done();
    }).detach();
}

