if (!can_preview) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Preview")) {
#ifdef _WIN32
        if (can_folder_preview && !S.selected_folder_path.empty()) {
            std::vector<std::pair<std::string, std::string>> mdl_paths;
            if (find_mdl_files_in_folder(g_tree_root, S.selected_folder_path, mdl_paths)) {
                progress_open(0, "Loading preview...");

                std::thread([mdl_paths]() {
                    std::vector<MDLMeshGeom> all_meshes;
                    MDLInfo combined_info;
                    bool any_success = false;

                    for (const auto& [mdl_path, bnk_source] : mdl_paths) {
                        std::vector<unsigned char> buf;
                        bool ok = false;

                        try {
                            ok = build_mdl_buffer_for_name(mdl_path, buf);
                        } catch (...) {}

                        if (ok && !buf.empty()) {
                            MDLInfo mdl_info;
                            if (parse_mdl_info(buf, mdl_info, mdl_path)) {
                                std::vector<MDLMeshGeom> meshes;
                                if (build_mdl_engine_geometry(buf, meshes)) {
                                    all_meshes.insert(all_meshes.end(), meshes.begin(), meshes.end());
                                    if (!any_success) {
                                        combined_info = mdl_info;
                                        any_success = true;
                                    }
                                }
                            }
                        }
                    }

                    if (any_success && !all_meshes.empty()) {
                        S.hex_data.clear();
                        S.mdl_info_ok = true;
                        S.mdl_info = combined_info;
                        S.mdl_meshes = all_meshes;

                        S.cam_yaw = 0.0f;
                        S.cam_pitch = 0.2f;
                        S.cam_dist = 3.0f;
                        S.pending_preview_build = true;
                    }

                    progress_done();
                    if (!any_success) {
                        show_error_box("Failed to load preview.");
                    }
                }).detach();
            }
        } else {
        auto item = S.files[(size_t)S.selected_file_index];
        auto name = item.name;

        {
            std::string filename = std::filesystem::path(name).filename().string();
            std::string filename_lower = filename;
            std::transform(filename_lower.begin(), filename_lower.end(), filename_lower.begin(), ::tolower);
            bool is_interior_or_exterior = (filename_lower == "interior.mdl" || filename_lower == "exterior.mdl");

            if (is_interior_or_exterior && can_mdl) {

                std::filesystem::path full_path(name);
                std::string folder_path = full_path.parent_path().string();

                std::vector<std::pair<std::string, int>> mdl_files;

                for (size_t i = 0; i < S.files.size(); ++i) {
                    const auto& file = S.files[i];
                    std::filesystem::path file_path(file.name);
                    std::string file_folder = file_path.parent_path().string();
                    std::string file_name_lower = file_path.filename().string();
                    std::transform(file_name_lower.begin(), file_name_lower.end(), file_name_lower.begin(), ::tolower);

                    if (file_folder == folder_path &&
                        (file_name_lower == "interior.mdl" || file_name_lower == "exterior.mdl")) {
                        mdl_files.push_back({file.name, file.index});
                    }
                }

                if (mdl_files.size() > 0) {

                    progress_open(0, "Loading preview...");

                    std::thread([mdl_files]() {
                        std::vector<MDLMeshGeom> all_meshes;
                        MDLInfo combined_info;
                        bool any_success = false;

                        for (const auto& [mdl_name, mdl_index] : mdl_files) {
                            std::vector<unsigned char> buf;
                            bool ok = false;

                            try {
                                ok = build_mdl_buffer_for_name(mdl_name, buf);
                            } catch (...) {}

                            if (ok && !buf.empty()) {
                                MDLInfo mdl_info;
                                if (parse_mdl_info(buf, mdl_info, mdl_name)) {
                                    std::vector<MDLMeshGeom> meshes;
                                    if (build_mdl_engine_geometry(buf, meshes)) {
                                        all_meshes.insert(all_meshes.end(), meshes.begin(), meshes.end());
                                        if (!any_success) {
                                            combined_info = mdl_info;
                                            any_success = true;
                                        }
                                    }
                                }
                            }
                        }

                        if (any_success && !all_meshes.empty()) {
                            S.hex_data.clear();
                            S.mdl_info_ok = true;
                            S.mdl_info = combined_info;
                            S.mdl_meshes = all_meshes;

                            S.cam_yaw = 0.0f;
                            S.cam_pitch = 0.2f;
                            S.cam_dist = 3.0f;
                            S.pending_preview_build = true;
                        }

                        progress_done();
                        if (!any_success) {
                            show_error_box("Failed to load preview.");
                        }
                    }).detach();

                    goto skip_preview;
                }

            }
        }

        {
        std::string bnk_to_use;
        std::string nested_temp_copy;
        bool is_nested = false;

        if (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty()) {
            is_nested = true;
            auto tmpdir = std::filesystem::temp_directory_path() / "f2_preview";
            std::error_code ec;
            std::filesystem::create_directories(tmpdir, ec);

            auto unique_temp = tmpdir / ("nested_" + std::to_string(std::hash<std::string>{}(S.selected_nested_temp_path + std::to_string(std::time(nullptr)))) + ".bnk");

            try {
                if (!std::filesystem::exists(S.selected_nested_temp_path)) {
                    show_error_box("Nested BNK source file does not exist");
                    goto skip_preview;
                }

                std::filesystem::copy_file(S.selected_nested_temp_path, unique_temp,
                                          std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) {
                    nested_temp_copy = unique_temp.string();
                    bnk_to_use = nested_temp_copy;
                } else {
                    show_error_box("Failed to copy nested BNK: " + ec.message());
                    goto skip_preview;
                }
            } catch (const std::exception& e) {
                show_error_box(std::string("Exception copying nested BNK: ") + e.what());
                goto skip_preview;
            }
        } else {
            bnk_to_use = S.selected_bnk;
        }

        progress_open(0, "Loading preview...");

        std::string preferred_for_tex = is_nested
            ? S.selected_nested_temp_path
            : S.selected_bnk;

        std::thread([item, name, can_tex, can_mdl, bnk_to_use, nested_temp_copy, is_nested, preferred_for_tex]() {
            std::vector<unsigned char> buf;
            bool ok = false;
            try {
                if (can_tex) {
                    ok = build_any_tex_buffer_for_name(name, buf, preferred_for_tex);
                } else if (can_mdl) {
                    if (is_nested) {
                        ok = reconstruct_nested_mdl(bnk_to_use, item.index, buf, name);
                    } else {
                        ok = build_mdl_buffer_for_name(name, buf);
                    }
                }

                if (!ok) {
                    auto tmpdir = std::filesystem::temp_directory_path() / "f2_preview";
                    std::error_code ec;
                    std::filesystem::create_directories(tmpdir, ec);
                    auto tmp_file = tmpdir / ("preview_" + std::to_string(std::hash<std::string>{}(name + std::to_string(std::time(nullptr)))) + ".bin");

                    try {
                        extract_one(bnk_to_use, item.index, tmp_file.string());
                        buf = read_all_bytes(tmp_file);
                        ok = !buf.empty();
                        std::filesystem::remove(tmp_file, ec);
                    } catch (...) {
                        std::filesystem::remove(tmp_file, ec);
                        throw;
                    }
                }
            } catch (...) {
                ok = false;
            }

            if (!nested_temp_copy.empty()) {
                std::error_code ec;
                std::filesystem::remove(nested_temp_copy, ec);
            }

            if (ok) {
                S.hex_data = buf;

                if (can_tex) {
                    S.tex_info_ok = parse_tex_info(S.hex_data, S.tex_info);
                    if (S.tex_info_ok && !S.tex_info.Mips.empty()) {

                        int best_mip = -1;
                        size_t best_area = 0;
                        for (int i = 0; i < (int)S.tex_info.Mips.size(); ++i) {
                            int w = S.tex_info.Mips[i].HasWH ? (int)S.tex_info.Mips[i].MipWidth : std::max(1, (int)S.tex_info.TextureWidth >> i);
                            int h = S.tex_info.Mips[i].HasWH ? (int)S.tex_info.Mips[i].MipHeight : std::max(1, (int)S.tex_info.TextureHeight >> i);
                            size_t area = (size_t)w * (size_t)h;
                            if (area > best_area) {
                                best_area = area;
                                best_mip = i;
                            }
                        }
                        if (best_mip >= 0) {
                            S.preview_mip_index = best_mip;
                            S.show_preview_popup = true;
                        } else {
                        }
                    } else if (!S.tex_info_ok) {
                    }
                } else if (can_mdl) {
                    S.mdl_info_ok = parse_mdl_info(S.hex_data, S.mdl_info, name);
                    if (S.mdl_info_ok) {
                        S.mdl_meshes.clear();
                        build_mdl_engine_geometry(S.hex_data, S.mdl_meshes);
                        S.current_mdl_path = name;
                        S.current_mdl_path_hash =
                            Anim::gdb_model_path_hash(name);
                        S.cam_yaw = 0.0f; S.cam_pitch = 0.2f; S.cam_dist = 3.0f;
                        S.pending_model_tab_capture = true;
                        S.pending_preview_build = true;
                    }
                }
            }

            progress_done();
            if (!ok) show_error_box("Failed to load preview.");
        }).detach();
        }

        skip_preview:;
    }
#else

        if (can_folder_preview && !S.selected_folder_path.empty()) {
            std::vector<std::pair<std::string, std::string>> mdl_paths;
            if (find_mdl_files_in_folder(g_tree_root, S.selected_folder_path, mdl_paths)) {
                progress_open(0, "Loading preview...");

                std::thread([mdl_paths]() {
                    std::vector<MDLMeshGeom> all_meshes;
                    MDLInfo combined_info;
                    bool any_success = false;

                    for (const auto& [mdl_path, bnk_source] : mdl_paths) {
                        std::vector<unsigned char> buf;
                        bool ok = false;

                        try {
                            ok = build_mdl_buffer_for_name(mdl_path, buf);
                        } catch (...) {}

                        if (ok && !buf.empty()) {
                            MDLInfo mdl_info;
                            if (parse_mdl_info(buf, mdl_info, mdl_path)) {
                                std::vector<MDLMeshGeom> meshes;
                                if (build_mdl_engine_geometry(buf, meshes)) {
                                    all_meshes.insert(all_meshes.end(), meshes.begin(), meshes.end());
                                    if (!any_success) {
                                        combined_info = mdl_info;
                                        any_success = true;
                                    }
                                }
                            }
                        }
                    }

                    if (any_success && !all_meshes.empty()) {
                        S.hex_data.clear();
                        S.mdl_info_ok = true;
                        S.mdl_info = combined_info;
                        S.mdl_meshes = all_meshes;
                        S.cam_yaw = 0.0f;
                        S.cam_pitch = 0.2f;
                        S.cam_dist = 3.0f;
                        S.pending_preview_build = true;
                    }

                    progress_done();
                    if (!any_success) {
                        show_error_box("Failed to load preview.");
                    }
                }).detach();
            }
        } else if (can_mdl) {
            auto item = S.files[(size_t)S.selected_file_index];
            auto name = item.name;
            progress_open(0, "Loading preview...");

            std::thread([name]() {
                bool ok = false;
                std::vector<unsigned char> buf;

                try {
                    ok = build_mdl_buffer_for_name(name, buf);
                } catch (...) {}

                if (ok && !buf.empty()) {
                    MDLInfo mdl_info;
                    if (parse_mdl_info(buf, mdl_info, name)) {
                        std::vector<MDLMeshGeom> meshes;
                        if (build_mdl_engine_geometry(buf, meshes)) {
                            S.hex_data.clear();
                            S.mdl_info_ok = true;
                            S.mdl_info = mdl_info;
                            S.mdl_meshes = meshes;
                            S.current_mdl_path = name;
                            S.current_mdl_path_hash =
                                Anim::gdb_model_path_hash(name);
                            S.cam_yaw = 0.0f;
                            S.cam_pitch = 0.2f;
                            S.cam_dist = 3.0f;
                            S.pending_model_tab_capture = true;
                            S.pending_preview_build = true;
                            ok = true;
                        }
                    }
                }

                progress_done();
                if (!ok) show_error_box("Failed to load preview.");
            }).detach();
        }
#endif
    }
    if (!can_preview) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
