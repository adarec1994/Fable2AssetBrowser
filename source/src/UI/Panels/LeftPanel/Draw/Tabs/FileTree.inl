        if (s_active_tab == 1) {
            drill_step_anim(g_tree_drill, ImGui::GetIO().DeltaTime);

            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float page_w = avail.x;
            const float page_h = avail.y;
            const float kVisEps = 0.0001f;
            const bool a_visible = g_tree_drill.anim_t < 1.0f - kVisEps;
            const bool b_visible = g_tree_drill.anim_t > 0.0f + kVisEps;

            ImGui::BeginChild("file_tree", ImVec2(0, 0), false);
            ImGui::BeginChild("##tree_drill_container",
                              ImVec2(page_w, page_h),
                              false,
                              ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::SetScrollX(g_tree_drill.anim_t * page_w);

            ImGui::BeginChild("##tree_page_a", ImVec2(page_w, page_h), false);
            if (a_visible) {
                if (g_tree_last_root_dir != S.root_dir && !S.bnk_paths.empty()
                    && !g_tree_building.load() && !g_tree_built.load())
                {
                    start_tree_build_for_root(S.root_dir, S.bnk_paths);
                }

                TreeNode& tree_render_root = g_tree_root;

                if (g_tree_building.load()) {
                    ImVec2 inner_avail = ImGui::GetContentRegionAvail();
                    float elapsed =
                        (float)ImGui::GetTime() - g_tree_build_start_time;

                    float dot_cycle = fmodf(elapsed * 2.0f, 4.0f);
                    int dot_count = (int)dot_cycle;
                    std::string dots(dot_count, '.');
                    std::string loading_text = "Loading file tree" + dots;

                    ImVec2 text_size =
                        ImGui::CalcTextSize(loading_text.c_str());
                    ImVec2 pos((inner_avail.x - text_size.x) * 0.5f,
                               (inner_avail.y - text_size.y) * 0.5f);
                    if (pos.x < 0) pos.x = 0;
                    if (pos.y < 0) pos.y = 0;
                    ImGui::SetCursorPos(pos);
                    ImGui::TextUnformatted(loading_text.c_str());

                    if (elapsed > 10.0f) {
                        ImVec2 warning_size =
                            ImGui::CalcTextSize("(this may take some time)");
                        ImVec2 warning_pos(
                            (inner_avail.x - warning_size.x) * 0.5f,
                            pos.y + text_size.y + 10.0f);
                        if (warning_pos.x < 0) warning_pos.x = 0;
                        ImGui::SetCursorPos(warning_pos);
                        ImGui::TextUnformatted("(this may take some time)");
                    }
                } else if (g_tree_built.load()) {
                    for (auto& pair : tree_render_root.children) {
#ifdef _WIN32
                        draw_tree_node(pair.second, device);
#else
                        draw_tree_node(pair.second);
#endif
                    }
                }
            }
            ImGui::EndChild();

            ImGui::SameLine(0.0f, 0.0f);

            ImGui::BeginChild("##tree_page_b", ImVec2(page_w, page_h), false);
            if (b_visible) {
                const bool b_can_click =
                    (g_tree_drill.target_t == 1.0f) &&
                    drill_settled(g_tree_drill);

                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                      ImVec4(0.30f, 0.45f, 0.65f, 0.40f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                      ImVec4(0.40f, 0.60f, 0.90f, 0.55f));
                if (ImGui::Button(ICON_FA_ARROW_LEFT "##tree_drill_back")) {
                    drill_back(g_tree_drill);
                }
                ImGui::PopStyleColor(3);
                ImGui::SameLine();
                ImGui::TextUnformatted(g_tree_drill.title.c_str());

                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##tree_drill_filter", "Filter",
                                         &g_tree_drill.filter);

                std::string flt = g_tree_drill.filter;
                std::transform(flt.begin(), flt.end(), flt.begin(),
                               ::tolower);
                std::vector<int> vis;
                vis.reserve(g_tree_drill.items.size());
                for (size_t i = 0; i < g_tree_drill.items.size(); ++i) {
                    if (flt.empty()) {
                        vis.push_back((int)i);
                    } else {
                        std::string n =
                            std::filesystem::path(g_tree_drill.items[i].name)
                                .filename().string();
                        std::transform(n.begin(), n.end(), n.begin(),
                                       ::tolower);
                        if (n.find(flt) != std::string::npos) {
                            vis.push_back((int)i);
                        }
                    }
                }

                ImGui::BeginChild("##tree_drill_list", ImVec2(0, 0), false);
                ImGuiListClipper drill_clipper;
                drill_clipper.Begin((int)vis.size());
                while (drill_clipper.Step()) {
                    for (int r = drill_clipper.DisplayStart;
                         r < drill_clipper.DisplayEnd; ++r) {
                        int idx = vis[(size_t)r];
                        const BNKItemUI& it =
                            g_tree_drill.items[(size_t)idx];
                        ImGui::PushID(r);

                        std::string label =
                            std::filesystem::path(it.name).filename().string();
                        const bool selected =
                            (S.selected_bnk == g_tree_drill.bnk_path &&
                             S.selected_file_index >= 0 &&
                             S.selected_file_index < (int)S.files.size() &&
                             S.files[(size_t)S.selected_file_index].index ==
                                 it.index);
                        if (ImGui::Selectable(
                                label.c_str(), selected,
                                ImGuiSelectableFlags_SpanAllColumns) &&
                            b_can_click) {
                            if (S.selected_bnk != g_tree_drill.bnk_path) {
                                S.viewing_adb = false;
                                S.viewing_lua = false;
                                S.global_search.clear();
                                S.selected_nested_bnk.clear();
                                S.selected_nested_index = -1;
                                pick_bnk(g_tree_drill.bnk_path);
                            }
                            if (g_tree_drill.from_nested) {
                                S.selected_nested_temp_path =
                                    g_tree_drill.bnk_path;
                                S.selected_nested_index = 0;
                            }

                            for (size_t j = 0; j < S.files.size(); ++j) {
                                if (S.files[j].index == it.index) {
                                    S.selected_file_index = (int)j;
                                    std::string ln = it.name;
                                    std::transform(ln.begin(), ln.end(),
                                                   ln.begin(), ::tolower);
                                    if (ln.size() >= 4 &&
                                        ln.rfind(".mdl") == ln.size() - 4) {
                                        g_pending_mdl_full_path = it.name;
                                        g_pending_mdl_load = true;
                                        g_pending_mdl_index = (int)j;
                                    } else if (ln.size() >= 4 &&
                                               ln.rfind(".tex") ==
                                                   ln.size() - 4) {
                                        g_pending_tex_load = true;
                                        g_pending_tex_index = (int)j;
                                    } else if (ln.size() >= 4 &&
                                               ln.rfind(".wav") ==
                                                   ln.size() - 4) {
                                        open_audio_player_for_selected((int)j);
                                    }
                                    break;
                                }
                            }
                        }

                        file_hex_context_menu(g_tree_drill.bnk_path,
                                              it.index,
                                              g_tree_drill.from_nested,
                                              it.name);
                        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(it.name.c_str());
                            ImGui::EndTooltip();
                        }
                        ImGui::PopID();
                    }
                }
                drill_clipper.End();
                ImGui::EndChild();
            }
            ImGui::EndChild();
            ImGui::EndChild();
            ImGui::EndChild();
        }

