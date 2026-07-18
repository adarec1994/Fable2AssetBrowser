        if (s_active_tab == 8) {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##item_filter", "Filter",
                                     &S.item_filter);
            std::string flow = S.item_filter;
            std::transform(flow.begin(), flow.end(), flow.begin(),
                           ::tolower);
            if (g_item_details.empty()) {
                ImGui::TextDisabled("No items indexed yet.");
                ImGui::TextDisabled(
                    "Open (load) a level to populate the item list.");
            } else {
                std::vector<int> vis;
                vis.reserve(g_item_details.size());
                for (int i = 0; i < (int)g_item_details.size(); ++i) {


                    if (g_item_details[i].is_money) continue;
                    if (flow.empty()) { vis.push_back(i); continue; }
                    std::string low = g_item_details[i].display_name;
                    std::transform(low.begin(), low.end(), low.begin(),
                                   ::tolower);
                    if (low.find(flow) != std::string::npos) {
                        vis.push_back(i);
                    }
                }
                ImGui::BeginChild("items_list", ImVec2(0, 0), false);
                ImGuiListClipper clipper;
                clipper.Begin((int)vis.size());
                while (clipper.Step()) {
                    for (int r = clipper.DisplayStart;
                         r < clipper.DisplayEnd; ++r) {
                        const int idx = vis[r];
                        const auto& it = g_item_details[idx];
                        ImGui::PushID(idx);
                        const bool sel = (S.selected_item == idx);
                        const char* row_name =
                            it.display_name.empty() ? it.label.c_str()
                                                    : it.display_name
                                                          .c_str();
                        if (ImGui::Selectable(row_name, sel)) {
                            extern std::atomic<bool> g_item_icon_dirty;
                            ContentTabs::OpenItem(idx, row_name);
                            S.selected_item = idx;
                            S.show_item_details = true;
                            g_item_icon_dirty = true;
#ifdef _WIN32
                            const FlatAssetEntry* hit = nullptr;
                            if (!it.model_path.empty()) {
                                hit = find_model_by_path_left(
                                    it.model_path);
                            }
                            if (!hit && it.model_path_hash) {
                                hit = find_model_by_path_hash_left(
                                    it.model_path_hash);
                            }
                            if (hit) {
                                extern std::atomic<bool>
                                    g_pending_mdl_is_item;
                                load_flat_asset_entry(*hit, 0);
                                g_pending_mdl_is_item = true;
                            }
#endif
                        }
                        ImGui::PopID();
                    }
                }
                clipper.End();
                ImGui::EndChild();
            }
        }
