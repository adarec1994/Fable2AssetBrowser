        if (s_active_tab == 7) {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##lua_scripts_filter", "Filter",
                                     &S.lua_filter);
            std::string flow = S.lua_filter;
            std::transform(flow.begin(), flow.end(), flow.begin(),
                           [](unsigned char c){ return std::tolower(c); });

            std::vector<int> vis;
            vis.reserve(S.lua_files.size());
            for (size_t i = 0; i < S.lua_files.size(); ++i) {
                const std::string label = lua_script_list_label(S.lua_files[i]);
                if (flow.empty()) {
                    vis.push_back((int)i);
                    continue;
                }

                std::string haystack = label + " " + S.lua_files[i].path;
                std::transform(haystack.begin(), haystack.end(),
                               haystack.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                if (haystack.find(flow) != std::string::npos) {
                    vis.push_back((int)i);
                }
            }

            if (S.dev_mode) {
                ImGui::TextDisabled("%d / %zu scripts",
                                    (int)vis.size(), S.lua_files.size());
                ImGui::Separator();
            }

            ImGui::BeginChild("lua_scripts_list", ImVec2(0, 0), false);
            if (S.lua_files.empty()) {
                ImGui::TextDisabled("No Lua scripts indexed yet.");
                ImGui::TextDisabled("Open a Fable 2 root to populate the list.");
            } else {
                ImGuiListClipper clipper;
                clipper.Begin((int)vis.size());
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart;
                         row < clipper.DisplayEnd; ++row) {
                        const int idx = vis[(size_t)row];
                        const LuaFileUI& e = S.lua_files[(size_t)idx];
                        const std::string label = lua_script_list_label(e);
                        const bool selected =
                            S.viewing_lua && S.selected_file_index == idx;

                        ImGui::PushID(idx);
                        if (ImGui::Selectable(label.c_str(), selected,
                                              ImGuiSelectableFlags_SpanAllColumns)) {
                            select_lua_script((size_t)idx);
                        }
                        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(e.path.c_str());
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

        if (s_active_tab == 11) {
            DetailsPanel::Draw();
        }

