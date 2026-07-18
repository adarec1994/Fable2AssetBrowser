    if (g_pending_mdl_load && g_pending_mdl_index >= 0 && g_pending_mdl_index < (int)S.files.size()) {
        g_pending_mdl_load = false;
        auto item = S.files[(size_t)g_pending_mdl_index];
        auto name = item.name;
        std::string parse_path = g_pending_mdl_full_path.empty() ? name : g_pending_mdl_full_path;
        g_pending_mdl_full_path.clear();
        S.current_mdl_path.clear();
        S.current_mdl_path_hash = 0;
        S.anim_authored_signature = 0;
        S.anim_authored_cache.clear();

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
                if (std::filesystem::exists(S.selected_nested_temp_path)) {
                    std::filesystem::copy_file(S.selected_nested_temp_path, unique_temp,
                                              std::filesystem::copy_options::overwrite_existing, ec);
                    if (!ec) {
                        nested_temp_copy = unique_temp.string();
                        bnk_to_use = nested_temp_copy;
                    }
                }
            } catch (...) {}
        } else {
            bnk_to_use = S.selected_bnk;
        }

        if (!bnk_to_use.empty()) {
            std::thread([item, name, parse_path, bnk_to_use, nested_temp_copy, is_nested]() {
                std::vector<unsigned char> buf;
                bool ok = false;

                try {
                    if (is_nested) {
                        ok = reconstruct_nested_mdl(bnk_to_use, item.index, buf, parse_path);
                    } else {
                        ok = build_mdl_buffer_for_name(name, buf);
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
                        }
                    }
                } catch (...) {
                    ok = false;
                }

                if (!nested_temp_copy.empty()) {
                    std::error_code ec;
                    std::filesystem::remove(nested_temp_copy, ec);
                }

                if (ok && !buf.empty()) {
                    try {
                        S.mdl_info_ok = parse_mdl_info(buf, S.mdl_info, parse_path);
                        if (S.mdl_info_ok) {
                            S.current_mdl_path = parse_path;
                            S.current_mdl_path_hash =
                                Anim::gdb_model_path_hash(parse_path);
                            S.anim_authored_signature = 0;
                            S.anim_authored_cache.clear();
                            S.mdl_meshes.clear();
                            build_mdl_engine_geometry(buf, S.mdl_meshes);
                            bool geom_ok = true;
                            if (geom_ok) {
                                S.pending_model_tab_capture = true;
                                S.pending_preview_build = true;
                            } else {
                                ok = false;
                            }
                        } else {
                            ok = false;
                        }
                    } catch (...) {
                        ok = false;
                    }
                }
            }).detach();
        }
        g_pending_mdl_index = -1;
    }
