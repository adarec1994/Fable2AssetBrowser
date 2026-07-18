static void draw_gdb_placements_overlay(const ImVec2& origin,
                                        const ImVec2& region)
{
    using namespace DirectX;
    if (g_level_gdb_placements.empty()) return;
    if (!S.show_gdb_placements) return;

    float cy = cosf(g_flycam.yaw);
    float sy = sinf(g_flycam.yaw);
    float cp = cosf(g_flycam.pitch);
    float sp = sinf(g_flycam.pitch);
    XMVECTOR eye = XMVectorSet(g_flycam.pos[0], g_flycam.pos[1], g_flycam.pos[2], 1);
    XMVECTOR at  = XMVectorSet(g_flycam.pos[0] + sy * cp,
                               g_flycam.pos[1] + sp,
                               g_flycam.pos[2] + cy * cp, 1);
    XMVECTOR up  = XMVectorSet(0, 1, 0, 0);
    XMMATRIX V = XMMatrixLookAtLH(eye, at, up);
    float fov = XMConvertToRadians(60.0f);
    float aspect = region.x / std::max(1.0f, region.y);
    float far_plane = std::max(g_mp.radius * 100.0f, 1000.0f);
    XMMATRIX P = XMMatrixPerspectiveFovLH(fov, aspect, 0.05f, far_plane);
    XMMATRIX VP = V * P;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col_fixed = IM_COL32(255, 80, 80, 230);
    const ImU32 col_var   = IM_COL32(120, 220, 255, 180);

    const int   gw = g_pending_terrain_ghf_width;
    const int   gh = g_pending_terrain_ghf_height;
    const float tile = g_pending_terrain_ghf_tile_size > 0.0f
                         ? g_pending_terrain_ghf_tile_size : 0.5f;
    const auto& heights = g_pending_terrain_ghf_heights;
    const bool  have_terrain = (gw > 0 && gh > 0 &&
                                heights.size() == size_t(gw) * size_t(gh));
    auto sample_height = [&](float wx, float wy) -> float {
        if (!have_terrain) return 0.0f;
        const float gx = wx / tile;
        const float gy = wy / tile;
        int ix = int(gx); int iy = int(gy);
        if (ix < 0) ix = 0; else if (ix >= gw) ix = gw - 1;
        if (iy < 0) iy = 0; else if (iy >= gh) iy = gh - 1;
        return heights[size_t(iy) * size_t(gw) + size_t(ix)];
    };

    size_t drawn = 0;
    for (const auto& gp : g_level_gdb_placements) {
        const float rx = gp.x;
        const float ry = sample_height(gp.x, gp.y) + 1.0f;
        const float rz = gp.y;
        XMVECTOR clip = XMVector4Transform(XMVectorSet(rx, ry, rz, 1.0f), VP);
        float w = XMVectorGetW(clip);
        if (w <= 0.05f) continue;
        float ndcx = XMVectorGetX(clip) / w;
        float ndcy = XMVectorGetY(clip) / w;
        if (ndcx < -1.2f || ndcx > 1.2f) continue;
        if (ndcy < -1.2f || ndcy > 1.2f) continue;
        ImVec2 sp_screen;
        sp_screen.x = origin.x + (ndcx * 0.5f + 0.5f) * region.x;
        sp_screen.y = origin.y + (1.0f - (ndcy * 0.5f + 0.5f)) * region.y;

        const bool fixed = (gp.marker == 0x00004B40);
        const ImU32 col  = fixed ? col_fixed : col_var;
        const float r    = fixed ? 4.0f : 2.5f;
        dl->AddCircleFilled(sp_screen, r, col);
        if (fixed) {
            dl->AddCircle(sp_screen, r + 1.0f, IM_COL32(0, 0, 0, 200), 12, 1.0f);
        }
        if (fixed) {
            dl->AddText(ImVec2(sp_screen.x + r + 4.0f, sp_screen.y - 7.0f),
                        IM_COL32(255, 150, 150, 235), "Player start");
        }
        ++drawn;
    }

    char buf[96];
    std::snprintf(buf, sizeof(buf),
                  "gdb placements: %zu shown / %zu total",
                  drawn, g_level_gdb_placements.size());
    dl->AddText(ImVec2(origin.x + 14, origin.y + region.y - 22),
                IM_COL32(220, 220, 220, 200), buf);
}
