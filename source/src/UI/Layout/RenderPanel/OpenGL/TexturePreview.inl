void draw_texture_in_panel_gl() {
    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(origin,
                      ImVec2(origin.x + region.x, origin.y + region.y),
                      IM_COL32(20, 22, 28, 255));

    if (!S.texture_window_gl || S.texture_window_width <= 0 || S.texture_window_height <= 0) {
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

    dl->AddImage((ImTextureID)(intptr_t)S.texture_window_gl,
                 ImVec2(x0, y0),
                 ImVec2(x0 + dw, y0 + dh));

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

void apply_orbit_to_flycam_gl() {
    float cy = std::cos(S.cam_yaw);
    float sy = std::sin(S.cam_yaw);
    float cp = std::cos(S.cam_pitch);
    float sp = std::sin(S.cam_pitch);
    g_flycam.pos[0] = g_mp.center[0] + sy * cp * S.cam_dist * g_mp.radius;
    float hero_head_offset = 0.0f;
    if (ContentTabs::ActiveKind() == ContentTabs::Kind::Hero) {
        float focus = std::clamp((1.8f - S.cam_dist) / 1.3f, 0.0f, 1.0f);
        focus = focus * focus * (3.0f - 2.0f * focus);
        hero_head_offset = g_mp.radius * 0.82f * focus;
    }
    const float target_y = g_mp.center[1] + S.cam_target_offset_y +
                           hero_head_offset;
    g_flycam.pos[1] = target_y + sp * S.cam_dist * g_mp.radius;
    g_flycam.pos[2] = g_mp.center[2] + cy * cp * S.cam_dist * g_mp.radius;
    float dx = g_mp.center[0] - g_flycam.pos[0];
    float dy = target_y - g_flycam.pos[1];
    float dz = g_mp.center[2] - g_flycam.pos[2];
    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 0.0001f) {
        g_flycam.yaw = std::atan2(dx, dz);
        g_flycam.pitch = std::asin(dy / len);
    }
}
