void MP_ComputeWorldPose(const ModelPreview& mp,
                         const std::vector<float>& deltas,
                         std::vector<float>& out_world_pose){
    out_world_pose.clear();
    if (mp.bone_count == 0) return;

    const uint32_t n = mp.bone_count;

    if (mp.local_rest.size()   < (size_t)n * 11) return;
    if (mp.bone_parents.size() < (size_t)n)       return;

    const bool have_deltas = (deltas.size() >= (size_t)n * 4);
    const bool have_anim_pose =
        S.bone_anim_pose_active &&
        S.bone_anim_rot_absolute.size() >= (size_t)n * 4 &&
        S.bone_anim_rot_present.size() >= (size_t)n;
    const bool have_anim_trans =
        S.bone_anim_trans_delta.size() >= (size_t)n * 3 &&
        S.bone_anim_trans_present.size() >= (size_t)n;

    std::vector<XMFLOAT4X4> local(n);
    for (uint32_t i = 0; i < n; ++i){
        const float* tf = &mp.local_rest[(size_t)i * 11];
        const float* dq = have_deltas ? &deltas[(size_t)i * 4] : nullptr;
        const float* aq =
            (have_anim_pose && S.bone_anim_rot_present[(size_t)i])
                ? &S.bone_anim_rot_absolute[(size_t)i * 4]
                : nullptr;
        const float* at =
            (have_anim_pose && have_anim_trans &&
             S.bone_anim_trans_present[(size_t)i])
                ? &S.bone_anim_trans_delta[(size_t)i * 3]
                : nullptr;
        XMMATRIX L = have_anim_pose
            ? bone_local_matrix_anim_delta(tf, aq, at)
            : bone_local_matrix(tf, dq);
        XMStoreFloat4x4(&local[i], L);
    }

    std::vector<XMFLOAT4X4> world(n);
    std::vector<uint8_t> done(n, 0);
    for (uint32_t i = 0; i < n; ++i){
        if (done[i]) continue;
        std::vector<int> chain;
        int cur = (int)i;
        while (cur >= 0 && cur < (int)n && !done[cur]){
            chain.push_back(cur);
            cur = mp.bone_parents[cur];
        }
        XMMATRIX accum;
        if (cur >= 0 && cur < (int)n) accum = XMLoadFloat4x4(&world[cur]);
        else                          accum = XMMatrixIdentity();
        for (auto it = chain.rbegin(); it != chain.rend(); ++it){
            XMMATRIX L = XMLoadFloat4x4(&local[*it]);
            accum = L * accum;
            XMStoreFloat4x4(&world[*it], accum);
            done[*it] = 1;
        }
    }

    out_world_pose.resize((size_t)n * 16);
    for (uint32_t i = 0; i < n; ++i){
        std::memcpy(&out_world_pose[(size_t)i * 16],
                    &world[i], sizeof(float) * 16);
    }
}
