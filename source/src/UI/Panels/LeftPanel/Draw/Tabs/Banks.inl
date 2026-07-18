    if (s_active_tab == 0) {

            drill_step_anim(g_bnk_drill, ImGui::GetIO().DeltaTime);

            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float page_w = avail.x;
            const float page_h = avail.y;

            const float kVisEps = 0.0001f;
            const bool a_visible = g_bnk_drill.anim_t <  1.0f - kVisEps;
            const bool b_visible = g_bnk_drill.anim_t >  0.0f + kVisEps;

            ImGui::BeginChild("##bnk_drill_container",
                              ImVec2(page_w, page_h),
                              false,
                              ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);

            ImGui::SetScrollX(g_bnk_drill.anim_t * page_w);

            ImGui::BeginChild("##bnk_page_a", ImVec2(page_w, page_h), false);
            if (a_visible) {

            ImGui::SetNextItemWidth(-1);
            if (!S.bnk_paths.empty()) {
                ImGui::InputTextWithHint("##bnk_filter", "Filter", &S.bnk_filter);
            }

            auto paths = filtered_bnk_paths();

            const bool a_can_click = (g_bnk_drill.target_t == 0.0f) &&
                                      drill_settled(g_bnk_drill);

            if (!S.adb_paths.empty()) {
                ImGui::PushID("adb_entry");
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                if (ImGui::Selectable("Audio Database",
                                      g_bnk_drill.kind == DrillKind::Adb,
                                      ImGuiSelectableFlags_SpanAllColumns) &&
                    a_can_click) {
                    drill_open_adb(g_bnk_drill);
                }
                ImGui::PopStyleColor();
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Audio Database Files (%d)",
                                (int)S.adb_paths.size());
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }

            if (!S.lua_files.empty()) {
                ImGui::PushID("lua_entry");
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
                if (ImGui::Selectable("Lua Scripts",
                                      g_bnk_drill.kind == DrillKind::Lua,
                                      ImGuiSelectableFlags_SpanAllColumns) &&
                    a_can_click) {
                    drill_open_lua(g_bnk_drill);
                }
                ImGui::PopStyleColor();
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Lua Script Files (%d)",
                                (int)S.lua_files.size());
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }

            struct NestedChild { int index; std::string name; };
            static std::unordered_map<std::string, std::vector<NestedChild>> s_nested_cache;
            static std::string s_nested_cache_root;
            if (s_nested_cache_root != S.root_dir) {
                s_nested_cache.clear();
                s_nested_cache_root = S.root_dir;
            }

            struct Row {
                int kind;
                int top_idx;
                int nested_idx;
                std::string nested_name;
            };
            std::vector<Row> rows;
            rows.reserve(paths.size() + 64);
            for (size_t idx = 0; idx < paths.size(); ++idx) {
                rows.push_back({0, (int)idx, -1, {}});
                const auto& p = paths[idx];
                std::string label = std::filesystem::path(p).filename().string();
                std::string label_lower = label;
                std::transform(label_lower.begin(), label_lower.end(),
                               label_lower.begin(), ::tolower);
                bool is_container = (label_lower == "levels.bnk" ||
                                     label_lower == "streaming.bnk");
                bool is_expanded  = S.expanded_bnks.count(p) > 0;
                if (is_container && is_expanded) {
                    auto it_cache = s_nested_cache.find(p);
                    if (it_cache == s_nested_cache.end()) {
                        std::vector<NestedChild> children;
                        try {
                            BNKReader reader(p);
                            const auto& files = reader.list_files();
                            for (size_t i = 0; i < files.size(); ++i) {
                                std::string fl = files[i].name;
                                std::transform(fl.begin(), fl.end(),
                                               fl.begin(), ::tolower);
                                if (fl.size() >= 4 &&
                                    fl.substr(fl.size() - 4) == ".bnk") {
                                    children.push_back({(int)i, files[i].name});
                                }
                            }
                        } catch (...) {}
                        it_cache = s_nested_cache.emplace(p, std::move(children)).first;
                    }
                    for (const auto& c : it_cache->second) {
                        rows.push_back({1, (int)idx, c.index, c.name});
                    }
                }
            }

            ImGuiListClipper clipper;
            clipper.Begin((int)rows.size());
            while (clipper.Step()) {
                for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                    const Row& row = rows[(size_t)r];
                    if (row.kind == 0) {
                        const auto& p = paths[(size_t)row.top_idx];
                        ImGui::PushID(r);

                        std::string label = std::filesystem::path(p)
                                                .filename().string();
                        std::string label_lower = label;
                        std::transform(label_lower.begin(), label_lower.end(),
                                       label_lower.begin(), ::tolower);
                        bool is_container = (label_lower == "levels.bnk" ||
                                             label_lower == "streaming.bnk");
                        bool is_expanded  = S.expanded_bnks.count(p) > 0;
                        if (is_container) {
                            label = (is_expanded ? "- " : "+ ") + label;
                        }

                        bool drilled_here =
                            (g_bnk_drill.kind == DrillKind::Bnk &&
                             g_bnk_drill.bnk_path == p);
                        if (ImGui::Selectable(label.c_str(), drilled_here,
                                              ImGuiSelectableFlags_SpanAllColumns) &&
                            a_can_click) {
                            if (is_container) {

                                if (is_expanded) S.expanded_bnks.erase(p);
                                else             S.expanded_bnks.insert(p);
                            } else {

                                drill_open_bnk(g_bnk_drill, p,
                                               false);
                            }
                        }

                        if (ImGui::BeginPopupContextItem()) {
                            if (ImGui::MenuItem("Extract")) {
                                extract_single_bnk_contents(p);
                            }
                            ImGui::EndPopup();
                        }
                        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(p.c_str());
                            ImGui::EndTooltip();
                        }
                        ImGui::PopID();
                    } else {

                        const auto& p = paths[(size_t)row.top_idx];
                        const std::string& nested_name = row.nested_name;
                        int nested_idx = row.nested_idx;

                        ImGui::PushID(r);
                        std::string nested_label =
                            "    " + std::filesystem::path(nested_name)
                                         .filename().string();
                        bool drilled_here =
                            (g_bnk_drill.kind == DrillKind::Bnk &&
                             g_bnk_drill.from_nested &&
                             std::filesystem::path(g_bnk_drill.bnk_path)
                                 .filename() ==
                             std::filesystem::path(nested_name)
                                 .filename());
                        if (ImGui::Selectable(nested_label.c_str(), drilled_here,
                                              ImGuiSelectableFlags_SpanAllColumns) &&
                            a_can_click) {
                            try {
                                auto tmpdir = std::filesystem::temp_directory_path()
                                            / "f2_nested_bnk";
                                std::error_code ec;
                                std::filesystem::create_directories(tmpdir, ec);
                                auto tmp_nested = tmpdir /
                                    (std::to_string(std::hash<std::string>{}(nested_name)) + ".bnk");
                                extract_one(p, nested_idx, tmp_nested.string());
                                drill_open_bnk(g_bnk_drill,
                                               tmp_nested.string(),
                                               true);
                            } catch (const std::exception& e) {
                                OutputLog::error(std::string(
                                    "Failed to extract nested BNK ") +
                                    nested_name + ": " + e.what());
                            } catch (...) {
                                OutputLog::error(std::string(
                                    "Failed to extract nested BNK ") +
                                    nested_name);
                            }
                        }
                        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(nested_name.c_str());
                            ImGui::EndTooltip();
                        }
                        ImGui::PopID();
                    }
                }
            }
            clipper.End();
            }
            ImGui::EndChild();

            ImGui::SameLine(0.0f, 0.0f);

            ImGui::BeginChild("##bnk_page_b", ImVec2(page_w, page_h), false);
            if (b_visible) {

            const bool b_can_click = (g_bnk_drill.target_t == 1.0f) &&
                                      drill_settled(g_bnk_drill);

            {

                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                      ImVec4(0.30f, 0.45f, 0.65f, 0.40f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                      ImVec4(0.40f, 0.60f, 0.90f, 0.55f));
                if (ImGui::Button(ICON_FA_ARROW_LEFT "##drill_back")) {
                    drill_back(g_bnk_drill);
                }
                ImGui::PopStyleColor(3);
                ImGui::SameLine();
                ImGui::TextUnformatted(g_bnk_drill.title.c_str());
            }

            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##drill_filter", "Filter",
                                     &g_bnk_drill.filter);

            std::string flt = g_bnk_drill.filter;
            std::transform(flt.begin(), flt.end(), flt.begin(), ::tolower);
            std::vector<int> vis;
            vis.reserve(g_bnk_drill.items.size());
            for (size_t i = 0; i < g_bnk_drill.items.size(); ++i) {
                if (flt.empty()) {
                    vis.push_back((int)i);
                } else {
                    std::string n =
                        std::filesystem::path(g_bnk_drill.items[i].name)
                            .filename().string();
                    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                    if (n.find(flt) != std::string::npos)
                        vis.push_back((int)i);
                }
            }

            ImGui::BeginChild("##drill_list", ImVec2(0, 0), false);

            ImGuiListClipper drill_clipper;
            drill_clipper.Begin((int)vis.size());
            while (drill_clipper.Step()) {
                for (int r = drill_clipper.DisplayStart;
                     r < drill_clipper.DisplayEnd; ++r) {
                    int idx = vis[(size_t)r];
                    const BNKItemUI& it = g_bnk_drill.items[(size_t)idx];
                    ImGui::PushID(r);

                    std::string label =
                        std::filesystem::path(it.name).filename().string();
                    bool selected =
                        (g_bnk_drill.kind == DrillKind::Bnk &&
                         S.selected_bnk == g_bnk_drill.bnk_path &&
                         S.selected_file_index >= 0 &&
                         S.selected_file_index < (int)S.files.size() &&
                         S.files[(size_t)S.selected_file_index].index == it.index);
                    if (ImGui::Selectable(label.c_str(), selected,
                                          ImGuiSelectableFlags_SpanAllColumns) &&
                        b_can_click) {

                        if (g_bnk_drill.kind == DrillKind::Bnk) {

                            if (S.selected_bnk != g_bnk_drill.bnk_path) {
                                S.viewing_adb = false;
                                S.viewing_lua = false;
                                S.global_search.clear();
                                S.selected_nested_bnk.clear();
                                S.selected_nested_index = -1;
                                pick_bnk(g_bnk_drill.bnk_path);
                            }
                            if (g_bnk_drill.from_nested) {
                                S.selected_nested_temp_path = g_bnk_drill.bnk_path;
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
                                        S.show_gdb_render = false;
                                        g_pending_mdl_full_path = it.name;
                                        g_pending_mdl_load = true;
                                        g_pending_mdl_index = (int)j;
                                    } else if (ln.size() >= 4 &&
                                               ln.rfind(".tex") == ln.size() - 4) {
                                        S.show_gdb_render = false;
                                        g_pending_tex_load = true;
                                        g_pending_tex_index = (int)j;
                                    } else if (ln.size() >= 4 &&
                                               ln.rfind(".gdb") == ln.size() - 4) {
                                        open_gdb_viewer_for_bnk_entry(
                                            g_bnk_drill.bnk_path,
                                            it.index,
                                            it.name);
                                    } else if (ln.size() >= 4 &&
                                               ln.rfind(".wav") == ln.size() - 4) {

                                        S.show_gdb_render = false;
                                        open_audio_player_for_selected((int)j);
                                    }
                                    break;
                                }
                            }
                        } else if (g_bnk_drill.kind == DrillKind::Adb) {
                            S.viewing_adb = true;
                            S.viewing_lua = false;
                            S.show_gdb_render = false;
                            S.selected_bnk.clear();
                            S.global_search.clear();
                            S.files.clear();
                            S.selected_file_index = -1;
                            for (size_t i = 0; i < S.adb_paths.size(); ++i) {
                                std::error_code ec;
                                auto fs = std::filesystem::file_size(S.adb_paths[i], ec);
                                S.files.push_back({(int)i, S.adb_paths[i],
                                                   ec ? 0u : (uint32_t)fs});
                            }
                            S.selected_file_index = idx;
                        } else if (g_bnk_drill.kind == DrillKind::Lua) {
                            select_lua_script((size_t)idx);
                        }
                    }

                    if (g_bnk_drill.kind == DrillKind::Bnk) {
                        file_context_menu(g_bnk_drill.bnk_path,
                                              it.index,
                                              g_bnk_drill.from_nested,
                                              it.name);
                    }
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
        }
