bool quest_source_has_debug_symbols(std::string bnk_path) {
    std::replace(bnk_path.begin(), bnk_path.end(), '\\', '/');
    const size_t slash = bnk_path.find_last_of('/');
    if (slash != std::string::npos) bnk_path.erase(0, slash + 1);
    std::transform(bnk_path.begin(), bnk_path.end(), bnk_path.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return bnk_path == "gamescripts.bnk";
}

void select_quest_script(size_t idx) {
    if (idx >= S.all_quest_files.size()) return;

    const FlatAssetEntry entry = S.all_quest_files[idx];
    const std::string quest_tab_key = entry.full_path.empty()
        ? entry.bnk_path + "#" + std::to_string(entry.file_index)
        : entry.full_path;
    const std::string quest_tab_title = entry.full_path.empty()
        ? entry.name : entry.full_path;
    ContentTabs::OpenLua(quest_tab_key, quest_tab_title, true);
    S.selected_quest = (int)idx;
    S.selected_item = -1;
    S.show_item_details = false;
    S.item_model_active = false;
    S.selected_entity = -1;
    S.show_entity_details = false;
    S.entity_model_active = false;
    S.viewing_lua = false;
    S.viewing_adb = false;
    S.show_gdb_render = false;

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
    S.texture_window_width = 0;
    S.texture_window_height = 0;
#else
    g_mp.has_model = false;
#endif

    S.lua_preview_selected = -1;
    S.lua_preview_title = quest_tab_title;
    S.lua_preview_content.clear();
    S.lua_preview_loading = true;
    S.lua_preview_is_quest = true;
    S.quest_preview_select_nodes = true;
    const uint64_t preview_request = ++S.lua_preview_request;
    QuestUI::Clear();
    QuestUI::RefreshReferenceCatalog();
    S.show_lua_render = true;

    OutputLog::info("Decompiling quest Lua: " + entry.full_path);
    progress_open(0, "Decompiling " + entry.name + "...");
    std::thread([entry, quest_tab_key, preview_request]() {
        std::string content;
        try {
            const auto bytes =
                BnkCache::extract_bytes(entry.bnk_path, entry.file_index);
            if (bytes.empty()) {
                content = "-- Error: empty quest script entry";
            } else if (bytes.size() > 10 * 1024 * 1024) {
                content = "-- Error: quest script is too large to preview (>10MB)";
            } else {
                const bool is_bytecode =
                    bytes.size() >= 4 && bytes[0] == 0x1B &&
                    bytes[1] == 'L' && bytes[2] == 'u' &&
                    bytes[3] == 'a';
                if (is_bytecode) {
                    content = decompile_lua51_bytecode(bytes.data(),
                                                       bytes.size());
                } else {
                    content.assign(bytes.begin(), bytes.end());
                }
            }
        } catch (const std::exception& ex) {
            content = std::string("-- Error: ") + ex.what();
        } catch (...) {
            content = "-- Error: extracting quest Lua failed";
        }
        ContentTabs::CompleteLua(quest_tab_key, content);
        if (S.lua_preview_request.load() == preview_request) {
            QuestUI::SetQuestSource(
                entry.full_path.empty() ? entry.name : entry.full_path,
                content);
            if (S.lua_preview_request.load() == preview_request) {
                S.lua_preview_content = std::move(content);
                S.lua_preview_loading = false;
            }
        }
        progress_done();
    }).detach();
}

void show_authored_quest(const std::string& quest_id) {
    ++S.lua_preview_request;
    if (!QuestUI::OpenAuthoredQuest(quest_id)) return;
    ContentTabs::OpenCustomQuest(quest_id,
                                "Custom quest: " + quest_id);

    S.selected_quest = -1;
    S.selected_item = -1;
    S.show_item_details = false;
    S.item_model_active = false;
    S.selected_entity = -1;
    S.show_entity_details = false;
    S.entity_model_active = false;
    S.viewing_lua = false;
    S.viewing_adb = false;
    S.show_gdb_render = false;
    S.lua_preview_selected = -1;
    S.lua_preview_title = "Custom quest: " + quest_id;
    S.lua_preview_content = QuestUI::ActiveAuthoredLua();
    S.lua_preview_loading = false;
    S.lua_preview_is_quest = true;
    S.quest_preview_select_nodes = true;
    S.show_lua_render = true;
}

bool shipped_quest_id_exists(const std::string& quest_id) {
    std::string wanted = quest_id;
    std::transform(wanted.begin(), wanted.end(), wanted.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    for (const FlatAssetEntry& entry : S.all_quest_files) {
        std::string stem = std::filesystem::path(
            entry.name.empty() ? entry.full_path : entry.name).stem().string();
        std::transform(stem.begin(), stem.end(), stem.begin(),
                       [](unsigned char c) {
                           return char(std::tolower(c));
                       });
        if (stem == wanted) return true;
    }
    return false;
}

