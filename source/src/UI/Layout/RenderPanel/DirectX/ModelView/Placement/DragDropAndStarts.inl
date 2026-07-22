    if (g_mp.no_tilt && LevelEdit::Enabled() &&
        ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pay =
                ImGui::AcceptDragDropPayload("F2_MODEL")) {
            const std::string drop_model(
                (const char*)pay->Data, (size_t)pay->DataSize);
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            float engine_pos[3] = {};
            if (level_placement_surface_at(mouse, origin, region, false,
                                           engine_pos)) {
                spawn_level_model_at(device, drop_model, engine_pos);
            }
        }
        if (const ImGuiPayload* pay =
                ImGui::AcceptDragDropPayload("F2_START_PIN")) {
            size_t marker_index = std::numeric_limits<size_t>::max();
            if (pay->DataSize == (int)sizeof(size_t)) {
                std::memcpy(&marker_index, pay->Data, sizeof(size_t));
            }
            float target[3] = {};
            if (marker_index < g_level_spawn_markers.size() &&
                is_player_start_marker(
                    g_level_spawn_markers[marker_index]) &&
                level_placement_surface_at(ImGui::GetIO().MousePos,
                                           origin, region, false,
                                           target)) {
                LevelSpawnMarker& marker =
                    g_level_spawn_markers[marker_index];
                if (!marker.pos_off[0] && !marker.pos_off[1] &&
                    !marker.pos_off[2]) {
                    OutputLog::error(
                        "player start: this point has no editable "
                        "transform");
                } else {
                    const uint32_t edit_id =
                        0x70000000u | uint32_t(marker_index);
                    LevelEdit::ClearDeleted(edit_id);
                    float current[3] = {marker.x, marker.y, marker.z};
                    float delta_pos[3] = {};
                    float delta_rot[3] = {};
                    if (LevelEdit::EditFor(edit_id, delta_pos,
                                           delta_rot)) {
                        for (int axis = 0; axis < 3; ++axis) {
                            current[axis] += delta_pos[axis];
                        }
                    }
                    const float step[3] = {target[0] - current[0],
                                           target[1] - current[1],
                                           target[2] - current[2]};
                    LevelEdit::InstInfo info;
                    const float original[3] = {marker.x, marker.y,
                                               marker.z};
                    info.orig_pos = original;
                    info.gdb_off = marker.pos_off;
                    info.gdb_rot_off = marker.rot_off;
                    info.gdb_entity_hash = marker.entity_hash;
                    LevelEdit::PushUndoSnapshot({edit_id});
                    LevelEdit::AddMove(edit_id, step, info);
                    {
                        char dbg[240];
                        std::snprintf(
                            dbg, sizeof(dbg),
                            "pin drop '%s' id=0x%08X target=(%.2f,%.2f,"
                            "%.2f) orig=(%.2f,%.2f,%.2f) step=(%.2f,"
                            "%.2f,%.2f) pos_off=(%u,%u,%u) hash=0x%08X",
                            marker.name.c_str(), edit_id, target[0],
                            target[1], target[2], original[0],
                            original[1], original[2], step[0], step[1],
                            step[2], marker.pos_off[0],
                            marker.pos_off[1], marker.pos_off[2],
                            marker.entity_hash);
                        DebugLog::Write("pin", dbg);
                    }
                    OutputLog::success(
                        marker.name +
                        " placed; Save writes it to the level");
                    UI::select_level_marker(marker_index);
                }
            }
        }
        if (const ImGuiPayload* pay =
                ImGui::AcceptDragDropPayload("F2_ENTITY_NPC")) {
            int catalog_index = -1;
            if (pay->DataSize == (int)sizeof(int)) {
                std::memcpy(&catalog_index, pay->Data, sizeof(int));
            }
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            float engine_pos[3] = {};
            if (catalog_index >= 0 &&
                catalog_index < (int)g_global_entity_catalog.size() &&
                level_placement_surface_at(mouse, origin, region, false,
                                           engine_pos)) {
                const Gdb::CreatureCatalogEntry& entry =
                    g_global_entity_catalog[(size_t)catalog_index];
                LevelEdit::NpcPlacementInfo info;
                static int s_placed_serial = 0;
                info.instance_name =
                    entry.name + "_placed" +
                    std::to_string(++s_placed_serial);
                info.creature_name = entry.name;
                info.creature_entity = entry.entity_hash;
                info.transform_component_field =
                    entry.transform_component_field
                        ? entry.transform_component_field
                        : g_level_npc_donor.transform_field;
                info.transform_component_template =
                    entry.transform_component_template
                        ? entry.transform_component_template
                        : g_level_npc_donor.transform_parent;
                info.position_template =
                    entry.position_template
                        ? entry.position_template
                        : g_level_npc_donor.position_parent;
                info.rotation_template =
                    entry.rotation_template
                        ? entry.rotation_template
                        : g_level_npc_donor.rotation_parent;
                for (uint32_t model_hash : entry.model_hashes) {
                    const FlatAssetEntry* model =
                        FindGlobalModelAssetByPathHash(model_hash);
                    if (model) {
                        info.asset_models.push_back(model->full_path);
                    }
                }
                const int addition =
                    LevelEdit::AddNpcPlacement(engine_pos, info);
                if (addition >= 0) {
                    LevelSpawnMarker marker;
                    marker.x = engine_pos[0];
                    marker.y = engine_pos[1];
                    marker.z = engine_pos[2];
                    marker.kind = 3;
                    marker.pending_addition_index = addition;
                    marker.name = info.instance_name;
                    marker.creature_name = entry.name;
                    marker.creature_entity_hash = entry.entity_hash;
                    marker.model_hashes = entry.model_hashes;
                    g_level_spawn_markers.push_back(marker);
                    const size_t marker_index =
                        g_level_spawn_markers.size() - 1;
                    append_level_entity_model_at(
                        device, entry.model_hashes, marker_index,
                        engine_pos);
                    UI::select_level_marker(marker_index);
                    S.show_ent_npcs = true;
                    S.show_entity_models = true;
                    OutputLog::success("placed " + entry.name +
                                       " in the level");
                } else {
                    OutputLog::error(
                        "could not place " + entry.name +
                        " (is level editing enabled?)");
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

