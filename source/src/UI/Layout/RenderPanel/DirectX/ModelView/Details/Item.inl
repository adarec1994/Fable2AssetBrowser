    if (S.show_item_details && S.selected_item >= 0 &&
        S.selected_item < (int)g_item_details.size() &&
        !LevelEdit::Enabled()) {
        const auto& it = g_item_details[(size_t)S.selected_item];

        static ID3D11ShaderResourceView* s_icon_srv = nullptr;
        static uint32_t s_icon_for = 0xFFFFFFFFu;
        static int s_icon_w = 0, s_icon_h = 0;
        if (g_item_icon_dirty.exchange(false) ||
            s_icon_for != it.record_hash) {
            s_icon_for = it.record_hash;
            if (s_icon_srv) { s_icon_srv->Release(); s_icon_srv = nullptr; }
            s_icon_w = s_icon_h = 0;
            if (!it.icon_tex.empty()) {
                std::vector<unsigned char> tex_buf;
                if (build_any_tex_buffer_for_name(it.icon_tex, tex_buf,
                                                  std::string())) {
                    std::vector<uint8_t> rgba;
                    int w = 0, h = 0;
                    bool has_a = false;
                    if (decode_tex_to_rgba(tex_buf, rgba, w, h, &has_a,
                                           -1) &&
                        w > 0 && h > 0) {
                        s_icon_srv = create_srv_from_rgba(device, w, h,
                                                          rgba);
                        s_icon_w = w;
                        s_icon_h = h;
                    }
                }
            }
        }

        static float s_item_alpha = 0.30f;
        const float kIdleAlpha  = 0.30f;
        const float kHoverAlpha = 1.00f;
        const float kItemW  = 300.0f;
        const float kItemPad = 6.0f;
        ImGui::SetNextWindowPos(
            ImVec2(origin.x + region.x - kItemW - kItemPad,
                   origin.y + kItemPad));


        ImGui::SetNextWindowSizeConstraints(
            ImVec2(kItemW, 0.0f),
            ImVec2(kItemW, std::max(200.0f, region.y - 2 * kItemPad)));
        ImGui::SetNextWindowBgAlpha(s_item_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_item_alpha);
        ImGuiWindowFlags ifl = ImGuiWindowFlags_NoTitleBar |
                               ImGuiWindowFlags_NoResize |
                               ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoCollapse |
                               ImGuiWindowFlags_NoSavedSettings |
                               ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##item_details_overlay", nullptr, ifl)) {
            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            ImVec2 mp = ImGui::GetIO().MousePos;
            bool hovering = mp.x >= wp.x && mp.x < wp.x + ws.x &&
                            mp.y >= wp.y && mp.y < wp.y + ws.y;
            static bool s_was_hovering = false;
            if (!hovering && s_was_hovering &&
                ImGui::GetIO().MouseDown[0]) {
                hovering = true;
            }
            s_was_hovering = hovering;
            const float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_item_alpha += (target - s_item_alpha) * 0.18f;
            if (std::fabs(s_item_alpha - target) < 0.005f) {
                s_item_alpha = target;
            }

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                               "Item Details");
            ImGui::Separator();

            std::string disp_name;
            if (it.name_tag) TextBank::Lookup(it.name_tag, disp_name);
            if (disp_name.empty()) disp_name = it.label;
            ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s",
                               disp_name.c_str());
            if (s_icon_srv) {
                float iw = float(s_icon_w), ih = float(s_icon_h);
                const float maxdim = 80.0f;
                if (iw > maxdim || ih > maxdim) {
                    const float s = maxdim / std::max(iw, ih);
                    iw *= s; ih *= s;
                }
                ImGui::Image((ImTextureID)s_icon_srv, ImVec2(iw, ih));
            }
            if (it.money >= 0) {
                ImGui::Text("Value: %d gold", it.money);
            }

            std::string desc;
            if (it.desc_tag) TextBank::Lookup(it.desc_tag, desc);
            if (!desc.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                                   "Description");
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(desc.c_str());
                ImGui::PopTextWrapPos();
            }

            if (!it.stats.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                                   "Stats");
                if (ImGui::BeginTable("##item_stats", 2,
                                      ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Field");
                    ImGui::TableSetupColumn(
                        "Value", ImGuiTableColumnFlags_WidthFixed,
                        84.0f);
                    for (const auto& kv : it.stats) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(kv.first.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(kv.second.c_str());
                    }
                    ImGui::EndTable();
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
