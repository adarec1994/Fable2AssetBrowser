    ImGui::BeginChild("right_panel", ImVec2(0, 0), false);

    ImGui::BeginChild("extract_box", ImVec2(0, 100), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginGroup();
    ImGui::PushItemWidth(-1);

    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 0));

    ImGui::BeginGroup();

    if (!S.viewing_adb && !S.viewing_lua) {
        if (ImGui::Button("Dump All Files")) {
            ImGui::OpenPopup("progress_win");
            if (!S.global_search.empty()) {
                on_dump_all_global(g_global_hits);
            } else {
                on_dump_all_raw();
            }
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            if (!S.global_search.empty()) {
                ImGui::TextUnformatted("DUMPS ALL FILTERED GLOBAL RESULTS");
            } else {
                ImGui::TextUnformatted("DUMPS ALL FILES IN THE CURRENT BANK");
            }
            ImGui::EndTooltip();
        }

        ImGui::SameLine();
        bool has_selection = (S.selected_file_index >= 0 && S.selected_file_index < (int)S.files.size());
        if (!has_selection) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Dump File")) {
            ImGui::OpenPopup("progress_win");
            on_extract_selected_raw();
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Dump the selected file raw");
            ImGui::EndTooltip();
        }
        if (!has_selection) {
            ImGui::EndDisabled();
        }

        bool has_wav_files = false;
        if (!S.global_search.empty()) {
            for (const auto& h : g_global_hits) {
                if (is_audio_file(h.file_name)) {
                    has_wav_files = true;
                    break;
                }
            }
        } else {
            has_wav_files = any_wav_in_bnk();
        }

        ImGui::SameLine();
        if (has_wav_files) {
            if (ImGui::Button("Export WAV's")) {
                ImGui::OpenPopup("progress_win");
                if (!S.global_search.empty()) {
                    on_export_wavs_global(g_global_hits);
                } else {
                    on_export_wavs();
                }
            }
        }
        if (has_wav_files && !S.hide_tooltips && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Convert and export only the .wav files");
            ImGui::EndTooltip();
        }

        bool has_tex_files = false;
        if (!S.global_search.empty()) {
            for (const auto& h : g_global_hits) {
                if (is_tex_file(h.file_name)) {
                    has_tex_files = true;
                    break;
                }
            }
        } else {
            has_tex_files = is_texture_bnk_selected() && any_tex_in_bnk();
        }

        if (has_tex_files) {
            ImGui::SameLine();
            if (ImGui::Button("Rebuild and Extract All (.tex)")) {
                ImGui::OpenPopup("progress_win");
                if (!S.global_search.empty()) {
                    on_rebuild_and_extract_global_tex(g_global_hits);
                } else {
                    on_rebuild_and_extract();
                }
            }
            if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("Rebuilds every .tex file bitstream");
                ImGui::EndTooltip();
            }
        }

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

        if (has_mdl_files) {
            ImGui::SameLine();
            if (ImGui::Button("Rebuild and Extract All (.mdl)")) {
                ImGui::OpenPopup("progress_win");
                if (!S.global_search.empty()) {
                    on_rebuild_and_extract_global_mdl(g_global_hits);
                } else {
                    on_rebuild_and_extract_models();
                }
            }
            if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("Rebuilds every .mdl file bitstream");
                ImGui::EndTooltip();
            }
        }

        if (S.selected_file_index >= 0) {
            bool can_wav = false;
            if (S.selected_file_index >= 0 && S.selected_file_index < (int) S.files.size()) {
                std::string n = S.files[(size_t) S.selected_file_index].name;
                std::string l = n;
                std::transform(l.begin(), l.end(), l.begin(), ::tolower);
                can_wav = l.size() >= 4 && l.rfind(".wav") == l.size() - 4;
            }
            if (can_wav) {
                ImGui::SameLine();
                if (ImGui::Button("Play")) {
                    open_audio_player_for_selected(S.selected_file_index);
                }
                ImGui::SameLine();
                if (ImGui::Button("Extract WAV")) {
                    ImGui::OpenPopup("progress_win");
                    on_extract_selected_wav();
                }
            }
            bool can_tex = false, can_mdl = false;
            if (S.selected_file_index >= 0 && S.selected_file_index < (int) S.files.size()) {
                std::string n = S.files[(size_t) S.selected_file_index].name;
                std::string l = n;
                std::transform(l.begin(), l.end(), l.begin(), ::tolower);
                can_tex = l.size() >= 4 && l.rfind(".tex") == l.size() - 4;
                can_mdl = l.size() >= 4 && l.rfind(".mdl") == l.size() - 4;
            }

            if (can_tex && is_texture_bnk_selected()) {
                ImGui::SameLine();
                if (ImGui::Button("Rebuild and Extract (.tex)")) {
                    auto name = S.files[(size_t) S.selected_file_index].name;
                    ImGui::OpenPopup("progress_win");
                    on_rebuild_and_extract_one(name);
                }
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted("Rebuilds the .tex file bitstreams");
                    ImGui::EndTooltip();
                }
            }

            if (can_mdl && is_model_bnk_selected()) {
                ImGui::SameLine();
                if (ImGui::Button("Rebuild and Extract (.mdl)")) {
                    auto name = S.files[(size_t) S.selected_file_index].name;
                    ImGui::OpenPopup("progress_win");
                    on_rebuild_and_extract_one_mdl(name);
                }
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted("Rebuilds the .mdl file bitstreams");
                    ImGui::EndTooltip();
                }
            }
        }
    } else if (S.viewing_adb) {
        if (ImGui::Button("Extract All Uncompressed")) {
            ImGui::OpenPopup("progress_win");
            on_extract_all_adb();
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Extract all ADB files uncompressed to /extracted/audio_database/");
            ImGui::EndTooltip();
        }

        ImGui::SameLine();
        bool has_selection = (S.selected_file_index >= 0 && S.selected_file_index < (int)S.files.size());
        if (!has_selection) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Extract Uncompressed")) {
            ImGui::OpenPopup("progress_win");
            on_extract_adb_selected();
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Extract selected ADB file uncompressed");
            ImGui::EndTooltip();
        }
        if (!has_selection) {
            ImGui::EndDisabled();
        }
    } else if (S.viewing_lua) {
        bool has_selection = (S.selected_file_index >= 0 && S.selected_file_index < (int)S.files.size());

        if (!has_selection) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Dump Lua")) {
            if (has_selection && S.selected_file_index < (int)S.lua_files.size()) {
                std::string path = S.lua_files[S.selected_file_index].path;
                dump_single_lua_file(path, S.root_dir);
            }
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Decompile and dump selected Lua file");
            ImGui::EndTooltip();
        }
        if (!has_selection) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Dump All Lua")) {
            dump_all_lua_files(S.root_dir);
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Decompile and dump all Lua files");
            ImGui::EndTooltip();
        }
    }
