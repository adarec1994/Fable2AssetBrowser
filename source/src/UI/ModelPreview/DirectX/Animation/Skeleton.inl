static XMMATRIX bone_local_matrix(const float* tf, const float* delta ){
    XMVECTOR q = XMVectorSet(tf[0], tf[1], tf[2], tf[3]);
    XMVECTOR t = XMVectorSet(tf[4], tf[5], tf[6], 0.0f);
    XMVECTOR s = XMVectorSet(tf[7], tf[8], tf[9], 1.0f);
    XMMATRIX S_ = XMMatrixScalingFromVector(s);
    XMMATRIX R_ = XMMatrixRotationQuaternion(q);
    if (delta) {
        XMVECTOR qd = XMVectorSet(delta[0], delta[1], delta[2], delta[3]);

        XMMATRIX D_ = XMMatrixRotationQuaternion(qd);
        R_ = D_ * R_;
    }
    XMMATRIX T_ = XMMatrixTranslationFromVector(t);
    return S_ * R_ * T_;
}

static XMMATRIX bone_local_matrix_anim_delta(const float* tf,
                                             const float* anim_q,
                                             const float* anim_t) {
    XMVECTOR q = XMVectorSet(tf[0], tf[1], tf[2], tf[3]);
    XMVECTOR t = XMVectorSet(tf[4], tf[5], tf[6], 0.0f);
    XMVECTOR s = XMVectorSet(tf[7], tf[8], tf[9], 1.0f);
    XMMATRIX S_ = XMMatrixScalingFromVector(s);
    XMMATRIX R_ = XMMatrixRotationQuaternion(q);
    if (anim_q) {
        XMVECTOR qd = XMVectorSet(anim_q[0], anim_q[1],
                                  anim_q[2], anim_q[3]);
        R_ = XMMatrixRotationQuaternion(qd);
    }
    if (anim_t) {
        XMVECTOR dt = XMVectorSet(anim_t[0], anim_t[1], anim_t[2], 0.0f);
        t = XMVectorAdd(t, dt);
    }
    XMMATRIX T_ = XMMatrixTranslationFromVector(t);
    return S_ * R_ * T_;
}

static void compute_rest_world(const MDLInfo& info,
                               uint32_t n_cap,
                               std::vector<XMFLOAT4X4>& out_world){
    const uint32_t n = std::min<uint32_t>(info.BoneCount, n_cap);
    out_world.assign(n, XMFLOAT4X4());
    XMFLOAT4X4 ident_f; XMStoreFloat4x4(&ident_f, XMMatrixIdentity());
    for (uint32_t i = 0; i < n; ++i) out_world[i] = ident_f;

    if (n == 0 || !info.HasBoneTransforms) return;
    if (info.Bones.size() != info.BoneTransforms.size()) return;

    std::vector<XMFLOAT4X4> local(n);
    for (uint32_t i = 0; i < n; ++i){
        const auto& tf = info.BoneTransforms[i];
        XMMATRIX L = (tf.size() >= 10)
                     ? bone_local_matrix(tf.data(), nullptr)
                     : XMMatrixIdentity();
        XMStoreFloat4x4(&local[i], L);
    }
    std::vector<uint8_t> done(n, 0);
    for (uint32_t i = 0; i < n; ++i){
        if (done[i]) continue;
        std::vector<int> chain;
        int cur = (int)i;
        while (cur >= 0 && cur < (int)n && !done[cur]){
            chain.push_back(cur);
            cur = info.Bones[cur].ParentID;
        }
        XMMATRIX accum;
        if (cur >= 0 && cur < (int)n) {
            accum = XMLoadFloat4x4(&out_world[cur]);
        } else {
            accum = XMMatrixIdentity();
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it){
            XMMATRIX L = XMLoadFloat4x4(&local[*it]);
            accum = L * accum;
            XMStoreFloat4x4(&out_world[*it], accum);
            done[*it] = 1;
        }
    }
}

static void mp_set_skeleton_bind(const MDLInfo& info,
                                 ModelPreview& mp,
                                 bool reset_pose) {
    uint32_t n = 0;
    if (info.HasBoneTransforms && info.BoneCount > 0 &&
        info.Bones.size() == info.BoneTransforms.size()) {
        n = std::min<uint32_t>(info.BoneCount, MP_MAX_BONES);
    }

    std::vector<int> parents(n);
    std::vector<float> local_rest((size_t)n * 11, 0.0f);
    std::vector<float> inv_bind((size_t)n * 16, 0.0f);
    for (uint32_t i = 0; i < n; ++i) {
        int parent = info.Bones[i].ParentID;
        if (parent >= (int)n) parent = -1;
        parents[i] = parent;
        const auto& transform = info.BoneTransforms[i];
        for (int component = 0; component < 11; ++component) {
            local_rest[(size_t)i * 11 + component] =
                component < (int)transform.size()
                    ? transform[(size_t)component] : 0.0f;
        }
    }

    std::vector<XMFLOAT4X4> rest_world;
    compute_rest_world(info, n, rest_world);
    for (uint32_t i = 0; i < n && i < rest_world.size(); ++i) {
        const XMMATRIX world = XMLoadFloat4x4(&rest_world[i]);
        const XMMATRIX inverse = XMMatrixInverse(nullptr, world);
        XMFLOAT4X4 matrix;
        XMStoreFloat4x4(&matrix, inverse);
        std::memcpy(&inv_bind[(size_t)i * 16], &matrix,
                    sizeof(float) * 16);
    }

    const bool topology_changed = mp.bone_count != n ||
                                  mp.bone_parents != parents;
    mp.bone_count = n;
    mp.bone_parents = std::move(parents);
    mp.local_rest = std::move(local_rest);
    mp.inv_bind = std::move(inv_bind);

    if (!reset_pose && !topology_changed) return;
    S.bone_rot_deltas.assign((size_t)n * 4, 0.0f);
    for (uint32_t i = 0; i < n; ++i) {
        S.bone_rot_deltas[(size_t)i * 4 + 3] = 1.0f;
    }
    S.bone_anim_rot_absolute.clear();
    S.bone_anim_rot_present.clear();
    S.bone_anim_trans_delta.clear();
    S.bone_anim_trans_present.clear();
    S.bone_anim_pose_active = false;
    S.selected_bone = -1;
    S.bone_rotate_mode = false;
}
