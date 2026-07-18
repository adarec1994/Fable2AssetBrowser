        if (s_active_tab == 10) {
            static std::string new_quest_id = "QO000_NewQuest";
            static std::string new_quest_error;
            if (ImGui::Button("Create Quest", ImVec2(-1.0f, 0.0f))) {
                new_quest_error.clear();
                ImGui::OpenPopup("Create Quest##modal");
            }
            ImGui::SetNextWindowSize(ImVec2(430.0f, 0.0f),
                                     ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal("Create Quest##modal", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                if (ImGui::IsWindowAppearing()) {
                    ImGui::SetKeyboardFocusHere();
                }
                ImGui::SetNextItemWidth(390.0f);
                ImGui::InputTextWithHint("##new_quest_id", "Quest ID",
                                         &new_quest_id);
                if (!new_quest_error.empty()) {
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 390.0f);
                    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                                       "%s", new_quest_error.c_str());
                    ImGui::PopTextWrapPos();
                }
                if (ImGui::Button("Create")) {
                    if (shipped_quest_id_exists(new_quest_id)) {
                        new_quest_error =
                            "A shipped quest already uses this ID.";
                    } else {
                        ++S.lua_preview_request;
                        if (QuestUI::CreateNewBlueprintQuest(
                                new_quest_id, new_quest_error)) {
                            const std::string title =
                                "Custom quest: " + new_quest_id;
                            ContentTabs::OpenCustomQuest(
                                new_quest_id, title);
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
                            S.lua_preview_title = title;
                            S.lua_preview_content =
                                QuestUI::ActiveAuthoredLua();
                            S.lua_preview_loading = false;
                            S.lua_preview_is_quest = true;
                            S.quest_preview_select_nodes = true;
                            S.show_lua_render = true;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##quest_filter", "Filter quests",
                                     &S.quest_filter);
            std::string flow = S.quest_filter;
            std::transform(flow.begin(), flow.end(), flow.begin(),
                           [](unsigned char c) {
                               return char(std::tolower(c));
                           });

            std::vector<int> vis;
            vis.reserve(S.all_quest_files.size());
            for (size_t i = 0; i < S.all_quest_files.size(); ++i) {
                const FlatAssetEntry& e = S.all_quest_files[i];
                std::string haystack = e.name + " " + e.full_path;
                std::transform(haystack.begin(), haystack.end(),
                               haystack.begin(),
                               [](unsigned char c) {
                                   return char(std::tolower(c));
                               });
                if (flow.empty() || haystack.find(flow) != std::string::npos) {
                    vis.push_back((int)i);
                }
            }

            ImGui::BeginChild("quests_list", ImVec2(0, 0), false);
            static std::string s_delete_quest_id;
            const std::vector<std::string> authored_quests =
                QuestUI::AuthoredQuestIds();
            bool showed_authored_heading = false;
            for (const std::string& quest_id : authored_quests) {
                std::string lower_id = quest_id;
                std::transform(lower_id.begin(), lower_id.end(),
                               lower_id.begin(), [](unsigned char c) {
                                   return char(std::tolower(c));
                               });
                if (!flow.empty() &&
                    lower_id.find(flow) == std::string::npos) continue;
                if (!showed_authored_heading) {
                    ImGui::TextDisabled("CUSTOM QUESTS");
                    showed_authored_heading = true;
                }
                const bool selected = QuestUI::IsAuthoredQuestActive() &&
                                      QuestUI::ActiveAuthoredQuestId() ==
                                          quest_id;
                if (ImGui::Selectable(quest_id.c_str(), selected,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    show_authored_quest(quest_id);
                }
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Delete Quest")) {
                        s_delete_quest_id = quest_id;
                    }
                    ImGui::EndPopup();
                }
            }
            if (!s_delete_quest_id.empty() &&
                !ImGui::IsPopupOpen("Delete custom quest?")) {
                ImGui::OpenPopup("Delete custom quest?");
            }
            if (ImGui::BeginPopupModal("Delete custom quest?", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Delete %s? This removes its blueprint file "
                            "for good.",
                            s_delete_quest_id.c_str());
                if (ImGui::Button("Delete", ImVec2(120, 0))) {
                    const std::string id = s_delete_quest_id;
                    s_delete_quest_id.clear();
                    ContentTabs::CloseCustomQuest(id);
                    std::string derr;
                    if (QuestUI::DeleteAuthoredQuest(id, derr)) {
                        OutputLog::success("quest deleted: " + id);
                    } else {
                        OutputLog::error("quest delete: " + derr);
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    s_delete_quest_id.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            if (showed_authored_heading && !vis.empty()) {
                ImGui::Separator();
            }
            if (S.all_quest_files.empty()) {
                if (tree_build_in_progress()) {
                    ImGui::TextDisabled("Indexing quest scripts...");
                } else {
                    ImGui::TextDisabled("No embedded quest scripts found.");
                    ImGui::TextDisabled(
                        "Open a Fable 2 root containing gamescripts.bnk or gamescripts_r.bnk.");
                }
            } else {
                ImGuiListClipper clipper;
                clipper.Begin((int)vis.size());
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart;
                         row < clipper.DisplayEnd; ++row) {
                        const int idx = vis[(size_t)row];
                        const FlatAssetEntry& e =
                            S.all_quest_files[(size_t)idx];
                        const bool selected = S.selected_quest == idx;

                        ImGui::PushID(idx);
                        if (ImGui::Selectable(
                                e.name.c_str(), selected,
                                ImGuiSelectableFlags_SpanAllColumns)) {
                            select_quest_script((size_t)idx);
                        }
                        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(e.full_path.c_str());
                            ImGui::Text("Source: %s",
                                std::filesystem::path(e.bnk_path)
                                    .filename().string().c_str());
                            ImGui::TextUnformatted(
                                quest_source_has_debug_symbols(e.bnk_path)
                                    ? "Symbol-rich script"
                                    : "Stripped runtime script");
                            ImGui::Text("Size: %u bytes", e.size);
                            ImGui::EndTooltip();
                        }
                        ImGui::PopID();
                    }
                }
                clipper.End();
            }
            ImGui::EndChild();
        }
