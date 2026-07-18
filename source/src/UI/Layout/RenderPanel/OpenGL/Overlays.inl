void draw_materials_overlay_gl(const ImVec2& origin,
                               const ImVec2& region,
                               float next_overlay_y) {
    if (g_mp.has_model && g_mp.lod_count > 1 &&
        !details_panel_docked()) {
        static float s_lod_alpha = 0.30f;
        const float kIdleAlpha = 0.30f;
        const float kHoverAlpha = 1.00f;

        ImGui::SetNextWindowPos(ImVec2(origin.x + 6, next_overlay_y));
        ImGui::SetNextWindowSize(ImVec2(190, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(s_lod_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_lod_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##lod_overlay", nullptr, fl)) {
            bool hovering = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                ImGuiHoveredFlags_ChildWindows);
            float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_lod_alpha += (target - s_lod_alpha) * 0.18f;
            if (std::fabs(s_lod_alpha - target) < 0.005f) s_lod_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "LOD");
            const int lod_count = (int)g_mp.lod_count;
            int current = g_mp.selected_lod;
            if (current < -1 || current >= lod_count) current = 0;

            if (ImGui::RadioButton("All", current == -1)) {
                g_mp.selected_lod = -1;
            }
            for (int i = 0; i < lod_count; ++i) {
                ImGui::SameLine();
                char lbl[16];
                std::snprintf(lbl, sizeof(lbl), "%d", i);
                if (ImGui::RadioButton(lbl, current == i)) {
                    g_mp.selected_lod = i;
                }
            }

            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            next_overlay_y = wp.y + ws.y + 6.0f;
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    if (!g_mp.has_model || g_mp.meshes.empty()) {
        ::g_highlight_mesh_idx = -1;
        ::g_isolate_mesh_idx = -1;
        ::g_tex_popout_open = false;
        ::g_tex_popout_gl = 0;
        ::g_tex_popout_name.clear();
        ::g_tex_popout_mesh_idx = -1;
        return;
    }

    static float s_mat_alpha = 0.30f;
    const float kIdleAlpha = 0.30f;
    const float kHoverAlpha = 1.00f;
    const float kMatW = 296.0f;
    float max_h = std::max(160.0f,
                           region.y - (next_overlay_y - origin.y) - 20.0f);

    ImGui::SetNextWindowPos(ImVec2(origin.x + 6, next_overlay_y));
    ImGui::SetNextWindowSizeConstraints(ImVec2(kMatW, 0.0f),
                                        ImVec2(kMatW, max_h));
    ImGui::SetNextWindowBgAlpha(s_mat_alpha * 0.78f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_mat_alpha);

    ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##materials_overlay", nullptr, fl)) {
        bool hovering = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_AllowWhenBlockedByPopup |
            ImGuiHoveredFlags_ChildWindows);
        float target = hovering ? kHoverAlpha : kIdleAlpha;
        s_mat_alpha += (target - s_mat_alpha) * 0.18f;
        if (std::fabs(s_mat_alpha - target) < 0.005f) s_mat_alpha = target;

        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Materials");
        ImGui::Separator();

        const ImVec2 thumb_size(48, 48);
        if (!g_mp.no_tilt) for (size_t mi = 0; mi < g_mp.meshes.size(); ++mi) {
        auto& mesh = g_mp.meshes[mi];

        if (g_mp.selected_lod >= 0 &&
            mesh.lod_index != (uint32_t)g_mp.selected_lod) {
            continue;
        }

        ImGui::PushID((int)mi);
        ImGui::TextUnformatted(mesh.name.c_str());
            bool h = (::g_highlight_mesh_idx == (int)mi);
            bool iso = (::g_isolate_mesh_idx == (int)mi);
            if (ImGui::Checkbox("Highlight", &h)) {
                if (h) {
                    ::g_highlight_mesh_idx = (int)mi;
                    ::g_isolate_mesh_idx = -1;
                } else if (::g_highlight_mesh_idx == (int)mi) {
                    ::g_highlight_mesh_idx = -1;
                }
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Isolate", &iso)) {
                if (iso) {
                    ::g_isolate_mesh_idx = (int)mi;
                    ::g_highlight_mesh_idx = -1;
                } else if (::g_isolate_mesh_idx == (int)mi) {
                    ::g_isolate_mesh_idx = -1;
                }
            }

            struct ThumbSpec {
                const char* slot_id;
                unsigned int tex;
                const std::string* name;
                bool* visible;
            };
            ThumbSpec thumbs[5] = {
                {"diffuse",  mesh.tex_diffuse,  &mesh.diffuse_tex_name,  &mesh.diffuse_visible},
                {"normal",   mesh.tex_normal,   &mesh.normal_tex_name,   &mesh.normal_visible},
                {"specular", mesh.tex_specular, &mesh.specular_tex_name, &mesh.specular_visible},
                {"metallic", mesh.tex_metallic, &mesh.metallic_tex_name, &mesh.metallic_visible},
                {"extra",    mesh.tex_extra,    &mesh.extra_tex_name,    &mesh.extra_visible},
            };

            bool any_thumb = false;
            for (int ti = 0; ti < 5; ++ti) {
                const ThumbSpec& t = thumbs[ti];
                if (!t.tex || t.tex == g_mp.default_tex) continue;
                if (t.name->empty()) continue;
                if (any_thumb) ImGui::SameLine();
                any_thumb = true;
                ImGui::PushID(t.slot_id);
                ImGui::BeginGroup();
                ImVec4 tint = (*t.visible) ? ImVec4(1, 1, 1, 1)
                                           : ImVec4(0.45f, 0.45f, 0.45f, 1);
                if (ImGui::ImageButton("##t",
                                       (ImTextureID)(intptr_t)t.tex,
                                       thumb_size,
                                       ImVec2(0, 0), ImVec2(1, 1),
                                       ImVec4(0, 0, 0, 0), tint)) {
                    ::g_tex_popout_gl = t.tex;
                    ::g_tex_popout_name = *t.name;
                    ::g_tex_popout_open = true;
                    ::g_tex_popout_mesh_idx = (int)mi;
                }
                if (ImGui::BeginPopupContextItem()) {
                    const std::string& preferred_bnk =
                        (S.selected_nested_index != -1 &&
                         !S.selected_nested_temp_path.empty())
                            ? S.selected_nested_temp_path
                            : S.selected_bnk;
                    tex_export_menu_named(*t.name, *t.name,
                                          preferred_bnk, 0);
                    ImGui::EndPopup();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\n[%s]", t.name->c_str(), t.slot_id);
                }
                ImGui::Checkbox("##vis", t.visible);
                ImGui::EndGroup();
                ImGui::PopID();
            }
            if (!any_thumb) ImGui::TextDisabled("(no textures)");
            ImGui::Separator();
        ImGui::PopID();
    }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void draw_gdb_placements_overlay_gl(const ImVec2& origin, const ImVec2& region) {
    if (g_level_gdb_placements.empty() || !S.show_gdb_placements ||
        !g_mp.no_tilt) return;

    const float fx = std::sin(g_flycam.yaw) * std::cos(g_flycam.pitch);
    const float fy = std::sin(g_flycam.pitch);
    const float fz = std::cos(g_flycam.yaw) * std::cos(g_flycam.pitch);
    const float rx = fz, ry = 0.0f, rz = -fx;
    const float ux = fy * rz;
    const float uy = fz * rx - fx * rz;
    const float uz = fx * ry - fy * rx;
    const float tan_half_fov = std::tan(3.1415926535f / 6.0f);
    const float aspect = region.x / std::max(1.0f, region.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const int gw = g_pending_terrain_ghf_width;
    const int gh = g_pending_terrain_ghf_height;
    const float tile = g_pending_terrain_ghf_tile_size > 0.0f
        ? g_pending_terrain_ghf_tile_size : 0.5f;
    const auto& heights = g_pending_terrain_ghf_heights;
    const bool have_terrain = gw > 0 && gh > 0 &&
        heights.size() == size_t(gw) * size_t(gh);
    auto sample_height = [&](float x, float z) {
        if (!have_terrain) return 0.0f;
        int ix = std::clamp(int(x / tile), 0, gw - 1);
        int iz = std::clamp(int(z / tile), 0, gh - 1);
        return heights[size_t(iz) * size_t(gw) + size_t(ix)];
    };

    for (const auto& placement : g_level_gdb_placements) {
        const float wx = placement.x;
        const float wy = sample_height(placement.x, placement.y) + 1.0f;
        const float wz = placement.y;
        const float dx = wx - g_flycam.pos[0];
        const float dy = wy - g_flycam.pos[1];
        const float dz = wz - g_flycam.pos[2];
        const float view_x = dx * rx + dy * ry + dz * rz;
        const float view_y = dx * ux + dy * uy + dz * uz;
        const float view_z = dx * fx + dy * fy + dz * fz;
        if (view_z <= 0.05f) continue;
        const float ndc_x = view_x / (view_z * tan_half_fov * aspect);
        const float ndc_y = view_y / (view_z * tan_half_fov);
        if (ndc_x < -1.2f || ndc_x > 1.2f ||
            ndc_y < -1.2f || ndc_y > 1.2f) continue;
        ImVec2 point(origin.x + (ndc_x * 0.5f + 0.5f) * region.x,
                     origin.y + (1.0f - (ndc_y * 0.5f + 0.5f)) * region.y);
        const bool player_start = placement.marker == 0x00004B40u;
        const float radius = player_start ? 4.0f : 2.5f;
        dl->AddCircleFilled(point, radius,
            player_start ? IM_COL32(255, 80, 80, 230)
                         : IM_COL32(120, 220, 255, 180));
        if (player_start) {
            dl->AddCircle(point, radius + 1.0f,
                          IM_COL32(0, 0, 0, 200), 12, 1.0f);
            dl->AddText(ImVec2(point.x + radius + 4.0f, point.y - 7.0f),
                        IM_COL32(255, 150, 150, 235), "Player start");
        }
    }
}
