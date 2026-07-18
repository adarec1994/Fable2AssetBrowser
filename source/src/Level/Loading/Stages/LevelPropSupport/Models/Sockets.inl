static bool ReadMdlSocket(const std::vector<uint8_t>& d,
                          const std::string& socket_name,
                          bool allow_fx_particle_prefix,
                          float out[3],
                          float out_q[4] = nullptr) {

    const size_t n = d.size();
    if (n < 40) return false;
    auto beU = [&](size_t o) -> uint32_t {
        return (uint32_t(d[o]) << 24) | (uint32_t(d[o + 1]) << 16) |
               (uint32_t(d[o + 2]) << 8) | uint32_t(d[o + 3]);
    };
    auto beF = [&](size_t o) -> float {
        uint32_t v = beU(o); float f; std::memcpy(&f, &v, 4); return f;
    };
    size_t o;
    if (n >= 16 && std::memcmp(d.data(), "MeshFile", 8) == 0) {
        uint32_t hs = beU(12);
        if (hs < 16 || hs > n) return false;
        o = hs;
    } else {
        o = 0;
    }
    o += 32;
    if (o + 4 > n) return false;
    uint32_t bc = beU(o); o += 4;
    if (bc == 0 || bc > 1024) return false;
    std::vector<std::string> names(bc);
    std::vector<int> parents(bc);
    for (uint32_t i = 0; i < bc; ++i) {
        size_t s = o;
        while (o < n && d[o] != 0) ++o;
        if (o >= n) return false;
        names[i].assign(reinterpret_cast<const char*>(&d[s]), o - s);
        ++o;
        if (o + 4 > n) return false;
        uint32_t pid = beU(o); o += 4;
        parents[i] = (pid == 0xFFFFFFFFu) ? -1 : (int)pid;
    }
    if (o + 4 > n) return false;
    uint32_t tc = beU(o); o += 4;
    if (tc != bc) return false;
    const size_t xf = o;
    if (xf + size_t(tc) * 44 > n) return false;

    auto lower = [](std::string s) {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };
    const std::string want = lower(socket_name);
    int idx = -1;
    if (!want.empty()) {
        for (uint32_t i = 0; i < bc; ++i)
            if (lower(names[i]) == want) { idx = int(i); break; }
    }
    if (idx < 0 && allow_fx_particle_prefix)
        for (uint32_t i = 0; i < bc; ++i)
            if (lower(names[i]).rfind("fx_particle", 0) == 0) {
                idx = int(i); break;
            }
    if (idx < 0) return false;

    auto par = [&](int i) -> int {
        int p = parents[i];
        return (p >= 0 && (uint32_t)p < bc && p != i) ? p : -1;
    };
    auto qx = [&](int i) { return beF(xf + 44 * i + 0); };
    auto qy = [&](int i) { return beF(xf + 44 * i + 4); };
    auto qz = [&](int i) { return beF(xf + 44 * i + 8); };
    auto qw = [&](int i) { return beF(xf + 44 * i + 12); };
    auto px = [&](int i) { return beF(xf + 44 * i + 16); };
    auto py = [&](int i) { return beF(xf + 44 * i + 20); };
    auto pz = [&](int i) { return beF(xf + 44 * i + 24); };

    std::vector<int> chain;
    std::vector<char> seen(bc, 0);
    for (int cur = idx, guard = 0; guard < 256; ++guard) {
        if (cur < 0 || (uint32_t)cur >= bc || seen[cur]) break;
        seen[cur] = 1; chain.push_back(cur); cur = par(cur);
    }

    auto qnorm = [](float q[4]) {
        const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] +
                                    q[2] * q[2] + q[3] * q[3]);
        if (std::isfinite(len) && len > 1e-6f) {
            q[0] /= len; q[1] /= len; q[2] /= len; q[3] /= len;
        } else {
            q[0] = q[1] = q[2] = 0.0f; q[3] = 1.0f;
        }
    };
    auto qmul = [](const float a[4], const float b[4], float out[4]) {
        out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
        out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
        out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
        out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    };
    auto qrot = [](const float q[4], const float v[3], float out[3]) {
        const float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
        const float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
        const float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
        out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
        out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
        out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
    };

    float w[3] = { 0, 0, 0 };
    float q[4] = { 0, 0, 0, 1 };
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        int i = *it;
        float lp[3] = { px(i), py(i), pz(i) };
        float rp[3] = { 0, 0, 0 };
        qrot(q, lp, rp);
        w[0] += rp[0]; w[1] += rp[1]; w[2] += rp[2];

        float lq[4] = { qx(i), qy(i), qz(i), qw(i) };
        qnorm(lq);
        float nq[4] = { 0, 0, 0, 1 };
        qmul(q, lq, nq);
        q[0] = nq[0]; q[1] = nq[1]; q[2] = nq[2]; q[3] = nq[3];
        qnorm(q);
    }
    out[0] = w[0]; out[1] = w[1]; out[2] = w[2];
    if (out_q) {
        out_q[0] = q[0]; out_q[1] = q[1]; out_q[2] = q[2]; out_q[3] = q[3];
    }
    return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
}

static bool ReadMdlFxSocket(const std::vector<uint8_t>& d, float out[3]) {
    return ReadMdlSocket(d, "fx_particle_dummyobject", true, out);
}

struct GmdFxAttachment {
    std::string effect;
    float pos[3] = { 0, 0, 0 };
    float quat[4] = { 0, 0, 0, 1 };
};

static bool ParseGmdFxAttachments(const std::vector<uint8_t>& d,
                                  std::vector<GmdFxAttachment>& out) {
    out.clear();
    const size_t n = d.size();
    if (n < 0x3C || std::memcmp(d.data(), "GameMesh", 8) != 0) return false;
    auto beU = [&](size_t o) -> uint32_t {
        return (uint32_t(d[o]) << 24) | (uint32_t(d[o + 1]) << 16) |
               (uint32_t(d[o + 2]) << 8) | uint32_t(d[o + 3]);
    };
    auto beI = [&](size_t o) -> int32_t { return (int32_t)beU(o); };
    auto beF = [&](size_t o) -> float {
        uint32_t v = beU(o); float f; std::memcpy(&f, &v, 4); return f;
    };
    auto qrot = [](const float q[4], const float v[3], float o3[3]) {
        const float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
        const float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
        const float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
        o3[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
        o3[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
        o3[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
    };
    auto qmul = [](const float a[4], const float b[4], float o4[4]) {
        o4[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
        o4[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
        o4[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
        o4[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    };

    const uint32_t node_count = beU(0x2C);
    if (node_count > 512) return false;
    size_t o = 0x30;
    std::vector<int> parents(node_count, -1);
    std::vector<std::array<float, 7>> node_tf;
    for (uint32_t i = 0; i < node_count; ++i) {
        while (o < n && d[o] != 0) ++o;
        if (++o + 4 > n) return false;
        parents[i] = beI(o); o += 4;
    }
    if (node_count) {
        if (o + 4 > n) return false;
        const uint32_t tc = beU(o); o += 4;
        if (tc != node_count || o + size_t(tc) * 44 > n) return false;
        node_tf.resize(node_count);
        for (uint32_t i = 0; i < node_count; ++i) {
            for (int k = 0; k < 7; ++k)
                node_tf[i][k] = beF(o + size_t(k) * 4);
            o += 44;
        }
    }
    if (o + 4 > n) return false;
    const uint32_t att_count = beU(o); o += 4;
    if (att_count > 64) return false;

    for (uint32_t i = 0; i < att_count; ++i) {
        const size_t s = o;
        while (o < n && d[o] != 0) ++o;
        if (o >= n) return false;
        std::string name(reinterpret_cast<const char*>(&d[s]), o - s);
        ++o;
        if (o + 32 > n) return false;
        float q[4], p3[3];
        for (int k = 0; k < 4; ++k) q[k] = beF(o + size_t(k) * 4);
        for (int k = 0; k < 3; ++k) p3[k] = beF(o + 16 + size_t(k) * 4);
        const int par = beI(o + 28);
        o += 32;

        constexpr const char* kPfx = "Prop.FX.Particle.";
        if (name.rfind(kPfx, 0) != 0) continue;
        std::string rest = name.substr(std::strlen(kPfx));
        if (rest.size() > 4 &&
            rest.compare(rest.size() - 4, 4, ".par") == 0)
            rest.resize(rest.size() - 4);
        const size_t dot = rest.find_last_of('.');
        if (dot != std::string::npos) rest = rest.substr(dot + 1);
        if (rest.empty()) continue;

        GmdFxAttachment att;
        att.effect = std::move(rest);

        float wq[4] = { 0, 0, 0, 1 };
        float wp[3] = { 0, 0, 0 };
        std::vector<int> chain;
        for (int cur = par, guard = 0;
             cur >= 0 && cur < (int)node_count && guard < 64; ++guard) {
            chain.push_back(cur);
            cur = parents[cur];
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const auto& tf = node_tf[*it];
            const float lp[3] = { tf[4], tf[5], tf[6] };
            float rp[3];
            qrot(wq, lp, rp);
            wp[0] += rp[0]; wp[1] += rp[1]; wp[2] += rp[2];
            const float lq[4] = { tf[0], tf[1], tf[2], tf[3] };
            float nq[4];
            qmul(wq, lq, nq);
            std::memcpy(wq, nq, sizeof nq);
        }
        float rp[3];
        qrot(wq, p3, rp);
        att.pos[0] = wp[0] + rp[0];
        att.pos[1] = wp[1] + rp[1];
        att.pos[2] = wp[2] + rp[2];
        float fq[4];
        qmul(wq, q, fq);
        std::memcpy(att.quat, fq, sizeof fq);
        if (std::isfinite(att.pos[0]) && std::isfinite(att.pos[1]) &&
            std::isfinite(att.pos[2]))
            out.push_back(std::move(att));
    }
    return !out.empty();
}

