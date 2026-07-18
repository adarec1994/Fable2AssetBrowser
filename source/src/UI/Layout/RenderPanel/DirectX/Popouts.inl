void draw_heightmap_popout() {
    if (::g_heightmap_popout_open && ::g_heightmap_popout_srv) {
        const int hw = ::g_heightmap_popout_w;
        const int hh = ::g_heightmap_popout_h;

        if (hw > 0 && hh > 0) {
            ImGuiViewport* vp = ImGui::GetMainViewport();
            const float vw = vp->WorkSize.x;
            const float vh = vp->WorkSize.y;
            const float cap_w = vw * 0.8f;
            const float cap_h = vh * 0.8f;
            float scale = 1.0f;
            if ((float)hw > cap_w || (float)hh > cap_h) {
                scale = std::min(cap_w / (float)hw, cap_h / (float)hh);
            }
            const float dw = std::max(64.0f, (float)hw * scale);
            const float dh = std::max(64.0f, (float)hh * scale);

            std::string title = ::g_heightmap_popout_kind + ": " +
                              ::g_heightmap_popout_name
                              + "##heightmap_popout";
            ImGuiWindowFlags fl = ImGuiWindowFlags_NoCollapse
                                | ImGuiWindowFlags_AlwaysAutoResize;
            if (ImGui::Begin(title.c_str(),
                             &::g_heightmap_popout_open, fl)) {
                ImGui::Image((ImTextureID)::g_heightmap_popout_srv,
                             ImVec2(dw, dh));

                ImVec2 img_min = ImGui::GetItemRectMin();
                ImGui::SetCursorScreenPos(img_min);
                ImGui::InvisibleButton("##hmap_popout_hit",
                                       ImVec2(dw, dh));
                if (ImGui::BeginPopupContextItem()) {
                    tex_export_menu_rgba(::g_heightmap_popout_name,
                                         ::g_heightmap_popout_rgba,
                                         hw, hh);
                    ImGui::EndPopup();
                }
            }
            ImGui::End();
        }

        if (!::g_heightmap_popout_open) {
            if (::g_heightmap_popout_srv) {
                ::g_heightmap_popout_srv->Release();
                ::g_heightmap_popout_srv = nullptr;
            }
            ::g_heightmap_popout_name.clear();
            ::g_heightmap_popout_kind = "Heightmap";
            ::g_heightmap_popout_rgba.clear();
            ::g_heightmap_popout_w = 0;
            ::g_heightmap_popout_h = 0;
        }
    }
}
