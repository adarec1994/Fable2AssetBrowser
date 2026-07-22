void draw_render_panel() {
    if (S.show_gdb_render) {
        draw_gdb_in_panel();
    } else if (S.content_tabs_visible && ContentTabs::HasTabs()) {
        ContentTabs::DrawTabBar();
        const ContentTabs::Kind kind = ContentTabs::ActiveKind();
        if (kind == ContentTabs::Kind::Lua ||
            kind == ContentTabs::Kind::Quest ||
            kind == ContentTabs::Kind::CustomQuest) {
            draw_lua_in_panel();
        } else if (kind == ContentTabs::Kind::VfsConfig) {
            VfsConfigViewer::Draw(ContentTabs::ActiveVfsConfigContent());
        } else if (kind == ContentTabs::Kind::Level) {
            const FlatAssetEntry* level_entry =
                ContentTabs::ActiveLevelEntry();
            const bool landscape_panel =
                level_entry && LandscapePanel::AppliesTo(*level_entry);
            if (landscape_panel) {
                LandscapePanel::DrawSidePanel(*level_entry);
                ImGui::SameLine();
                ImGui::BeginChild("##level_view_area", ImVec2(0, 0), false,
                                  ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse);
            }
            if (g_mp.has_model && S.terrain_mode && g_mp.no_tilt) {
                draw_model_in_panel_gl();
            } else {
                draw_placeholder();
            }
            if (landscape_panel) ImGui::EndChild();
        } else if (kind == ContentTabs::Kind::Model ||
                   kind == ContentTabs::Kind::Hero) {
            if (ContentTabs::ActiveHasModel() && g_mp.has_model &&
                !S.terrain_mode && !g_mp.no_tilt) {
                draw_model_in_panel_gl();
            } else {
                draw_placeholder();
            }
        } else if (kind == ContentTabs::Kind::Item) {
            if (ContentTabs::ActiveHasModel() && g_mp.has_model &&
                !S.terrain_mode && !g_mp.no_tilt) {
                draw_model_in_panel_gl();
            } else {
                draw_item_tab_content();
            }
        } else if (kind == ContentTabs::Kind::Entity) {
            if (ContentTabs::ActiveHasModel() && g_mp.has_model &&
                !S.terrain_mode && !g_mp.no_tilt) {
                draw_model_in_panel_gl();
            } else {
                draw_entity_tab_content();
            }
        } else {
            draw_placeholder();
        }
    } else if (g_mp.has_model) {
        draw_model_in_panel_gl();
    } else if (S.texture_window_gl) {
        draw_texture_in_panel_gl();
    } else if (S.show_lua_render) {
        draw_lua_in_panel();
    } else {
        draw_placeholder();
    }
    draw_texture_popout_gl();
}
