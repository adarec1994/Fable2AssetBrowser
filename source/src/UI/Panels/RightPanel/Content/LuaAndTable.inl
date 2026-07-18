    if (S.viewing_lua) {
        float available_height = ImGui::GetContentRegionAvail().y;
        float table_height = available_height * 0.35f;
        float preview_height = available_height - table_height - 8.0f;

        ImGui::BeginChild("lua_table_container", ImVec2(0, table_height), false);
        draw_file_table();
        ImGui::EndChild();

        if (S.selected_file_index >= 0 && S.selected_file_index < (int)S.lua_files.size()) {
            if (S.lua_preview_selected != S.selected_file_index && !S.lua_preview_loading) {
                S.lua_preview_selected = S.selected_file_index;
                S.lua_preview_title = S.lua_files[S.selected_file_index].filename;
                S.lua_preview_content.clear();
                S.lua_preview_loading = true;
                S.lua_preview_is_quest = false;
                S.quest_preview_select_nodes = false;
                const uint64_t preview_request = ++S.lua_preview_request;
                S.show_gdb_render = false;

                std::string path = S.lua_files[S.selected_file_index].path;
                std::string title = S.lua_preview_title;
                ContentTabs::OpenLua(path, title, false);

                progress_open(0, "Decompiling " + title + "...");

                std::thread([path, title, preview_request]() {
                    std::string content = read_lua_file_content(path);
                    ContentTabs::CompleteLua(path, content);
                    if (S.lua_preview_request.load() == preview_request) {
                        S.lua_preview_content = std::move(content);
                        S.lua_preview_loading = false;
                    }
                    progress_done();
                }).detach();
            }
        }

        ImGui::Dummy(ImVec2(0, 4));

        if (S.lua_preview_loading) {
            ImGui::BeginChild("lua_preview_loading", ImVec2(0, preview_height), true);
            ImGui::TextDisabled("Decompiling...");
            ImGui::EndChild();
        } else if (!S.lua_preview_content.empty()) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
            ImGui::BeginChild("lua_preview", ImVec2(0, preview_height), true, ImGuiWindowFlags_HorizontalScrollbar);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.7f, 1.0f));
            ImGui::TextUnformatted(S.lua_preview_title.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.9f, 0.8f, 1.0f));
            ImGui::TextUnformatted(S.lua_preview_content.c_str());
            ImGui::PopStyleColor();

            ImGui::EndChild();
            ImGui::PopStyleColor();
        } else {
            ImGui::BeginChild("lua_preview_empty", ImVec2(0, preview_height), true);
            ImGui::TextDisabled("Select a Lua file to preview");
            ImGui::EndChild();
        }
    } else {
        ImGui::BeginChild("right_table_container", ImVec2(0, 0), false);
        if (!S.global_search.empty()) {
            draw_global_results_table();
        } else {
            draw_file_table();
        }
        ImGui::EndChild();
    };
