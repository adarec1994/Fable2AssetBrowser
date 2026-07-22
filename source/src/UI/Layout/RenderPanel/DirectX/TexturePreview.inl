ID3D11SamplerState*  g_tex_point_sampler = nullptr;
ID3D11DeviceContext* g_tex_preview_ctx   = nullptr;

void ensure_point_sampler(ID3D11Device* device) {
    if (g_tex_point_sampler || !device) return;
    if (!g_tex_preview_ctx) {

        device->GetImmediateContext(&g_tex_preview_ctx);
    }
    D3D11_SAMPLER_DESC desc{};
    desc.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;
    desc.AddressU = desc.AddressV = desc.AddressW =
        D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.MinLOD   = 0.0f;
    desc.MaxLOD   = D3D11_FLOAT32_MAX;
    device->CreateSamplerState(&desc, &g_tex_point_sampler);
}

void bind_point_sampler_cb(const ImDrawList* ,
                           const ImDrawCmd*  ) {
    if (g_tex_preview_ctx && g_tex_point_sampler) {
        g_tex_preview_ctx->PSSetSamplers(0, 1, &g_tex_point_sampler);
    }
}
}

void draw_texture_in_panel(ID3D11Device* device) {
    ensure_point_sampler(device);

    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(origin,
                      ImVec2(origin.x + region.x, origin.y + region.y),
                      IM_COL32(20, 22, 28, 255));

    if (!S.texture_window_srv || S.texture_window_width <= 0 || S.texture_window_height <= 0) {
        const char* msg = S.texture_window_name.empty()
            ? "Texture decode failed"
            : S.texture_window_name.c_str();
        ImVec2 sz = ImGui::CalcTextSize(msg);
        ImVec2 pos(origin.x + (region.x - sz.x) * 0.5f,
                   origin.y + (region.y - sz.y) * 0.5f);
        dl->AddText(pos, IM_COL32(255, 90, 90, 230), msg);
        ImGui::Dummy(region);
        return;
    }

    float tw = (float)S.texture_window_width;
    float th = (float)S.texture_window_height;
    float scale = std::min(region.x / tw, region.y / th);
    if (scale > 4.0f) scale = 4.0f;
    float dw = tw * scale;
    float dh = th * scale;
    float x0 = origin.x + (region.x - dw) * 0.5f;
    float y0 = origin.y + (region.y - dh) * 0.5f;

    if (g_tex_point_sampler) {
        dl->AddCallback(bind_point_sampler_cb, nullptr);
    }
    dl->AddImage((ImTextureID)S.texture_window_srv,
                 ImVec2(x0, y0),
                 ImVec2(x0 + dw, y0 + dh));
    if (g_tex_point_sampler) {
        dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }

    {
        ImGui::SetCursorScreenPos(ImVec2(x0, y0));
        ImGui::InvisibleButton("##tex_preview_hit", ImVec2(dw, dh));
        if (S.tex_info_ok && !S.texture_blob.empty() &&
            ImGui::BeginPopupContextItem()) {
            tex_export_menu_blob(S.texture_window_name,
                                 S.texture_blob,
                                 S.texture_mip_index);
            ImGui::EndPopup();
        }
    }

    if (S.tex_info_ok && !S.texture_blob.empty()) {
        const int total = (int)S.tex_info.Mips.size();

        if (S.texture_mip_index < 0) S.texture_mip_index = 0;
        if (S.texture_mip_index >= std::max(1, total))
            S.texture_mip_index = std::max(0, total - 1);

        int mw = 0, mh = 0;
        if (S.texture_mip_index >= 0 && S.texture_mip_index < total) {
            const auto& mm = S.tex_info.Mips[(size_t)S.texture_mip_index];
            mw = mm.HasWH ? (int)mm.MipWidth
                          : std::max(1, (int)S.tex_info.TextureWidth  >> S.texture_mip_index);
            mh = mm.HasWH ? (int)mm.MipHeight
                          : std::max(1, (int)S.tex_info.TextureHeight >> S.texture_mip_index);
        }

        const float kOverlayW = 230.0f;
        ImGui::SetNextWindowPos(ImVec2(origin.x + region.x - kOverlayW - 8.0f,
                                       origin.y + 6.0f));
        ImGui::SetNextWindowSize(ImVec2(kOverlayW, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.78f);
        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##tex_mip_selector", nullptr, fl)) {

            if (total > 1) {
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Mip");
                ImGui::SameLine();
                if (ImGui::ArrowButton("##mip_prev", ImGuiDir_Left)) {
                    if (S.texture_mip_index > 0) {
                        S.texture_mip_index--;
                        S.pending_texture_mip_change = true;
                    }
                }
                ImGui::SameLine();
                ImGui::Text("%d / %d", S.texture_mip_index, total - 1);
                ImGui::SameLine();
                if (ImGui::ArrowButton("##mip_next", ImGuiDir_Right)) {
                    if (S.texture_mip_index < total - 1) {
                        S.texture_mip_index++;
                        S.pending_texture_mip_change = true;
                    }
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(%dx%d)", mw, mh);
            } else {

                ImGui::TextDisabled("%dx%d", mw, mh);
            }

            if (ImGui::Checkbox("R", &S.tex_show_r))
                S.pending_texture_mip_change = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("G", &S.tex_show_g))
                S.pending_texture_mip_change = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("B", &S.tex_show_b))
                S.pending_texture_mip_change = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("A", &S.tex_show_a))
                S.pending_texture_mip_change = true;
        }
        ImGui::End();
    }

    ImGui::Dummy(region);
}

void apply_orbit_to_flycam() {
    float r = std::max(g_mp.radius, 0.5f) * std::max(S.cam_dist, 0.1f);
    float hero_head_offset = 0.0f;
    if (ContentTabs::ActiveKind() == ContentTabs::Kind::Hero) {
        float focus = std::clamp((1.8f - S.cam_dist) / 1.3f, 0.0f, 1.0f);
        focus = focus * focus * (3.0f - 2.0f * focus);
        hero_head_offset = g_mp.radius * 0.82f * focus;
    }
    const float target_y = g_mp.center[1] + S.cam_target_offset_y +
                           hero_head_offset;
    float yaw   = S.cam_yaw;
    float pitch = S.cam_pitch;
    float cy = cosf(pitch);
    float sy = sinf(pitch);
    float cx = cosf(yaw);
    float sx = sinf(yaw);

    g_flycam.pos[0] = g_mp.center[0] + r * cy * sx;
    g_flycam.pos[1] = target_y + r * sy;
    g_flycam.pos[2] = g_mp.center[2] + r * cy * cx;

    g_flycam.yaw   = yaw + 3.14159265f;
    g_flycam.pitch = -pitch;
    g_flycam.is_looking = false;
}
