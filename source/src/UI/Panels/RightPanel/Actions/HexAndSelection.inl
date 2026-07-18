    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0, 8));

    bool has_selection = (S.selected_file_index >= 0 && S.selected_file_index < (int)S.files.size());

    if (S.dev_mode) {
        if (!has_selection) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Hex View")) {
            ImGui::OpenPopup("progress_win");
            if (S.viewing_adb) {
                auto item = S.files[(size_t)S.selected_file_index];
                progress_open(0, "Decompressing ADB...");
                std::thread([item]() {
                    auto entries = decompress_adb(item.name);
                    if (!entries.empty() && !entries[0].data.empty()) {
                        S.hex_data = entries[0].data;
                        S.hex_title = "Hex Editor - " + std::filesystem::path(item.name).filename().string() + " (decompressed)";
                        S.hex_open = true;
                        memset(&S.hex_state, 0, sizeof(S.hex_state));
                        S.hex_state.Bytes = (void*)S.hex_data.data();
                        S.hex_state.MaxBytes = (int)S.hex_data.size();
                        S.hex_state.ReadOnly = true;
                        S.hex_state.ShowAscii = true;
                        S.hex_state.ShowAddress = true;
                        S.hex_state.BytesPerLine = 16;
                    }
                    progress_done();
                    if (entries.empty() || entries[0].data.empty()) show_error_box("Failed to decompress ADB file.");
                }).detach();
            } else {
                open_hex_for_selected();
            }
        }
        if (!has_selection) {
            ImGui::EndDisabled();
        }
    }

    ImGui::SameLine();

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
