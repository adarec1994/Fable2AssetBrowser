    if (g_pending_tex_load && g_pending_tex_index >= 0 && g_pending_tex_index < (int)S.files.size()) {
        g_pending_tex_load = false;
        S.content_tabs_visible = false;
        S.show_item_details = false;
        S.selected_item = -1;
        auto item = S.files[(size_t)g_pending_tex_index];
        auto name = item.name;
        S.texture_window_name = name;

        std::string preferred_for_tex = (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty())
            ? S.selected_nested_temp_path
            : S.selected_bnk;

        progress_open(0, "Loading texture...");

        std::thread([name, preferred_for_tex]() {
            std::vector<unsigned char> tex_buf;
            if (!build_any_tex_buffer_for_name(name, tex_buf, preferred_for_tex)) {
                OutputLog::error("Texture preview: could not assemble buffer for '" + name + "' (no matching entry in any texture BNK)");
                S.texture_window_name = "ERROR: Could not load texture file";
                S.pending_texture_load = true;
                S.pending_texture_w = 0;
                S.pending_texture_h = 0;
                S.texture_blob.clear();
                S.tex_info_ok = false;
                progress_done();
                return;
            }

            S.tex_info_ok = parse_tex_info(tex_buf, S.tex_info);
            S.texture_mip_index = -1;
            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            bool has_alpha = false;
            if (!decode_tex_to_rgba(tex_buf, rgba, w, h, &has_alpha,
                                    -1)) {
                extern const std::string& mp_last_decode_fail_reason();
                extern const std::string& mp_last_decode_info();
                OutputLog::error("Texture preview: decode failed for '" + name +
                                 "' - reason=" + mp_last_decode_fail_reason() +
                                 "  info=[" + mp_last_decode_info() + "]");
                S.texture_window_name = "ERROR: Could not decode texture";
                S.pending_texture_load = true;
                S.pending_texture_w = 0;
                S.pending_texture_h = 0;
                S.texture_blob.clear();
                S.tex_info_ok = false;
                progress_done();
                return;
            }
            {
                extern const std::string& mp_last_decode_info();
                OutputLog::info("Texture preview: '" + name + "' decoded " +
                                std::to_string(w) + "x" + std::to_string(h) +
                                " (" + mp_last_decode_info() + ")");
            }
            S.texture_blob = std::move(tex_buf);

#ifdef _WIN32
            if (g_mp.has_model) {
                MP_Release(g_mp);
                g_mp.has_model = false;
                g_mp_initialized = false;
            }
#endif
            S.pending_texture_rgba = std::move(rgba);
            S.pending_texture_w = w;
            S.pending_texture_h = h;
            S.pending_texture_load = true;
            progress_done();
        }).detach();
        g_pending_tex_index = -1;
    }
