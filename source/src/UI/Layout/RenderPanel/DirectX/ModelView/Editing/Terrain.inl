    const bool sculpt_mode_active =
        details_panel_docked() && g_mp.has_model && g_mp.no_tilt &&
        LandscapePanel::InSculptMode();
    const bool paint_mode_active =
        details_panel_docked() && g_mp.has_model && g_mp.no_tilt &&
        LandscapePanel::InPaintMode() && TerrainPaint::Active();
    bool paint_dab_applied = false;
    if (sculpt_mode_active || paint_mode_active) {
        enum TerrainTool {
            TT_NONE = 0,
            TT_RAISE,
            TT_LOWER,
            TT_SMOOTH,
            TT_FLATTEN,
            TT_NOISE,
        };
        auto upload_after_edit = [&]() {
            if (!g_mp.meshes.empty()) {
                TerrainEdit::ApplyToGpu(device, &g_mp.meshes[0]);
            }
        };

        
        
        int   eff_tool     = TT_NONE;
        float eff_size     = LandscapePanel::BrushSize();
        float eff_strength = LandscapePanel::ToolStrength();
        float eff_falloff  = LandscapePanel::BrushFalloff();
        if (sculpt_mode_active) {
            const bool lower_mod = ImGui::GetIO().KeyShift;
            switch (LandscapePanel::SculptTool()) {
                case 0: eff_tool = lower_mod ? TT_LOWER : TT_RAISE; break;
                case 1: eff_tool = TT_SMOOTH; break;
                case 2: eff_tool = TT_FLATTEN; break;
                case 3: eff_tool = TT_NOISE; break;
                default: eff_tool = TT_NONE; break;
            }
            eff_size = LandscapePanel::BrushSize();
            const float str01 = LandscapePanel::ToolStrength();
            eff_strength =
                (eff_tool == TT_SMOOTH || eff_tool == TT_FLATTEN)
                    ? str01
                    : str01 * 0.35f;
            eff_falloff = LandscapePanel::BrushFalloff();
        } else if (paint_mode_active) {
            eff_tool = TT_RAISE;   
            eff_size = LandscapePanel::BrushSize();
            eff_strength = LandscapePanel::ToolStrength();
            eff_falloff = LandscapePanel::BrushFalloff();
        }

        if (TerrainEdit::IsLoaded() && eff_tool != TT_NONE) {
            ImVec2 mp_pos  = ImGui::GetIO().MousePos;
            const bool over_view =
                mp_pos.x >= origin.x   && mp_pos.x < origin.x + region.x &&
                mp_pos.y >= origin.y   && mp_pos.y < origin.y + region.y;
            
            
            
            
            const bool imgui_captured =
                !hovered ||
                ImGui::IsPopupOpen(nullptr,
                                   ImGuiPopupFlags_AnyPopupId |
                                       ImGuiPopupFlags_AnyPopupLevel);

            if (over_view && g_mp.width > 0 && g_mp.height > 0) {
                using namespace DirectX;

                const float cy = cosf(g_flycam.yaw);
                const float sy = sinf(g_flycam.yaw);
                const float cp = cosf(g_flycam.pitch);
                const float sp = sinf(g_flycam.pitch);
                const float forward[3] = { sy * cp, sp, cy * cp };
                XMVECTOR eye = XMVectorSet(g_flycam.pos[0],
                    g_flycam.pos[1], g_flycam.pos[2], 1);
                XMVECTOR at  = XMVectorSet(g_flycam.pos[0] + forward[0],
                    g_flycam.pos[1] + forward[1],
                    g_flycam.pos[2] + forward[2], 1);
                XMVECTOR up  = XMVectorSet(0, 1, 0, 0);
                XMMATRIX V = XMMatrixLookAtLH(eye, at, up);
                const float fov = XMConvertToRadians(60.0f);
                const float aspect = (float)g_mp.width / (float)g_mp.height;
                const float far_plane = g_mp.radius * 100.0f;
                XMMATRIX P = XMMatrixPerspectiveFovLH(fov, aspect,
                                                     0.05f, far_plane);
                XMMATRIX VP = V * P;
                XMVECTOR det;
                XMMATRIX inv_VP = XMMatrixInverse(&det, VP);

                const float u = (mp_pos.x - origin.x) / region.x;
                const float v = (mp_pos.y - origin.y) / region.y;
                const float ndc_x =  u * 2.f - 1.f;
                const float ndc_y =  1.f - v * 2.f;

                XMVECTOR near_pt = XMVector4Transform(
                    XMVectorSet(ndc_x, ndc_y, 0.f, 1.f), inv_VP);
                XMVECTOR far_pt  = XMVector4Transform(
                    XMVectorSet(ndc_x, ndc_y, 1.f, 1.f), inv_VP);
                near_pt = XMVectorScale(near_pt,
                    1.f / XMVectorGetW(near_pt));
                far_pt  = XMVectorScale(far_pt,
                    1.f / XMVectorGetW(far_pt));
                const float ox = XMVectorGetX(near_pt);
                const float oy = XMVectorGetY(near_pt);
                const float oz = XMVectorGetZ(near_pt);
                const float dx = XMVectorGetX(far_pt) - ox;
                const float dy = XMVectorGetY(far_pt) - oy;
                const float dz = XMVectorGetZ(far_pt) - oz;

                float hx, hy, hz;
                if (TerrainEdit::Raycast(ox, oy, oz, dx, dy, dz,
                                         hx, hy, hz))
                {
                    const int kSeg = 48;
                    ImDrawList* dlay = ImGui::GetForegroundDrawList();
                    auto draw_terrain_ring = [&](float ring_radius,
                                                 ImU32 col,
                                                 float thickness) {
                        if (ring_radius <= 0.01f) return;
                        ImVec2 last_screen{};
                        bool last_valid = false;
                        for (int i = 0; i <= kSeg; ++i) {
                            const float ang =
                                (float)i / (float)kSeg * 6.2831853f;
                            const float wx =
                                hx + cosf(ang) * ring_radius;
                            const float wz =
                                hz + sinf(ang) * ring_radius;
                            const float wy =
                                TerrainEdit::SampleHeightAtWorldXZ(wx, wz);
                            XMVECTOR wpt = XMVectorSet(wx, wy, wz, 1.f);
                            XMVECTOR cs  = XMVector4Transform(wpt, VP);
                            const float ws = XMVectorGetW(cs);
                            if (ws <= 0.f) { last_valid = false; continue; }
                            const float nx = XMVectorGetX(cs) / ws;
                            const float ny = XMVectorGetY(cs) / ws;
                            const float sx = origin.x +
                                (nx * 0.5f + 0.5f) * region.x;
                            const float sy = origin.y +
                                (1.f - (ny * 0.5f + 0.5f)) * region.y;
                            const ImVec2 sc(sx, sy);
                            if (last_valid) {
                                dlay->AddLine(last_screen, sc, col,
                                              thickness);
                            }
                            last_screen = sc;
                            last_valid = true;
                        }
                    };
                    const float radius = eff_size;
                    draw_terrain_ring(radius,
                                      IM_COL32(255, 215, 0, 220), 1.5f);
                    
                    
                    if (eff_falloff > 0.02f && eff_falloff < 0.98f) {
                        draw_terrain_ring(radius * (1.0f - eff_falloff),
                                          IM_COL32(255, 235, 130, 140),
                                          1.0f);
                    }
                    XMVECTOR cpt = XMVector4Transform(
                        XMVectorSet(hx, hy, hz, 1.f), VP);
                    const float cw = XMVectorGetW(cpt);
                    if (cw > 0.f) {
                        const float cnx = XMVectorGetX(cpt) / cw;
                        const float cny = XMVectorGetY(cpt) / cw;
                        const float csx = origin.x
                            + (cnx * 0.5f + 0.5f) * region.x;
                        const float csy = origin.y
                            + (1.f - (cny * 0.5f + 0.5f)) * region.y;
                        dlay->AddCircleFilled(ImVec2(csx, csy), 3.f,
                            IM_COL32(255, 215, 0, 255));
                    }

                    if (paint_mode_active && !imgui_captured &&
                        ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    {
                        
                        TerrainPaint::ApplyBrush(
                            hx, hz, eff_size, eff_strength, eff_falloff,
                            ImGui::GetIO().KeyShift);
                        paint_dab_applied = true;
                    }
                    else if (!imgui_captured &&
                        ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    {
                        TerrainEdit::BrushTool bt =
                            TerrainEdit::BrushTool::None;
                        switch (eff_tool) {
                            case TT_RAISE:   bt = TerrainEdit::BrushTool::Raise; break;
                            case TT_LOWER:   bt = TerrainEdit::BrushTool::Lower; break;
                            case TT_SMOOTH:  bt = TerrainEdit::BrushTool::Smooth; break;
                            case TT_FLATTEN: bt = TerrainEdit::BrushTool::Flatten; break;
                            case TT_NOISE:   bt = TerrainEdit::BrushTool::Noise; break;
                            default: break;
                        }
                        
                        
                        
                        static float s_flatten_target = 0.0f;
                        if (ImGui::IsMouseClicked(
                                ImGuiMouseButton_Left)) {
                            s_flatten_target =
                                TerrainEdit::SampleHeightAtWorldXZ(hx,
                                                                   hz);
                        }
                        const float target_h =
                            (eff_tool == TT_FLATTEN) ? s_flatten_target
                                                     : 0.f;
                        TerrainEdit::ApplyBrush(bt, hx, hz,
                            eff_size, eff_strength, target_h,
                            eff_falloff);
                        upload_after_edit();
                    }
                }
            }
        }
    }

    if (!paint_mode_active || !paint_dab_applied) {
        TerrainPaint::EndStroke();
    }
