    auto draw_flat_asset_tab = [](const char* ,
                                  std::vector<FlatAssetEntry>& entries,
                                  std::string& filter,
                                  const char* child_id,
                                  int kind,
                                  float footer_h = 0.0f,
                                  bool dedup_by_name_size = true,
                                  const char* drag_type = nullptr) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint(("##" + std::string(child_id) + "_filter").c_str(),
                                 "Filter", &filter);

        std::string flow = filter;
        std::transform(flow.begin(), flow.end(), flow.begin(), ::tolower);

        struct CacheEntry {
            const void* entries_ptr = nullptr;
            size_t      entries_size = 0;
            std::string filter_lc;
            bool        dedup = false;
            std::vector<int> vis;
            size_t      dups_skipped = 0;
        };
        static std::unordered_map<std::string, CacheEntry> cache;
        CacheEntry& c = cache[child_id];

        const bool cache_valid =
            c.entries_ptr == (const void*)entries.data() &&
            c.entries_size == entries.size() &&
            c.filter_lc == flow &&
            c.dedup == dedup_by_name_size;

        if (!cache_valid) {
            c.entries_ptr  = (const void*)entries.data();
            c.entries_size = entries.size();
            c.filter_lc    = flow;
            c.dedup        = dedup_by_name_size;
            c.vis.clear();
            c.vis.reserve(entries.size());
            c.dups_skipped = 0;

            std::unordered_set<std::string> seen_keys;
            for (size_t i = 0; i < entries.size(); ++i) {
                const auto& e = entries[i];
                if (!flow.empty()) {
                    std::string nlow = e.name;
                    std::transform(nlow.begin(), nlow.end(), nlow.begin(),
                                   ::tolower);
                    if (nlow.find(flow) == std::string::npos) continue;
                }
                if (dedup_by_name_size) {
                    std::string nlow = e.name;
                    std::transform(nlow.begin(), nlow.end(), nlow.begin(),
                                   ::tolower);
                    std::string k = nlow + "|" + std::to_string(e.size);
                    if (!seen_keys.insert(std::move(k)).second) {
                        ++c.dups_skipped;
                        continue;
                    }
                }
                c.vis.push_back((int)i);
            }
        }
        auto& vis = c.vis;
        const size_t dups_skipped = c.dups_skipped;

        if (S.dev_mode) {
            if (dedup_by_name_size && dups_skipped > 0) {
                ImGui::TextDisabled("%d / %zu  (%zu dup hidden)",
                    (int)vis.size(), entries.size(), dups_skipped);
            } else {
                ImGui::TextDisabled("%d / %zu", (int)vis.size(), entries.size());
            }
            ImGui::Separator();
        }

        const float child_h = (footer_h > 0.0f) ? -footer_h : 0.0f;
        ImGui::BeginChild(child_id, ImVec2(0, child_h), false);
        ImGuiListClipper clipper;
        clipper.Begin((int)vis.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const FlatAssetEntry& e = entries[(size_t)vis[(size_t)row]];
                ImGui::PushID(row);
                bool selected = (S.selected_bnk == e.bnk_path &&
                                 S.selected_file_index >= 0 &&
                                 S.selected_file_index < (int)S.files.size() &&
                                 S.files[(size_t)S.selected_file_index].index == e.file_index);
                if (ImGui::Selectable(e.name.c_str(), selected,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    load_flat_asset_entry(e, kind);
                }

                if (drag_type && !e.full_path.empty() &&
                    ImGui::BeginDragDropSource(
                        ImGuiDragDropFlags_SourceAllowNullID)) {
                    ImGui::SetDragDropPayload(drag_type,
                                              e.full_path.c_str(),
                                              e.full_path.size());
                    ImGui::TextUnformatted(e.name.c_str());
                    ImGui::EndDragDropSource();
                }

                file_hex_context_menu(e.bnk_path, e.file_index,
                                      e.from_nested, e.name);
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    if (S.dev_mode) {
                        ImGui::TextUnformatted(e.full_path.c_str());
                        ImGui::Text("Size: %u bytes", e.size);
                        ImGui::Text("BNK: %s",
                            std::filesystem::path(e.bnk_path).filename().string().c_str());
                        if (e.from_nested) ImGui::TextDisabled("(nested)");
                    } else {
                        ImGui::TextUnformatted(e.name.c_str());
                    }
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
        }
        clipper.End();
        ImGui::EndChild();
    };

