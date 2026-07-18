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
