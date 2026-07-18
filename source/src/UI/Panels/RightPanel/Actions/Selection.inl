    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0, 8));

    bool has_selection = (S.selected_file_index >= 0 && S.selected_file_index < (int)S.files.size());

    bool can_preview = false;
    bool can_tex = false, can_mdl = false;
    bool can_folder_preview = false;

    if (has_selection && !S.viewing_adb && !S.viewing_lua) {
        std::string n = S.files[(size_t)S.selected_file_index].name;
        std::string l = n;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        can_tex = l.size() >= 4 && l.rfind(".tex") == l.size() - 4;
        can_mdl = l.size() >= 4 && l.rfind(".mdl") == l.size() - 4;
        can_preview = can_tex || can_mdl;
    } else if (!S.selected_folder_path.empty()) {
        std::vector<std::pair<std::string, std::string>> mdl_paths;
        can_folder_preview = find_mdl_files_in_folder(g_tree_root, S.selected_folder_path, mdl_paths);
        can_preview = can_folder_preview;
    }
