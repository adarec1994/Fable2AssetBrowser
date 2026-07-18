bool select_level_entity_model(unsigned int entity_hash) {
    if (!entity_hash) return false;
    for (size_t mesh_index = 0; mesh_index < g_mp.meshes.size();
         ++mesh_index) {
        const MPPerMesh& mesh = g_mp.meshes[mesh_index];
        if (!mesh.is_entity_model) continue;
        for (const auto& range : mesh.pick_ranges) {
            if (range.gdb_entity_hash != entity_hash) continue;
            ::g_selected_level_mesh_idx = static_cast<int>(mesh_index);
            ::g_selected_level_pick_id = range.selection_id;
            ::g_selected_level_hash = range.inst_hash;
            g_sel_spawn_marker = -1;
            g_sel_pending_sp = -1;
            g_sel_pending_gen = -1;
            g_marker_clear_selection = false;
            return true;
        }
    }
    return false;
}

bool select_level_marker(std::size_t marker_index) {
    if (marker_index >= g_level_spawn_markers.size()) return false;
    if ((g_level_spawn_markers[marker_index].kind == 2 ||
         g_level_spawn_markers[marker_index].kind == 3 ||
         g_level_spawn_markers[marker_index].kind == 6) &&
        !g_level_spawn_markers[marker_index].model_hashes.empty()) {
        const unsigned int entity_hash =
            g_level_spawn_markers[marker_index].entity_hash;
        if (entity_hash && select_level_entity_model(entity_hash)) {
            return true;
        }
        const uint32_t selection_id =
            0x70000000u | uint32_t(marker_index);
        for (size_t mesh_index = 0; mesh_index < g_mp.meshes.size();
             ++mesh_index) {
            const MPPerMesh& mesh = g_mp.meshes[mesh_index];
            if (!mesh.is_entity_model) continue;
            for (const auto& range : mesh.pick_ranges) {
                if (range.selection_id != selection_id) continue;
                ::g_selected_level_mesh_idx = int(mesh_index);
                ::g_selected_level_pick_id = selection_id;
                ::g_selected_level_hash = range.inst_hash;
                g_sel_spawn_marker = -1;
                g_sel_pending_sp = -1;
                g_sel_pending_gen = -1;
                g_marker_clear_selection = false;
                return true;
            }
        }
    }
    g_sel_spawn_marker = static_cast<int>(marker_index);
    g_sel_pending_sp = -1;
    g_sel_pending_gen = -1;
    g_marker_clear_selection = true;
    return true;
}

void request_level_add_menu_at(const float engine_pos[3]) {
    if (!engine_pos) return;
    for (int i = 0; i < 3; ++i) {
        g_add_menu_requested_pos[i] = engine_pos[i];
    }
    g_add_menu_requested = true;
}

void request_player_start_placement(std::size_t marker_index) {
    if (marker_index >= g_level_spawn_markers.size() ||
        !is_player_start_marker(g_level_spawn_markers[marker_index])) {
        return;
    }
    if (g_player_start_placement == marker_index) {
        g_player_start_placement = std::numeric_limits<size_t>::max();
    } else {
        g_player_start_placement = marker_index;
        select_level_marker(marker_index);
    }
}

bool player_start_placement_pending(std::size_t marker_index) {
    return g_player_start_placement == marker_index;
}

void draw_render_panel(ID3D11Device* device) {

    if (S.show_gdb_render) {
        draw_gdb_in_panel();
    } else if (S.content_tabs_visible && ContentTabs::HasTabs()) {
        ContentTabs::DrawTabBar();
        const ContentTabs::Kind kind = ContentTabs::ActiveKind();
        if (kind == ContentTabs::Kind::Lua ||
            kind == ContentTabs::Kind::Quest ||
            kind == ContentTabs::Kind::CustomQuest) {
            draw_lua_in_panel();
        } else if (kind == ContentTabs::Kind::Level) {
            const FlatAssetEntry* level_entry =
                ContentTabs::ActiveLevelEntry();
            const bool landscape_panel =
                level_entry && LandscapePanel::AppliesTo(*level_entry);
            if (landscape_panel) {
                LandscapePanel::DrawSidePanel(*level_entry, device);
                ImGui::SameLine();
                ImGui::BeginChild("##level_view_area", ImVec2(0, 0), false,
                                  ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse);
            }
            if (g_mp.has_model && S.terrain_mode && g_mp.no_tilt) {
                draw_model_in_panel(device);
            } else {
                draw_placeholder();
            }
            if (landscape_panel) ImGui::EndChild();
        } else if (kind == ContentTabs::Kind::Model) {
            if (ContentTabs::ActiveHasModel() && g_mp.has_model &&
                !S.terrain_mode && !g_mp.no_tilt) {
                draw_model_in_panel(device);
            } else {
                draw_placeholder();
            }
        } else if (kind == ContentTabs::Kind::Item) {
            if (ContentTabs::ActiveHasModel() && g_mp.has_model &&
                !S.terrain_mode && !g_mp.no_tilt) {
                draw_model_in_panel(device);
            } else {
                draw_item_tab_content();
            }
        } else if (kind == ContentTabs::Kind::Entity) {
            if (ContentTabs::ActiveHasModel() && g_mp.has_model &&
                !S.terrain_mode && !g_mp.no_tilt) {
                draw_model_in_panel(device);
            } else {
                draw_entity_tab_content();
            }
        } else {
            draw_placeholder();
        }
    } else if (g_mp.has_model) {
        draw_model_in_panel(device);
    } else if (S.texture_window_srv) {
        draw_texture_in_panel(device);
    } else if (S.show_lua_render) {
        draw_lua_in_panel();
    } else {
        draw_placeholder();
    }

    draw_heightmap_popout();
}
