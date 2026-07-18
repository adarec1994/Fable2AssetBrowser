static const FlatAssetEntry* find_model_by_path_hash_left(uint32_t h) {
    return FindGlobalModelAssetByPathHash(h);
}



static const FlatAssetEntry* find_model_by_path_left(
    const std::string& path) {
    if (path.empty()) return nullptr;
    auto norm = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        std::replace(s.begin(), s.end(), '/', '\\');
        return s;
    };
    const std::string want = norm(path);
    const size_t ws = want.find_last_of('\\');
    const std::string want_leaf =
        ws == std::string::npos ? want : want.substr(ws + 1);
    const FlatAssetEntry* leaf_hit = nullptr;
    for (const auto& mf : S.all_mdl_files) {
        const std::string full = norm(mf.full_path);
        if (full == want) return &mf;
        if (!leaf_hit) {
            const size_t fs = full.find_last_of('\\');
            const std::string leaf =
                fs == std::string::npos ? full : full.substr(fs + 1);
            if (leaf == want_leaf) leaf_hit = &mf;
        }
    }
    return leaf_hit;
}

static void apply_entity_preview_completions() {
    std::vector<EntityPreviewCompletion> completed;
    {
        std::lock_guard<std::mutex> lock(g_entity_preview_mutex);
        completed.swap(g_entity_preview_completions);
    }
    for (EntityPreviewCompletion& result : completed) {
        if (result.request != g_entity_preview_request.load() ||
            result.entity_index != S.selected_entity ||
            ContentTabs::ActiveKind() != ContentTabs::Kind::Entity) {
            continue;
        }
        if (result.meshes.empty()) {
            OutputLog::warn("entity preview: no renderable model parts found");
            continue;
        }
        S.hex_data.clear();
        S.mdl_info_ok = true;
        S.mdl_info = std::move(result.model_info);
        S.mdl_meshes = std::move(result.meshes);
        S.current_mdl_path = std::move(result.primary_model_path);
        S.current_mdl_path_hash = result.primary_model_hash;
        S.item_model_active = false;
        S.selected_item = -1;
        S.show_item_details = false;
        S.entity_model_active = true;
        S.show_entity_details = true;
        S.cam_yaw = 3.14159265f;
        S.cam_pitch = 0.2f;
        S.cam_dist = 3.0f;
        S.pending_model_tab_capture = true;
        S.pending_preview_build = true;
    }
}

static void load_entity_preview(int entity_index) {
    if (entity_index < 0 ||
        entity_index >= static_cast<int>(g_global_entity_catalog.size())) {
        return;
    }
    const auto& entity = g_global_entity_catalog[entity_index];
    if (entity.model_hashes.empty()) {
        OutputLog::warn("entity preview: no model assets resolve for '" +
                        (entity.display_name.empty() ? entity.name
                                                     : entity.display_name) +
                        "'");
        return;
    }

    const std::uint64_t request = ++g_entity_preview_request;
    g_mp.has_model = false;
    S.mdl_info_ok = false;
    S.mdl_meshes.clear();
    progress_open(0, "Loading full entity model...");
    std::thread([request, entity_index,
                 model_hashes = entity.model_hashes]() mutable {
        EntityPreviewCompletion result;
        result.request = request;
        result.entity_index = entity_index;
        EntityModels::ResolvedModel resolved;
        std::string error;
        if (EntityModels::Resolve(model_hashes, resolved, &error)) {
            result.model_info = std::move(resolved.info);
            result.meshes = std::move(resolved.meshes);
            result.primary_model_path =
                std::move(resolved.primary_model_path);
            result.primary_model_hash = resolved.primary_model_hash;
        }

        {
            std::lock_guard<std::mutex> lock(g_entity_preview_mutex);
            g_entity_preview_completions.push_back(std::move(result));
        }
        progress_done();
    }).detach();
}

bool select_entity_by_query(const std::string& query) {
    std::string needle = query;
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });

    int best = -1;
    for (int i = 0; i < static_cast<int>(g_global_entity_catalog.size()); ++i) {
        const auto& entity = g_global_entity_catalog[static_cast<size_t>(i)];
        std::string name = entity.name;
        std::string display = entity.display_name;
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        std::transform(display.begin(), display.end(), display.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (name == needle || display == needle) {
            best = i;
            break;
        }
        if (best < 0 &&
            (name.find(needle) != std::string::npos ||
             display.find(needle) != std::string::npos)) {
            best = i;
        }
    }
    if (best < 0) return false;

    const auto& entity = g_global_entity_catalog[static_cast<size_t>(best)];
    const std::string& label = entity.display_name.empty()
        ? entity.name : entity.display_name;
    ContentTabs::OpenEntity(best, label);
    load_entity_preview(best);
    return true;
}

void load_flat_asset_entry(const FlatAssetEntry& e, int kind) {
    if (S.selected_bnk != e.bnk_path) {
        S.viewing_adb = false;
        S.global_search.clear();
        S.selected_nested_bnk.clear();
        S.selected_nested_index = -1;
        pick_bnk(e.bnk_path);
    }

    if (e.from_nested) {
        S.selected_nested_temp_path = e.bnk_path;
        S.selected_nested_index = 0;
    }
    for (size_t i = 0; i < S.files.size(); ++i) {
        if (S.files[i].index == e.file_index) {
            S.selected_file_index = (int)i;
            if (kind == 0) {
                S.show_gdb_render = false;
                g_pending_mdl_full_path = e.full_path;
                g_pending_mdl_load = true;
                g_pending_mdl_index = (int)i;
            } else if (kind == 1) {
                S.show_gdb_render = false;
                g_pending_tex_load = true;
                g_pending_tex_index = (int)i;
            } else if (kind == 2) {

                S.show_gdb_render = false;
                open_audio_player_for_selected((int)i);
            }
            break;
        }
    }
}
