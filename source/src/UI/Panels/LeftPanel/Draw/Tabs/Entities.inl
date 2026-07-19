        if (s_active_tab == 9) {
            const bool create_entity_clicked =
                ImGui::Button("Create Entity", ImVec2(-1.0f, 0.0f));
            if (create_entity_clicked || g_open_create_npc_requested) {
                if (g_open_create_npc_requested) {
                    g_new_entity_kind = NewEntityKind::Npc;
                }
                g_open_create_npc_requested = false;
                g_new_npc = NpcAuthoring::Definition{};
                g_new_npc_template_index = -1;
                g_new_npc_template_filter[0] = 0;
                g_new_npc_error.clear();
                g_new_static_prop = StaticPropAuthoring::Definition{};
                g_new_static_prop_model_index = -1;
                g_new_static_prop_model_filter[0] = 0;
                g_new_static_prop_error.clear();
                if (S.selected_entity >= 0 &&
                    static_cast<std::size_t>(S.selected_entity) <
                        g_global_entity_catalog.size() &&
                    g_global_entity_catalog[
                        static_cast<std::size_t>(S.selected_entity)].kind ==
                        Gdb::EntityCatalogKind::Creature) {
                    select_new_npc_template(S.selected_entity);
                }
                ImGui::OpenPopup("Create Entity##modal");
            }
            draw_create_entity_modal();

            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##entity_filter", "Filter entities",
                                     &S.entity_filter);
            std::string filter = S.entity_filter;
            std::transform(filter.begin(), filter.end(), filter.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });

            if (g_global_entity_catalog.empty()) {
                ImGui::TextDisabled("No entities indexed yet.");
                ImGui::TextDisabled("Open a Fable 2 game root to index them.");
            } else {
                static uint64_t cached_catalog_revision =
                    std::numeric_limits<uint64_t>::max();
                static std::string cached_filter;
                static std::vector<std::string> searchable_entities;
                static std::vector<int> visible;
                bool catalog_changed = false;
                if (cached_catalog_revision !=
                    g_global_entity_catalog_revision) {
                    searchable_entities.clear();
                    searchable_entities.reserve(g_global_entity_catalog.size());
                    for (const auto& entity : g_global_entity_catalog) {
                        std::string searchable = entity.name + " " +
                            entity.display_name;
                        const auto gameplay =
                            g_global_entity_gameplay.find(entity.entity_hash);
                        if (gameplay != g_global_entity_gameplay.end()) {
                            searchable += " " + gameplay->second.faction_name;
                            searchable += " " +
                                gameplay->second.combat_profile_name;
                        }
                        std::transform(
                            searchable.begin(), searchable.end(),
                            searchable.begin(), [](unsigned char c) {
                                return static_cast<char>(std::tolower(c));
                            });
                        searchable_entities.push_back(std::move(searchable));
                    }
                    cached_catalog_revision =
                        g_global_entity_catalog_revision;
                    catalog_changed = true;
                }
                if (catalog_changed || cached_filter != filter) {
                    visible.clear();
                    visible.reserve(searchable_entities.size());
                    for (int i = 0;
                         i < static_cast<int>(searchable_entities.size()); ++i) {
                        if (filter.empty() ||
                            searchable_entities[static_cast<size_t>(i)].find(
                                filter) != std::string::npos) {
                            visible.push_back(i);
                        }
                    }
                    cached_filter = filter;
                }
                ImGui::BeginChild("entities_list", ImVec2(0, 0), false);
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(visible.size()));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart;
                         row < clipper.DisplayEnd; ++row) {
                        const int index = visible[static_cast<std::size_t>(row)];
                        const auto& entity = g_global_entity_catalog[index];
                        const std::string& label =
                            entity.display_name.empty()
                                ? entity.name : entity.display_name;
                        ImGui::PushID(index);
                        if (ImGui::Selectable(label.c_str(),
                                              S.selected_entity == index,
                                              ImGuiSelectableFlags_SpanAllColumns)) {
                            ContentTabs::OpenEntity(index, label);
                            load_entity_preview(index);
                        }
                        if (ImGui::BeginPopupContextItem()) {
                            const bool has_tree =
                                AnimTreeUI::Available(entity.entity_hash);
                            if (ImGui::MenuItem("Show Animation Tree",
                                                nullptr, false, has_tree)) {
                                AnimTreeUI::Open(entity.entity_hash, label);
                            }
                            if (!has_tree) {
                                ImGui::TextDisabled(
                                    "No animation set on this entity.");
                            }
                            ImGui::EndPopup();
                        }
                        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(label.c_str());
                            ImGui::Text("Model parts: %zu",
                                        entity.model_hashes.size());
                            ImGui::EndTooltip();
                        }
                        ImGui::PopID();
                    }
                }
                clipper.End();
                ImGui::EndChild();
            }
        }
