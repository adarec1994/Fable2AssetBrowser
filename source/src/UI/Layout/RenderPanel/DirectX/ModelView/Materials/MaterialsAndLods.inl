    if (g_mp.has_model && g_mp.lod_count > 1 &&
        !details_panel_docked()) {
        static float s_lod_alpha = 0.30f;
        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        const ImVec2 lod_pos (origin.x + 6, next_overlay_y);
        const ImVec2 lod_size(190, 0);
        ImGui::SetNextWindowPos(lod_pos);
        ImGui::SetNextWindowSize(lod_size, ImGuiCond_Always);
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

    if (g_mp.has_model && !g_mp.meshes.empty() &&
        !custom_level_clean_viewport) {
        static float s_mat_alpha = 0.30f;
        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

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

                bool h   = (::g_highlight_mesh_idx == (int)mi);
                bool iso = (::g_isolate_mesh_idx   == (int)mi);

                if (ImGui::Checkbox("Highlight", &h)) {
                    if (h) {
                        ::g_highlight_mesh_idx = (int)mi;
                        ::g_isolate_mesh_idx   = -1;
                    } else if (::g_highlight_mesh_idx == (int)mi) {
                        ::g_highlight_mesh_idx = -1;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Isolate", &iso)) {
                    if (iso) {
                        ::g_isolate_mesh_idx   = (int)mi;
                        ::g_highlight_mesh_idx = -1;
                    } else if (::g_isolate_mesh_idx == (int)mi) {
                        ::g_isolate_mesh_idx = -1;
                    }
                }

                struct ThumbSpec {
                    const char*               slot_id;
                    ID3D11ShaderResourceView* srv;
                    const std::string*        name;
                    bool*                     visible;
                };
                ThumbSpec thumbs[5] = {
                    {"diffuse",  mesh.srv_diffuse,  &mesh.diffuse_tex_name,  &mesh.diffuse_visible},
                    {"normal",   mesh.srv_normal,   &mesh.normal_tex_name,   &mesh.normal_visible},
                    {"specular", mesh.srv_specular, &mesh.specular_tex_name, &mesh.specular_visible},
                    {"metallic", mesh.srv_metallic, &mesh.metallic_tex_name, &mesh.metallic_visible},
                    {"extra",    mesh.srv_extra,    &mesh.extra_tex_name,    &mesh.extra_visible},
                };
                bool any_thumb = false;
                for (int ti = 0; ti < 5; ++ti) {
                    const ThumbSpec& t = thumbs[ti];
                    if (!t.srv || t.srv == g_mp.default_srv) continue;
                    if (t.name->empty()) continue;
                    if (any_thumb) ImGui::SameLine();
                    any_thumb = true;
                    ImGui::PushID(t.slot_id);

                    ImGui::BeginGroup();

                    ImVec4 tint = (*t.visible) ? ImVec4(1, 1, 1, 1)
                                               : ImVec4(0.45f, 0.45f, 0.45f, 1);
                    if (ImGui::ImageButton("##t",
                                           (ImTextureID)t.srv,
                                           thumb_size,
                                           ImVec2(0, 0), ImVec2(1, 1),
                                           ImVec4(0, 0, 0, 0), tint)) {
                        ::g_tex_popout_srv      = t.srv;
                        ::g_tex_popout_name     = *t.name;
                        ::g_tex_popout_open     = true;

                        ::g_tex_popout_mesh_idx = (int)mi;
                    }

                    if (ImGui::BeginPopupContextItem()) {
                        const auto* terrain_tex =
                            TerrainTextureRegistry::Find(*t.name);
                        if (terrain_tex) {
                            tex_export_menu_rgba(*t.name,
                                                 terrain_tex->rgba,
                                                 terrain_tex->width,
                                                 terrain_tex->height);
                        } else {
                            const std::string& preferred_bnk =
                                (S.selected_nested_index != -1 &&
                                 !S.selected_nested_temp_path.empty())
                                    ? S.selected_nested_temp_path
                                    : S.selected_bnk;
                            tex_export_menu_named(*t.name, *t.name,
                                                  preferred_bnk, 0);
                        }
                        ImGui::EndPopup();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s\n[%s]",
                                          t.name->c_str(), t.slot_id);
                    }

                    ImGui::Checkbox("##vis", t.visible);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Show %s in render", t.slot_id);
                    }
                    ImGui::EndGroup();
                    ImGui::PopID();
                }
                if (!any_thumb) {
                    ImGui::TextDisabled("(no textures)");
                }

                ImGui::Separator();
                ImGui::PopID();
            }

            if (g_mp.no_tilt && ::g_selected_level_mesh_idx >= 0 &&
                ::g_selected_level_mesh_idx < (int)g_mp.meshes.size())
            {
                const int mi   = ::g_selected_level_mesh_idx;
                auto&     mesh = g_mp.meshes[mi];
                const std::string selected_model_key =
                    level_model_key_from_mesh_name(mesh.name);
                const std::string selected_model_name =
                    clean_level_model_name(mesh.name);

                ImGui::TextWrapped("%s", selected_model_name.c_str());
                ImGui::Separator();

                ImGui::PushID(0x20000 + mi);

                struct ThumbSpec {
                    const char*               slot_id;
                    ID3D11ShaderResourceView* srv;
                    const std::string*        name;
                    int                       mesh_idx;
                    std::vector<bool*>        visible;
                };
                struct MaterialRow {
                    std::string key;
                    std::string label;
                    std::array<ThumbSpec, 5> thumbs;
                };

                auto make_row_key = [](const MPPerMesh& m,
                                       const std::string& label) {
                    return label + "|" +
                           m.diffuse_tex_name + "|" +
                           m.normal_tex_name + "|" +
                           m.specular_tex_name + "|" +
                           m.metallic_tex_name + "|" +
                           m.extra_tex_name;
                };

                std::vector<MaterialRow> rows;
                auto append_row_mesh =
                    [&](MPPerMesh& related, int related_idx) {
                        const std::string label =
                            clean_level_material_name(related.name);
                        const std::string key = make_row_key(related, label);
                        MaterialRow* row = nullptr;
                        for (auto& existing : rows) {
                            if (existing.key == key) {
                                row = &existing;
                                break;
                            }
                        }
                        if (!row) {
                            MaterialRow fresh;
                            fresh.key = key;
                            fresh.label = label;
                            fresh.thumbs = {{
                                {"diffuse",  related.srv_diffuse,
                                 &related.diffuse_tex_name, related_idx, {}},
                                {"normal",   related.srv_normal,
                                 &related.normal_tex_name, related_idx, {}},
                                {"specular", related.srv_specular,
                                 &related.specular_tex_name, related_idx, {}},
                                {"metallic", related.srv_metallic,
                                 &related.metallic_tex_name, related_idx, {}},
                                {"extra",    related.srv_extra,
                                 &related.extra_tex_name, related_idx, {}},
                            }};
                            rows.push_back(std::move(fresh));
                            row = &rows.back();
                        }

                        row->thumbs[0].visible.push_back(
                            &related.diffuse_visible);
                        row->thumbs[1].visible.push_back(
                            &related.normal_visible);
                        row->thumbs[2].visible.push_back(
                            &related.specular_visible);
                        row->thumbs[3].visible.push_back(
                            &related.metallic_visible);
                        row->thumbs[4].visible.push_back(
                            &related.extra_visible);
                    };

                for (size_t ri = 0; ri < g_mp.meshes.size(); ++ri) {
                    auto& related = g_mp.meshes[ri];
                    if (level_model_key_from_mesh_name(related.name) !=
                        selected_model_key)
                    {
                        continue;
                    }
                    append_row_mesh(related, (int)ri);
                }

                if (rows.empty()) {
                    ImGui::TextDisabled("(no materials)");
                }
                for (size_t row_i = 0; row_i < rows.size(); ++row_i) {
                    MaterialRow& row = rows[row_i];
                    ImGui::PushID((int)row_i);
                    ImGui::TextUnformatted(row.label.c_str());

                    bool any_thumb = false;
                    for (size_t ti = 0; ti < row.thumbs.size(); ++ti) {
                        ThumbSpec& t = row.thumbs[ti];
                        if (!t.srv || t.srv == g_mp.default_srv) continue;
                        if (!t.name || t.name->empty()) continue;
                        if (any_thumb) ImGui::SameLine();
                        any_thumb = true;
                        ImGui::PushID((int)ti);
                        ImGui::BeginGroup();

                        bool visible = true;
                        for (bool* v : t.visible) {
                            if (v && !*v) {
                                visible = false;
                                break;
                            }
                        }

                        ImVec4 tint = visible
                            ? ImVec4(1, 1, 1, 1)
                            : ImVec4(0.45f, 0.45f, 0.45f, 1);
                        if (ImGui::ImageButton("##t",
                                               (ImTextureID)t.srv,
                                               thumb_size,
                                               ImVec2(0, 0), ImVec2(1, 1),
                                               ImVec4(0, 0, 0, 0), tint)) {
                            ::g_tex_popout_srv      = t.srv;
                            ::g_tex_popout_name     = *t.name;
                            ::g_tex_popout_open     = true;
                            ::g_tex_popout_mesh_idx = t.mesh_idx;
                        }
                        if (ImGui::BeginPopupContextItem()) {
                            const auto* terrain_tex =
                                TerrainTextureRegistry::Find(*t.name);
                            if (terrain_tex) {
                                tex_export_menu_rgba(*t.name,
                                                     terrain_tex->rgba,
                                                     terrain_tex->width,
                                                     terrain_tex->height);
                            } else {
                                const std::string& preferred_bnk =
                                    (S.selected_nested_index != -1 &&
                                     !S.selected_nested_temp_path.empty())
                                        ? S.selected_nested_temp_path
                                        : S.selected_bnk;
                                tex_export_menu_named(*t.name, *t.name,
                                                      preferred_bnk, 0);
                            }
                            ImGui::EndPopup();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s\n[%s]",
                                              t.name->c_str(), t.slot_id);
                        }
                        bool checkbox_visible = visible;
                        if (ImGui::Checkbox("##vis", &checkbox_visible)) {
                            for (bool* v : t.visible) {
                                if (v) *v = checkbox_visible;
                            }
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Show %s in render", t.slot_id);
                        }
                        ImGui::EndGroup();
                        ImGui::PopID();
                    }
                    if (!any_thumb) {
                        ImGui::TextDisabled("(no textures)");
                    }
                    ImGui::Separator();
                    ImGui::PopID();
                }
                ImGui::PopID();
            } else if (g_mp.no_tilt) {
                const auto& lod = EhfLodThumbnails::Get();
                if (!lod.empty()) {
                    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f),
                        ".ehf LOD palette");
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%zu materials)", lod.size());
                    ImGui::Separator();

                    auto basename = [](const std::string& s) -> std::string {
                        if (s.empty()) return {};
                        size_t pos = s.find_last_of("/\\");
                        return (pos == std::string::npos)
                            ? s : s.substr(pos + 1);
                    };

                    auto thumb_or_placeholder =
                        [&](ID3D11ShaderResourceView* srv,
                            const std::string& path,
                            const char* slot_tag,
                            int slot_idx)
                    {
                        const ImVec2 sz(48, 48);
                        ImGui::PushID(slot_idx);
                        if (srv) {
                            if (ImGui::ImageButton("##t",
                                (ImTextureID)srv, sz,
                                ImVec2(0, 0), ImVec2(1, 1),
                                ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 1)))
                            {
                                ::g_tex_popout_srv      = srv;
                                ::g_tex_popout_name     = path;
                                ::g_tex_popout_open     = true;
                                ::g_tex_popout_mesh_idx = -1;
                            }
                            if (ImGui::BeginPopupContextItem()) {
                                const std::string& preferred_bnk =
                                    (S.selected_nested_index != -1 &&
                                     !S.selected_nested_temp_path.empty())
                                        ? S.selected_nested_temp_path
                                        : S.selected_bnk;
                                tex_export_menu_named(path, path,
                                                      preferred_bnk, 0);
                                ImGui::EndPopup();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s\n[%s]",
                                                  path.c_str(), slot_tag);
                            }
                        } else {
                            ImGui::Dummy(sz);
                            if (!path.empty() && ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("decode failed: %s\n[%s]",
                                                  path.c_str(), slot_tag);
                            }
                        }
                        ImGui::PopID();
                    };

                    for (size_t i = 0; i < lod.size(); ++i) {
                        const auto& e = lod[i];
                        ImGui::PushID(int(0x10000 + i));

                        const std::string title = "[" + std::to_string(i)
                            + "] " + basename(e.base_diffuse_path);
                        ImGui::TextUnformatted(title.c_str());

                        thumb_or_placeholder(e.srv_base_diffuse,
                                             e.base_diffuse_path,
                                             "base diffuse", 0);
                        ImGui::SameLine();
                        thumb_or_placeholder(e.srv_base_normal,
                                             e.base_normal_path,
                                             "base normal", 1);
                        ImGui::SameLine();
                        thumb_or_placeholder(e.srv_detail_diffuse,
                                             e.detail_diffuse_path,
                                             "detail diffuse", 2);
                        ImGui::SameLine();
                        thumb_or_placeholder(e.srv_detail_normal,
                                             e.detail_normal_path,
                                             "detail normal", 3);

                        ImGui::Separator();
                        ImGui::PopID();
                    }
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    } else {

        ::g_highlight_mesh_idx       = -1;
        ::g_isolate_mesh_idx         = -1;
        ::g_selected_level_mesh_idx  = -1;
        ::g_selected_level_pick_id   = 0;
        ::g_tex_popout_open          = false;
        ::g_tex_popout_srv           = nullptr;
        ::g_tex_popout_name.clear();
        ::g_tex_popout_mesh_idx      = -1;
    }
