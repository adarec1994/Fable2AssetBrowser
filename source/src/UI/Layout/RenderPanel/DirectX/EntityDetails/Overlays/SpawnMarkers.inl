static void draw_spawn_markers_overlay(const ImVec2& origin,
                                       const ImVec2& region,
                                       bool viewport_hovered)
{
    using namespace DirectX;
    const bool any_player_start =
        std::any_of(g_level_spawn_markers.begin(),
                    g_level_spawn_markers.end(), ::is_player_start_marker);
    if (!S.show_spawn_markers && !S.show_ent_npcs &&
        !S.show_dig_spots && !S.show_containers && !S.show_ent_text &&
        !any_player_start) {
        return;
    }
    if (!g_mp.no_tilt) return;
    if (g_sel_spawn_marker >= (int)g_level_spawn_markers.size()) {
        g_sel_spawn_marker = -1;
    }
    std::unordered_set<uint32_t> generator_spawn_points_pending_removal;
    for (const auto& marker : g_level_spawn_markers) {
        if (marker.kind == 1 &&
            LevelEdit::EntityRemovalPending(marker.entity_hash)) {
            generator_spawn_points_pending_removal.insert(
                marker.spawn_point_entities.begin(),
                marker.spawn_point_entities.end());
        }
    }
    if (g_sel_spawn_marker >= 0 &&
        (LevelEdit::EntityRemovalPending(
             g_level_spawn_markers[(size_t)g_sel_spawn_marker]
                 .entity_hash) ||
         generator_spawn_points_pending_removal.count(
             g_level_spawn_markers[(size_t)g_sel_spawn_marker]
                 .entity_hash))) {
        g_sel_spawn_marker = -1;
    }

    float cy = cosf(g_flycam.yaw);
    float sy = sinf(g_flycam.yaw);
    float cp = cosf(g_flycam.pitch);
    float sp = sinf(g_flycam.pitch);
    XMVECTOR eye = XMVectorSet(g_flycam.pos[0], g_flycam.pos[1],
                               g_flycam.pos[2], 1);
    XMVECTOR at = XMVectorSet(g_flycam.pos[0] + sy * cp,
                              g_flycam.pos[1] + sp,
                              g_flycam.pos[2] + cy * cp, 1);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    XMMATRIX V = XMMatrixLookAtLH(eye, at, up);
    float fov = XMConvertToRadians(60.0f);
    float aspect = region.x / std::max(1.0f, region.y);
    float far_plane = std::max(g_mp.radius * 100.0f, 1000.0f);
    XMMATRIX P = XMMatrixPerspectiveFovLH(fov, aspect, 0.05f, far_plane);
    XMMATRIX VP = V * P;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 kCol[7] = {
        IM_COL32(255, 255, 255, 220),
        IM_COL32(255, 90, 90, 235),
        IM_COL32(255, 200, 80, 235),
        IM_COL32(120, 255, 140, 235),
        IM_COL32(85, 210, 255, 235),
        IM_COL32(220, 125, 255, 235),
        IM_COL32(90, 225, 225, 235),
    };

    size_t text_drawn = 0;
    for (const auto& kv : g_level_entity_text) {
        if (!S.show_ent_text) break;
        if (!kv.second.has_pos) continue;
        XMVECTOR clip = XMVector4Transform(
            XMVectorSet(kv.second.x, kv.second.z, kv.second.y, 1.0f),
            VP);
        const float w = XMVectorGetW(clip);
        if (w <= 0.05f) continue;
        const float ndcx = XMVectorGetX(clip) / w;
        const float ndcy = XMVectorGetY(clip) / w;
        if (ndcx < -1.1f || ndcx > 1.1f) continue;
        if (ndcy < -1.1f || ndcy > 1.1f) continue;
        ImVec2 pt;
        pt.x = origin.x + (ndcx * 0.5f + 0.5f) * region.x;
        pt.y = origin.y + (1.0f - (ndcy * 0.5f + 0.5f)) * region.y;
        const float r = 4.5f;
        dl->AddQuadFilled(ImVec2(pt.x, pt.y - r),
                          ImVec2(pt.x + r, pt.y),
                          ImVec2(pt.x, pt.y + r),
                          ImVec2(pt.x - r, pt.y),
                          IM_COL32(90, 170, 255, 235));
        if (w < 30.0f) {
            dl->AddText(ImVec2(pt.x + r + 3.0f, pt.y - 7.0f),
                        IM_COL32(160, 210, 255, 235), "text");
        }
        ++text_drawn;
    }
    (void)text_drawn;

    size_t drawn = 0;
    const bool can_pick = viewport_hovered && !LevelEdit::Saving() &&
                          !LevelGizmo::WantsMouse();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool context_clicked =
        ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    int click_hit = -1;
    bool overlay_click_hit = false;
    float click_best = 12.0f * 12.0f;
    uint32_t selected_model_entity_hash = 0;
    if (::g_selected_level_pick_id != 0) {
        for (const MPPerMesh& mesh : g_mp.meshes) {
            if (!mesh.is_entity_model) continue;
            for (const auto& range : mesh.pick_ranges) {
                if (range.selection_id == ::g_selected_level_pick_id) {
                    selected_model_entity_hash = range.gdb_entity_hash;
                    break;
                }
            }
            if (selected_model_entity_hash != 0) break;
        }
    }
    for (size_t mi = 0; mi < g_level_spawn_markers.size(); ++mi) {
        const auto& m = g_level_spawn_markers[mi];
        if (LevelEdit::EntityRemovalPending(m.entity_hash) ||
            generator_spawn_points_pending_removal.count(m.entity_hash)) {
            continue;
        }
        if (m.kind == 2 &&
            LevelEdit::SpawnPointRemovalPending(m.entity_hash)) {
            continue;
        }
        if (!level_marker_visible(m)) continue;
        float ex = m.x, ey = m.y, ez = m.z;
        {
            float d_pos[3], d_rot[3];
            if (LevelEdit::EditFor(0x70000000u | uint32_t(mi), d_pos,
                                   d_rot)) {
                ex += d_pos[0];
                ey += d_pos[1];
                ez += d_pos[2];
            }
        }
        XMVECTOR clip = XMVector4Transform(
            XMVectorSet(ex, ez, ey, 1.0f), VP);
        const float w = XMVectorGetW(clip);
        if (w <= 0.05f) continue;
        const float ndcx = XMVectorGetX(clip) / w;
        const float ndcy = XMVectorGetY(clip) / w;
        if (ndcx < -1.1f || ndcx > 1.1f) continue;
        if (ndcy < -1.1f || ndcy > 1.1f) continue;
        ImVec2 pt;
        pt.x = origin.x + (ndcx * 0.5f + 0.5f) * region.x;
        pt.y = origin.y + (1.0f - (ndcy * 0.5f + 0.5f)) * region.y;
        const bool player_start = ::is_player_start_marker(m);
        const ImU32 col = m.is_container && m.kind != 4
            ? kCol[5]
            : kCol[m.kind < 7 ? m.kind : 0];
        const float r = player_start ? 7.0f
                        : m.kind == 1 ? 6.0f
                                      : (m.is_container ? 5.5f : 4.5f);
        const bool model_owns_selection =
            (m.kind == 2 || m.kind == 3 || m.kind == 6) &&
            !m.model_hashes.empty();
        const bool selected = model_owns_selection
            ? (::g_selected_level_pick_id ==
                   (0x70000000u | uint32_t(mi)) ||
               (selected_model_entity_hash != 0 &&
                selected_model_entity_hash == m.entity_hash))
            : (int(mi) == g_sel_spawn_marker);
        if (player_start) {
            
            const ImU32 kFlag = IM_COL32(70, 230, 110, 245);
            ImVec2 top = pt;
            {
                XMVECTOR tclip = XMVector4Transform(
                    XMVectorSet(ex, ez + 2.2f, ey, 1.0f), VP);
                const float tw = XMVectorGetW(tclip);
                if (tw > 0.05f) {
                    const float tx = XMVectorGetX(tclip) / tw;
                    const float ty = XMVectorGetY(tclip) / tw;
                    top.x = origin.x + (tx * 0.5f + 0.5f) * region.x;
                    top.y = origin.y +
                            (1.0f - (ty * 0.5f + 0.5f)) * region.y;
                }
            }
            dl->AddCircleFilled(pt, 4.5f, kFlag);
            dl->AddCircle(pt, 5.5f,
                          selected ? IM_COL32(255, 255, 255, 255)
                                   : IM_COL32(0, 0, 0, 200),
                          0, selected ? 2.0f : 1.0f);
            dl->AddLine(pt, top, kFlag, 2.0f);
            const float fw = std::max(10.0f, (pt.y - top.y) * 0.35f);
            dl->AddTriangleFilled(
                top, ImVec2(top.x + fw, top.y + fw * 0.4f),
                ImVec2(top.x, top.y + fw * 0.8f), kFlag);
            dl->AddText(ImVec2(top.x + fw + 4.0f, top.y - 3.0f),
                        kFlag, "Player Start");
        } else {
            dl->AddQuadFilled(ImVec2(pt.x, pt.y - r),
                              ImVec2(pt.x + r, pt.y),
                              ImVec2(pt.x, pt.y + r),
                              ImVec2(pt.x - r, pt.y), col);
            dl->AddQuad(ImVec2(pt.x, pt.y - r - 1),
                        ImVec2(pt.x + r + 1, pt.y),
                        ImVec2(pt.x, pt.y + r + 1),
                        ImVec2(pt.x - r - 1, pt.y),
                        selected ? IM_COL32(255, 255, 255, 255)
                                 : IM_COL32(0, 0, 0, 200),
                        selected ? 2.0f : 1.0f);
            if ((w < 45.0f || selected) && !m.name.empty()) {
                dl->AddText(ImVec2(pt.x + r + 3.0f, pt.y - 7.0f),
                            IM_COL32(235, 235, 235, 235),
                            m.name.c_str());
            }
        }


        if (!model_owns_selection && can_pick &&
            (clicked || context_clicked)) {
            const float dx = mouse.x - pt.x;
            const float dy = mouse.y - pt.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < click_best) {
                click_best = d2;
                click_hit = int(mi);
            }
        }
        ++drawn;
    }
    if (click_hit >= 0) {
        overlay_click_hit = true;
        g_sel_spawn_marker = click_hit;
        g_marker_clear_selection = true;
    }

    if (S.show_spawn_markers) {
        std::vector<LevelEdit::GeneratorAddition> pending;
        LevelEdit::GetGenerators(pending);
        int gen_click = -1;
        float gen_best = 12.0f * 12.0f;
        for (size_t gi = 0; gi < pending.size(); ++gi) {
            const auto& pg = pending[gi];
            if (pg.removed) continue;
            XMVECTOR clip = XMVector4Transform(
                XMVectorSet(pg.pos[0], pg.pos[2], pg.pos[1], 1.0f),
                VP);
            const float w = XMVectorGetW(clip);
            if (w <= 0.05f) continue;
            const float ndcx = XMVectorGetX(clip) / w;
            const float ndcy = XMVectorGetY(clip) / w;
            if (ndcx < -1.1f || ndcx > 1.1f) continue;
            if (ndcy < -1.1f || ndcy > 1.1f) continue;
            ImVec2 pt;
            pt.x = origin.x + (ndcx * 0.5f + 0.5f) * region.x;
            pt.y = origin.y +
                   (1.0f - (ndcy * 0.5f + 0.5f)) * region.y;
            const float r = 6.0f;
            dl->AddQuadFilled(ImVec2(pt.x, pt.y - r),
                              ImVec2(pt.x + r, pt.y),
                              ImVec2(pt.x, pt.y + r),
                              ImVec2(pt.x - r, pt.y),
                              IM_COL32(200, 120, 255, 235));
            const bool gsel = (int(gi) == g_sel_pending_gen);
            dl->AddQuad(ImVec2(pt.x, pt.y - r - 1),
                        ImVec2(pt.x + r + 1, pt.y),
                        ImVec2(pt.x, pt.y + r + 1),
                        ImVec2(pt.x - r - 1, pt.y),
                        gsel ? IM_COL32(255, 255, 255, 255)
                             : IM_COL32(0, 0, 0, 200),
                        gsel ? 2.0f : 1.0f);
            const std::string lbl = "new: " + pg.creature_name;
            dl->AddText(ImVec2(pt.x + r + 3.0f, pt.y - 7.0f),
                        IM_COL32(225, 190, 255, 235), lbl.c_str());
            if (can_pick && clicked) {
                const float dx = mouse.x - pt.x;
                const float dy = mouse.y - pt.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 < gen_best) {
                    gen_best = d2;
                    gen_click = int(gi);
                }
            }
        }
        if (gen_click >= 0) {
            overlay_click_hit = true;
            g_sel_pending_gen = gen_click;
            g_sel_pending_sp = -1;
            g_sel_spawn_marker = -1;
            g_marker_clear_selection = true;
        }

        std::vector<LevelEdit::PendingSpawnPoint> psps;
        LevelEdit::GetPendingSpawnPoints(psps);
        int sp_click = -1;
        float sp_best = 12.0f * 12.0f;
        for (const auto& sp : psps) {
            XMVECTOR clip = XMVector4Transform(
                XMVectorSet(sp.pos[0], sp.pos[2], sp.pos[1], 1.0f),
                VP);
            const float w = XMVectorGetW(clip);
            if (w <= 0.05f) continue;
            const float ndcx = XMVectorGetX(clip) / w;
            const float ndcy = XMVectorGetY(clip) / w;
            if (ndcx < -1.1f || ndcx > 1.1f) continue;
            if (ndcy < -1.1f || ndcy > 1.1f) continue;
            ImVec2 pt;
            pt.x = origin.x + (ndcx * 0.5f + 0.5f) * region.x;
            pt.y = origin.y +
                   (1.0f - (ndcy * 0.5f + 0.5f)) * region.y;
            const float r = 4.5f;
            dl->AddQuadFilled(ImVec2(pt.x, pt.y - r),
                              ImVec2(pt.x + r, pt.y),
                              ImVec2(pt.x, pt.y + r),
                              ImVec2(pt.x - r, pt.y),
                              IM_COL32(225, 160, 255, 235));
            const bool spsel = (sp.id == g_sel_pending_sp);
            dl->AddQuad(ImVec2(pt.x, pt.y - r - 1),
                        ImVec2(pt.x + r + 1, pt.y),
                        ImVec2(pt.x, pt.y + r + 1),
                        ImVec2(pt.x - r - 1, pt.y),
                        spsel ? IM_COL32(255, 255, 255, 255)
                              : IM_COL32(0, 0, 0, 200),
                        spsel ? 2.0f : 1.0f);
            if (w < 30.0f || spsel) {
                dl->AddText(ImVec2(pt.x + r + 3.0f, pt.y - 7.0f),
                            IM_COL32(235, 205, 255, 235),
                            sp.label.c_str());
            }
            if (can_pick && clicked) {
                const float dx = mouse.x - pt.x;
                const float dy = mouse.y - pt.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 < sp_best) {
                    sp_best = d2;
                    sp_click = sp.id;
                }
            }
        }
        if (sp_click >= 0) {
            overlay_click_hit = true;
            g_sel_pending_sp = sp_click;
            g_sel_pending_gen = -1;
            g_sel_spawn_marker = -1;
            g_marker_clear_selection = true;
        } else if (click_hit >= 0) {
            g_sel_pending_sp = -1;
            g_sel_pending_gen = -1;
        }
    }
    if (can_pick && (clicked || context_clicked) && !overlay_click_hit) {
        g_sel_spawn_marker = -1;
        g_sel_pending_sp = -1;
        g_sel_pending_gen = -1;
    }
    if (drawn) {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "entity markers: %zu shown / %zu total", drawn,
                      g_level_spawn_markers.size());
        dl->AddText(ImVec2(origin.x + 14, origin.y + region.y - 38),
                    IM_COL32(220, 220, 220, 200), buf);
    }
}
