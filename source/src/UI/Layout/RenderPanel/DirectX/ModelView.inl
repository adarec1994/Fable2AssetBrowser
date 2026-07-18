void draw_model_in_panel(ID3D11Device* device) {
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
    if (::g_selected_level_mesh_idx >= 0 && !S.dev_mode &&
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

#ifdef _WIN32
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
                DebugTrace::log(
                    "drop: '%s' at (%.2f, %.2f, %.2f)",
                    drop_model.c_str(), engine_pos[0],
                    engine_pos[1], engine_pos[2]);
                spawn_level_model_at(device, drop_model, engine_pos);
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

    if (player_start_place_active) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            g_player_start_placement = std::numeric_limits<size_t>::max();
        } else if (g_player_start_placement >=
                       g_level_spawn_markers.size() ||
                   !is_player_start_marker(
                       g_level_spawn_markers[g_player_start_placement])) {
            g_player_start_placement = std::numeric_limits<size_t>::max();
        } else if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("Click the terrain to place this point\nEsc cancels");
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false)) {
                const size_t marker_index = g_player_start_placement;
                LevelSpawnMarker& marker =
                    g_level_spawn_markers[marker_index];
                float target[3] = {};
                if (!LevelEdit::Enabled() || LevelEdit::Saving()) {
                    OutputLog::error(
                        "player start: enable level editing before placing");
                } else if (!marker.pos_off[0] && !marker.pos_off[1] &&
                           !marker.pos_off[2]) {
                    OutputLog::error(
                        "player start: this point has no editable transform");
                } else if (!level_placement_surface_at(
                               ImGui::GetIO().MousePos, origin, region,
                               false, target)) {
                    OutputLog::error(
                        "player start: click a visible terrain surface");
                } else {
                    const uint32_t edit_id =
                        0x70000000u | uint32_t(marker_index);
                    float current[3] = {marker.x, marker.y, marker.z};
                    float delta_pos[3] = {};
                    float delta_rot[3] = {};
                    if (LevelEdit::EditFor(edit_id, delta_pos, delta_rot)) {
                        for (int axis = 0; axis < 3; ++axis) {
                            current[axis] += delta_pos[axis];
                        }
                    }
                    const float step[3] = {target[0] - current[0],
                                           target[1] - current[1],
                                           target[2] - current[2]};
                    LevelEdit::InstInfo info;
                    const float original[3] = {marker.x, marker.y, marker.z};
                    info.orig_pos = original;
                    info.gdb_off = marker.pos_off;
                    info.gdb_rot_off = marker.rot_off;
                    info.gdb_entity_hash = marker.entity_hash;
                    LevelEdit::PushUndoSnapshot({edit_id});
                    LevelEdit::AddMove(edit_id, step, info);
                    OutputLog::success(marker.name +
                                       " placed; Save writes it to the level");
                    g_player_start_placement =
                        std::numeric_limits<size_t>::max();
                    UI::select_level_marker(marker_index);
                }
            }
        }
    }

    static bool  s_gen_popup = false;
    static bool  s_gen_click_pending = false;
    static bool  s_gen_on_water = false;
    static float s_gen_pos[3] = {0, 0, 0};
    static ImVec2 s_gen_click_mouse{};
    static char  s_add_filter[128] = {};
    if (g_add_menu_requested) {
        for (int i = 0; i < 3; ++i) {
            s_gen_pos[i] = g_add_menu_requested_pos[i];
        }
        g_add_menu_requested = false;
        s_gen_on_water = false;
        s_gen_popup = true;
    }
    if (g_mp.no_tilt && LevelEdit::Enabled() && !LevelEdit::Saving() &&
        hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        s_gen_click_pending = false;
        const ImVec2 mouse2 = ImGui::GetIO().MousePos;
        s_gen_click_mouse = mouse2;
        float engine_pos[3] = {};
        const bool land_hit = level_placement_surface_at(
            mouse2, origin, region, true, engine_pos);
        float water_pos[3] = {};
        const bool water_hit =
            level_water_surface_at(mouse2, origin, region, water_pos);
        
        
        if (water_hit &&
            (!land_hit || water_pos[2] > engine_pos[2] + 0.01f)) {
            s_gen_pos[0] = water_pos[0];
            s_gen_pos[1] = water_pos[1];
            s_gen_pos[2] = water_pos[2];
            s_gen_on_water = true;
            s_gen_click_pending = true;
        } else if (land_hit) {
            s_gen_pos[0] = engine_pos[0];
            s_gen_pos[1] = engine_pos[1];
            s_gen_pos[2] = engine_pos[2];
            s_gen_on_water = false;
            s_gen_click_pending = true;
        }
    }
    if (s_gen_click_pending &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        const ImVec2 released_at = ImGui::GetIO().MousePos;
        const float release_dx = released_at.x - s_gen_click_mouse.x;
        const float release_dy = released_at.y - s_gen_click_mouse.y;
        const float release_distance = std::sqrt(
            release_dx * release_dx + release_dy * release_dy);
        if (std::max(g_flycam.right_drag_distance,
                     release_distance) < 4.0f) {
            uint32_t picked_id = 0;
            uint64_t picked_hash = 0;
            const int picked = pick_level_mesh_at(
                released_at, origin, region, &picked_id, &picked_hash);
            if (picked >= 0) {
                ::g_selected_level_mesh_idx = picked;
                ::g_selected_level_pick_id = picked_id;
                ::g_selected_level_hash = picked_hash;
                if (g_mp.meshes[size_t(picked)].is_entity_model) {
                    g_sel_spawn_marker = -1;
                    g_sel_pending_sp = -1;
                    g_sel_pending_gen = -1;
                }
            }
            s_gen_popup = true;
        }
        s_gen_click_pending = false;
    }
    if (s_gen_popup) {
        ImGui::OpenPopup("Add to level");
        s_gen_popup = false;
        s_add_filter[0] = 0;
    }
    const ImGuiIO& add_io = ImGui::GetIO();
    ImGui::SetNextWindowSize(
        ImVec2(std::min(500.0f, std::max(300.0f, add_io.DisplaySize.x - 32.0f)),
               std::min(620.0f, std::max(280.0f, add_io.DisplaySize.y - 32.0f))),
        ImGuiCond_Appearing);
    if (ImGui::BeginPopup("Add to level")) {
        ImGui::TextDisabled("(%.1f, %.1f, %.1f)", s_gen_pos[0],
                            s_gen_pos[1], s_gen_pos[2]);
        auto place_generator = [&](const std::string& creature) {
            std::vector<std::string> assets;
            uint32_t creature_entity = 0;
            for (const auto& ce : g_level_creature_catalog) {
                if (ce.name != creature) continue;
                creature_entity = ce.entity_hash;
                for (uint32_t mh : ce.model_hashes) {
                    for (const auto& mf : S.all_mdl_files) {
                        std::string lp = mf.full_path;
                        std::transform(lp.begin(), lp.end(), lp.begin(),
                                       ::tolower);
                        std::replace(lp.begin(), lp.end(), '/', '\\');
                        uint32_t h = 0x811C9DC5u;
                        for (unsigned char c : lp) {
                            h *= 0x01000193u;
                            h ^= uint32_t(c);
                        }
                        if (h == mh) {
                            assets.push_back(mf.full_path);
                            break;
                        }
                    }
                }
                break;
            }
            const int gi =
                LevelEdit::AddGenerator(s_gen_pos, creature,
                                        creature_entity, assets);
            if (gi >= 0) {
                OutputLog::success(
                    "level edit: generator for '" + creature +
                    "' queued (" + std::to_string(assets.size()) +
                    " creature model(s) will stream into this level "
                    "on Save)");
            }
        };
        auto place_static_entity = [&](size_t catalog_index) {
            if (catalog_index >= g_global_entity_catalog.size()) return;
            const Gdb::CreatureCatalogEntry& entity =
                g_global_entity_catalog[catalog_index];
            if (entity.kind != Gdb::EntityCatalogKind::StaticProp ||
                entity.model_hashes.empty()) {
                return;
            }
            const FlatAssetEntry* model =
                FindGlobalModelAssetByPathHash(entity.model_hashes.front());
            if (!model) {
                OutputLog::error(
                    "level edit: static prop model is not available");
                return;
            }
            const int addition =
                LevelEdit::AddPlacement(model->full_path, s_gen_pos);
            if (addition < 0) {
                OutputLog::error(
                    "level edit: static prop placement rejected");
                return;
            }
            LevelEdit::StaticPropPlacementInfo info;
            info.instance_name =
                unique_static_prop_instance_name(entity);
            info.entity_template = entity.entity_hash;
            info.transform_component_field =
                entity.transform_component_field;
            info.transform_component_template =
                entity.transform_component_template;
            info.position_template = entity.position_template;
            info.rotation_template = entity.rotation_template;
            LevelEdit::MarkAdditionAsStaticProp(addition, info);

            LevelSpawnMarker marker;
            marker.x = s_gen_pos[0];
            marker.y = s_gen_pos[1];
            marker.z = s_gen_pos[2];
            marker.kind = 6;
            marker.pending_addition_index = addition;
            marker.name = info.instance_name;
            marker.model_hashes = entity.model_hashes;
            g_level_spawn_markers.push_back(std::move(marker));
            const size_t marker_index =
                g_level_spawn_markers.size() - 1;
            if (!append_level_entity_model_at(
                    device, entity.model_hashes, marker_index,
                    s_gen_pos)) {
                OutputLog::warn(
                    "level edit: static prop was queued, but its preview "
                    "model could not be drawn");
            }
            UI::select_level_marker(marker_index);
            S.show_entity_models = true;
            OutputLog::success(
                "level edit: static prop '" + info.instance_name +
                "' queued as a named, behaviour-free entity");
        };

        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##add_to_level_search",
                                 "Search actors, objects, containers...",
                                 s_add_filter, sizeof(s_add_filter));

        std::string filter = s_add_filter;
        std::transform(filter.begin(), filter.end(), filter.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        auto matches_filter = [&](const std::string& value) {
            if (filter.empty()) return true;
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) {
                               return (char)std::tolower(c);
                           });
            return lower.find(filter) != std::string::npos;
        };

        std::vector<std::string> actor_names;
        std::unordered_set<std::string> seen_actor_names;
        auto add_actor = [&](const std::string& name) {
            if (name.empty() || !seen_actor_names.insert(name).second ||
                !matches_filter(name)) return;
            actor_names.push_back(name);
        };
        for (const auto& creature : g_level_creature_catalog) {
            add_actor(creature.name);
        }
        for (const auto& marker : g_level_spawn_markers) {
            if (marker.kind != 1 && marker.kind != 3) continue;
            add_actor(marker.kind == 1 ? marker.creature_name : marker.name);
        }

        std::vector<size_t> static_entity_indices;
        for (size_t i = 0; i < g_global_entity_catalog.size(); ++i) {
            const Gdb::CreatureCatalogEntry& entity =
                g_global_entity_catalog[i];
            if (entity.kind != Gdb::EntityCatalogKind::StaticProp) continue;
            std::string searchable = entity.display_name + " " +
                                     entity.name;
            if (!entity.model_hashes.empty()) {
                const FlatAssetEntry* model =
                    FindGlobalModelAssetByPathHash(
                        entity.model_hashes.front());
                if (model) searchable += " " + model->full_path;
            }
            if (matches_filter(searchable)) {
                static_entity_indices.push_back(i);
            }
        }

        std::vector<size_t> object_indices;
        object_indices.reserve(S.all_mdl_files.size());
        for (size_t i = 0; i < S.all_mdl_files.size(); ++i) {
            const auto& model = S.all_mdl_files[i];
            if (matches_filter(model.full_path) ||
                matches_filter(model.name)) {
                object_indices.push_back(i);
            }
        }

        const auto container_choices = build_container_spawn_choices();
        std::vector<size_t> container_indices;
        std::vector<size_t> dig_indices;
        std::vector<size_t> dive_indices;
        for (size_t i = 0; i < container_choices.size(); ++i) {
            if (!matches_filter(container_choices[i].label)) continue;
            if (container_choices[i].is_dive) {
                dive_indices.push_back(i);
            } else if (container_choices[i].info.is_dig_spot) {
                dig_indices.push_back(i);
            } else {
                container_indices.push_back(i);
            }
        }
        
        
        if (s_gen_on_water) {
            actor_names.clear();
            object_indices.clear();
            container_indices.clear();
            dig_indices.clear();
        } else {
            dive_indices.clear();
        }

        ImGui::Separator();
        ImGui::BeginChild("##add_to_level_flat_list", ImVec2(0.0f, 0.0f),
                          false, ImGuiWindowFlags_HorizontalScrollbar);
        bool any_result = false;
        const ImVec4 heading_colour(0.55f, 0.75f, 1.0f, 1.0f);

        auto draw_heading = [&](const char* title, size_t count) {
            ImGui::TextColored(heading_colour, "%s  (%zu)", title, count);
        };
        QuestUI::NpcCreationRequest npc_request;
        if (!s_gen_on_water &&
            QuestUI::GetPendingNpcCreation(npc_request)) {
            any_result = true;
            draw_heading("QUEST NPC", 1);
            const bool can_author_npc = g_level_npc_donor.valid();
            ImGui::BeginDisabled(!can_author_npc);
            const std::string action =
                "Create " + npc_request.display_name + " here";
            if (ImGui::Selectable(action.c_str())) {
                LevelEdit::NpcPlacementInfo info;
                info.instance_name = npc_request.instance_name;
                info.creature_name = npc_request.creature_name;
                info.creature_entity = npc_request.creature_entity;
                info.transform_component_field =
                    g_level_npc_donor.transform_field;
                info.transform_component_template =
                    g_level_npc_donor.transform_parent;
                info.position_template =
                    g_level_npc_donor.position_parent;
                info.rotation_template =
                    g_level_npc_donor.rotation_parent;
                for (uint32_t model_hash : npc_request.model_hashes) {
                    const FlatAssetEntry* model =
                        FindGlobalModelAssetByPathHash(model_hash);
                    if (model) info.asset_models.push_back(model->full_path);
                }
                const int addition =
                    LevelEdit::AddNpcPlacement(s_gen_pos, info);
                if (addition >= 0) {
                    LevelSpawnMarker marker;
                    marker.x = s_gen_pos[0];
                    marker.y = s_gen_pos[1];
                    marker.z = s_gen_pos[2];
                    marker.kind = 3;
                    marker.pending_addition_index = addition;
                    marker.name = npc_request.instance_name;
                    marker.creature_name = npc_request.creature_name;
                    marker.creature_entity_hash =
                        npc_request.creature_entity;
                    marker.model_hashes = npc_request.model_hashes;
                    g_level_spawn_markers.push_back(marker);
                    const size_t marker_index =
                        g_level_spawn_markers.size() - 1;
                    append_level_entity_model_at(
                        device, npc_request.model_hashes, marker_index,
                        s_gen_pos);
                    UI::select_level_marker(marker_index);
                    S.show_ent_npcs = true;
                    S.show_entity_models = true;

                    QuestUI::LevelReferenceCandidate candidate;
                    candidate.is_npc = true;
                    candidate.authored_instance = true;
                    candidate.level_path =
                        g_pending_terrain_level_entry.full_path;
                    candidate.level_id = quest_level_id_from_path(
                        candidate.level_path);
                    candidate.entity_name = npc_request.instance_name;
                    candidate.x = s_gen_pos[0];
                    candidate.y = s_gen_pos[1];
                    candidate.z = s_gen_pos[2];
                    candidate.model_hashes = npc_request.model_hashes;
                    std::string error;
                    if (QuestUI::BindCreatedNpcInstance(candidate, error)) {
                        OutputLog::success(
                            "quest NPC: created '" +
                            npc_request.instance_name + "' in " +
                            candidate.level_id +
                            "; Save Level will inject it");
                        ImGui::CloseCurrentPopup();
                    } else {
                        OutputLog::error("quest NPC: " + error);
                    }
                } else {
                    OutputLog::error(
                        "quest NPC: level rejected the NPC placement");
                }
            }
            ImGui::EndDisabled();
            if (!can_author_npc) {
                ImGui::TextDisabled(
                    "No placed-NPC transform schema is available yet; "
                    "load a level containing an NPC first.");
            }
            ImGui::Separator();
        }
        const int quest_reference_marker =
            selected_level_spawn_marker_index();
        if (QuestUI::IsAuthoredQuestActive() &&
            quest_reference_marker >= 0) {
            const LevelSpawnMarker& marker =
                g_level_spawn_markers[static_cast<std::size_t>(
                    quest_reference_marker)];
            const QuestUI::LevelReferenceTarget target =
                QuestUI::PendingLevelReferenceTarget();
            
            const bool compatible =
                BlueprintUI::PendingPickPin() != 0 ||
                (target == QuestUI::LevelReferenceTarget::QuestGiver &&
                 marker.kind == 3) ||
                (target != QuestUI::LevelReferenceTarget::QuestGiver &&
                 marker.is_container);
            const std::string action =
                "Assign selected " + QuestUI::PendingLevelReferenceLabel();
            if (compatible && matches_filter(action)) {
                any_result = true;
                draw_heading("QUEST REFERENCES", 1);
                const bool existing_entity = marker.entity_hash != 0 &&
                                             marker.pending_addition_index < 0;
                const bool authored_entity =
                    marker.pending_addition_index >= 0 &&
                    LevelEdit::AdditionIsNamedEntity(
                        marker.pending_addition_index);
                ImGui::BeginDisabled(
                    !existing_entity && !authored_entity);
                if (ImGui::Selectable(action.c_str())) {
                    QuestUI::LevelReferenceCandidate candidate;
                    std::string error;
                    if (!build_quest_level_reference(
                            marker, size_t(quest_reference_marker),
                            candidate)) {
                        error = "The selected entity has no usable GDB name.";
                    } else if (QuestUI::BindActiveLevelReference(candidate,
                                                                  error)) {
                        if (target !=
                            QuestUI::LevelReferenceTarget::QuestGiver) {
                            add_quest_item_to_container(
                                marker.entity_hash,
                                QuestUI::PendingLevelReferenceItemHash());
                        }
                        OutputLog::success(
                            "quest reference: " + action + " in " +
                            candidate.level_id);
                        ImGui::CloseCurrentPopup();
                    }
                    if (!error.empty()) {
                        OutputLog::error("quest reference: " + error);
                    }
                }
                ImGui::EndDisabled();
                if (!existing_entity && !authored_entity) {
                    ImGui::TextDisabled(
                        "This newly placed object is not a named entity.");
                }
            }
        }
        if (!static_entity_indices.empty()) {
            if (any_result) ImGui::Separator();
            any_result = true;
            draw_heading("STATIC ENTITIES",
                         static_entity_indices.size());
            ImGui::PushID("static_entities");
            for (size_t row = 0; row < static_entity_indices.size(); ++row) {
                const size_t catalog_index =
                    static_entity_indices[row];
                const Gdb::CreatureCatalogEntry& entity =
                    g_global_entity_catalog[catalog_index];
                const std::string& label = entity.display_name.empty()
                    ? entity.name : entity.display_name;
                ImGui::PushID(static_cast<int>(catalog_index));
                if (ImGui::Selectable(label.c_str())) {
                    place_static_entity(catalog_index);
                    ImGui::CloseCurrentPopup();
                }
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(entity.name.c_str());
                    if (!entity.model_hashes.empty()) {
                        const FlatAssetEntry* model =
                            FindGlobalModelAssetByPathHash(
                                entity.model_hashes.front());
                        if (model) {
                            ImGui::TextDisabled(
                                "%s", model->full_path.c_str());
                        }
                    }
                    ImGui::TextDisabled(
                        "Named static prop; no behaviours");
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        if (!actor_names.empty()) {
            any_result = true;
            draw_heading("ACTORS", actor_names.size());
            ImGui::PushID("actors");
            ImGuiListClipper clipper;
            clipper.Begin((int)actor_names.size());
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart;
                     i < clipper.DisplayEnd; ++i) {
                    ImGui::PushID(i);
                    if (ImGui::Selectable(actor_names[(size_t)i].c_str())) {
                        place_generator(actor_names[(size_t)i]);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                }
            }
            clipper.End();
            ImGui::PopID();
        }

        if (!object_indices.empty()) {
            if (any_result) ImGui::Separator();
            any_result = true;
            draw_heading("OBJECTS", object_indices.size());
            ImGui::PushID("objects");
            ImGuiListClipper clipper;
            clipper.Begin((int)object_indices.size());
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart;
                     row < clipper.DisplayEnd; ++row) {
                    const size_t model_index = object_indices[(size_t)row];
                    const auto& model = S.all_mdl_files[model_index];
                    const std::string label = clean_level_model_name(
                        model.name.empty() ? model.full_path : model.name);
                    ImGui::PushID((int)model_index);
                    if (ImGui::Selectable(label.c_str())) {
                        spawn_level_model_at(device, model.full_path,
                                             s_gen_pos);
                        ImGui::CloseCurrentPopup();
                    }
                    if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(model.full_path.c_str());
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();
                }
            }
            clipper.End();
            ImGui::PopID();
        }

        auto place_container_choice = [&](size_t choice_index) {
            const auto& choice = container_choices[choice_index];
            const int add_idx = spawn_level_container_at(
                device, choice.model_path, s_gen_pos, choice.info);
            if (add_idx >= 0) {
                if (choice.info.is_dig_spot) {
                    S.show_dig_spots = true;
                } else {
                    S.show_containers = true;
                }
                if (choice.model_path.empty()) {
                    LevelSpawnMarker marker;
                    marker.x = s_gen_pos[0];
                    marker.y = s_gen_pos[1];
                    marker.z = s_gen_pos[2];
                    marker.kind = choice.info.is_dig_spot ? 4 : 5;
                    marker.is_container = true;
                    marker.pending_addition_index = add_idx;
                    marker.name = choice.label;
                    g_level_spawn_markers.push_back(std::move(marker));
                    UI::select_level_marker(g_level_spawn_markers.size() - 1);
                }
            }
            ImGui::CloseCurrentPopup();
        };
        auto draw_container_section = [&](const char* title,
                                          const std::vector<size_t>& rows) {
            if (rows.empty()) return;
            if (any_result) ImGui::Separator();
            any_result = true;
            draw_heading(title, rows.size());
            ImGui::PushID(title);
            for (size_t row = 0; row < rows.size(); ++row) {
                const size_t choice_index = rows[row];
                ImGui::PushID((int)choice_index);
                if (ImGui::Selectable(
                        container_choices[choice_index].label.c_str())) {
                    place_container_choice(choice_index);
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        };
        draw_container_section("CONTAINERS", container_indices);
        draw_container_section("DIG SPOTS", dig_indices);
        draw_container_section("DIVE SPOTS", dive_indices);

        if (!any_result) {
            ImGui::TextDisabled("No matching placeable items.");
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
#endif

    bool skel_visible = ::g_skel_overlay_show && (g_mp.bone_count > 0);

    static float s_rot_snapshot[4]    = {0, 0, 0, 1};
    static int   s_rot_snapshot_bone  = -1;
    static bool  s_rot_snapshot_valid = false;

    auto cancel_rotate = [&]() {
        if (s_rot_snapshot_valid &&
            s_rot_snapshot_bone >= 0 &&
            s_rot_snapshot_bone < (int)g_mp.bone_count &&
            (size_t)s_rot_snapshot_bone * 4 + 4 <= S.bone_rot_deltas.size()) {
            for (int k = 0; k < 4; ++k) {
                S.bone_rot_deltas[(size_t)s_rot_snapshot_bone * 4 + (size_t)k]
                    = s_rot_snapshot[k];
            }
        }
        s_rot_snapshot_valid = false;
        S.bone_rotate_mode   = false;
    };
    auto confirm_rotate = [&]() {
        s_rot_snapshot_valid = false;
        S.bone_rotate_mode   = false;
    };

    bool rotate_active = (skel_visible && S.bone_rotate_mode &&
                          S.selected_bone >= 0 &&
                          S.selected_bone < (int)g_mp.bone_count);

    if (rotate_active) {

        if (hovered) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            if (d.x != 0.0f || d.y != 0.0f) {
                const float kRotSensitivity = 0.01f;
                float a_y = d.x * kRotSensitivity;
                float a_x = d.y * kRotSensitivity;

                using namespace DirectX;
                XMVECTOR qx = XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), a_x);
                XMVECTOR qy = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), a_y);
                XMVECTOR delta = XMQuaternionMultiply(qx, qy);

                int b = S.selected_bone;
                XMVECTOR cur = XMVectorSet(
                    S.bone_rot_deltas[(size_t)b * 4 + 0],
                    S.bone_rot_deltas[(size_t)b * 4 + 1],
                    S.bone_rot_deltas[(size_t)b * 4 + 2],
                    S.bone_rot_deltas[(size_t)b * 4 + 3]);

                XMVECTOR nxt = XMQuaternionNormalize(XMQuaternionMultiply(cur, delta));
                XMFLOAT4 nf;
                XMStoreFloat4(&nf, nxt);
                S.bone_rot_deltas[(size_t)b * 4 + 0] = nf.x;
                S.bone_rot_deltas[(size_t)b * 4 + 1] = nf.y;
                S.bone_rot_deltas[(size_t)b * 4 + 2] = nf.z;
                S.bone_rot_deltas[(size_t)b * 4 + 3] = nf.w;
            }
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            cancel_rotate();
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            confirm_rotate();
        }
    }

    if (skel_visible && !rotate_active && hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImVec2 mp = ImGui::GetIO().MousePos;
        int picked = pick_bone_at(mp, origin, region, 12.0f);
        S.selected_bone = picked;
    }

    
    
    const bool sculpt_click_owns_mouse =
        details_panel_docked() &&
        ((LandscapePanel::InSculptMode() && TerrainEdit::IsLoaded()) ||
         (LandscapePanel::InPaintMode() && TerrainPaint::Active()));
    if (g_mp.no_tilt && hovered && !rotate_active &&
        !player_start_place_active &&
        !sculpt_click_owns_mouse &&
        !LevelGizmo::WantsMouse() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f))
    {
        uint32_t picked_id = 0;
        uint64_t picked_hash = 0;
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        DebugTrace::log("pick: begin mouse=(%.0f,%.0f) meshes=%zu edit=%d",
                        mouse.x, mouse.y, g_mp.meshes.size(),
                        LevelEdit::Enabled() ? 1 : 0);
        const int picked = pick_level_mesh_at(mouse, origin, region,
                                              &picked_id, &picked_hash);
        DebugTrace::log("pick: done mesh=%d id=%u hash=%llu name='%s'",
                        picked, picked_id,
                        (unsigned long long)picked_hash,
                        picked >= 0
                            ? g_mp.meshes[(size_t)picked].name.c_str()
                            : "");
        ::g_selected_level_mesh_idx = picked;
        ::g_selected_level_pick_id = picked >= 0 ? picked_id : 0;
        ::g_selected_level_hash = picked >= 0 ? picked_hash : 0;
        if (picked >= 0) DetailsPanel::ClearSelection();
        if (picked >= 0 &&
            g_mp.meshes[static_cast<size_t>(picked)].is_entity_model) {


            g_sel_spawn_marker = -1;
            g_sel_pending_sp = -1;
            g_sel_pending_gen = -1;
        }
        if (picked < 0) LevelGizmo::CancelDrag();
    }

    if (S.terrain_mode) {
        const float dt = ImGui::GetIO().DeltaTime;
        if (hovered || g_flycam.is_looking ||
            g_flycam.right_press_pending) {
            ::render_panel_handle_flycam(dt);
        }
    } else {
        if (!rotate_active && active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            const float kOrbitSensitivity = 0.008f;
            S.cam_yaw   += d.x * kOrbitSensitivity;
            S.cam_pitch += d.y * kOrbitSensitivity;

            const float kPitchLimit = 1.5f;
            if (S.cam_pitch >  kPitchLimit) S.cam_pitch =  kPitchLimit;
            if (S.cam_pitch < -kPitchLimit) S.cam_pitch = -kPitchLimit;
        }

        if (hovered) {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                S.cam_dist *= (wheel > 0.0f) ? 0.9f : 1.111f;
                if (S.cam_dist < 0.3f)  S.cam_dist = 0.3f;
                if (S.cam_dist > 50.0f) S.cam_dist = 50.0f;
            }
        }
    }

    if (skel_visible && hovered && ImGui::IsKeyPressed(S.key_rotate_mode)) {
        if (S.selected_bone >= 0 && S.selected_bone < (int)g_mp.bone_count &&
            (size_t)S.selected_bone * 4 + 4 <= S.bone_rot_deltas.size()) {
            if (!S.bone_rotate_mode) {
                int b = S.selected_bone;
                for (int k = 0; k < 4; ++k) {
                    s_rot_snapshot[k] =
                        S.bone_rot_deltas[(size_t)b * 4 + (size_t)k];
                }
                s_rot_snapshot_bone  = b;
                s_rot_snapshot_valid = true;
                S.bone_rotate_mode   = true;
            } else {
                confirm_rotate();
            }
        }
    }

    for (size_t i = 0; i < g_mp.meshes.size(); ++i) {
        g_mp.meshes[i].highlight =
            ((int)i == ::g_highlight_mesh_idx) ||
            (::g_selected_level_pick_id == 0 &&
             (int)i == ::g_selected_level_mesh_idx) ||
            (DetailsPanel::WaterSelected() && g_mp.meshes[i].is_water);
        g_mp.meshes[i].isolated  = ((int)i == ::g_isolate_mesh_idx);
    }
    g_mp.selected_pick_id = ::g_selected_level_pick_id;
    g_mp.selected_pick_hash = ::g_selected_level_hash;

    if (!S.terrain_mode) apply_orbit_to_flycam();
    if (TerrainPaint::Active()) {
        TerrainPaint::SyncRenderResources(device);
    }
    MP_Render(device, g_mp, g_flycam);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (g_mp.srv) {
        dl->AddImage((ImTextureID)g_mp.srv,
                     origin,
                     ImVec2(origin.x + region.x, origin.y + region.y));
    }

#ifdef _WIN32
    if (S.terrain_mode) {
        draw_gdb_placements_overlay(origin, region);
    }
    draw_spawn_markers_overlay(origin, region, hovered);
    if (g_marker_clear_selection) {
        g_marker_clear_selection = false;
        DetailsPanel::ClearSelection();
        ::g_selected_level_pick_id = 0;
        ::g_selected_level_mesh_idx = -1;
        ::g_selected_level_hash = 0;
    }

    if (DetailsPanel::WaterSelected()) {
        g_sel_spawn_marker = -1;
        g_sel_pending_sp = -1;
        g_sel_pending_gen = -1;
    }

    if (g_sel_spawn_marker >= 0 &&
        g_sel_spawn_marker < (int)g_level_spawn_markers.size() &&
        level_marker_visible(
            g_level_spawn_markers[(size_t)g_sel_spawn_marker])) {
        const auto& mk =
            g_level_spawn_markers[(size_t)g_sel_spawn_marker];
        const Gdb::EntityGameplayDetails* marker_gameplay = nullptr;
        const uint32_t gameplay_hash = mk.creature_entity_hash != 0
            ? mk.creature_entity_hash : mk.entity_hash;
        auto gameplay_it = g_level_entity_gameplay.find(gameplay_hash);
        if (gameplay_it != g_level_entity_gameplay.end()) {
            marker_gameplay = &gameplay_it->second;
        }
        uint32_t spawn_owner_entity = 0;
        uint32_t spawn_owner_list = 0;
        if (mk.kind == 2) {
            for (const auto& candidate : g_level_spawn_markers) {
                if (candidate.kind != 1 ||
                    !candidate.spawn_points_record) {
                    continue;
                }
                if (std::find(candidate.spawn_point_entities.begin(),
                              candidate.spawn_point_entities.end(),
                              mk.entity_hash) !=
                    candidate.spawn_point_entities.end()) {
                    spawn_owner_entity = candidate.entity_hash;
                    spawn_owner_list = candidate.spawn_points_record;
                    break;
                }
            }
        }
        const uint32_t edit_id = 0x70000000u |
                                 uint32_t(g_sel_spawn_marker);
        const bool pending_addition = mk.pending_addition_index >= 0;
        const bool editable = LevelEdit::Enabled() &&
                              !LevelEdit::Saving() &&
                              (pending_addition || mk.pos_off[0] ||
                               mk.pos_off[1] ||
                               mk.pos_off[2]);
        const bool spawn_deletable =
            LevelEdit::Enabled() && !LevelEdit::Saving() &&
            mk.kind == 2 && spawn_owner_list != 0;
        const bool entity_deletable =
            LevelEdit::Enabled() && !LevelEdit::Saving() &&
            mk.kind != 2 && !is_player_start_marker(mk) &&
            (mk.entity_hash != 0 || pending_addition);
        auto queue_entity_delete = [&]() {
            LevelEdit::InstInfo info;
            const float orig[3] = {mk.x, mk.y, mk.z};
            info.orig_pos = orig;
            info.gdb_off = mk.pos_off;
            info.gdb_rot_off = mk.rot_off;
            info.gdb_entity_hash = mk.entity_hash;
            if (pending_addition) {
                info.lev_off = uint32_t(mk.pending_addition_index + 1);
                info.lev_kind = 5;
            }
            LevelEdit::PushUndoSnapshot({edit_id});
            LevelEdit::SetDeleted(edit_id, info);
            OutputLog::info("level edit: entity deletion queued");
            if (pending_addition) {
                g_level_spawn_markers[size_t(g_sel_spawn_marker)].kind = 0;
            }
            g_sel_spawn_marker = -1;
        };
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            g_sel_spawn_marker = -1;
        } else if (!ImGui::GetIO().WantTextInput &&
                   ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            if (spawn_deletable) {
                LevelEdit::RemoveSpawnPointFromExisting(
                    spawn_owner_entity, spawn_owner_list,
                    mk.entity_hash);
                OutputLog::info(
                    "level edit: spawn point deletion queued");
                g_sel_spawn_marker = -1;
            } else if (entity_deletable) {
                queue_entity_delete();
            }
        }
        const float kMarkerW =
            (mk.is_container || marker_gameplay) ? 320.0f : 220.0f;
        ImGui::SetNextWindowPos(
            ImVec2(origin.x + region.x - kMarkerW - 8.0f,
                   origin.y + 6.0f));
        ImGui::SetNextWindowSize(ImVec2(kMarkerW, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.78f);
        ImGuiWindowFlags mfl = ImGuiWindowFlags_NoTitleBar |
                               ImGuiWindowFlags_NoResize |
                               ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoCollapse |
                               ImGuiWindowFlags_NoSavedSettings |
                               ImGuiWindowFlags_AlwaysAutoResize;
        if (g_sel_spawn_marker >= 0 &&
            ImGui::Begin("##spawn_marker_editor", nullptr, mfl)) {
            float mpos[3] = {mk.x, mk.y, mk.z};
            {
                float d_pos[3], d_rot[3];
                if (LevelEdit::EditFor(edit_id, d_pos, d_rot)) {
                    mpos[0] += d_pos[0];
                    mpos[1] += d_pos[1];
                    mpos[2] += d_pos[2];
                }
            }
            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                               "Position:");
            static const char* kMAxis[3] = {"X##mpos", "Y##mpos",
                                            "Z##mpos"};
            float edit_pos[3] = {mpos[0], mpos[1], mpos[2]};
            bool commit = false;
            if (editable) {
                for (int a = 0; a < 3; ++a) {
                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::InputFloat(kMAxis[a], &edit_pos[a], 0.0f,
                                      0.0f, "%.3f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        commit = true;
                    }
                }
            } else {
                ImGui::Text("X: %.3f", mpos[0]);
                ImGui::Text("Y: %.3f", mpos[1]);
                ImGui::Text("Z: %.3f", mpos[2]);
            }
            if (commit) {
                const float step[3] = {edit_pos[0] - mpos[0],
                                       edit_pos[1] - mpos[1],
                                       edit_pos[2] - mpos[2]};
                if (step[0] != 0 || step[1] != 0 || step[2] != 0) {
                    LevelEdit::InstInfo info;
                    const float orig[3] = {mk.x, mk.y, mk.z};
                    info.orig_pos = orig;
                    info.gdb_off = mk.pos_off;
                    info.gdb_rot_off = mk.rot_off;
                    info.gdb_entity_hash = mk.entity_hash;
                    if (pending_addition) {
                        info.lev_off =
                            uint32_t(mk.pending_addition_index + 1);
                        info.lev_kind = 5;
                    }
                    LevelEdit::AddMove(edit_id, step, info);
                }
            }

            ImGui::Spacing();
            const char* kind_name =
                mk.kind == 1 ? "Creature generator"
                : mk.kind == 2 ? "Spawn point"
                : mk.kind == 4 ? "Dig spot"
                : mk.kind == 5 ? "Container"
                : mk.kind == 6 ? "Static prop"
                               : "NPC / creature";
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.55f, 1.0f), "%s",
                               kind_name);
            if (!mk.name.empty()) {
                ImGui::TextUnformatted(mk.name.c_str());
            }
            if (mk.kind == 1 && !mk.creature_name.empty()) {
                ImGui::TextDisabled("spawns: %s",
                                    mk.creature_name.c_str());
            }
            if (marker_gameplay) {
                draw_entity_gameplay_details(*marker_gameplay);
            }
            if (pending_addition) {
                draw_addition_container_details(
                    mk.pending_addition_index);
            } else if (mk.kind == 4 || mk.is_container) {
                draw_level_container_details(mk.entity_hash);
            }
            if (mk.kind == 2 && spawn_deletable &&
                ImGui::Button("Delete spawn point")) {
                LevelEdit::RemoveSpawnPointFromExisting(
                    spawn_owner_entity, spawn_owner_list,
                    mk.entity_hash);
                OutputLog::info(
                    "level edit: spawn point deletion queued");
                g_sel_spawn_marker = -1;
            }
            if (mk.kind != 2 && entity_deletable &&
                ImGui::Button(mk.kind == 1 ? "Delete generator"
                                           : pending_addition
                                               ? (mk.kind == 4
                                                      ? "Delete dig spot"
                                                      : mk.kind == 3
                                                            ? "Delete NPC"
                                                            : mk.kind == 6
                                                                  ? "Delete static prop"
                                                            : "Delete container")
                                               : "Delete entity")) {
                queue_entity_delete();
            }
            if (mk.kind == 1) {
                const bool can_add_sp =
                    LevelEdit::Enabled() && !LevelEdit::Saving() &&
                    mk.spawn_points_record != 0;
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Spawn points (%zu):",
                                   mk.spawn_point_entities.size());
                for (size_t si = 0;
                     si < mk.spawn_point_entities.size(); ++si) {
                    const uint32_t sph = mk.spawn_point_entities[si];
                    if (LevelEdit::SpawnPointRemovalPending(sph)) {
                        continue;
                    }
                    int target = -1;
                    for (size_t mj = 0;
                         mj < g_level_spawn_markers.size(); ++mj) {
                        if (g_level_spawn_markers[mj].entity_hash ==
                            sph) {
                            target = int(mj);
                            break;
                        }
                    }
                    ImGui::PushID(int(si) + 0x3000);
                    char lbl[64];
                    std::snprintf(lbl, sizeof(lbl), "0x%08X%s", sph,
                                  target < 0 ? " (no marker)" : "");
                    if (ImGui::Selectable(
                            lbl, target == g_sel_spawn_marker) &&
                        target >= 0) {
                        g_sel_spawn_marker = target;
                    }
                    if (can_add_sp) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Delete")) {
                            LevelEdit::RemoveSpawnPointFromExisting(
                                mk.entity_hash, mk.spawn_points_record,
                                sph);
                            if (target == g_sel_spawn_marker) {
                                g_sel_spawn_marker = -1;
                            }
                            OutputLog::info(
                                "level edit: spawn point deletion "
                                "queued");
                        }
                    }
                    ImGui::PopID();
                }
                if (can_add_sp &&
                    ImGui::SmallButton("+ Add spawn point")) {
                    const float n =
                        float(mk.spawn_point_entities.size() +
                              LevelEdit::PendingSpawnPointCount());
                    const float ang = n * 1.0471976f;
                    const float rad = 1.5f + 0.3f * n;
                    const float sp_pos[3] = {
                        mpos[0] + std::cos(ang) * rad,
                        mpos[1] + std::sin(ang) * rad,
                        mpos[2]};
                    LevelEdit::AddSpawnPointToExisting(
                        mk.entity_hash, mk.spawn_points_record,
                        sp_pos);
                    OutputLog::success(
                        "level edit: spawn point queued next to the "
                        "generator (authored on Save; move it after "
                        "reload)");
                }
                if (LevelEdit::PendingSpawnPointCount() > 0) {
                    ImGui::TextDisabled(
                        "%zu pending spawn point(s)",
                        LevelEdit::PendingSpawnPointCount());
                }
            }
            ImGui::End();
        } else if (g_sel_spawn_marker >= 0) {
            ImGui::End();
        }

        if (g_sel_spawn_marker >= 0 && editable) {
            float gpos[3] = {mk.x, mk.y, mk.z};
            {
                float d_pos[3], d_rot[3];
                if (LevelEdit::EditFor(edit_id, d_pos, d_rot)) {
                    gpos[0] += d_pos[0];
                    gpos[1] += d_pos[1];
                    gpos[2] += d_pos[2];
                }
            }
            LevelGizmo::Result gz = LevelGizmo::DrawAndHandle(
                g_flycam, origin, region, gpos, true);
            static bool s_mk_dragging = false;
            if (gz.dragging && !s_mk_dragging) {
                LevelEdit::PushUndoSnapshot({edit_id});
            }
            s_mk_dragging = gz.dragging;
            if (gz.moved &&
                (gz.step[0] != 0.0f || gz.step[1] != 0.0f ||
                 gz.step[2] != 0.0f)) {
                LevelEdit::InstInfo info;
                const float orig[3] = {mk.x, mk.y, mk.z};
                info.orig_pos = orig;
                info.gdb_off = mk.pos_off;
                info.gdb_rot_off = mk.rot_off;
                info.gdb_entity_hash = mk.entity_hash;
                LevelEdit::AddMove(edit_id, gz.step, info);
            }
        }
    }

    if (g_sel_pending_sp >= 0 && S.show_spawn_markers) {
        std::vector<LevelEdit::PendingSpawnPoint> psps;
        LevelEdit::GetPendingSpawnPoints(psps);
        const LevelEdit::PendingSpawnPoint* sel_sp = nullptr;
        for (const auto& sp : psps) {
            if (sp.id == g_sel_pending_sp) {
                sel_sp = &sp;
                break;
            }
        }
        if (!sel_sp) {
            g_sel_pending_sp = -1;
        } else {
            const bool sp_editable =
                LevelEdit::Enabled() && !LevelEdit::Saving();
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                g_sel_pending_sp = -1;
            } else if (sp_editable && !ImGui::GetIO().WantTextInput &&
                       ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                LevelEdit::RemovePendingSpawnPoint(sel_sp->id);
                OutputLog::info(
                    "level edit: pending spawn point removed");
                g_sel_pending_sp = -1;
                sel_sp = nullptr;
            }
        }
        if (sel_sp) {
            const bool sp_editable =
                LevelEdit::Enabled() && !LevelEdit::Saving();
            if (!details_panel_docked()) {
            const float kSpW = 220.0f;
            ImGui::SetNextWindowPos(
                ImVec2(origin.x + region.x - kSpW - 8.0f,
                       origin.y + 6.0f));
            ImGui::SetNextWindowSize(ImVec2(kSpW, 0), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.78f);
            ImGuiWindowFlags sfl = ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_AlwaysAutoResize;
            if (ImGui::Begin("##pending_sp_editor", nullptr, sfl)) {
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Position:");
                float ep[3] = {sel_sp->pos[0], sel_sp->pos[1],
                               sel_sp->pos[2]};
                bool commit = false;
                if (sp_editable) {
                    static const char* kAx[3] = {"X##psp", "Y##psp",
                                                 "Z##psp"};
                    for (int a = 0; a < 3; ++a) {
                        ImGui::SetNextItemWidth(120.0f);
                        ImGui::InputFloat(kAx[a], &ep[a], 0.0f, 0.0f,
                                          "%.3f");
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            commit = true;
                        }
                    }
                } else {
                    ImGui::Text("X: %.3f", ep[0]);
                    ImGui::Text("Y: %.3f", ep[1]);
                    ImGui::Text("Z: %.3f", ep[2]);
                }
                if (commit) {
                    LevelEdit::MovePendingSpawnPoint(sel_sp->id, ep);
                }
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.9f, 0.75f, 1.0f, 1.0f),
                                   "%s", sel_sp->label.c_str());
                ImGui::TextDisabled("unsaved - authored on Save");
                if (sp_editable &&
                    ImGui::Button("Delete spawn point")) {
                    LevelEdit::RemovePendingSpawnPoint(sel_sp->id);
                    OutputLog::info(
                        "level edit: pending spawn point removed");
                    g_sel_pending_sp = -1;
                }
            }
            ImGui::End();
            }   

            if (sp_editable && g_sel_pending_sp >= 0) {
                float gpos[3] = {sel_sp->pos[0], sel_sp->pos[1],
                                 sel_sp->pos[2]};
                LevelGizmo::Result gz = LevelGizmo::DrawAndHandle(
                    g_flycam, origin, region, gpos, true);
                if (gz.moved &&
                    (gz.step[0] != 0.0f || gz.step[1] != 0.0f ||
                     gz.step[2] != 0.0f)) {
                    const float np[3] = {gpos[0] + gz.step[0],
                                         gpos[1] + gz.step[1],
                                         gpos[2] + gz.step[2]};
                    LevelEdit::MovePendingSpawnPoint(sel_sp->id, np);
                }
            }
        }
    }

    if (g_sel_pending_gen >= 0 && S.show_spawn_markers) {
        std::vector<LevelEdit::GeneratorAddition> pgens;
        LevelEdit::GetGenerators(pgens);
        const bool gen_valid =
            g_sel_pending_gen < (int)pgens.size() &&
            !pgens[(size_t)g_sel_pending_gen].removed;
        if (!gen_valid) {
            g_sel_pending_gen = -1;
        } else {
            const auto& pg = pgens[(size_t)g_sel_pending_gen];
            const bool gen_editable =
                LevelEdit::Enabled() && !LevelEdit::Saving();
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                g_sel_pending_gen = -1;
            } else if (gen_editable && !ImGui::GetIO().WantTextInput &&
                       ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                LevelEdit::RemoveGenerator(g_sel_pending_gen);
                g_sel_pending_gen = -1;
            }
            if (g_sel_pending_gen >= 0) {
                if (!details_panel_docked()) {
                const float kGenW = 220.0f;
                ImGui::SetNextWindowPos(
                    ImVec2(origin.x + region.x - kGenW - 8.0f,
                           origin.y + 6.0f));
                ImGui::SetNextWindowSize(ImVec2(kGenW, 0),
                                         ImGuiCond_Always);
                ImGui::SetNextWindowBgAlpha(0.78f);
                ImGuiWindowFlags gfl =
                    ImGuiWindowFlags_NoTitleBar |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_AlwaysAutoResize;
                if (ImGui::Begin("##pending_gen_editor", nullptr,
                                 gfl)) {
                    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                       "Position:");
                    float ep[3] = {pg.pos[0], pg.pos[1], pg.pos[2]};
                    bool commit = false;
                    if (gen_editable) {
                        static const char* kAx[3] = {
                            "X##pgen", "Y##pgen", "Z##pgen"};
                        for (int a = 0; a < 3; ++a) {
                            ImGui::SetNextItemWidth(120.0f);
                            ImGui::InputFloat(kAx[a], &ep[a], 0.0f,
                                              0.0f, "%.3f");
                            if (ImGui::IsItemDeactivatedAfterEdit()) {
                                commit = true;
                            }
                        }
                    } else {
                        ImGui::Text("X: %.3f", ep[0]);
                        ImGui::Text("Y: %.3f", ep[1]);
                        ImGui::Text("Z: %.3f", ep[2]);
                    }
                    if (commit) {
                        LevelEdit::MovePendingGenerator(
                            g_sel_pending_gen, ep);
                    }
                    ImGui::Spacing();
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.75f, 0.55f, 1.0f),
                        "Creature generator (unsaved)");
                    ImGui::TextDisabled("spawns: %s",
                                        pg.creature_name.c_str());
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                       "Spawn points (%zu):",
                                       pg.spawn_points.size());
                    for (size_t si = 0; si < pg.spawn_points.size();
                         ++si) {
                        ImGui::PushID(int(si) + 0x4000);
                        char lbl[48];
                        std::snprintf(lbl, sizeof(lbl),
                                      "#%zu (%.1f, %.1f, %.1f)",
                                      si + 1, pg.spawn_points[si][0],
                                      pg.spawn_points[si][1],
                                      pg.spawn_points[si][2]);
                        const int sp_id =
                            (g_sel_pending_gen << 8) | int(si);
                        if (ImGui::Selectable(
                                lbl, g_sel_pending_sp == sp_id)) {
                            g_sel_pending_sp = sp_id;
                            g_sel_pending_gen = -1;
                        }
                        ImGui::PopID();
                    }
                    if (gen_editable &&
                        ImGui::SmallButton("+ Add spawn point")) {
                        const float n = float(pg.spawn_points.size());
                        const float ang = n * 1.0471976f;
                        const float rad = 1.5f + 0.3f * n;
                        const float sp_pos[3] = {
                            pg.pos[0] + std::cos(ang) * rad,
                            pg.pos[1] + std::sin(ang) * rad,
                            pg.pos[2]};
                        LevelEdit::AddGeneratorSpawnPoint(
                            g_sel_pending_gen, sp_pos);
                    }
                    ImGui::TextDisabled("unsaved - authored on Save");
                    if (gen_editable &&
                        ImGui::Button("Delete generator")) {
                        LevelEdit::RemoveGenerator(g_sel_pending_gen);
                        OutputLog::info(
                            "level edit: unsaved generator removed");
                        g_sel_pending_gen = -1;
                    }
                }
                ImGui::End();
                }   

                if (gen_editable && g_sel_pending_gen >= 0) {
                    float gpos[3] = {pg.pos[0], pg.pos[1], pg.pos[2]};
                    LevelGizmo::Result gz = LevelGizmo::DrawAndHandle(
                        g_flycam, origin, region, gpos, true);
                    if (gz.moved &&
                        (gz.step[0] != 0.0f || gz.step[1] != 0.0f ||
                         gz.step[2] != 0.0f)) {
                        const float np[3] = {gpos[0] + gz.step[0],
                                             gpos[1] + gz.step[1],
                                             gpos[2] + gz.step[2]};
                        LevelEdit::MovePendingGenerator(
                            g_sel_pending_gen, np);
                    }
                }
            }
        }
    }

    float water_position[3] = {};
    if (DetailsPanel::SelectedWaterPosition(water_position)) {
        LevelGizmo::SetMode(LevelGizmo::Mode::Translate);
        LevelGizmo::Result gz = LevelGizmo::DrawAndHandle(
            g_flycam, origin, region, water_position,
            !Level::IsAsyncLoadInProgress());
        if (gz.moved &&
            (gz.step[0] != 0.0f || gz.step[1] != 0.0f ||
             gz.step[2] != 0.0f)) {
            DetailsPanel::MoveSelectedWater(gz.step);
        }
    }
#endif

    if (g_mp.no_tilt && ::g_selected_level_mesh_idx >= 0 &&
        ::g_selected_level_mesh_idx < (int)g_mp.meshes.size())
    {
        const auto& sel_mesh =
            g_mp.meshes[(size_t)::g_selected_level_mesh_idx];
        float sel_pos[3] = {0.0f, 0.0f, 0.0f};
        float sel_rot[3] = {0.0f, 0.0f, 0.0f};
        bool  sel_has_rot = false;
        bool  sel_found = false;
        uint32_t sel_gdb_entity_hash = 0;
        if (::g_selected_level_pick_id != 0) {
            for (const auto& pr : sel_mesh.pick_ranges) {
                if (pr.selection_id != ::g_selected_level_pick_id) continue;
                sel_gdb_entity_hash = pr.gdb_entity_hash;
                if (pr.has_transform) {
                    sel_pos[0] = pr.inst_pos[0];
                    sel_pos[1] = pr.inst_pos[1];
                    sel_pos[2] = pr.inst_pos[2];
                    sel_rot[0] = pr.inst_rot_deg[0];
                    sel_rot[1] = pr.inst_rot_deg[1];
                    sel_rot[2] = pr.inst_rot_deg[2];
                    sel_has_rot = true;
                } else {
                    sel_pos[0] = pr.center[0];
                    sel_pos[1] = pr.center[2];
                    sel_pos[2] = pr.center[1];
                }
                sel_found = true;
                break;
            }
        }
        if (!sel_found) {
            sel_pos[0] = sel_mesh.center[0];
            sel_pos[1] = sel_mesh.center[2];
            sel_pos[2] = sel_mesh.center[1];
        }
        const bool whole_mesh_sel = (::g_selected_level_pick_id == 0);
        const uint32_t edit_key = whole_mesh_sel
            ? (0x80000000u | (uint32_t)::g_selected_level_mesh_idx)
            : ::g_selected_level_pick_id;
        {
            float d_pos[3], d_rot[3];
            if (LevelEdit::EditFor(edit_key, d_pos, d_rot)) {
                sel_pos[0] += d_pos[0];
                sel_pos[1] += d_pos[1];
                sel_pos[2] += d_pos[2];
                sel_rot[0] += d_rot[0];
                sel_rot[1] += d_rot[1];
                sel_rot[2] += d_rot[2];
            }
        }

        auto range_in_group = [](const MPPerMesh::PickRange& pr) {
            if (pr.selection_id == ::g_selected_level_pick_id) return true;
            return ::g_selected_level_hash != 0 &&
                   pr.inst_hash == ::g_selected_level_hash;
        };
        auto collect_group_ids = [&]() {
            std::vector<uint32_t> ids;
            if (whole_mesh_sel) {
                ids.push_back(edit_key);
                return ids;
            }
            std::unordered_set<uint32_t> seen;
            for (const auto& m2 : g_mp.meshes) {
                for (const auto& pr : m2.pick_ranges) {
                    if (!range_in_group(pr)) continue;
                    if (seen.insert(pr.selection_id).second) {
                        ids.push_back(pr.selection_id);
                    }
                }
            }
            return ids;
        };
        enum { kEditMove, kEditRotate, kEditDelete };
        auto apply_group_edit = [&](int what, const float v[3]) {
            if (whole_mesh_sel) {
                const float orig[3] = { sel_mesh.center[0],
                                        sel_mesh.center[2],
                                        sel_mesh.center[1] };
                LevelEdit::InstInfo info;
                info.orig_pos = orig;
                if (what == kEditMove) {
                    LevelEdit::AddMove(edit_key, v, info);
                } else if (what == kEditRotate) {
                    LevelEdit::AddRotate(edit_key, v, info);
                } else {
                    LevelEdit::SetDeleted(edit_key, info);
                }
                return;
            }
            std::unordered_set<uint32_t> done;
            for (const auto& m2 : g_mp.meshes) {
                for (const auto& pr : m2.pick_ranges) {
                    if (!range_in_group(pr)) continue;
                    if (!done.insert(pr.selection_id).second) continue;
                    LevelEdit::InstInfo info;
                    info.orig_pos = pr.inst_pos;
                    info.orig_rot_deg[0] = pr.inst_rot_deg[0];
                    info.orig_rot_deg[1] = pr.inst_rot_deg[1];
                    info.orig_rot_deg[2] = pr.inst_rot_deg[2];
                    info.lev_off = pr.pos_file_offset;
                    info.lev_kind = pr.lev_rec_kind;
                    info.gdb_off = pr.gdb_pos_off;
                    info.gdb_rot_off = pr.gdb_rot_off;
                    info.gdb_entity_hash = pr.gdb_entity_hash;
                    if (what == kEditMove) {
                        LevelEdit::AddMove(pr.selection_id, v, info);
                    } else if (what == kEditRotate) {
                        LevelEdit::AddRotate(pr.selection_id, v, info);
                    } else {
                        LevelEdit::SetDeleted(pr.selection_id, info);
                    }
                }
            }
        };
        const bool sel_finite = std::isfinite(sel_pos[0]) &&
                                std::isfinite(sel_pos[1]) &&
                                std::isfinite(sel_pos[2]);
        const bool edit_active = LevelEdit::Enabled() &&
                                 !LevelEdit::Saving() &&
                                 (whole_mesh_sel || sel_found) &&
                                 sel_finite;

        static int      s_dbg_idx = -2;
        static uint32_t s_dbg_id  = 0xFFFFFFFFu;
        const bool dbg_sel_changed =
            s_dbg_idx != ::g_selected_level_mesh_idx ||
            s_dbg_id  != ::g_selected_level_pick_id;
        if (dbg_sel_changed) {
            s_dbg_idx = ::g_selected_level_mesh_idx;
            s_dbg_id  = ::g_selected_level_pick_id;
            DebugTrace::log(
                "sel: idx=%d id=%u hash=%llu ranges=%zu found=%d whole=%d "
                "finite=%d pos=(%.2f,%.2f,%.2f) edit_active=%d",
                ::g_selected_level_mesh_idx, ::g_selected_level_pick_id,
                (unsigned long long)::g_selected_level_hash,
                sel_mesh.pick_ranges.size(), sel_found ? 1 : 0,
                whole_mesh_sel ? 1 : 0, sel_finite ? 1 : 0,
                sel_pos[0], sel_pos[1], sel_pos[2], edit_active ? 1 : 0);
        }

        const Gdb::EntityContents* sel_contents = nullptr;

        if (::g_selected_level_hash != 0 &&
            ::g_selected_level_hash <= 0xFFFFFFFFull) {
            auto cit = g_level_entity_contents.find(
                uint32_t(::g_selected_level_hash));
            if (cit != g_level_entity_contents.end()) {
                sel_contents = &cit->second;
            }
        }
        if (!sel_contents && sel_gdb_entity_hash != 0) {
            auto cit = g_level_entity_contents.find(sel_gdb_entity_hash);
            if (cit != g_level_entity_contents.end()) {
                sel_contents = &cit->second;
            }
        }
        const Gdb::EntityGameplayDetails* sel_gameplay = nullptr;
        if (::g_selected_level_hash != 0 &&
            ::g_selected_level_hash <= 0xFFFFFFFFull) {
            auto git = g_level_entity_gameplay.find(
                uint32_t(::g_selected_level_hash));
            if (git != g_level_entity_gameplay.end()) {
                sel_gameplay = &git->second;
            }
        }
        const Gdb::PropertyDetails* sel_property = nullptr;
        if (::g_selected_level_hash != 0 &&
            ::g_selected_level_hash <= 0xFFFFFFFFull) {
            auto pit = g_level_property_details.find(
                uint32_t(::g_selected_level_hash));
            if (pit != g_level_property_details.end()) {
                sel_property = &pit->second;
            }
        }
        if (!sel_property && sel_gdb_entity_hash != 0) {
            auto pit = g_level_property_details.find(sel_gdb_entity_hash);
            if (pit != g_level_property_details.end()) {
                sel_property = &pit->second;
            }
        }
        if (!sel_gameplay && sel_gdb_entity_hash != 0) {
            auto git = g_level_entity_gameplay.find(sel_gdb_entity_hash);
            if (git != g_level_entity_gameplay.end()) {
                sel_gameplay = &git->second;
            }
        }
        if (!sel_gameplay) {
            uint32_t creature_hash = 0;
            const int selected_marker =
                selected_level_spawn_marker_index();
            if (selected_marker >= 0) {
                creature_hash = g_level_spawn_markers[
                    size_t(selected_marker)].creature_entity_hash;
            }
            if (creature_hash == 0 && sel_gdb_entity_hash != 0) {
                for (const auto& marker : g_level_spawn_markers) {
                    if (marker.entity_hash == sel_gdb_entity_hash) {
                        creature_hash = marker.creature_entity_hash;
                        if (creature_hash != 0) break;
                    }
                }
            }
            auto git = g_level_entity_gameplay.find(creature_hash);
            if (git != g_level_entity_gameplay.end()) {
                sel_gameplay = &git->second;
            }
        }
        constexpr uint64_t kAdditionHashBase = 0xADD0000000000000ull;
        int sel_chest_addition = -1;
        int sel_readable_addition = -1;
        if (::g_selected_level_hash >= kAdditionHashBase) {
            const int add_idx =
                int(::g_selected_level_hash - kAdditionHashBase);
            if (LevelEdit::AdditionIsChest(add_idx)) {
                sel_chest_addition = add_idx;
            }
            if (LevelEdit::AdditionIsReadable(add_idx)) {
                sel_readable_addition = add_idx;
            }
        }
        const Gdb::EntityTextTags* sel_text = nullptr;
        if (::g_selected_level_hash != 0 &&
            ::g_selected_level_hash <= 0xFFFFFFFFull) {
            auto tit = g_level_entity_text.find(
                uint32_t(::g_selected_level_hash));
            if (tit != g_level_entity_text.end()) {
                sel_text = &tit->second;
            }
        }
        if (!sel_text && sel_gdb_entity_hash != 0) {
            auto tit = g_level_entity_text.find(sel_gdb_entity_hash);
            if (tit != g_level_entity_text.end()) {
                sel_text = &tit->second;
            }
        }
        if (!sel_text && sel_found) {
            float best = 3.0f * 3.0f;
            for (const auto& kv : g_level_entity_text) {
                if (!kv.second.has_pos) continue;
                const float dx = kv.second.x - sel_pos[0];
                const float dy = kv.second.y - sel_pos[1];
                const float dz = kv.second.z - sel_pos[2];
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 < best) {
                    best = d2;
                    sel_text = &kv.second;
                }
            }
        }
        if (!details_panel_docked()) {
        const float kOverlayW = (sel_contents || sel_gameplay ||
                                 sel_property ||
                                 sel_chest_addition >= 0 || sel_text ||
                                 sel_readable_addition >= 0)
            ? 290.0f
            : (LevelEdit::Enabled() ? 190.0f : 150.0f);
        ImGui::SetNextWindowPos(ImVec2(origin.x + region.x - kOverlayW - 8.0f,
                                       origin.y + 6.0f));
        ImGui::SetNextWindowSize(ImVec2(kOverlayW, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.78f);
        ImGuiWindowFlags ofl = ImGuiWindowFlags_NoTitleBar
                             | ImGuiWindowFlags_NoResize
                             | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoCollapse
                             | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_AlwaysAutoResize
                             | ImGuiWindowFlags_NoFocusOnAppearing;
        if (ImGui::Begin("##sel_transform_overlay", nullptr, ofl)) {
            if (dbg_sel_changed) DebugTrace::log("ov: begin");
            if (edit_active) {
                const char* mode_name =
                    LevelGizmo::GetMode() == LevelGizmo::Mode::Rotate
                        ? "Rotate (E)"
                        : "Move (W)";
                ImGui::TextDisabled("%s", mode_name);
            }
            if (S.dev_mode) {
                ImGui::TextDisabled(
                    "sel 0x%016llX link 0x%08X text %s(%zu)",
                    (unsigned long long)::g_selected_level_hash,
                    sel_gdb_entity_hash, sel_text ? "HIT" : "miss",
                    g_level_entity_text.size());
            }
            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Position:");
            if (edit_active) {
                static const char* kAxis[3] = { "X##selpos", "Y##selpos",
                                                "Z##selpos" };
                float edit_pos[3] = { sel_pos[0], sel_pos[1], sel_pos[2] };
                bool commit = false;
                for (int a = 0; a < 3; ++a) {
                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::InputFloat(kAxis[a], &edit_pos[a], 0.0f, 0.0f,
                                      "%.3f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) commit = true;
                }
                if (commit) {
                    const float step[3] = { edit_pos[0] - sel_pos[0],
                                            edit_pos[1] - sel_pos[1],
                                            edit_pos[2] - sel_pos[2] };
                    if (step[0] != 0.0f || step[1] != 0.0f ||
                        step[2] != 0.0f) {
                        LevelEdit::PushUndoSnapshot(collect_group_ids());
                        apply_group_edit(kEditMove, step);
                    }
                }
                if (dbg_sel_changed) DebugTrace::log("ov: pos done");
            } else {
                ImGui::Text("X: %.3f", sel_pos[0]);
                ImGui::Text("Y: %.3f", sel_pos[1]);
                ImGui::Text("Z: %.3f", sel_pos[2]);
            }
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Rotation:");
            if (edit_active) {
                static const char* kRAxis[3] = { "X##selrot", "Y##selrot",
                                                 "Z##selrot" };
                float edit_rot[3] = { sel_rot[0], sel_rot[1], sel_rot[2] };
                bool commit = false;
                for (int a = 0; a < 3; ++a) {
                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::InputFloat(kRAxis[a], &edit_rot[a], 0.0f, 0.0f,
                                      "%.1f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) commit = true;
                }
                if (commit) {
                    const float step[3] = { edit_rot[0] - sel_rot[0],
                                            edit_rot[1] - sel_rot[1],
                                            edit_rot[2] - sel_rot[2] };
                    if (step[0] != 0.0f || step[1] != 0.0f ||
                        step[2] != 0.0f) {
                        LevelEdit::PushUndoSnapshot(collect_group_ids());
                        apply_group_edit(kEditRotate, step);
                    }
                }
            } else if (sel_has_rot) {
                ImGui::Text("X: %.1f", sel_rot[0]);
                ImGui::Text("Y: %.1f", sel_rot[1]);
                ImGui::Text("Z: %.1f", sel_rot[2]);
            } else {
                ImGui::TextDisabled("n/a");
            }
            if (sel_text) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Text:");
                static uint32_t s_text_sel = 0;
                static std::vector<std::string> s_text_bufs;
                const uint32_t text_key = sel_text->tag_hashes.front();
                if (s_text_sel != text_key ||
                    s_text_bufs.size() != sel_text->tag_hashes.size()) {
                    s_text_sel = text_key;
                    s_text_bufs.clear();
                    for (uint32_t th : sel_text->tag_hashes) {
                        std::string t;
                        if (!LevelEdit::GetEntityTextEdit(th, t)) {
                            TextBank::Lookup(th, t);
                        }
                        s_text_bufs.push_back(std::move(t));
                    }
                }
                const bool text_editable =
                    LevelEdit::Enabled() && !LevelEdit::Saving();
                for (size_t pi = 0; pi < s_text_bufs.size(); ++pi) {
                    ImGui::PushID(int(pi) + 0x2000);
                    if (s_text_bufs.size() > 1) {
                        if (pi == 0) {
                            ImGui::TextDisabled("Item Name");
                        } else if (pi == 1) {
                            ImGui::TextDisabled("Description");
                        } else {
                            ImGui::TextDisabled("Page %d", int(pi) + 1);
                        }
                    }
                    if (text_editable) {
                        ImGui::InputTextMultiline(
                            "##entity_text", &s_text_bufs[pi],
                            ImVec2(268.0f, 110.0f));
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            LevelEdit::SetEntityTextEdit(
                                sel_text->tag_hashes[pi],
                                s_text_bufs[pi]);
                        }
                    } else {
                        std::string shown = s_text_bufs[pi];
                        if (shown.size() > 1200) {
                            shown.resize(1200);
                            shown += " [...]";
                        }
                        ImGui::TextWrapped("%s", shown.c_str());
                    }
                    ImGui::PopID();
                }
                if (text_editable) {
                    ImGui::TextDisabled(
                        "Text is written to book.babel on Save");
                }
            }
            if (sel_readable_addition >= 0) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                                   "New readable (unsaved)");
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Text:");
                static int s_addtext_sel = -1;
                static std::string s_addtext_buf;
                if (s_addtext_sel != sel_readable_addition) {
                    s_addtext_sel = sel_readable_addition;
                    s_addtext_buf.clear();
                    LevelEdit::GetAdditionReadableText(
                        sel_readable_addition, s_addtext_buf);
                }
                const bool add_text_editable =
                    LevelEdit::Enabled() && !LevelEdit::Saving();
                ImGui::BeginDisabled(!add_text_editable);
                ImGui::InputTextMultiline("##add_readable_text",
                                          &s_addtext_buf,
                                          ImVec2(268.0f, 110.0f));
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    LevelEdit::SetAdditionReadableText(
                        sel_readable_addition, s_addtext_buf);
                }
                ImGui::EndDisabled();
            }
            if (sel_gameplay) {
                draw_entity_gameplay_details(*sel_gameplay);
            }
            if (sel_property) {
                draw_property_details(*sel_property);
            }
            if (sel_contents) {
                auto pretty_tag = [](std::string tag, int money) {

                    for (const char* pfx : { "INV_ITEM_", "OBJECT_",
                                             "TEXT_" }) {
                        const size_t n = std::strlen(pfx);
                        if (tag.size() > n && tag.compare(0, n, pfx) == 0) {
                            tag = tag.substr(n);
                            break;
                        }
                    }
                    constexpr const char* kNameSuffix = "_NAME";
                    constexpr size_t kNameSuffixLen = 5;
                    if (tag.size() > kNameSuffixLen &&
                        tag.compare(tag.size() - kNameSuffixLen,
                                    kNameSuffixLen, kNameSuffix) == 0) {
                        tag.resize(tag.size() - kNameSuffixLen);
                    }
                    if (tag.find('_') != std::string::npos ||
                        std::none_of(tag.begin(), tag.end(),
                                     [](unsigned char c) {
                                         return std::islower(c);
                                     })) {
                        bool word_start = true;
                        for (auto& c : tag) {
                            if (c == '_') {
                                c = ' ';
                                word_start = true;
                            } else {
                                c = word_start
                                    ? char(std::toupper((unsigned char)c))
                                    : char(std::tolower((unsigned char)c));
                                word_start = false;
                            }
                        }
                    }
                    if (money >= 0) {
                        tag += " (" + std::to_string(money) + " gold)";
                    }
                    return tag;
                };
                auto catalog_label = [&](uint32_t record_hash) {
                    for (const auto& c : g_item_details) {
                        if (c.record_hash == record_hash &&
                            !c.display_name.empty()) {
                            return c.display_name;
                        }
                    }
                    for (const auto& c : g_level_item_catalog) {
                        if (c.record_hash == record_hash) {
                            return pretty_tag(c.label, c.money);
                        }
                    }
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "0x%08X", record_hash);
                    return std::string(buf);
                };
                auto item_label = [&](const Gdb::EntityContentsItem& it) {
                    if (!it.display_name.empty()) return it.display_name;
                    std::string tag = !it.name_tag.empty() ? it.name_tag
                                                           : it.entry_label;
                    if (tag.empty()) return catalog_label(it.record_hash);
                    return pretty_tag(std::move(tag), it.money);
                };
                ImGui::Spacing();
                ImGui::Separator();
                if (!sel_gameplay && !sel_contents->entity_name.empty()) {
                    ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                                       "%s",
                                       sel_contents->entity_name.c_str());
                }
                draw_container_authored_rules(*sel_contents);
                const uint32_t sel_entity =
                    sel_gdb_entity_hash != 0
                        ? sel_gdb_entity_hash
                        : uint32_t(::g_selected_level_hash);
                std::vector<uint32_t> shown_items;
                bool staged = LevelEdit::GetChestContents(sel_entity,
                                                          shown_items);
                if (!staged) {
                    for (const auto& it : sel_contents->initial_items) {
                        shown_items.push_back(it.record_hash);
                    }
                }
                const bool contents_editable =
                    LevelEdit::Enabled() && !LevelEdit::Saving();

                static int s_picker_slot = -1;
                static uint32_t s_picker_entity = 0;
                static char s_picker_filter[64] = {};

                if (staged || !shown_items.empty() ||
                    sel_contents->has_inventory_component ||
                    contents_editable) {
                    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                       staged ? "Initial items (edited):"
                                              : "Initial items:");
                    int remove_idx = -1;
                    bool open_picker = false;
                    for (size_t ii = 0; ii < shown_items.size(); ++ii) {
                        ImGui::PushID(int(ii));
                        if (contents_editable) {
                            if (ImGui::SmallButton("x")) remove_idx = int(ii);
                            ImGui::SameLine();
                            std::string label;
                            if (!staged &&
                                ii < sel_contents->initial_items.size()) {
                                label = item_label(
                                    sel_contents->initial_items[ii]);
                            } else {
                                label = catalog_label(shown_items[ii]);
                            }
                            if (ImGui::Selectable(label.c_str(), false,
                                    ImGuiSelectableFlags_DontClosePopups)) {
                                s_picker_slot = int(ii);
                                s_picker_entity = sel_entity;
                                s_picker_filter[0] = 0;
                                open_picker = true;
                            }
                        } else {
                            std::string label;
                            if (!staged &&
                                ii < sel_contents->initial_items.size()) {
                                label = item_label(
                                    sel_contents->initial_items[ii]);
                            } else {
                                label = catalog_label(shown_items[ii]);
                            }
                            ImGui::BulletText("%s", label.c_str());
                        }
                        ImGui::PopID();
                    }
                    if (shown_items.empty()) {
                        ImGui::TextDisabled(staged ? "  (emptied)"
                                                   : "  (empty)");
                    }
                    if (contents_editable) {
                        if (ImGui::SmallButton("+ Add item")) {
                            s_picker_slot = int(shown_items.size());
                            s_picker_entity = sel_entity;
                            s_picker_filter[0] = 0;
                            open_picker = true;
                        }
                        if (staged) {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Revert")) {
                                LevelEdit::ClearChestContents(sel_entity);
                            }
                        }
                        if (staged) {
                            ImGui::TextDisabled("staged - Save to bake");
                        }
                    }
                    if (remove_idx >= 0 &&
                        size_t(remove_idx) < shown_items.size()) {
                        std::vector<uint32_t> next = shown_items;
                        next.erase(next.begin() + remove_idx);
                        LevelEdit::SetChestContents(sel_entity, next);
                    }
                    if (open_picker) {
                        ImGui::OpenPopup("##chest_item_picker");
                    }
                    if (ImGui::BeginPopup("##chest_item_picker")) {
                        if (s_picker_entity != sel_entity) {
                            ImGui::CloseCurrentPopup();
                        } else {
                            ImGui::SetNextItemWidth(260.0f);
                            ImGui::InputTextWithHint("##item_filter",
                                                     "search items...",
                                                     s_picker_filter,
                                                     sizeof(s_picker_filter));
                            std::string filter = s_picker_filter;
                            std::transform(filter.begin(), filter.end(),
                                           filter.begin(), ::tolower);
                            ImGui::BeginChild("##item_list",
                                              ImVec2(320.0f, 300.0f), true);
                            std::vector<int> rows;
                            rows.reserve(g_item_details.size());
                            for (int ci = 0;
                                 ci < int(g_item_details.size());
                                 ++ci) {
                                if (filter.empty()) {
                                    rows.push_back(ci);
                                    continue;
                                }
                                std::string low =
                                    g_item_details[size_t(ci)]
                                        .display_name;
                                std::transform(low.begin(), low.end(),
                                               low.begin(), ::tolower);
                                if (low.find(filter) !=
                                    std::string::npos) {
                                    rows.push_back(ci);
                                }
                            }
                            ImGuiListClipper clipper;
                            clipper.Begin(int(rows.size()));
                            while (clipper.Step()) {
                                for (int ri = clipper.DisplayStart;
                                     ri < clipper.DisplayEnd; ++ri) {
                                    const auto& c = g_item_details
                                        [size_t(rows[size_t(ri)])];
                                    const std::string& pl =
                                        c.display_name.empty()
                                            ? c.label
                                            : c.display_name;
                                    ImGui::PushID(int(c.record_hash));
                                    if (ImGui::Selectable(pl.c_str())) {
                                        std::vector<uint32_t> next =
                                            shown_items;
                                        if (size_t(s_picker_slot) <
                                            next.size()) {
                                            next[size_t(s_picker_slot)] =
                                                c.record_hash;
                                        } else {
                                            next.push_back(c.record_hash);
                                        }
                                        LevelEdit::SetChestContents(
                                            sel_entity, next);
                                        ImGui::CloseCurrentPopup();
                                    }
                                    ImGui::PopID();
                                }
                            }
                            ImGui::EndChild();
                        }
                        ImGui::EndPopup();
                    }
                }

                draw_container_loot_table_editor(sel_entity,
                                                 *sel_contents);
            } else if (sel_chest_addition >= 0) {
                auto pretty_tag2 = [](std::string tag, int money) {
                    for (const char* pfx : { "INV_ITEM_", "OBJECT_",
                                             "TEXT_" }) {
                        const size_t n = std::strlen(pfx);
                        if (tag.size() > n && tag.compare(0, n, pfx) == 0) {
                            tag = tag.substr(n);
                            break;
                        }
                    }
                    if (tag.size() > 5 &&
                        tag.compare(tag.size() - 5, 5, "_NAME") == 0) {
                        tag.resize(tag.size() - 5);
                    }
                    if (tag.find('_') != std::string::npos ||
                        std::none_of(tag.begin(), tag.end(),
                                     [](unsigned char c) {
                                         return std::islower(c);
                                     })) {
                        bool ws = true;
                        for (auto& c : tag) {
                            if (c == '_') {
                                c = ' ';
                                ws = true;
                            } else {
                                c = ws ? char(std::toupper((unsigned char)c))
                                       : char(std::tolower((unsigned char)c));
                                ws = false;
                            }
                        }
                    }
                    if (money >= 0) {
                        tag += " (" + std::to_string(money) + " gold)";
                    }
                    return tag;
                };
                auto catalog_label2 = [&](uint32_t record_hash) {
                    for (const auto& c : g_item_details) {
                        if (c.record_hash == record_hash &&
                            !c.display_name.empty()) {
                            return c.display_name;
                        }
                    }
                    for (const auto& c : g_level_item_catalog) {
                        if (c.record_hash == record_hash) {
                            return pretty_tag2(c.label, c.money);
                        }
                    }
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "0x%08X", record_hash);
                    return std::string(buf);
                };
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                                   LevelEdit::AdditionIsDigSpot(
                                       sel_chest_addition)
                                       ? "New dig spot (unsaved)"
                                       : "New container (unsaved)");
                std::vector<uint32_t> add_items;
                LevelEdit::GetAdditionChestItems(sel_chest_addition,
                                                 add_items);
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Contents:");
                const bool add_editable =
                    LevelEdit::Enabled() && !LevelEdit::Saving();
                static char s_add_filter[64] = {};
                bool open_add_picker = false;
                static int s_add_slot = -1;
                int remove_idx = -1;
                for (size_t ii = 0; ii < add_items.size(); ++ii) {
                    ImGui::PushID(int(ii) + 0x1000);
                    if (add_editable) {
                        if (ImGui::SmallButton("x")) remove_idx = int(ii);
                        ImGui::SameLine();
                        if (ImGui::Selectable(
                                catalog_label2(add_items[ii]).c_str(),
                                false,
                                ImGuiSelectableFlags_DontClosePopups)) {
                            s_add_slot = int(ii);
                            s_add_filter[0] = 0;
                            open_add_picker = true;
                        }
                    } else {
                        ImGui::BulletText(
                            "%s", catalog_label2(add_items[ii]).c_str());
                    }
                    ImGui::PopID();
                }
                if (add_editable) {
                    if (ImGui::SmallButton("+ Add item")) {
                        s_add_slot = int(add_items.size());
                        s_add_filter[0] = 0;
                        open_add_picker = true;
                    }

                    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                       "Random loot:");
                    const uint32_t cur_loot =
                        LevelEdit::GetAdditionLootTable(
                            sel_chest_addition);
                    std::string cur_label = "None";
                    if (cur_loot) {
                        char lb[32];
                        std::snprintf(lb, sizeof(lb), "table 0x%08X",
                                      cur_loot);
                        cur_label = lb;
                        for (const auto& kv : g_level_entity_contents) {
                            if (kv.second.potential_items_record ==
                                    cur_loot &&
                                !kv.second.entity_name.empty()) {
                                cur_label =
                                    "like " + kv.second.entity_name;
                                break;
                            }
                        }
                    }
                    ImGui::SetNextItemWidth(240.0f);
                    if (ImGui::BeginCombo("##rand_loot",
                                          cur_label.c_str())) {
                        if (ImGui::Selectable("None", cur_loot == 0)) {
                            LevelEdit::SetAdditionLootTable(
                                sel_chest_addition, 0);
                        }
                        std::unordered_set<uint32_t> seen_tables;
                        for (const auto& kv : g_level_entity_contents) {
                            const auto& ec = kv.second;
                            if (!ec.potential_items_record ||
                                ec.potential_items.empty()) {
                                continue;
                            }
                            if (!seen_tables
                                     .insert(ec.potential_items_record)
                                     .second) {
                                continue;
                            }
                            char lbl[128];
                            std::snprintf(
                                lbl, sizeof(lbl),
                                "%s (%zu entr%s)##%08X",
                                ec.entity_name.empty()
                                    ? "<unnamed>"
                                    : ec.entity_name.c_str(),
                                ec.potential_items.size(),
                                ec.potential_items.size() == 1 ? "y"
                                                               : "ies",
                                ec.potential_items_record);
                            if (ImGui::Selectable(
                                    lbl,
                                    cur_loot ==
                                        ec.potential_items_record)) {
                                LevelEdit::SetAdditionLootTable(
                                    sel_chest_addition,
                                    ec.potential_items_record);
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Give this container a weighted random loot "
                            "table borrowed from an existing container "
                            "in this level.");
                    }
                }
                if (remove_idx >= 0 &&
                    size_t(remove_idx) < add_items.size()) {
                    add_items.erase(add_items.begin() + remove_idx);
                    LevelEdit::SetAdditionChestItems(sel_chest_addition,
                                                     add_items);
                }
                if (open_add_picker) {
                    ImGui::OpenPopup("##add_chest_item_picker");
                }
                if (ImGui::BeginPopup("##add_chest_item_picker")) {
                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::InputTextWithHint("##add_item_filter",
                                             "search items...",
                                             s_add_filter,
                                             sizeof(s_add_filter));
                    std::string filter = s_add_filter;
                    std::transform(filter.begin(), filter.end(),
                                   filter.begin(), ::tolower);
                    ImGui::BeginChild("##add_item_list",
                                      ImVec2(320.0f, 300.0f), true);
                    std::vector<int> rows;
                    rows.reserve(g_item_details.size());
                    for (int ci = 0;
                         ci < int(g_item_details.size()); ++ci) {
                        if (filter.empty()) {
                            rows.push_back(ci);
                            continue;
                        }
                        std::string low =
                            g_item_details[size_t(ci)].display_name;
                        std::transform(low.begin(), low.end(),
                                       low.begin(), ::tolower);
                        if (low.find(filter) != std::string::npos) {
                            rows.push_back(ci);
                        }
                    }
                    ImGuiListClipper clipper;
                    clipper.Begin(int(rows.size()));
                    while (clipper.Step()) {
                        for (int ri = clipper.DisplayStart;
                             ri < clipper.DisplayEnd; ++ri) {
                            const auto& c = g_item_details
                                [size_t(rows[size_t(ri)])];
                            const std::string& pl =
                                c.display_name.empty() ? c.label
                                                       : c.display_name;
                            ImGui::PushID(int(c.record_hash));
                            if (ImGui::Selectable(pl.c_str())) {
                                if (size_t(s_add_slot) <
                                    add_items.size()) {
                                    add_items[size_t(s_add_slot)] =
                                        c.record_hash;
                                } else {
                                    add_items.push_back(c.record_hash);
                                }
                                LevelEdit::SetAdditionChestItems(
                                    sel_chest_addition, add_items);
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();
                    ImGui::EndPopup();
                }
            }
        }
        ImGui::End();
        }   
        if (dbg_sel_changed) DebugTrace::log("sel: overlay done");

        if (edit_active) {
            if (dbg_sel_changed) DebugTrace::log("gz: call");
            LevelGizmo::Result gz = LevelGizmo::DrawAndHandle(
                g_flycam, origin, region, sel_pos, true);
            static bool s_was_dragging = false;
            if (gz.dragging && !s_was_dragging) {
                LevelEdit::PushUndoSnapshot(collect_group_ids());
                DebugTrace::log("gizmo: drag begin");
            }
            s_was_dragging = gz.dragging;
            if (gz.moved) {
                if (gz.step[0] != 0.0f || gz.step[1] != 0.0f ||
                    gz.step[2] != 0.0f) {
                    apply_group_edit(kEditMove, gz.step);
                }
                if (gz.rot_step_deg[0] != 0.0f ||
                    gz.rot_step_deg[1] != 0.0f ||
                    gz.rot_step_deg[2] != 0.0f) {
                    apply_group_edit(kEditRotate, gz.rot_step_deg);
                }
            }
        } else {
            LevelGizmo::CancelDrag();
        }
        if (dbg_sel_changed) DebugTrace::log("sel: gizmo done");

        if (edit_active && !ImGui::GetIO().WantTextInput &&
            ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            DebugTrace::log("del: id=%u hash=%llu",
                            ::g_selected_level_pick_id,
                            (unsigned long long)::g_selected_level_hash);
            bool removed_spawn_point = false;
            const int marker_index = selected_level_spawn_marker_index();
            if (marker_index >= 0) {
                const LevelSpawnMarker& marker =
                    g_level_spawn_markers[size_t(marker_index)];
                if (marker.kind == 2) {
                    for (const LevelSpawnMarker& owner :
                         g_level_spawn_markers) {
                        if (owner.kind != 1 ||
                            owner.spawn_points_record == 0 ||
                            std::find(owner.spawn_point_entities.begin(),
                                      owner.spawn_point_entities.end(),
                                      marker.entity_hash) ==
                                owner.spawn_point_entities.end()) {
                            continue;
                        }
                        LevelEdit::RemoveSpawnPointFromExisting(
                            owner.entity_hash, owner.spawn_points_record,
                            marker.entity_hash);
                        OutputLog::info(
                            "level edit: spawn point deletion queued");
                        removed_spawn_point = true;
                        break;
                    }
                }
            }
            if (!removed_spawn_point) {
                LevelEdit::PushUndoSnapshot(collect_group_ids());
                apply_group_edit(kEditDelete, nullptr);
            }
            ::g_selected_level_mesh_idx = -1;
            ::g_selected_level_pick_id = 0;
            ::g_selected_level_hash = 0;
            LevelGizmo::CancelDrag();
        }
    }

    if (g_mp.no_tilt && LevelEdit::Enabled() && hovered &&
        !g_flycam.is_looking && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
            LevelGizmo::SetMode(LevelGizmo::Mode::Translate);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
            LevelGizmo::SetMode(LevelGizmo::Mode::Rotate);
        }
    }

    if (LevelEdit::Enabled() && !ImGui::GetIO().WantTextInput &&
        (ImGui::GetIO().KeyAlt || ImGui::GetIO().KeyCtrl) &&
        ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (!LevelEdit::Undo()) {
            OutputLog::info("level edit: nothing to undo");
        }
    }

    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (S.bone_rotate_mode) {
            cancel_rotate();
        } else if (g_mp.no_tilt && ::g_selected_level_mesh_idx >= 0) {
            ::g_selected_level_mesh_idx = -1;
            ::g_selected_level_pick_id = 0;
            ::g_selected_level_hash = 0;
            LevelGizmo::CancelDrag();
        } else if (g_mp.no_tilt) {

        } else {
            if (S.content_tabs_visible && ContentTabs::HasTabs()) {
                ContentTabs::CloseActive();
            } else {
                MP_Release(g_mp);
                g_mp.has_model = false;
                g_mp_initialized = false;
                S.show_model_preview = false;
                S.model_preview_open = false;
                S.selected_bone = -1;
            }
        }
    }

    dl->AddRectFilled(ImVec2(origin.x + 6, origin.y + 6),
                      ImVec2(origin.x + 196, origin.y + 70),
                      IM_COL32(20, 22, 28, 200), 4.0f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 12));
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Controls");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 30));
    ImGui::TextDisabled("L-Drag  rotate");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 46));
    ImGui::TextDisabled("Wheel  zoom  /  ESC  close");

    float next_overlay_y = origin.y + 76.0f;

    
    
    const bool custom_level_clean_viewport = details_panel_docked();

    bool has_skeleton = g_mp.has_model && g_mp.bone_count > 0 &&
                        !custom_level_clean_viewport;
    if (has_skeleton) {

        static float s_skel_alpha    = 0.30f;

        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        const ImVec2 win_pos (origin.x + 6, origin.y + 76);
        const ImVec2 win_size(190, 0);
        ImGui::SetNextWindowPos(win_pos);
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Always);

        ImGui::SetNextWindowBgAlpha(s_skel_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_skel_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##skeleton_overlay", nullptr, fl)) {

            bool hovering = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                ImGuiHoveredFlags_ChildWindows);
            float target = hovering ? kHoverAlpha : kIdleAlpha;

            s_skel_alpha += (target - s_skel_alpha) * 0.18f;
            if (std::fabs(s_skel_alpha - target) < 0.005f) s_skel_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Skeleton");
            ImGui::Checkbox("Show", &::g_skel_overlay_show);
            if (S.selected_bone >= 0 && S.selected_bone < (int)S.mdl_info.Bones.size()) {

                ImGui::TextDisabled(S.bone_rotate_mode
                                        ? "RMB cancel  /  LMB confirm"
                                        : "R: rotate selected");
            }

            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            next_overlay_y = wp.y + ws.y + 6.0f;
        }
        ImGui::End();
        ImGui::PopStyleVar();

        if (!::g_skel_overlay_show) {
            S.selected_bone     = -1;
            S.bone_rotate_mode  = false;
        }

        if (::g_skel_overlay_show) {
            draw_skeleton_overlay(origin, region);
        }
    } else {

        ::g_skel_overlay_show = false;
        S.selected_bone       = -1;
        S.bone_rotate_mode    = false;
    }

    if (g_mp.has_model && !custom_level_clean_viewport) {
        static float s_wire_alpha = 0.30f;
        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        const ImVec2 wire_pos (origin.x + 6, next_overlay_y);
        const ImVec2 wire_size(190, 0);
        ImGui::SetNextWindowPos(wire_pos);
        ImGui::SetNextWindowSize(wire_size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(s_wire_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_wire_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##wireframe_overlay", nullptr, fl)) {
            bool hovering = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                ImGuiHoveredFlags_ChildWindows);
            float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_wire_alpha += (target - s_wire_alpha) * 0.18f;
            if (std::fabs(s_wire_alpha - target) < 0.005f) s_wire_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Wireframe");
            ImGui::Checkbox("Show", &g_mp.wireframe);
            if (g_mp.no_tilt && (!g_level_spawn_markers.empty() ||
                                 !g_level_entity_text.empty())) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Entities");
                ImGui::Checkbox("Generators / spawn points",
                                &S.show_spawn_markers);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Creature generators (red) and their spawn "
                        "points (orange). In edit mode: click to "
                        "select, Right-click ground to add a "
                        "new generator.");
                ImGui::Checkbox("NPC / creature markers",
                                &S.show_ent_npcs);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Show the labelled diamond at each authored NPC or "
                        "creature position. Markers are not selectable.");
                }
                if (ImGui::Checkbox("Entity models",
                                    &S.show_entity_models) &&
                    !S.show_entity_models &&
                    ::g_selected_level_mesh_idx >= 0 &&
                    ::g_selected_level_mesh_idx <
                        static_cast<int>(g_mp.meshes.size()) &&
                    g_mp.meshes[static_cast<size_t>(
                        ::g_selected_level_mesh_idx)].is_entity_model) {
                    ::g_selected_level_mesh_idx = -1;
                    ::g_selected_level_pick_id = 0;
                    ::g_selected_level_hash = 0;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Render the resolved full NPC and creature models. "
                        "Select and move entities by clicking their model.");
                }
                ImGui::Checkbox("Containers", &S.show_containers);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "All inventory-bearing objects (purple), including "
                        "chests, cupboards, registers, and barrels. "
                        "Dig spots remain under their own "
                        "checkbox.");
                }
                ImGui::Checkbox("Dig spots", &S.show_dig_spots);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Dig spots (blue). Click one to inspect its loot, "
                        "search radius, priority, and respawn chance when "
                        "those values are present.");
                }
                ImGui::Checkbox("Text objects", &S.show_ent_text);
            }
            if (S.dev_mode) {
                ImGui::Checkbox("Terrain: engine blend",
                                &S.terrain_landscape_blend);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Dev-only: engine-reconciled LANDSCAPEMATERIAL terrain "
                        "blend (per-material tiling, 16/dim). A/B vs the current "
                        "shared-scale shader.");
            }
            if (g_mp.has_sky_theme) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Time");

                const bool has_cycle =
                    g_mp.has_day_night_cycle &&
                    g_mp.day_night_keyframes.size() >= 2;
                bool auto_time =
                    has_cycle && !g_mp.time_of_day_override;
                ImGui::BeginDisabled(!has_cycle);
                if (ImGui::Checkbox("Auto", &auto_time)) {
                    if (auto_time) {
                        g_mp.time_of_day_override = false;
                    } else {
                        g_mp.time_of_day_override = true;
                        g_mp.time_of_day_override_value =
                            g_mp.current_time_of_day;
                    }
                }
                ImGui::EndDisabled();

                float hour =
                    (g_mp.time_of_day_override
                         ? g_mp.time_of_day_override_value
                         : g_mp.current_time_of_day) * 24.0f;
                hour = std::clamp(hour, 0.0f, 24.0f);
                ImGui::SetNextItemWidth(150.0f);
                if (ImGui::SliderFloat("##time_of_day", &hour,
                                       0.0f, 24.0f, "%.2f h",
                                       ImGuiSliderFlags_AlwaysClamp)) {
                    g_mp.time_of_day_override = true;
                    g_mp.time_of_day_override_value =
                        std::clamp(hour / 24.0f, 0.0f, 1.0f);
                }
            }
            if (S.terrain_mode || g_mp.has_sky_theme ||
                g_mp.has_weather_theme ||
                g_mp.has_fog_theme) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Environment");
                if (S.terrain_mode) {
                    ImGui::Checkbox("Adjacent terrain",
                                    &S.show_adjacent_terrain);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Show the neighbouring levels' heightfields "
                            "around this level (textured with their baked "
                            "ground).");
                    }
                }
                if (g_mp.has_sky_theme) {
                    ImGui::Checkbox("Sky", &g_mp.show_sky);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "Procedural sky, sun/moon and cloud layers "
                            "from the level's environment theme.");
                }
                if (g_mp.has_weather_theme) {
                    const bool theme_has_rain =
                        g_mp.weather_precip[0] > 0.0001f &&
                        g_mp.weather_precip[1] > 0.0001f;
                    const bool theme_has_snow =
                        g_mp.weather_precip[2] > 0.0001f &&
                        g_mp.weather_precip[3] > 0.0001f;
                    ImGui::Checkbox("Weather", &g_mp.show_weather);
                    if (ImGui::IsItemHovered()) {
                        if (theme_has_rain || theme_has_snow) {
                            char buf[160];
                            std::snprintf(
                                buf, sizeof(buf),
                                "Theme precipitation:%s%s\n"
                                "rain density %.2f size %.2f\n"
                                "snow fallspeed %.2f size %.2f",
                                theme_has_rain ? " rain" : "",
                                theme_has_snow ? " snow" : "",
                                g_mp.weather_precip[0],
                                g_mp.weather_precip[1],
                                g_mp.weather_precip[2],
                                g_mp.weather_precip[3]);
                            ImGui::SetTooltip("%s", buf);
                        } else {
                            ImGui::SetTooltip(
                                "This level's theme has no rain or "
                                "snow at the current time of day.");
                        }
                    }
                }
                if (g_mp.has_weather_theme || g_mp.has_fog_theme) {
                    ImGui::Checkbox("Mist / fog", &g_mp.show_mist);
                    if (ImGui::IsItemHovered()) {
                        if (g_mp.weather_mist_strength > 0.0001f ||
                            g_mp.has_fog_theme) {
                            char buf[120];
                            std::snprintf(
                                buf, sizeof(buf),
                                "Theme fogging + ground mist "
                                "(GroundMist strength %.2f).",
                                g_mp.weather_mist_strength);
                            ImGui::SetTooltip("%s", buf);
                        } else {
                            ImGui::SetTooltip(
                                "This level's theme has no fogging or "
                                "ground mist parameters.");
                        }
                    }
                }
            }

            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            next_overlay_y = wp.y + ws.y + 6.0f;
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    if (g_mp.has_model && g_mp.lod_count > 1 &&
        !details_panel_docked()) {
        static float s_lod_alpha = 0.30f;
        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        const ImVec2 lod_pos (origin.x + 6, next_overlay_y);
        const ImVec2 lod_size(190, 0);
        ImGui::SetNextWindowPos(lod_pos);
        ImGui::SetNextWindowSize(lod_size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(s_lod_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_lod_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##lod_overlay", nullptr, fl)) {
            bool hovering = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                ImGuiHoveredFlags_ChildWindows);
            float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_lod_alpha += (target - s_lod_alpha) * 0.18f;
            if (std::fabs(s_lod_alpha - target) < 0.005f) s_lod_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "LOD");

            const int lod_count = (int)g_mp.lod_count;
            int current = g_mp.selected_lod;
            if (current < -1 || current >= lod_count) current = 0;

            if (ImGui::RadioButton("All", current == -1)) {
                g_mp.selected_lod = -1;
            }
            for (int i = 0; i < lod_count; ++i) {
                ImGui::SameLine();
                char lbl[16];
                std::snprintf(lbl, sizeof(lbl), "%d", i);
                if (ImGui::RadioButton(lbl, current == i)) {
                    g_mp.selected_lod = i;
                }
            }

            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            next_overlay_y = wp.y + ws.y + 6.0f;
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    if (g_mp.has_model && !g_mp.meshes.empty() &&
        !custom_level_clean_viewport) {
        static float s_mat_alpha = 0.30f;
        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        const float kMatW = 296.0f;
        float max_h = std::max(160.0f,
                               region.y - (next_overlay_y - origin.y) - 20.0f);

        ImGui::SetNextWindowPos(ImVec2(origin.x + 6, next_overlay_y));
        ImGui::SetNextWindowSizeConstraints(ImVec2(kMatW, 0.0f),
                                            ImVec2(kMatW, max_h));
        ImGui::SetNextWindowBgAlpha(s_mat_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_mat_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##materials_overlay", nullptr, fl)) {
            bool hovering = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                ImGuiHoveredFlags_ChildWindows);
            float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_mat_alpha += (target - s_mat_alpha) * 0.18f;
            if (std::fabs(s_mat_alpha - target) < 0.005f) s_mat_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Materials");
            ImGui::Separator();

            const ImVec2 thumb_size(48, 48);

            if (!g_mp.no_tilt) for (size_t mi = 0; mi < g_mp.meshes.size(); ++mi) {
                auto& mesh = g_mp.meshes[mi];

                if (g_mp.selected_lod >= 0 &&
                    mesh.lod_index != (uint32_t)g_mp.selected_lod) {
                    continue;
                }

                ImGui::PushID((int)mi);

                ImGui::TextUnformatted(mesh.name.c_str());

                bool h   = (::g_highlight_mesh_idx == (int)mi);
                bool iso = (::g_isolate_mesh_idx   == (int)mi);

                if (ImGui::Checkbox("Highlight", &h)) {
                    if (h) {
                        ::g_highlight_mesh_idx = (int)mi;
                        ::g_isolate_mesh_idx   = -1;
                    } else if (::g_highlight_mesh_idx == (int)mi) {
                        ::g_highlight_mesh_idx = -1;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Isolate", &iso)) {
                    if (iso) {
                        ::g_isolate_mesh_idx   = (int)mi;
                        ::g_highlight_mesh_idx = -1;
                    } else if (::g_isolate_mesh_idx == (int)mi) {
                        ::g_isolate_mesh_idx = -1;
                    }
                }

                struct ThumbSpec {
                    const char*               slot_id;
                    ID3D11ShaderResourceView* srv;
                    const std::string*        name;
                    bool*                     visible;
                };
                ThumbSpec thumbs[5] = {
                    {"diffuse",  mesh.srv_diffuse,  &mesh.diffuse_tex_name,  &mesh.diffuse_visible},
                    {"normal",   mesh.srv_normal,   &mesh.normal_tex_name,   &mesh.normal_visible},
                    {"specular", mesh.srv_specular, &mesh.specular_tex_name, &mesh.specular_visible},
                    {"metallic", mesh.srv_metallic, &mesh.metallic_tex_name, &mesh.metallic_visible},
                    {"extra",    mesh.srv_extra,    &mesh.extra_tex_name,    &mesh.extra_visible},
                };
                bool any_thumb = false;
                for (int ti = 0; ti < 5; ++ti) {
                    const ThumbSpec& t = thumbs[ti];
                    if (!t.srv || t.srv == g_mp.default_srv) continue;
                    if (t.name->empty()) continue;
                    if (any_thumb) ImGui::SameLine();
                    any_thumb = true;
                    ImGui::PushID(t.slot_id);

                    ImGui::BeginGroup();

                    ImVec4 tint = (*t.visible) ? ImVec4(1, 1, 1, 1)
                                               : ImVec4(0.45f, 0.45f, 0.45f, 1);
                    if (ImGui::ImageButton("##t",
                                           (ImTextureID)t.srv,
                                           thumb_size,
                                           ImVec2(0, 0), ImVec2(1, 1),
                                           ImVec4(0, 0, 0, 0), tint)) {
                        ::g_tex_popout_srv      = t.srv;
                        ::g_tex_popout_name     = *t.name;
                        ::g_tex_popout_open     = true;

                        ::g_tex_popout_mesh_idx = (int)mi;
                    }

                    if (ImGui::BeginPopupContextItem()) {
                        const auto* terrain_tex =
                            TerrainTextureRegistry::Find(*t.name);
                        if (terrain_tex) {
                            tex_export_menu_rgba(*t.name,
                                                 terrain_tex->rgba,
                                                 terrain_tex->width,
                                                 terrain_tex->height);
                        } else {
                            const std::string& preferred_bnk =
                                (S.selected_nested_index != -1 &&
                                 !S.selected_nested_temp_path.empty())
                                    ? S.selected_nested_temp_path
                                    : S.selected_bnk;
                            tex_export_menu_named(*t.name, *t.name,
                                                  preferred_bnk, 0);
                        }
                        ImGui::EndPopup();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s\n[%s]",
                                          t.name->c_str(), t.slot_id);
                    }

                    ImGui::Checkbox("##vis", t.visible);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Show %s in render", t.slot_id);
                    }
                    ImGui::EndGroup();
                    ImGui::PopID();
                }
                if (!any_thumb) {
                    ImGui::TextDisabled("(no textures)");
                }

                ImGui::Separator();
                ImGui::PopID();
            }

            if (g_mp.no_tilt && ::g_selected_level_mesh_idx >= 0 &&
                ::g_selected_level_mesh_idx < (int)g_mp.meshes.size())
            {
                const int mi   = ::g_selected_level_mesh_idx;
                auto&     mesh = g_mp.meshes[mi];
                const std::string selected_model_key =
                    level_model_key_from_mesh_name(mesh.name);
                const std::string selected_model_name =
                    clean_level_model_name(mesh.name);

                ImGui::TextWrapped("%s", selected_model_name.c_str());
                ImGui::Separator();

                ImGui::PushID(0x20000 + mi);

                struct ThumbSpec {
                    const char*               slot_id;
                    ID3D11ShaderResourceView* srv;
                    const std::string*        name;
                    int                       mesh_idx;
                    std::vector<bool*>        visible;
                };
                struct MaterialRow {
                    std::string key;
                    std::string label;
                    std::array<ThumbSpec, 5> thumbs;
                };

                auto make_row_key = [](const MPPerMesh& m,
                                       const std::string& label) {
                    return label + "|" +
                           m.diffuse_tex_name + "|" +
                           m.normal_tex_name + "|" +
                           m.specular_tex_name + "|" +
                           m.metallic_tex_name + "|" +
                           m.extra_tex_name;
                };

                std::vector<MaterialRow> rows;
                auto append_row_mesh =
                    [&](MPPerMesh& related, int related_idx) {
                        const std::string label =
                            clean_level_material_name(related.name);
                        const std::string key = make_row_key(related, label);
                        MaterialRow* row = nullptr;
                        for (auto& existing : rows) {
                            if (existing.key == key) {
                                row = &existing;
                                break;
                            }
                        }
                        if (!row) {
                            MaterialRow fresh;
                            fresh.key = key;
                            fresh.label = label;
                            fresh.thumbs = {{
                                {"diffuse",  related.srv_diffuse,
                                 &related.diffuse_tex_name, related_idx, {}},
                                {"normal",   related.srv_normal,
                                 &related.normal_tex_name, related_idx, {}},
                                {"specular", related.srv_specular,
                                 &related.specular_tex_name, related_idx, {}},
                                {"metallic", related.srv_metallic,
                                 &related.metallic_tex_name, related_idx, {}},
                                {"extra",    related.srv_extra,
                                 &related.extra_tex_name, related_idx, {}},
                            }};
                            rows.push_back(std::move(fresh));
                            row = &rows.back();
                        }

                        row->thumbs[0].visible.push_back(
                            &related.diffuse_visible);
                        row->thumbs[1].visible.push_back(
                            &related.normal_visible);
                        row->thumbs[2].visible.push_back(
                            &related.specular_visible);
                        row->thumbs[3].visible.push_back(
                            &related.metallic_visible);
                        row->thumbs[4].visible.push_back(
                            &related.extra_visible);
                    };

                for (size_t ri = 0; ri < g_mp.meshes.size(); ++ri) {
                    auto& related = g_mp.meshes[ri];
                    if (level_model_key_from_mesh_name(related.name) !=
                        selected_model_key)
                    {
                        continue;
                    }
                    append_row_mesh(related, (int)ri);
                }

                if (rows.empty()) {
                    ImGui::TextDisabled("(no materials)");
                }
                for (size_t row_i = 0; row_i < rows.size(); ++row_i) {
                    MaterialRow& row = rows[row_i];
                    ImGui::PushID((int)row_i);
                    ImGui::TextUnformatted(row.label.c_str());

                    bool any_thumb = false;
                    for (size_t ti = 0; ti < row.thumbs.size(); ++ti) {
                        ThumbSpec& t = row.thumbs[ti];
                        if (!t.srv || t.srv == g_mp.default_srv) continue;
                        if (!t.name || t.name->empty()) continue;
                        if (any_thumb) ImGui::SameLine();
                        any_thumb = true;
                        ImGui::PushID((int)ti);
                        ImGui::BeginGroup();

                        bool visible = true;
                        for (bool* v : t.visible) {
                            if (v && !*v) {
                                visible = false;
                                break;
                            }
                        }

                        ImVec4 tint = visible
                            ? ImVec4(1, 1, 1, 1)
                            : ImVec4(0.45f, 0.45f, 0.45f, 1);
                        if (ImGui::ImageButton("##t",
                                               (ImTextureID)t.srv,
                                               thumb_size,
                                               ImVec2(0, 0), ImVec2(1, 1),
                                               ImVec4(0, 0, 0, 0), tint)) {
                            ::g_tex_popout_srv      = t.srv;
                            ::g_tex_popout_name     = *t.name;
                            ::g_tex_popout_open     = true;
                            ::g_tex_popout_mesh_idx = t.mesh_idx;
                        }
                        if (ImGui::BeginPopupContextItem()) {
                            const auto* terrain_tex =
                                TerrainTextureRegistry::Find(*t.name);
                            if (terrain_tex) {
                                tex_export_menu_rgba(*t.name,
                                                     terrain_tex->rgba,
                                                     terrain_tex->width,
                                                     terrain_tex->height);
                            } else {
                                const std::string& preferred_bnk =
                                    (S.selected_nested_index != -1 &&
                                     !S.selected_nested_temp_path.empty())
                                        ? S.selected_nested_temp_path
                                        : S.selected_bnk;
                                tex_export_menu_named(*t.name, *t.name,
                                                      preferred_bnk, 0);
                            }
                            ImGui::EndPopup();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s\n[%s]",
                                              t.name->c_str(), t.slot_id);
                        }
                        bool checkbox_visible = visible;
                        if (ImGui::Checkbox("##vis", &checkbox_visible)) {
                            for (bool* v : t.visible) {
                                if (v) *v = checkbox_visible;
                            }
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Show %s in render", t.slot_id);
                        }
                        ImGui::EndGroup();
                        ImGui::PopID();
                    }
                    if (!any_thumb) {
                        ImGui::TextDisabled("(no textures)");
                    }
                    ImGui::Separator();
                    ImGui::PopID();
                }
                ImGui::PopID();
            } else if (g_mp.no_tilt) {
                const auto& lod = EhfLodThumbnails::Get();
                if (!lod.empty()) {
                    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f),
                        ".ehf LOD palette");
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%zu materials)", lod.size());
                    ImGui::Separator();

                    auto basename = [](const std::string& s) -> std::string {
                        if (s.empty()) return {};
                        size_t pos = s.find_last_of("/\\");
                        return (pos == std::string::npos)
                            ? s : s.substr(pos + 1);
                    };

                    auto thumb_or_placeholder =
                        [&](ID3D11ShaderResourceView* srv,
                            const std::string& path,
                            const char* slot_tag,
                            int slot_idx)
                    {
                        const ImVec2 sz(48, 48);
                        ImGui::PushID(slot_idx);
                        if (srv) {
                            if (ImGui::ImageButton("##t",
                                (ImTextureID)srv, sz,
                                ImVec2(0, 0), ImVec2(1, 1),
                                ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 1)))
                            {
                                ::g_tex_popout_srv      = srv;
                                ::g_tex_popout_name     = path;
                                ::g_tex_popout_open     = true;
                                ::g_tex_popout_mesh_idx = -1;
                            }
                            if (ImGui::BeginPopupContextItem()) {
                                const std::string& preferred_bnk =
                                    (S.selected_nested_index != -1 &&
                                     !S.selected_nested_temp_path.empty())
                                        ? S.selected_nested_temp_path
                                        : S.selected_bnk;
                                tex_export_menu_named(path, path,
                                                      preferred_bnk, 0);
                                ImGui::EndPopup();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s\n[%s]",
                                                  path.c_str(), slot_tag);
                            }
                        } else {
                            ImGui::Dummy(sz);
                            if (!path.empty() && ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("decode failed: %s\n[%s]",
                                                  path.c_str(), slot_tag);
                            }
                        }
                        ImGui::PopID();
                    };

                    for (size_t i = 0; i < lod.size(); ++i) {
                        const auto& e = lod[i];
                        ImGui::PushID(int(0x10000 + i));

                        const std::string title = "[" + std::to_string(i)
                            + "] " + basename(e.base_diffuse_path);
                        ImGui::TextUnformatted(title.c_str());

                        thumb_or_placeholder(e.srv_base_diffuse,
                                             e.base_diffuse_path,
                                             "base diffuse", 0);
                        ImGui::SameLine();
                        thumb_or_placeholder(e.srv_base_normal,
                                             e.base_normal_path,
                                             "base normal", 1);
                        ImGui::SameLine();
                        thumb_or_placeholder(e.srv_detail_diffuse,
                                             e.detail_diffuse_path,
                                             "detail diffuse", 2);
                        ImGui::SameLine();
                        thumb_or_placeholder(e.srv_detail_normal,
                                             e.detail_normal_path,
                                             "detail normal", 3);

                        ImGui::Separator();
                        ImGui::PopID();
                    }
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    } else {

        ::g_highlight_mesh_idx       = -1;
        ::g_isolate_mesh_idx         = -1;
        ::g_selected_level_mesh_idx  = -1;
        ::g_selected_level_pick_id   = 0;
        ::g_tex_popout_open          = false;
        ::g_tex_popout_srv           = nullptr;
        ::g_tex_popout_name.clear();
        ::g_tex_popout_mesh_idx      = -1;
    }

    const bool sculpt_mode_active =
        details_panel_docked() && g_mp.has_model && g_mp.no_tilt &&
        LandscapePanel::InSculptMode();
    const bool paint_mode_active =
        details_panel_docked() && g_mp.has_model && g_mp.no_tilt &&
        LandscapePanel::InPaintMode() && TerrainPaint::Active();
    bool paint_dab_applied = false;
    if (sculpt_mode_active || paint_mode_active) {
        enum TerrainTool {
            TT_NONE = 0,
            TT_RAISE,
            TT_LOWER,
            TT_SMOOTH,
            TT_FLATTEN,
            TT_NOISE,
        };
        auto upload_after_edit = [&]() {
            if (!g_mp.meshes.empty()) {
                TerrainEdit::ApplyToGpu(device, &g_mp.meshes[0]);
            }
        };

        
        
        int   eff_tool     = TT_NONE;
        float eff_size     = LandscapePanel::BrushSize();
        float eff_strength = LandscapePanel::ToolStrength();
        float eff_falloff  = LandscapePanel::BrushFalloff();
        if (sculpt_mode_active) {
            const bool lower_mod = ImGui::GetIO().KeyShift;
            switch (LandscapePanel::SculptTool()) {
                case 0: eff_tool = lower_mod ? TT_LOWER : TT_RAISE; break;
                case 1: eff_tool = TT_SMOOTH; break;
                case 2: eff_tool = TT_FLATTEN; break;
                case 3: eff_tool = TT_NOISE; break;
                default: eff_tool = TT_NONE; break;
            }
            eff_size = LandscapePanel::BrushSize();
            const float str01 = LandscapePanel::ToolStrength();
            eff_strength =
                (eff_tool == TT_SMOOTH || eff_tool == TT_FLATTEN)
                    ? str01
                    : str01 * 0.35f;
            eff_falloff = LandscapePanel::BrushFalloff();
        } else if (paint_mode_active) {
            eff_tool = TT_RAISE;   
            eff_size = LandscapePanel::BrushSize();
            eff_strength = LandscapePanel::ToolStrength();
            eff_falloff = LandscapePanel::BrushFalloff();
        }

        if (TerrainEdit::IsLoaded() && eff_tool != TT_NONE) {
            ImVec2 mp_pos  = ImGui::GetIO().MousePos;
            const bool over_view =
                mp_pos.x >= origin.x   && mp_pos.x < origin.x + region.x &&
                mp_pos.y >= origin.y   && mp_pos.y < origin.y + region.y;
            
            
            
            
            const bool imgui_captured =
                !hovered ||
                ImGui::IsPopupOpen(nullptr,
                                   ImGuiPopupFlags_AnyPopupId |
                                       ImGuiPopupFlags_AnyPopupLevel);

            if (over_view && g_mp.width > 0 && g_mp.height > 0) {
                using namespace DirectX;

                const float cy = cosf(g_flycam.yaw);
                const float sy = sinf(g_flycam.yaw);
                const float cp = cosf(g_flycam.pitch);
                const float sp = sinf(g_flycam.pitch);
                const float forward[3] = { sy * cp, sp, cy * cp };
                XMVECTOR eye = XMVectorSet(g_flycam.pos[0],
                    g_flycam.pos[1], g_flycam.pos[2], 1);
                XMVECTOR at  = XMVectorSet(g_flycam.pos[0] + forward[0],
                    g_flycam.pos[1] + forward[1],
                    g_flycam.pos[2] + forward[2], 1);
                XMVECTOR up  = XMVectorSet(0, 1, 0, 0);
                XMMATRIX V = XMMatrixLookAtLH(eye, at, up);
                const float fov = XMConvertToRadians(60.0f);
                const float aspect = (float)g_mp.width / (float)g_mp.height;
                const float far_plane = g_mp.radius * 100.0f;
                XMMATRIX P = XMMatrixPerspectiveFovLH(fov, aspect,
                                                     0.05f, far_plane);
                XMMATRIX VP = V * P;
                XMVECTOR det;
                XMMATRIX inv_VP = XMMatrixInverse(&det, VP);

                const float u = (mp_pos.x - origin.x) / region.x;
                const float v = (mp_pos.y - origin.y) / region.y;
                const float ndc_x =  u * 2.f - 1.f;
                const float ndc_y =  1.f - v * 2.f;

                XMVECTOR near_pt = XMVector4Transform(
                    XMVectorSet(ndc_x, ndc_y, 0.f, 1.f), inv_VP);
                XMVECTOR far_pt  = XMVector4Transform(
                    XMVectorSet(ndc_x, ndc_y, 1.f, 1.f), inv_VP);
                near_pt = XMVectorScale(near_pt,
                    1.f / XMVectorGetW(near_pt));
                far_pt  = XMVectorScale(far_pt,
                    1.f / XMVectorGetW(far_pt));
                const float ox = XMVectorGetX(near_pt);
                const float oy = XMVectorGetY(near_pt);
                const float oz = XMVectorGetZ(near_pt);
                const float dx = XMVectorGetX(far_pt) - ox;
                const float dy = XMVectorGetY(far_pt) - oy;
                const float dz = XMVectorGetZ(far_pt) - oz;

                float hx, hy, hz;
                if (TerrainEdit::Raycast(ox, oy, oz, dx, dy, dz,
                                         hx, hy, hz))
                {
                    const int kSeg = 48;
                    ImDrawList* dlay = ImGui::GetForegroundDrawList();
                    auto draw_terrain_ring = [&](float ring_radius,
                                                 ImU32 col,
                                                 float thickness) {
                        if (ring_radius <= 0.01f) return;
                        ImVec2 last_screen{};
                        bool last_valid = false;
                        for (int i = 0; i <= kSeg; ++i) {
                            const float ang =
                                (float)i / (float)kSeg * 6.2831853f;
                            const float wx =
                                hx + cosf(ang) * ring_radius;
                            const float wz =
                                hz + sinf(ang) * ring_radius;
                            const float wy =
                                TerrainEdit::SampleHeightAtWorldXZ(wx, wz);
                            XMVECTOR wpt = XMVectorSet(wx, wy, wz, 1.f);
                            XMVECTOR cs  = XMVector4Transform(wpt, VP);
                            const float ws = XMVectorGetW(cs);
                            if (ws <= 0.f) { last_valid = false; continue; }
                            const float nx = XMVectorGetX(cs) / ws;
                            const float ny = XMVectorGetY(cs) / ws;
                            const float sx = origin.x +
                                (nx * 0.5f + 0.5f) * region.x;
                            const float sy = origin.y +
                                (1.f - (ny * 0.5f + 0.5f)) * region.y;
                            const ImVec2 sc(sx, sy);
                            if (last_valid) {
                                dlay->AddLine(last_screen, sc, col,
                                              thickness);
                            }
                            last_screen = sc;
                            last_valid = true;
                        }
                    };
                    const float radius = eff_size;
                    draw_terrain_ring(radius,
                                      IM_COL32(255, 215, 0, 220), 1.5f);
                    
                    
                    if (eff_falloff > 0.02f && eff_falloff < 0.98f) {
                        draw_terrain_ring(radius * (1.0f - eff_falloff),
                                          IM_COL32(255, 235, 130, 140),
                                          1.0f);
                    }
                    XMVECTOR cpt = XMVector4Transform(
                        XMVectorSet(hx, hy, hz, 1.f), VP);
                    const float cw = XMVectorGetW(cpt);
                    if (cw > 0.f) {
                        const float cnx = XMVectorGetX(cpt) / cw;
                        const float cny = XMVectorGetY(cpt) / cw;
                        const float csx = origin.x
                            + (cnx * 0.5f + 0.5f) * region.x;
                        const float csy = origin.y
                            + (1.f - (cny * 0.5f + 0.5f)) * region.y;
                        dlay->AddCircleFilled(ImVec2(csx, csy), 3.f,
                            IM_COL32(255, 215, 0, 255));
                    }

                    if (paint_mode_active && !imgui_captured &&
                        ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    {
                        
                        TerrainPaint::ApplyBrush(
                            hx, hz, eff_size, eff_strength, eff_falloff,
                            ImGui::GetIO().KeyShift);
                        paint_dab_applied = true;
                    }
                    else if (!imgui_captured &&
                        ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    {
                        TerrainEdit::BrushTool bt =
                            TerrainEdit::BrushTool::None;
                        switch (eff_tool) {
                            case TT_RAISE:   bt = TerrainEdit::BrushTool::Raise; break;
                            case TT_LOWER:   bt = TerrainEdit::BrushTool::Lower; break;
                            case TT_SMOOTH:  bt = TerrainEdit::BrushTool::Smooth; break;
                            case TT_FLATTEN: bt = TerrainEdit::BrushTool::Flatten; break;
                            case TT_NOISE:   bt = TerrainEdit::BrushTool::Noise; break;
                            default: break;
                        }
                        
                        
                        
                        static float s_flatten_target = 0.0f;
                        if (ImGui::IsMouseClicked(
                                ImGuiMouseButton_Left)) {
                            s_flatten_target =
                                TerrainEdit::SampleHeightAtWorldXZ(hx,
                                                                   hz);
                        }
                        const float target_h =
                            (eff_tool == TT_FLATTEN) ? s_flatten_target
                                                     : 0.f;
                        TerrainEdit::ApplyBrush(bt, hx, hz,
                            eff_size, eff_strength, target_h,
                            eff_falloff);
                        upload_after_edit();
                    }
                }
            }
        }
    }

    if (!paint_mode_active || !paint_dab_applied) {
        TerrainPaint::EndStroke();
    }


    if (g_mp.has_model && g_mp.bone_count > 0 && !S.anim_clips.empty() &&
        !S.item_model_active && !S.entity_model_active) {
        static float s_anim_alpha = 0.30f;
        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        const float kAnimW   = 280.0f;
        const float kAnimPad = 6.0f;

        const float anim_h = std::max(160.0f, region.y - 2 * kAnimPad);
        const ImVec2 anim_pos(origin.x + region.x - kAnimW - kAnimPad,
                              origin.y + kAnimPad);
        const ImVec2 anim_size(kAnimW, anim_h);

        ImGui::SetNextWindowPos(anim_pos);
        ImGui::SetNextWindowSize(anim_size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(s_anim_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_anim_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("##anims_overlay", nullptr, fl)) {

            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            ImVec2 mp = ImGui::GetIO().MousePos;
            bool in_rect = mp.x >= wp.x && mp.x < wp.x + ws.x &&
                           mp.y >= wp.y && mp.y < wp.y + ws.y;
            static bool s_was_hovering = false;
            bool hovering = in_rect;

            if (!hovering && s_was_hovering &&
                ImGui::GetIO().MouseDown[0]) {
                hovering = true;
            }
            s_was_hovering = hovering;
            float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_anim_alpha += (target - s_anim_alpha) * 0.18f;
            if (std::fabs(s_anim_alpha - target) < 0.005f) s_anim_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Animations");
            ImGui::Separator();

            {
                auto& pl = Anim::global_player();
                const auto* cur = pl.clip();
                if (cur) {
                    const float dur_s = Anim::clip_duration_seconds(*cur);
                    const bool playing =
                        (pl.state() == Anim::AnimPlayer::State::Playing);
                    const bool paused  =
                        (pl.state() == Anim::AnimPlayer::State::Paused);

                    const float btn_lg = 36.0f;
                    const float btn_sm = 26.0f;
                    const float gap    = 10.0f;
                    const float row_w  = ImGui::GetContentRegionAvail().x;
                    const float group_w = btn_sm + gap + btn_lg + gap + btn_sm;
                    const float group_x = (row_w - group_w) * 0.5f;
                    const float row_y   = ImGui::GetCursorPosY();
                    const float sm_y    = row_y + (btn_lg - btn_sm) * 0.5f;

                    ImGui::SetCursorPos(ImVec2(group_x, sm_y));
                    if (UI::icon_button("##anim_stop", ICON_FA_STOP,
                                        btn_sm, false)) {
                        pl.stop();
                    }

                    ImGui::SetCursorPos(ImVec2(group_x + btn_sm + gap, row_y));
                    const char* play_glyph = playing ? ICON_FA_PAUSE : ICON_FA_PLAY;

                    float play_dx = playing ? 0.0f : 0.17f;
                    if (UI::icon_button("##anim_playpause", play_glyph,
                                        btn_lg, true, false, play_dx)) {
                        if (playing) pl.pause();
                        else if (paused) pl.resume();
                        else pl.play(cur, pl.is_loop());
                    }

                    ImGui::SetCursorPos(ImVec2(
                        group_x + btn_sm + gap + btn_lg + gap, sm_y));
                    bool loop = pl.is_loop();
                    if (UI::icon_button("##anim_loop", ICON_FA_REPEAT,
                                        btn_sm, false, loop)) {
                        pl.set_loop(!loop);
                    }

                    ImGui::Dummy(ImVec2(0, btn_lg + 4.0f));

                    ImGui::Text("%.2fs / %.2fs", pl.time(), dur_s);

                    {
                        const float scrub_h = 18.0f;
                        ImGui::InvisibleButton("##anim_scrub",
                                               ImVec2(-1, scrub_h));
                        ImVec2 r0 = ImGui::GetItemRectMin();
                        ImVec2 r1 = ImGui::GetItemRectMax();
                        bool active = ImGui::IsItemActive();
                        ImDrawList* dl = ImGui::GetWindowDrawList();

                        dl->AddRectFilled(r0, r1,
                                          IM_COL32(20, 22, 28, 255), 4.0f);

                        const float w = r1.x - r0.x;
                        const float cy = (r0.y + r1.y) * 0.5f;
                        const float prog = (dur_s > 0.0f)
                            ? (pl.time() / dur_s) : 0.0f;
                        const float playhead_x = r0.x + w * prog;

                        dl->AddRectFilled(r0,
                                          ImVec2(playhead_x, r1.y),
                                          IM_COL32(120, 200, 255, 200),
                                          4.0f);

                        bool hovered_event = false;
                        std::string ev_tip;
                        const ImVec2 mp = ImGui::GetIO().MousePos;
                        for (const auto& ev : cur->events) {
                            if (dur_s <= 0.0f) break;
                            float t = ev.time / dur_s;
                            if (t < 0.0f || t > 1.0f) continue;
                            float ex = r0.x + w * t;
                            dl->AddLine(ImVec2(ex, r0.y + 2),
                                        ImVec2(ex, r1.y - 2),
                                        IM_COL32(255, 200, 90, 230),
                                        1.5f);

                            if (ImGui::IsItemHovered() &&
                                std::fabs(mp.x - ex) <= 4.0f &&
                                !hovered_event) {
                                hovered_event = true;
                                ev_tip = ev.name;
                                if (!ev.param.empty())
                                    ev_tip += " - " + ev.param;
                                char tbuf[16];
                                std::snprintf(tbuf, sizeof(tbuf),
                                              "  @ %.2fs", ev.time);
                                ev_tip += tbuf;
                            }
                        }

                        dl->AddLine(ImVec2(playhead_x, r0.y + 1),
                                    ImVec2(playhead_x, r1.y - 1),
                                    IM_COL32(240, 245, 250, 255),
                                    2.0f);

                        if (active && dur_s > 0.0f) {
                            float t = (mp.x - r0.x) / w;
                            if (t < 0.0f) t = 0.0f;
                            if (t > 1.0f) t = 1.0f;
                            pl.seek(t * dur_s);
                        }

                        if (hovered_event) {
                            ImGui::SetTooltip("%s", ev_tip.c_str());
                        }
                    }

                    ImGui::Separator();
                }
            }

            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##anims_overlay_filter", "Filter",
                                     &S.anim_filter);

            const uint32_t want_bones = g_mp.bone_count;
            size_t authored_count = 0;
            const bool can_filter_by_authored =
                g_mp.has_model && S.current_mdl_path_hash != 0 &&
                !S.anim_clips.empty();
            if (can_filter_by_authored) {
                const uint64_t authored_sig =
                    Anim::model_animation_binding_revision() ^
                    (uint64_t(S.current_mdl_path_hash) << 32) ^
                    uint64_t(S.anim_clips.size());
                if (S.anim_authored_signature != authored_sig ||
                    S.anim_authored_cache.size() != S.anim_clips.size()) {
                    authored_count =
                        Anim::build_model_animation_cache_for_hash(
                            S.current_mdl_path_hash, S.anim_clips.size(),
                            S.anim_authored_cache);
                    S.anim_authored_signature = authored_sig;
                } else {
                    authored_count = 0;
                    for (uint8_t v : S.anim_authored_cache) {
                        if (v) ++authored_count;
                    }
                }
            }
            const bool has_authored_filter =
                can_filter_by_authored && authored_count > 0;
            if (has_authored_filter) {
                ImGui::Checkbox("Authored model",
                                &S.anim_authored_only);
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Show clips referenced by GDB animation records for "
                        "this exact model path hash.");
                }
            } else if (S.dev_mode &&
                       g_mp.has_model && S.current_mdl_path_hash != 0) {
                ImGui::TextDisabled("No authored animation set");
            }
            const bool can_filter_by_skeleton =
                Anim::global_data_file().is_open() &&
                g_mp.has_model && want_bones > 0;
            if (can_filter_by_skeleton) {
                ImGui::Checkbox("Compatible rig",
                                &S.anim_compatible_only);
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Show clips whose AnimBank track map matches this "
                        "model's bone names. Falls back to the old %u-bone "
                        "track-count gate when no track map is available.",
                        want_bones);
                }
            } else if (S.dev_mode) {
                ImGui::TextDisabled("Track-count filter unavailable");
            }
            const bool filter_by_authored =
                S.anim_authored_only && has_authored_filter;
            const bool filter_by_bones =
                !filter_by_authored &&
                S.anim_compatible_only && can_filter_by_skeleton;
            if (filter_by_bones) {
                const uint64_t sig = Anim::rig_compatibility_signature(
                    S.mdl_info, want_bones, S.anim_clips,
                    Anim::global_data_file().is_open());
                if (S.anim_compat_signature != sig ||
                    S.anim_compat_cache.size() != S.anim_clips.size()) {
                    Anim::build_rig_compatibility_cache(
                        S.mdl_info, want_bones, S.anim_clips,
                        S.anim_compat_cache, S.anim_compat_matches,
                        S.anim_compat_named_tracks);
                    S.anim_compat_signature = sig;
                }
            }

            std::vector<int> vis;
            vis.reserve(S.anim_clips.size());
            std::string flow = S.anim_filter;
            std::transform(flow.begin(), flow.end(), flow.begin(), ::tolower);
            for (size_t i = 0; i < S.anim_clips.size(); ++i) {
                if (filter_by_authored) {
                    if (i >= S.anim_authored_cache.size() ||
                        !S.anim_authored_cache[i]) {
                        continue;
                    }
                } else if (filter_by_bones) {
                    if (i >= S.anim_compat_cache.size() ||
                        !S.anim_compat_cache[i]) {
                        continue;
                    }
                }
                if (flow.empty()) {
                    vis.push_back((int)i);
                } else {
                    std::string nlow = S.anim_clips[i].name;
                    std::transform(nlow.begin(), nlow.end(),
                                   nlow.begin(), ::tolower);
                    if (nlow.find(flow) != std::string::npos) {
                        vis.push_back((int)i);
                    }
                }
            }
            {
                ImGui::TextDisabled("%d / %zu%s",
                                    (int)vis.size(),
                                    S.anim_clips.size(),
                                    filter_by_authored
                                        ? " authored model"
                                        : (filter_by_bones ? " rig match" : ""));
                if (filter_by_bones) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%u bones)", want_bones);
                } else if (filter_by_authored) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%zu exact)", authored_count);
                }
            }

            if (S.dev_mode &&
                S.anim_selected_clip >= 0 &&
                S.anim_selected_clip < (int)S.anim_clips.size())
            {
                const auto& c = S.anim_clips[(size_t)S.anim_selected_clip];
                ImGui::Separator();
                if (Anim::global_data_file().is_open()) {
                    auto h = Anim::global_data_file().parse_clip_header(c);
                    if (h.ok) {
                        ImGui::TextDisabled(
                            "tracks=%u frames=%u fmt=%u",
                            h.bone_count, h.frame_count, h.bone_idx_bits);
                        if (ImGui::TreeNodeEx("##anim_bone_view",
                                              ImGuiTreeNodeFlags_None,
                                              "Per-bone bodies")) {
                            auto sp = Anim::global_data_file().clip_bytes(c);
                            const size_t total = sp.size;
                            ImGui::BeginChild("##anim_bone_list",
                                              ImVec2(0, 120), false,
                                              ImGuiWindowFlags_HorizontalScrollbar);
                            for (uint32_t bi = 0; bi < h.bone_count; ++bi) {
                                uint32_t bo_bits = h.bone_offsets[bi];
                                uint32_t be_bits = (bi + 1 < h.bone_count)
                                    ? h.bone_offsets[bi + 1]
                                    : (uint32_t)((total > h.packed_body_offset)
                                        ? (total - h.packed_body_offset) * 8
                                        : 0);
                                if (be_bits < bo_bits) continue;
                                uint32_t bo = (uint32_t)h.packed_body_offset
                                            + bo_bits / 8;
                                uint32_t be = (uint32_t)h.packed_body_offset
                                            + (be_bits + 7) / 8;
                                if (be < bo || be > total) continue;
                                uint32_t blen = be - bo;
                                char hexbuf[3 * 4 + 1] = "??";
                                if (bo + 4 <= total) {
                                    std::snprintf(hexbuf, sizeof(hexbuf),
                                                  "%02X %02X %02X %02X",
                                                  sp.data[bo + 0],
                                                  sp.data[bo + 1],
                                                  sp.data[bo + 2],
                                                  sp.data[bo + 3]);
                                }
                                ImGui::TextDisabled(
                                    "track %3u  bits=%6u  bytes=%5u  first4: %s",
                                    bi, be_bits - bo_bits, blen, hexbuf);
                            }
                            ImGui::EndChild();
                            ImGui::TreePop();
                        }
                    } else {
                        ImGui::TextDisabled(
                            "(unrecognised clip header: m=0x%08X v=%u)",
                            h.magic, h.version);
                    }
                } else {
                    ImGui::TextDisabled("(data file not loaded)");
                }
                ImGui::Separator();
            }

            ImGui::BeginChild("##anims_overlay_list", ImVec2(0, 0), false);
            ImGuiListClipper clipper;
            clipper.Begin((int)vis.size());
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart;
                     row < clipper.DisplayEnd; ++row) {
                    const int clip_idx = vis[(size_t)row];
                    const auto& c =
                        S.anim_clips[(size_t)clip_idx];
                    ImGui::PushID(row);
                    bool selected =
                        (S.anim_selected_clip == clip_idx);
                    char label[80];
                    float dur_s = Anim::clip_duration_seconds(c);
                    std::snprintf(label, sizeof(label), "%s  (%.2fs)",
                                  c.name.c_str(), dur_s);
                    if (ImGui::Selectable(label, selected,
                                          ImGuiSelectableFlags_SpanAllColumns)) {
                        S.anim_selected_clip = clip_idx;

                        Anim::global_player().play(
                            &S.anim_clips[(size_t)clip_idx],
                            Anim::global_player().is_loop());
                    }
                    if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(c.name.c_str());
                        ImGui::Text("Duration: %.3f s  (%.0f fps)",
                                    dur_s, c.fps);
                        if (Anim::global_data_file().is_open()) {
                            auto h = Anim::global_data_file().parse_clip_header(c);
                            if (h.ok) {
                                ImGui::Text("Tracks: %u / model bones: %u%s",
                                            h.bone_count, want_bones,
                                            h.bone_count == want_bones
                                                ? "  track-count match"
                                                : "");
                            }
                        }
                        if (c.track_map) {
                            ImGui::Text("Track map: %zu / %zu model-name matches",
                                        (clip_idx >= 0 &&
                                         (size_t)clip_idx < S.anim_compat_matches.size())
                                            ? (size_t)S.anim_compat_matches[(size_t)clip_idx]
                                            : 0u,
                                        (clip_idx >= 0 &&
                                         (size_t)clip_idx < S.anim_compat_named_tracks.size())
                                            ? (size_t)S.anim_compat_named_tracks[(size_t)clip_idx]
                                            : 0u);
                        }
                        ImGui::Text("Events: %zu", c.events.size());
                        if (S.dev_mode) {
                            ImGui::Text("offset=0x%08X frames=%u bytes=%u",
                                        c.data_offset, c.toc_frame_count,
                                        c.data_size_bytes);
                        }
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();
                }
            }
            clipper.End();
            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    if (S.show_item_details && S.selected_item >= 0 &&
        S.selected_item < (int)g_item_details.size() &&
        !LevelEdit::Enabled()) {
        const auto& it = g_item_details[(size_t)S.selected_item];

        static ID3D11ShaderResourceView* s_icon_srv = nullptr;
        static uint32_t s_icon_for = 0xFFFFFFFFu;
        static int s_icon_w = 0, s_icon_h = 0;
        if (g_item_icon_dirty.exchange(false) ||
            s_icon_for != it.record_hash) {
            s_icon_for = it.record_hash;
            if (s_icon_srv) { s_icon_srv->Release(); s_icon_srv = nullptr; }
            s_icon_w = s_icon_h = 0;
            if (!it.icon_tex.empty()) {
                std::vector<unsigned char> tex_buf;
                if (build_any_tex_buffer_for_name(it.icon_tex, tex_buf,
                                                  std::string())) {
                    std::vector<uint8_t> rgba;
                    int w = 0, h = 0;
                    bool has_a = false;
                    if (decode_tex_to_rgba(tex_buf, rgba, w, h, &has_a,
                                           -1) &&
                        w > 0 && h > 0) {
                        s_icon_srv = create_srv_from_rgba(device, w, h,
                                                          rgba);
                        s_icon_w = w;
                        s_icon_h = h;
                    }
                }
            }
        }

        static float s_item_alpha = 0.30f;
        const float kIdleAlpha  = 0.30f;
        const float kHoverAlpha = 1.00f;
        const float kItemW  = 300.0f;
        const float kItemPad = 6.0f;
        ImGui::SetNextWindowPos(
            ImVec2(origin.x + region.x - kItemW - kItemPad,
                   origin.y + kItemPad));


        ImGui::SetNextWindowSizeConstraints(
            ImVec2(kItemW, 0.0f),
            ImVec2(kItemW, std::max(200.0f, region.y - 2 * kItemPad)));
        ImGui::SetNextWindowBgAlpha(s_item_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_item_alpha);
        ImGuiWindowFlags ifl = ImGuiWindowFlags_NoTitleBar |
                               ImGuiWindowFlags_NoResize |
                               ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoCollapse |
                               ImGuiWindowFlags_NoSavedSettings |
                               ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##item_details_overlay", nullptr, ifl)) {
            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            ImVec2 mp = ImGui::GetIO().MousePos;
            bool hovering = mp.x >= wp.x && mp.x < wp.x + ws.x &&
                            mp.y >= wp.y && mp.y < wp.y + ws.y;
            static bool s_was_hovering = false;
            if (!hovering && s_was_hovering &&
                ImGui::GetIO().MouseDown[0]) {
                hovering = true;
            }
            s_was_hovering = hovering;
            const float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_item_alpha += (target - s_item_alpha) * 0.18f;
            if (std::fabs(s_item_alpha - target) < 0.005f) {
                s_item_alpha = target;
            }

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                               "Item Details");
            ImGui::Separator();

            std::string disp_name;
            if (it.name_tag) TextBank::Lookup(it.name_tag, disp_name);
            if (disp_name.empty()) disp_name = it.label;
            ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s",
                               disp_name.c_str());
            if (s_icon_srv) {
                float iw = float(s_icon_w), ih = float(s_icon_h);
                const float maxdim = 80.0f;
                if (iw > maxdim || ih > maxdim) {
                    const float s = maxdim / std::max(iw, ih);
                    iw *= s; ih *= s;
                }
                ImGui::Image((ImTextureID)s_icon_srv, ImVec2(iw, ih));
            }
            if (it.money >= 0) {
                ImGui::Text("Value: %d gold", it.money);
            }

            std::string desc;
            if (it.desc_tag) TextBank::Lookup(it.desc_tag, desc);
            if (!desc.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                                   "Description");
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(desc.c_str());
                ImGui::PopTextWrapPos();
            }

            if (!it.stats.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                                   "Stats");
                if (ImGui::BeginTable("##item_stats", 2,
                                      ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Field");
                    ImGui::TableSetupColumn(
                        "Value", ImGuiTableColumnFlags_WidthFixed,
                        84.0f);
                    for (const auto& kv : it.stats) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(kv.first.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(kv.second.c_str());
                    }
                    ImGui::EndTable();
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    if (S.show_entity_details && S.selected_entity >= 0 &&
        S.selected_entity < static_cast<int>(g_global_entity_catalog.size()) &&
        !LevelEdit::Enabled() &&
        ContentTabs::ActiveKind() == ContentTabs::Kind::Entity) {
        const auto& entity = g_global_entity_catalog[
            static_cast<std::size_t>(S.selected_entity)];
        static float s_entity_alpha = 0.30f;
        constexpr float kIdleAlpha = 0.30f;
        constexpr float kHoverAlpha = 1.00f;
        constexpr float kEntityW = 350.0f;
        constexpr float kEntityPad = 6.0f;
        const float entity_h = (std::min)(
            620.0f, (std::max)(180.0f, region.y - 2.0f * kEntityPad));
        ImGui::SetNextWindowPos(
            ImVec2(origin.x + region.x - kEntityW - kEntityPad,
                   origin.y + kEntityPad));
        ImGui::SetNextWindowSize(ImVec2(kEntityW, entity_h),
                                 ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(s_entity_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_entity_alpha);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar;
        if (ImGui::Begin("##entity_details_overlay", nullptr, flags)) {
            const ImVec2 window_pos = ImGui::GetWindowPos();
            const ImVec2 window_size = ImGui::GetWindowSize();
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            bool hovering = mouse.x >= window_pos.x &&
                            mouse.x < window_pos.x + window_size.x &&
                            mouse.y >= window_pos.y &&
                            mouse.y < window_pos.y + window_size.y;
            static bool s_was_hovering = false;
            if (!hovering && s_was_hovering &&
                ImGui::GetIO().MouseDown[0]) {
                hovering = true;
            }
            s_was_hovering = hovering;
            const float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_entity_alpha += (target - s_entity_alpha) * 0.18f;
            if (std::fabs(s_entity_alpha - target) < 0.005f) {
                s_entity_alpha = target;
            }

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                               "Entity Details");
            ImGui::Separator();
            ImGui::BeginChild("##entity_details_scroll", ImVec2(0.0f, 0.0f),
                              false);
            ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s",
                               (entity.display_name.empty()
                                    ? entity.name : entity.display_name)
                                   .c_str());
            if (S.dev_mode) {
                if (!entity.display_name.empty() &&
                    entity.display_name != entity.name) {
                    ImGui::TextDisabled("Internal: %s", entity.name.c_str());
                }
                ImGui::TextDisabled("Entity 0x%08X", entity.entity_hash);
            }

            static std::uint32_t cached_animation_entity = 0;
            static std::uint64_t cached_animation_binding_revision = 0;
            static std::uint64_t cached_animation_catalog_revision = 0;
            static std::size_t cached_animation_clip_count = 0;
            static std::string entity_animation_filter;
            static std::vector<std::pair<std::size_t, std::string>> animations;
            const std::uint64_t binding_revision =
                Anim::model_animation_binding_revision();
            if (cached_animation_entity != entity.entity_hash ||
                cached_animation_binding_revision != binding_revision ||
                cached_animation_catalog_revision !=
                    g_global_entity_catalog_revision ||
                cached_animation_clip_count != S.anim_clips.size()) {
                animations.clear();
                entity_animation_filter.clear();
                std::unordered_set<std::size_t> seen_animations;
                const std::unordered_set<std::uint32_t> model_hashes(
                    entity.model_hashes.begin(), entity.model_hashes.end());
                for (const Anim::ModelAnimationBinding& binding :
                     Anim::model_animation_bindings()) {
                    if (model_hashes.find(binding.model_path_hash) ==
                        model_hashes.end()) {
                        continue;
                    }
                    if (binding.clip_index >= S.anim_clips.size() ||
                        !seen_animations.insert(binding.clip_index).second) {
                        continue;
                    }
                    std::string name = binding.animation_name.empty()
                        ? binding.source_name : binding.animation_name;
                    if (name.empty()) {
                        name = S.anim_clips[binding.clip_index].name;
                    }
                    animations.emplace_back(binding.clip_index,
                                             std::move(name));
                }
                cached_animation_entity = entity.entity_hash;
                cached_animation_binding_revision = binding_revision;
                cached_animation_catalog_revision =
                    g_global_entity_catalog_revision;
                cached_animation_clip_count = S.anim_clips.size();
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                               "Animations (%zu)", animations.size());
            if (animations.empty()) {
                ImGui::TextDisabled("None indexed");
            } else {
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##entity_animation_filter",
                                         "Filter animations...",
                                         &entity_animation_filter);
                std::string filter_lower = entity_animation_filter;
                std::transform(filter_lower.begin(), filter_lower.end(),
                               filter_lower.begin(), ::tolower);
                std::vector<std::size_t> visible_animations;
                visible_animations.reserve(animations.size());
                for (std::size_t i = 0; i < animations.size(); ++i) {
                    if (filter_lower.empty()) {
                        visible_animations.push_back(i);
                        continue;
                    }
                    std::string name_lower = animations[i].second;
                    std::transform(name_lower.begin(), name_lower.end(),
                                   name_lower.begin(), ::tolower);
                    if (name_lower.find(filter_lower) != std::string::npos) {
                        visible_animations.push_back(i);
                    }
                }
                auto& player = Anim::global_player();
                const Anim::AnimClip* current = player.clip();
                if (current) {
                    const bool playing = player.state() ==
                        Anim::AnimPlayer::State::Playing;
                    const bool paused = player.state() ==
                        Anim::AnimPlayer::State::Paused;
                    if (UI::icon_button("##entity_anim_stop", ICON_FA_STOP,
                                        26.0f, false)) {
                        player.stop();
                    }
                    ImGui::SameLine();
                    const char* glyph = playing ? ICON_FA_PAUSE : ICON_FA_PLAY;
                    if (UI::icon_button("##entity_anim_play", glyph,
                                        30.0f, true)) {
                        if (playing) player.pause();
                        else if (paused) player.resume();
                        else player.play(current, player.is_loop());
                    }
                    ImGui::SameLine();
                    if (UI::icon_button("##entity_anim_loop", ICON_FA_REPEAT,
                                        26.0f, false,
                                        player.is_loop())) {
                        player.set_loop(!player.is_loop());
                    }
                    const float duration =
                        Anim::clip_duration_seconds(*current);
                    float time = player.time();
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::SliderFloat("##entity_anim_time", &time,
                                           0.0f,
                                           (std::max)(duration, 0.001f),
                                           "%.2fs")) {
                        player.seek(time);
                    }
                } else {
                    ImGui::TextDisabled("Select an animation to play it");
                }
                const float animation_list_height = (std::min)(
                    240.0f,
                    (std::max)(100.0f, ImGui::GetContentRegionAvail().y));
                ImGui::BeginChild("##entity_animation_list",
                                  ImVec2(0.0f, animation_list_height),
                                  false);
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(visible_animations.size()));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart;
                         row < clipper.DisplayEnd; ++row) {
                        const auto& animation =
                            animations[visible_animations[
                                static_cast<std::size_t>(row)]];
                        const bool selected =
                            S.anim_selected_clip ==
                            static_cast<int>(animation.first);
                        ImGui::PushID(row);
                        const float duration = Anim::clip_duration_seconds(
                            S.anim_clips[animation.first]);
                        char animation_label[192];
                        std::snprintf(animation_label,
                                      sizeof(animation_label),
                                      "%s  (%.2fs)",
                                      animation.second.c_str(), duration);
                        if (ImGui::Selectable(animation_label, selected)) {
                            S.anim_selected_clip =
                                static_cast<int>(animation.first);
                            player.play(&S.anim_clips[animation.first],
                                        player.is_loop());
                        }
                        ImGui::PopID();
                    }
                }
                clipper.End();
                ImGui::EndChild();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                               "Model parts");
            if (entity.model_hashes.empty()) {
                ImGui::TextDisabled("None");
            } else {
                for (std::uint32_t hash : entity.model_hashes) {
                    const FlatAssetEntry* match =
                        FindGlobalModelAssetByPathHash(hash);
                    if (match) {
                        ImGui::Bullet();
                        ImGui::SameLine();
                        ImGui::TextWrapped("%s", match->full_path.c_str());
                    } else {
                        ImGui::BulletText("Unresolved model 0x%08X", hash);
                    }
                }
            }

            const auto gameplay =
                g_global_entity_gameplay.find(entity.entity_hash);
            if (gameplay != g_global_entity_gameplay.end()) {
                draw_entity_gameplay_details(gameplay->second, false);
            }
            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    if (::g_tex_popout_open && ::g_tex_popout_srv) {
        int tw = 0, th = 0;
        ID3D11Resource* res = nullptr;
        ::g_tex_popout_srv->GetResource(&res);
        if (res) {

            ID3D11Texture2D* t2d = (ID3D11Texture2D*)res;
            D3D11_TEXTURE2D_DESC desc{};
            t2d->GetDesc(&desc);
            tw = (int)desc.Width;
            th = (int)desc.Height;
            res->Release();
        }
        if (tw > 0 && th > 0) {
            std::string title = "Texture: "
                + std::filesystem::path(::g_tex_popout_name).filename().string()
                + "##tex_popout";

            ImGuiWindowFlags fl = ImGuiWindowFlags_NoCollapse
                                | ImGuiWindowFlags_NoResize
                                | ImGuiWindowFlags_AlwaysAutoResize;
            if (ImGui::Begin(title.c_str(), &::g_tex_popout_open, fl)) {

                ImGui::Checkbox("Show UVs", &::g_tex_popout_show_uvs);

                ImGui::Image((ImTextureID)::g_tex_popout_srv,
                             ImVec2((float)tw, (float)th));

                {
                    ImVec2 img_min = ImGui::GetItemRectMin();
                    ImGui::SetCursorScreenPos(img_min);
                    ImGui::InvisibleButton("##popout_hit",
                                           ImVec2((float)tw, (float)th));
                    if (ImGui::BeginPopupContextItem()) {
                        const std::string& preferred_bnk =
                            (S.selected_nested_index != -1 &&
                             !S.selected_nested_temp_path.empty())
                                ? S.selected_nested_temp_path
                                : S.selected_bnk;
                        tex_export_menu_named(::g_tex_popout_name,
                                              ::g_tex_popout_name,
                                              preferred_bnk, 0);
                        ImGui::EndPopup();
                    }
                }

                if (::g_tex_popout_show_uvs &&
                    ::g_tex_popout_mesh_idx >= 0 &&
                    (size_t)::g_tex_popout_mesh_idx < g_mp.meshes.size())
                {
                    uint32_t src = g_mp.meshes[(size_t)::g_tex_popout_mesh_idx].source_mesh_idx;
                    if (src < S.mdl_meshes.size()) {
                        const auto& geom = S.mdl_meshes[src];
                        if (!geom.uvs.empty() && !geom.indices.empty()) {
                            ImVec2 img_min = ImGui::GetItemRectMin();
                            ImVec2 img_max = ImGui::GetItemRectMax();
                            float w_px = img_max.x - img_min.x;
                            float h_px = img_max.y - img_min.y;
                            ImDrawList* dl = ImGui::GetWindowDrawList();

                            const ImU32 col = IM_COL32(255, 255, 255, 200);
                            const float thickness = 1.0f;

                            for (size_t i = 0; i + 2 < geom.indices.size(); i += 3) {
                                uint32_t a = geom.indices[i];
                                uint32_t b = geom.indices[i + 1];
                                uint32_t c = geom.indices[i + 2];
                                if ((size_t)a * 2 + 1 >= geom.uvs.size()) continue;
                                if ((size_t)b * 2 + 1 >= geom.uvs.size()) continue;
                                if ((size_t)c * 2 + 1 >= geom.uvs.size()) continue;
                                ImVec2 pa(img_min.x + geom.uvs[a * 2 + 0] * w_px,
                                          img_min.y + geom.uvs[a * 2 + 1] * h_px);
                                ImVec2 pb(img_min.x + geom.uvs[b * 2 + 0] * w_px,
                                          img_min.y + geom.uvs[b * 2 + 1] * h_px);
                                ImVec2 pc(img_min.x + geom.uvs[c * 2 + 0] * w_px,
                                          img_min.y + geom.uvs[c * 2 + 1] * h_px);
                                dl->AddLine(pa, pb, col, thickness);
                                dl->AddLine(pb, pc, col, thickness);
                                dl->AddLine(pc, pa, col, thickness);
                            }
                        }
                    }
                }
            }
            ImGui::End();
        }

        if (!::g_tex_popout_open) {
            ::g_tex_popout_srv = nullptr;
            ::g_tex_popout_name.clear();
        }
    }
}
