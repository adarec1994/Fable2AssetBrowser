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
