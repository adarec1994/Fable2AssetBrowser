    if (g_pending_tex_load && g_pending_tex_index >= 0 && g_pending_tex_index < (int)S.files.size()) {
        g_pending_tex_load = false;
        S.content_tabs_visible = false;
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
                S.texture_window_name = "ERROR: Could not load texture file";
                S.pending_texture_load = true;
                S.pending_texture_w = 0;
                S.pending_texture_h = 0;
                progress_done();
                return;
            }

            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            bool has_alpha = false;
            if (!decode_tex_to_rgba(tex_buf, rgba, w, h, &has_alpha)) {
                S.texture_window_name = "ERROR: Could not decode texture";
                S.pending_texture_load = true;
                S.pending_texture_w = 0;
                S.pending_texture_h = 0;
                progress_done();
                return;
            }

            S.pending_texture_rgba = std::move(rgba);
            S.pending_texture_w = w;
            S.pending_texture_h = h;
            S.pending_texture_load = true;
            progress_done();
        }).detach();

        g_pending_tex_index = -1;
    }
