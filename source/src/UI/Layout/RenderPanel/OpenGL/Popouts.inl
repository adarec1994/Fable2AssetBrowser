void draw_texture_popout_gl() {
    if (!::g_tex_popout_open || !::g_tex_popout_gl) return;

    int tw = 0;
    int th = 0;
    GLint prev_tex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
    glBindTexture(GL_TEXTURE_2D, ::g_tex_popout_gl);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex);

    if (tw > 0 && th > 0) {
        std::string title = "Texture: "
            + std::filesystem::path(::g_tex_popout_name).filename().string()
            + "##tex_popout";
        ImGuiWindowFlags fl = ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin(title.c_str(), &::g_tex_popout_open, fl)) {
            ImGui::Checkbox("Show UVs", &::g_tex_popout_show_uvs);
            ImGui::Image((ImTextureID)(intptr_t)::g_tex_popout_gl,
                         ImVec2((float)tw, (float)th));

            ImVec2 img_min = ImGui::GetItemRectMin();
            ImGui::SetCursorScreenPos(img_min);
            ImGui::InvisibleButton("##popout_hit",
                                   ImVec2((float)tw, (float)th));
            if (ImGui::BeginPopupContextItem()) {
                const std::string& preferred_bnk =
                    (S.selected_nested_index != -1 &&
                     !S.selected_nested_temp_path.empty())
                        ? S.selected_nested_temp_path
                        : S.selected_bnk;
                tex_export_menu_named(::g_tex_popout_name,
                                      ::g_tex_popout_name,
                                      preferred_bnk, 0);
                ImGui::EndPopup();
            }

            if (::g_tex_popout_show_uvs &&
                ::g_tex_popout_mesh_idx >= 0 &&
                (size_t)::g_tex_popout_mesh_idx < g_mp.meshes.size()) {
                uint32_t src = g_mp.meshes[(size_t)::g_tex_popout_mesh_idx].source_mesh_idx;
                if (src < S.mdl_meshes.size()) {
                    const auto& geom = S.mdl_meshes[src];
                    if (!geom.uvs.empty() && !geom.indices.empty()) {
                        ImVec2 img_max = ImGui::GetItemRectMax();
                        float w_px = img_max.x - img_min.x;
                        float h_px = img_max.y - img_min.y;
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        const ImU32 col = IM_COL32(255, 255, 255, 200);
                        for (size_t i = 0; i + 2 < geom.indices.size(); i += 3) {
                            uint32_t a = geom.indices[i];
                            uint32_t b = geom.indices[i + 1];
                            uint32_t c = geom.indices[i + 2];
                            if ((size_t)a * 2 + 1 >= geom.uvs.size()) continue;
                            if ((size_t)b * 2 + 1 >= geom.uvs.size()) continue;
                            if ((size_t)c * 2 + 1 >= geom.uvs.size()) continue;
                            ImVec2 pa(img_min.x + geom.uvs[a * 2 + 0] * w_px,
                                      img_min.y + geom.uvs[a * 2 + 1] * h_px);
                            ImVec2 pb(img_min.x + geom.uvs[b * 2 + 0] * w_px,
                                      img_min.y + geom.uvs[b * 2 + 1] * h_px);
                            ImVec2 pc(img_min.x + geom.uvs[c * 2 + 0] * w_px,
                                      img_min.y + geom.uvs[c * 2 + 1] * h_px);
                            dl->AddLine(pa, pb, col, 1.0f);
                            dl->AddLine(pb, pc, col, 1.0f);
                            dl->AddLine(pc, pa, col, 1.0f);
                        }
                    }
                }
            }
        }
        ImGui::End();
    }

    if (!::g_tex_popout_open) {
        ::g_tex_popout_gl = 0;
        ::g_tex_popout_name.clear();
    }
}
