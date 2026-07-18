    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    int w = std::max(1, (int)region.x);
    int h = std::max(1, (int)region.y);

    if (::g_selected_level_mesh_idx >= (int)g_mp.meshes.size() ||
        !g_mp.has_model || !g_mp.no_tilt) {
        ::g_selected_level_mesh_idx = -1;
        ::g_selected_level_pick_id = 0;
        ::g_selected_level_hash = 0;
    }
    if (::g_selected_level_mesh_idx >= 0 &&
        is_adjacent_terrain_mesh_name(
            g_mp.meshes[(size_t)::g_selected_level_mesh_idx].name))
    {
        ::g_selected_level_mesh_idx = -1;
        ::g_selected_level_pick_id = 0;
        ::g_selected_level_hash = 0;
    }

    {
        LevelEdit::CollectPreviewXforms(g_mp.range_edit_xforms);
        for (size_t i = 0; i < g_mp.meshes.size(); ++i) {
            auto& mm = g_mp.meshes[i];
            mm.edit_xform = LevelEdit::EditXform{};
            auto it = g_mp.range_edit_xforms.find(
                0x80000000u | (uint32_t)i);
            if (it != g_mp.range_edit_xforms.end()) {
                mm.edit_xform = it->second;
            }
        }
        float water_offset[3] = {};
        if (DetailsPanel::WaterPreviewOffset(water_offset)) {
            for (auto& mm : g_mp.meshes) {
                if (!mm.is_water) continue;
                mm.edit_xform.off[0] += water_offset[0];
                mm.edit_xform.off[1] += water_offset[1];
                mm.edit_xform.off[2] += water_offset[2];
            }
        }
    }

    if (!g_mp_initialized) {
        MP_Init(device, g_mp, w, h);
        g_mp_initialized = true;
    }
    MP_Resize(device, g_mp, w, h);

    ImGui::InvisibleButton("##model_render", region);
    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();
    const bool player_start_place_active =
        g_player_start_placement != std::numeric_limits<size_t>::max();
