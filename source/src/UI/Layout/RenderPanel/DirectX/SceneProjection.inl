static void project_bones_to_screen(
    const std::vector<float>& world_pose,
    uint32_t bone_count,
    const ImVec2& origin,
    const ImVec2& region,
    std::vector<ImVec2>& out_screen,
    std::vector<uint8_t>& out_visible)
{
    using namespace DirectX;

    out_screen.assign(bone_count, ImVec2(0, 0));
    out_visible.assign(bone_count, 0);
    if (bone_count == 0) return;

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

    float fov       = XMConvertToRadians(60.0f);
    float aspect    = region.x / std::max(1.0f, region.y);
    float far_plane = std::max(g_mp.radius * 100.0f, 100.0f);
    XMMATRIX P = XMMatrixPerspectiveFovLH(fov, aspect, 0.05f, far_plane);

    XMMATRIX W = XMMatrixIdentity();
    if (!g_mp.no_tilt) {
        const float tiltX = -XM_PIDIV2;
        XMMATRIX Tm = XMMatrixTranslation(-g_mp.center[0], -g_mp.center[1], -g_mp.center[2]);
        XMMATRIX Rx = XMMatrixRotationX(tiltX);
        XMMATRIX Tp = XMMatrixTranslation( g_mp.center[0],  g_mp.center[1],  g_mp.center[2]);
        XMMATRIX FlipX = XMMatrixScaling(-1.0f, 1.0f, 1.0f);
        W = Tm * Rx * Tp * FlipX;
    }
    XMMATRIX WVP = W * V * P;

    for (uint32_t i = 0; i < bone_count; ++i) {
        XMFLOAT4X4 wf;
        std::memcpy(&wf, &world_pose[(size_t)i * 16], sizeof(float) * 16);
        XMMATRIX Wp = XMLoadFloat4x4(&wf);
        XMVECTOR pos = XMVector3Transform(XMVectorSet(0, 0, 0, 1), Wp);
        XMVECTOR clip = XMVector4Transform(XMVectorSetW(pos, 1.0f), WVP);
        float w = XMVectorGetW(clip);
        if (w <= 0.05f) continue;
        float ndcx = XMVectorGetX(clip) / w;
        float ndcy = XMVectorGetY(clip) / w;
        if (ndcx < -1.5f || ndcx > 1.5f) continue;
        if (ndcy < -1.5f || ndcy > 1.5f) continue;
        out_screen[i].x = origin.x + (ndcx * 0.5f + 0.5f) * region.x;
        out_screen[i].y = origin.y + (1.0f - (ndcy * 0.5f + 0.5f)) * region.y;
        out_visible[i] = 1;
    }
}
