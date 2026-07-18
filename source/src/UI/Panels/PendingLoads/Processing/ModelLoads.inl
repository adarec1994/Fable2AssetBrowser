    if (g_pending_mdl_load && g_pending_mdl_index >= 0 && g_pending_mdl_index < (int)S.files.size()) {
        g_pending_mdl_load = false;
        S.item_model_active = g_pending_mdl_is_item.exchange(false);
        S.entity_model_active = false;
        S.show_entity_details = false;
        S.selected_entity = -1;
        if (!S.item_model_active) {
            S.show_item_details = false;
            S.selected_item = -1;
        }
        auto item = S.files[(size_t)g_pending_mdl_index];
        auto name = item.name;
        std::string parse_path = g_pending_mdl_full_path.empty() ? name : g_pending_mdl_full_path;
        g_pending_mdl_full_path.clear();

#ifdef _WIN32
        if (S.texture_window_srv) {
            S.texture_window_srv->Release();
            S.texture_window_srv = nullptr;
        }

        extern ID3D11ShaderResourceView* g_tex_popout_srv;
        extern std::string                g_tex_popout_name;
        extern bool                       g_tex_popout_open;
        extern int                        g_tex_popout_mesh_idx;
        g_tex_popout_srv      = nullptr;
        g_tex_popout_name.clear();
        g_tex_popout_open     = false;
        g_tex_popout_mesh_idx = -1;
#else
        if (S.texture_window_gl) {
            glDeleteTextures(1, &S.texture_window_gl);
            S.texture_window_gl = 0;
        }
        extern unsigned int g_tex_popout_gl;
        extern std::string  g_tex_popout_name;
        extern bool         g_tex_popout_open;
        extern int          g_tex_popout_mesh_idx;
        g_tex_popout_gl       = 0;
        g_tex_popout_name.clear();
        g_tex_popout_open     = false;
        g_tex_popout_mesh_idx = -1;
#endif
        S.texture_window_name.clear();
        S.texture_window_width  = 0;
        S.texture_window_height = 0;
        S.texture_blob.clear();
        S.tex_info_ok           = false;
        S.show_texture_window   = false;
        S.show_preview_popup    = false;
        S.preview_mip_index     = -1;

        if (g_mp.has_model) g_mp.has_model = false;
        S.mdl_info_ok        = false;
        S.show_model_preview = false;
        S.model_preview_open = false;
        S.model_materials_open = false;
        S.current_mdl_path.clear();
        S.current_mdl_path_hash = 0;
        S.anim_authored_signature = 0;
        S.anim_authored_cache.clear();
        S.selected_bone      = -1;
        S.bone_rotate_mode   = false;

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

            std::vector<unsigned char> buf;
            bool ok = false;
            try {
                if (is_nested) {
                    ok = reconstruct_nested_mdl(bnk_to_use, item.index, buf, parse_path);
                } else {
                    ok = build_mdl_buffer_for_name(name, buf);
                }
                if (!ok) {
                    try {
                        buf = BnkCache::extract_bytes(bnk_to_use, item.index);
                        ok  = !buf.empty();
                    } catch (...) {}
                }
            } catch (...) { ok = false; }

            if (!nested_temp_copy.empty()) {
                std::error_code ec;
                std::filesystem::remove(nested_temp_copy, ec);
            }

            if (ok && !buf.empty()) {
                S.mdl_info_ok = parse_mdl_info(buf, S.mdl_info, parse_path);
                if (!S.mdl_info_ok) {
                    OutputLog::error("MDL: parse_mdl_info FAILED for '" + name +
                                     "' (buf=" + std::to_string(buf.size()) + " bytes)");
                    if (S.mdl_info.MeshCount > 0) {
                        if (reparse_mdl_missing_buffers_optstr(buf, S.mdl_info)) {
                            OutputLog::info("MDL: optstr-scan fallback recovered after parse failure");
                            S.mdl_info_ok = true;
                        }
                    }
                    if (!S.mdl_info_ok) {
                        if (reparse_mdl_as_foliage_48b(buf, S.mdl_info)) {
                            OutputLog::info("MDL: foliage-48b fallback recovered after parse failure");
                            S.mdl_info_ok = true;
                        }
                    }
                }
                if (S.mdl_info_ok) {
                    OutputLog::info("MDL: parsed '" + name + "' meshes=" +
                                    std::to_string(S.mdl_info.MeshCount) +
                                    " buffers=" + std::to_string(S.mdl_info.MeshBuffers.size()));
                    for (size_t mi = 0; mi < S.mdl_info.MeshBuffers.size(); ++mi) {
                        const auto& mb = S.mdl_info.MeshBuffers[mi];
                        std::string nm = (mi < S.mdl_info.Meshes.size())
                                       ? S.mdl_info.Meshes[mi].MeshName : std::string("?");
                        OutputLog::info("  mesh[" + std::to_string(mi) + "] '" + nm +
                                        "' verts=" + std::to_string(mb.VertexCount) +
                                        " faces=" + std::to_string(mb.FaceCount) +
                                        " subs=" + std::to_string(mb.SubMeshCount) +
                                        " (in-list=" + std::to_string(mb.SubMeshes.size()) +
                                        ") alt=" + std::to_string(mb.IsAltPath ? 1 : 0) +
                                        " foliage=" + std::to_string(mb.IsFoliagePath ? 1 : 0));
                    }
                    if (!S.mdl_info.MeshBuffers.empty()) {
                        bool all_empty = true;
                        for (const auto& mb : S.mdl_info.MeshBuffers) {
                            if (mb.VertexCount > 0) { all_empty = false; break; }
                        }
                        if (all_empty) {
                            if (reparse_mdl_buffers_via_polymsh_scan(buf, S.mdl_info)) {
                                OutputLog::info("MDL: polymsh-scan fallback recovered " +
                                                std::to_string(S.mdl_info.MeshBuffers.size()) +
                                                " mesh buffer(s)");
                            } else {
                                OutputLog::warn("MDL: all buffers empty AND polymsh-scan fallback found nothing");
                            }
                        }
                    }
                    {
                        auto missing = [&]() -> size_t {
                            size_t n = 0;
                            if (S.mdl_info.MeshBuffers.size() < S.mdl_info.MeshCount) {
                                n += S.mdl_info.MeshCount - S.mdl_info.MeshBuffers.size();
                            }
                            for (const auto& mb : S.mdl_info.MeshBuffers) {
                                if (mb.VertexCount == 0) ++n;
                            }
                            return n;
                        };
                        if (missing() > 0) {
                            if (reparse_mdl_missing_buffers_optstr(buf, S.mdl_info)) {
                                OutputLog::info("MDL: optstr-scan fallback filled missing buffer(s)");
                            }
                        }
                        if (missing() > 0) {
                            if (reparse_mdl_as_foliage_48b(buf, S.mdl_info)) {
                                OutputLog::info("MDL: foliage-48b fallback filled the buffer");
                            }
                        }
                        {
                            std::string lp = parse_path;
                            std::transform(lp.begin(), lp.end(), lp.begin(),
                                           [](unsigned char c){ return (char)std::tolower(c); });
                            std::replace(lp.begin(), lp.end(), '\\', '/');
                            const bool multi_instance_target =
                                lp.find("bs_townhouse_basic_snow_v2") != std::string::npos &&
                                (lp.find("/exterior.mdl") != std::string::npos ||
                                 lp.find("/interior.mdl") != std::string::npos);
                            if (multi_instance_target) {
                                const size_t buffers_before_multi =
                                    S.mdl_info.MeshBuffers.size();
                                if (reparse_mdl_multi_instance_buffers(buf, S.mdl_info)) {
                                    OutputLog::info(
                                        "MDL: multi-instance fallback expanded " +
                                        std::to_string(buffers_before_multi) +
                                        " buffer(s) to " +
                                        std::to_string(S.mdl_info.MeshBuffers.size()));
                                }
                            }
                        }
                    }
                    S.mdl_meshes.clear();
                    build_mdl_engine_geometry(buf, S.mdl_meshes);
                    OutputLog::info("MDL: engine geometry produced " +
                                    std::to_string(S.mdl_meshes.size()) + " mesh(es)");
                    {
                        size_t nonempty = 0;
                        for (const auto& g : S.mdl_meshes) {
                            if (!g.positions.empty() && !g.indices.empty()) ++nonempty;
                        }
                        OutputLog::info("  -> " + std::to_string(nonempty) + " non-empty");
                    }
                }
                if (S.mdl_info_ok) {
                    S.current_mdl_path = parse_path;
                    S.current_mdl_path_hash =
                        Anim::gdb_model_path_hash(parse_path);
                    S.anim_authored_signature = 0;
                    S.anim_authored_cache.clear();
                    const size_t authored =
                        Anim::model_animation_binding_count_for_hash(
                            S.current_mdl_path_hash);
                    if (authored > 0) {
                        OutputLog::info(
                            "MDL: authored animation binding(s) for model=" +
                            std::to_string(authored));
                    }
#ifdef _WIN32
                    if (S.texture_window_srv) {
                        S.texture_window_srv->Release();
                        S.texture_window_srv = nullptr;
                    }
#endif
                    S.pending_model_tab_capture = true;
                    S.pending_preview_build = true;
                }
            }
        }
        g_pending_mdl_index = -1;
    }
