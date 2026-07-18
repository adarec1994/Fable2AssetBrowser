    bool has_mdl_files = false;
    if (!S.global_search.empty()) {
        for (const auto& h : g_global_hits) {
            if (is_mdl_file(h.file_name)) {
                has_mdl_files = true;
                break;
            }
        }
    } else {
        has_mdl_files = is_model_bnk_selected() && any_mdl_in_bnk();
    }

    if (!has_mdl_files || S.viewing_adb) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Export All to GLB")) {
        ImGui::OpenPopup("progress_win");
        if (!S.global_search.empty()) {
            on_export_global_mdl_to_glb(g_global_hits);
        } else {
            on_export_all_mdl_to_glb();
        }
    }
    if (!S.hide_tooltips && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Export all .mdl files to GLB format");
        ImGui::EndTooltip();
    }
    if (!has_mdl_files || S.viewing_adb) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    bool can_export_mdl = false;
    if (has_selection && !S.viewing_adb) {
        std::string n = S.files[(size_t)S.selected_file_index].name;
        std::string l = n;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        can_export_mdl = l.size() >= 4 && l.rfind(".mdl") == l.size() - 4;
    }

    if (!can_export_mdl) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Export to GLB")) {
        ImGui::OpenPopup("progress_win");
        on_export_mdl_to_glb();
    }
    if (!S.hide_tooltips && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Export selected .mdl file to GLB format");
        ImGui::EndTooltip();
    }
    if (!can_export_mdl) {
        ImGui::EndDisabled();
    }
