    ImGui::PopStyleVar();
    ImGui::EndGroup();

    static bool hide_tt = false;
    if (ImGui::Checkbox("Hide Paths Tooltip", &hide_tt)) { S.hide_tooltips = hide_tt; }

    int visible = count_visible_files();
    ImGui::Text("Files found: %d/%d", visible, (int) S.files.size());

    ImGui::PopItemWidth();
    ImGui::EndGroup();
    ImGui::EndChild();

    {
        std::vector<std::string> exts = unique_file_extensions();
        ImGui::SetNextItemWidth(160.0f);
        const char* current_label = S.ext_filter.empty() ? "(all extensions)" : S.ext_filter.c_str();
        if (ImGui::BeginCombo("##ext_filter", current_label)) {
            if (ImGui::Selectable("(all extensions)", S.ext_filter.empty())) {
                S.ext_filter.clear();
            }
            for (const auto& e : exts) {
                bool is_selected = (e == S.ext_filter);
                if (ImGui::Selectable(e.c_str(), is_selected)) {
                    S.ext_filter = e;
                }
                if (is_selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Show only files with this extension");
            ImGui::EndTooltip();
        }
    }
