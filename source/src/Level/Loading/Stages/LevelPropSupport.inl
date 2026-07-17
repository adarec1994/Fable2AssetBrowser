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

struct StreamingModelCandidate {
    std::string hint_path;
    std::string resolved_path;
    std::string key;
    std::string path_key;
    std::string hint_lower;
    std::string resolved_lower;
    std::string display_name;
    const FlatAssetEntry* entry = nullptr;
    bool from_gmd = false;
    std::string gmd_bnk_path;
    int gmd_file_index = -1;
};

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Mat3f {
    float m[9] = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f,
    };
};

struct Xform3f {
    Mat3f r;
    Vec3f t;
};

struct GmdLayoutChild {
    std::string raw_path;
    std::string asset_key;
    std::string resolved_path;
    std::string resolved_key;
    Xform3f local;
    size_t offset = 0;
};

void mat3_mul(const float a[9], const float b[9], float out[9])
{
    float r[9] = {};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            r[row * 3 + col] =
                a[row * 3 + 0] * b[0 * 3 + col] +
                a[row * 3 + 1] * b[1 * 3 + col] +
                a[row * 3 + 2] * b[2 * 3 + col];
        }
    }
    for (int i = 0; i < 9; ++i) {
        out[i] = r[i];
    }
}

Vec3f vec3_add(const Vec3f& a, const Vec3f& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3f vec3_sub(const Vec3f& a, const Vec3f& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

float vec3_len2(const Vec3f& v)
{
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

Vec3f mat3_apply(const Mat3f& a, const Vec3f& v)
{
    return {
        a.m[0] * v.x + a.m[1] * v.y + a.m[2] * v.z,
        a.m[3] * v.x + a.m[4] * v.y + a.m[5] * v.z,
        a.m[6] * v.x + a.m[7] * v.y + a.m[8] * v.z,
    };
}

Mat3f mat3_mul3(const Mat3f& a, const Mat3f& b)
{
    Mat3f out;
    mat3_mul(a.m, b.m, out.m);
    return out;
}

bool mat3_inverse(const Mat3f& a, Mat3f& out)
{
    const float* m = a.m;
    const float c00 =  m[4] * m[8] - m[5] * m[7];
    const float c01 = -m[3] * m[8] + m[5] * m[6];
    const float c02 =  m[3] * m[7] - m[4] * m[6];
    const float c10 = -m[1] * m[8] + m[2] * m[7];
    const float c11 =  m[0] * m[8] - m[2] * m[6];
    const float c12 = -m[0] * m[7] + m[1] * m[6];
    const float c20 =  m[1] * m[5] - m[2] * m[4];
    const float c21 = -m[0] * m[5] + m[2] * m[3];
    const float c22 =  m[0] * m[4] - m[1] * m[3];
    const float det = m[0] * c00 + m[1] * c01 + m[2] * c02;
    if (!std::isfinite(det) || std::fabs(det) < 1e-8f) {
        return false;
    }
    const float inv_det = 1.0f / det;
    out.m[0] = c00 * inv_det;
    out.m[1] = c10 * inv_det;
    out.m[2] = c20 * inv_det;
    out.m[3] = c01 * inv_det;
    out.m[4] = c11 * inv_det;
    out.m[5] = c21 * inv_det;
    out.m[6] = c02 * inv_det;
    out.m[7] = c12 * inv_det;
    out.m[8] = c22 * inv_det;
    return true;
}

Xform3f xform_compose(const Xform3f& a, const Xform3f& b)
{
    Xform3f out;
    out.r = mat3_mul3(a.r, b.r);
    out.t = vec3_add(a.t, mat3_apply(a.r, b.t));
    return out;
}

bool xform_inverse(const Xform3f& a, Xform3f& out)
{
    if (!mat3_inverse(a.r, out.r)) return false;
    out.t = mat3_apply(out.r, {-a.t.x, -a.t.y, -a.t.z});
    return true;
}

Vec3f xform_apply_point(const Xform3f& a, const Vec3f& p)
{
    return vec3_add(a.t, mat3_apply(a.r, p));
}

Mat3f mat3_from_quat(float qx, float qy, float qz, float qw)
{
    const float len =
        std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    Mat3f out;
    if (!std::isfinite(len) || len < 1e-6f) {
        return out;
    }
    qx /= len;
    qy /= len;
    qz /= len;
    qw /= len;

    const float xx = qx * qx;
    const float yy = qy * qy;
    const float zz = qz * qz;
    const float xy = qx * qy;
    const float xz = qx * qz;
    const float yz = qy * qz;
    const float wx = qw * qx;
    const float wy = qw * qy;
    const float wz = qw * qz;

    out.m[0] = 1.0f - 2.0f * (yy + zz);
    out.m[1] = 2.0f * (xy - wz);
    out.m[2] = 2.0f * (xz + wy);
    out.m[3] = 2.0f * (xy + wz);
    out.m[4] = 1.0f - 2.0f * (xx + zz);
    out.m[5] = 2.0f * (yz - wx);
    out.m[6] = 2.0f * (xz - wy);
    out.m[7] = 2.0f * (yz + wx);
    out.m[8] = 1.0f - 2.0f * (xx + yy);
    return out;
}

Vec3f game_vec_to_xform_axes(float x, float y, float z)
{
    return {x, z, y};
}

Mat3f game_mat_to_xform_axes(const Mat3f& game)
{
    Mat3f out;
    const int axis_map[3] = {0, 2, 1};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            out.m[row * 3 + col] =
                game.m[axis_map[row] * 3 + axis_map[col]];
        }
    }
    return out;
}

Xform3f prop_instance_xform(const Level::PropInstance& inst)
{
    Xform3f out;
    out.t = game_vec_to_xform_axes(
        inst.values[0], inst.values[1], inst.values[2]);
    if (inst.has_full_transform) {
        float scale = inst.values[12];
        if (!std::isfinite(scale) || scale == 0.0f) scale = 1.0f;
        for (int i = 0; i < 9; ++i) {
            out.r.m[i] = inst.values[3 + i] * scale;
        }
        return out;
    }

    const float s  = inst.values[6];
    const float c  = inst.values[7];
    const float sx = inst.values[9]  == 0.0f ? 1.0f : inst.values[9];
    const float sy = inst.values[10] == 0.0f ? sx   : inst.values[10];
    const float sz = inst.values[11] == 0.0f ? sx   : inst.values[11];
    out.r.m[0] = c * sx;
    out.r.m[1] = 0.0f;
    out.r.m[2] = s * sy;
    out.r.m[3] = 0.0f;
    out.r.m[4] = sz;
    out.r.m[5] = 0.0f;
    out.r.m[6] = -s * sx;
    out.r.m[7] = 0.0f;
    out.r.m[8] = c * sy;
    return out;
}

Level::PropInstance prop_instance_from_xform(const Xform3f& xf,
                                             uint32_t hash = 0)
{
    Level::PropInstance pi;
    pi.hash = hash;
    pi.values[0] = xf.t.x;
    pi.values[1] = xf.t.z;
    pi.values[2] = xf.t.y;
    for (int i = 0; i < 9; ++i) {
        pi.values[3 + i] = xf.r.m[i];
    }
    pi.values[12] = 1.0f;
    pi.has_full_transform = true;
    return pi;
}

bool is_gdb_pi_pair_yaw_rotation(float ry, float rz)
{
    constexpr float kPi = 3.14159265358979323846f;
    return std::isfinite(ry) && std::isfinite(rz) &&
           std::fabs(std::fabs(ry) - kPi) < 1e-4f &&
           std::fabs(std::fabs(rz) - kPi) < 1e-4f;
}

void fill_gdb_rotation_matrix(Level::PropInstance& pi,
                              float rx,
                              float ry,
                              float rz,
                              float scale)
{
    if (!std::isfinite(rx)) rx = 0.0f;
    if (!std::isfinite(ry)) ry = 0.0f;
    if (!std::isfinite(rz)) rz = 0.0f;
    if (!std::isfinite(scale) || scale <= 0.01f || scale >= 100.0f) {
        scale = 1.0f;
    }

    const float sx = std::sin(rx);
    const float cx = std::cos(rx);
    const float sy = std::sin(ry);
    const float cy = std::cos(ry);
    const float sz = std::sin(rz);
    const float cz = std::cos(rz);

    float game[9] = {};
    game[0] = cy * cx;
    game[1] = sx;
    game[2] = -sy * cx;
    game[3] = sy * sz - cy * cz * sx;
    game[4] = cz * cx;
    game[5] = sy * cz * sx + cy * sz;
    game[6] = cy * sz * sx + sy * cz;
    game[7] = -sz * cx;
    game[8] = cy * cz - sy * sz * sx;

    const int axis_map[3] = {0, 2, 1};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            pi.values[3 + row * 3 + col] =
                game[axis_map[row] * 3 + axis_map[col]];
        }
    }
    pi.values[12] = scale;
    pi.has_full_transform = true;
}

void fx_game_rotation_matrix(float rx, float ry, float rz, float out[9])
{
    if (!std::isfinite(rx)) rx = 0.0f;
    if (!std::isfinite(ry)) ry = 0.0f;
    if (!std::isfinite(rz)) rz = 0.0f;
    const float sx = std::sin(rx), cx = std::cos(rx);
    const float sy = std::sin(ry), cy = std::cos(ry);
    const float sz = std::sin(rz), cz = std::cos(rz);
    out[0] = cy * cx;
    out[1] = sx;
    out[2] = -sy * cx;
    out[3] = sy * sz - cy * cz * sx;
    out[4] = cz * cx;
    out[5] = sy * cz * sx + cy * sz;
    out[6] = cy * sz * sx + sy * cz;
    out[7] = -sz * cx;
    out[8] = cy * cz - sy * sz * sx;
}

const StreamingModelCandidate*
choose_streaming_model_for_gdb(const std::string& entity_name,
                               const std::vector<StreamingModelCandidate>& candidates,
                               int* out_score = nullptr,
                               uint32_t parent_hash = 0);
std::vector<StreamingModelCandidate>
collect_streaming_model_candidates(const std::vector<std::string>& streaming_bnks);

std::string lower_slash(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

void bridge_debug_write(const std::string&, bool = false) {}

bool is_bridge_debug_path(const std::string& path)
{
    return lower_slash(path).find("bridge") != std::string::npos;
}

const char* bridge_block_origin(const Level::PropBlock& block)
{
    if (block.type == 2 || block.type == 21) return "engine_level";
    if (block.type == 0xB1) return "gdb_derived";
    if (block.type == 0xB2) return "heuristic_derived";
    if (block.type == 0xB3) return "editor_addition";
    return "other";
}

void bridge_debug_dump_blocks(
    const char* stage, const std::vector<Level::PropBlock>& blocks)
{
    std::ostringstream out;
    std::size_t bridge_blocks = 0;
    std::size_t bridge_instances = 0;
    out << "\n=== " << stage << " ===\n";
    for (const Level::PropBlock& block : blocks) {
        if (!is_bridge_debug_path(block.model_path) &&
            !is_bridge_debug_path(block.lod_model_path) &&
            !is_bridge_debug_path(block.shadow_model_path) &&
            !is_bridge_debug_path(block.extra_model_path)) {
            continue;
        }
        ++bridge_blocks;
        bridge_instances += block.instances.size();
        out << "BLOCK origin=" << bridge_block_origin(block)
            << " type=0x" << std::hex << block.type << std::dec
            << " block_offset=" << block.offset
            << " instances=" << block.instances.size()
            << "\n  model=" << block.model_path;
        if (!block.lod_model_path.empty()) {
            out << "\n  lod=" << block.lod_model_path;
        }
        if (!block.shadow_model_path.empty()) {
            out << "\n  shadow=" << block.shadow_model_path;
        }
        if (!block.extra_model_path.empty()) {
            out << "\n  extra=" << block.extra_model_path;
        }
        out << '\n';
        for (std::size_t i = 0; i < block.instances.size(); ++i) {
            const Level::PropInstance& instance = block.instances[i];
            out << "  [" << i << "] pos=("
                << instance.values[0] << ", "
                << instance.values[1] << ", "
                << instance.values[2] << ") scale=("
                << instance.values[9] << ", "
                << instance.values[10] << ", "
                << instance.values[11] << ") hash=0x"
                << std::hex << instance.hash
                << " gdb_entity=0x" << instance.gdb_entity_hash
                << std::dec << " lev_kind="
                << static_cast<unsigned>(instance.lev_rec_kind)
                << " record_offset=" << instance.record_file_offset
                << " pos_offset=" << instance.pos_file_offset << '\n';
        }
    }
    out << "SUMMARY bridge_blocks=" << bridge_blocks
        << " bridge_instances=" << bridge_instances << '\n';
    bridge_debug_write(out.str());
}

uint32_t fnv1_model_path_hash(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    std::replace(s.begin(), s.end(), '/', '\\');

    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : s) {
        h *= 0x01000193u;
        h ^= uint32_t(c);
    }
    return h;
}

std::string strip_model_suffixes(std::string s)
{
    auto strip = [](std::string& v, const char* suffix) {
        const size_t n = std::strlen(suffix);
        if (v.size() >= n && v.compare(v.size() - n, n, suffix) == 0) {
            v.resize(v.size() - n);
        }
    };
    strip(s, ".gmd");
    strip(s, ".mdl");
    return s;
}

std::string compact_match_key(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (std::isalnum(c)) {
            out.push_back(char(std::tolower(c)));
        }
    }
    return out;
}

std::string model_name_from_path(const std::string& path)
{
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    p = strip_model_suffixes(p);
    const size_t slash = p.find_last_of('/');
    return (slash == std::string::npos) ? p : p.substr(slash + 1);
}

bool read_be_f32_at(const std::vector<uint8_t>& bytes,
                    size_t off,
                    float& out)
{
    if (off + 4 > bytes.size()) return false;
    const uint32_t u =
        (uint32_t(bytes[off + 0]) << 24) |
        (uint32_t(bytes[off + 1]) << 16) |
        (uint32_t(bytes[off + 2]) << 8) |
         uint32_t(bytes[off + 3]);
    std::memcpy(&out, &u, sizeof(out));
    return std::isfinite(out);
}

std::string gmd_asset_key_from_raw_path(const std::string& raw_path)
{
    std::string p = lower_slash(raw_path);
    const std::string marker = "layout.instance.";
    if (const size_t pos = p.find(marker); pos != std::string::npos) {
        p.erase(0, pos + marker.size());
    }
    const size_t art = p.find("art/");
    if (art != std::string::npos) {
        p = p.substr(art);
    }
    const size_t slash = p.find_last_of('/');
    std::string name = (slash == std::string::npos) ? p : p.substr(slash + 1);
    auto strip_suffix = [](std::string& s, const char* suffix) {
        const size_t n = std::strlen(suffix);
        if (s.size() >= n && s.compare(s.size() - n, n, suffix) == 0) {
            s.resize(s.size() - n);
        }
    };
    strip_suffix(name, ".emdl");
    strip_suffix(name, ".mdl");
    strip_suffix(name, "_asset");
    strip_suffix(name, "asset");
    return compact_match_key(name);
}

bool parse_gmd_payload_transform(const std::vector<uint8_t>& bytes,
                                 size_t payload_start,
                                 size_t payload_end,
                                 Xform3f& out)
{
    struct Candidate {
        float score = std::numeric_limits<float>::infinity();
        float qx = 0.0f;
        float qy = 0.0f;
        float qz = 0.0f;
        float qw = 1.0f;
        float tx = 0.0f;
        float ty = 0.0f;
        float tz = 0.0f;
    };
    Candidate best;

    for (size_t align = 0; align < 4; ++align) {
        std::vector<float> floats;
        for (size_t off = payload_start + align;
             off + 4 <= payload_end;
             off += 4)
        {
            float f = 0.0f;
            if (!read_be_f32_at(bytes, off, f)) {
                floats.push_back(std::numeric_limits<float>::quiet_NaN());
            } else {
                floats.push_back(f);
            }
        }
        if (floats.size() < 7) continue;

        for (size_t i = 0; i + 7 <= floats.size(); ++i) {
            const float qx = floats[i + 0];
            const float qy = floats[i + 1];
            const float qz = floats[i + 2];
            const float qw = floats[i + 3];
            const float tx = floats[i + 4];
            const float ty = floats[i + 5];
            const float tz = floats[i + 6];
            const float vals[] = {qx, qy, qz, qw, tx, ty, tz};
            bool finite = true;
            for (float v : vals) {
                if (!std::isfinite(v)) {
                    finite = false;
                    break;
                }
            }
            if (!finite) continue;
            if (std::fabs(tx) > 512.0f || std::fabs(ty) > 512.0f ||
                std::fabs(tz) > 512.0f)
            {
                continue;
            }
            const float qmag =
                std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
            if (!std::isfinite(qmag) || qmag < 0.6f || qmag > 1.4f) {
                continue;
            }
            const float pos_mag =
                std::sqrt(tx * tx + ty * ty + tz * tz);
            const float score =
                std::fabs(qmag - 1.0f) * 100.0f +
                (pos_mag < 1e-4f ? 20.0f : 0.0f) +
                float(i) * 0.01f + float(align) * 0.001f;
            if (score < best.score) {
                best = {score, qx, qy, qz, qw, tx, ty, tz};
            }
        }
    }

    if (!std::isfinite(best.score)) {
        return false;
    }
    out.r = game_mat_to_xform_axes(
        mat3_from_quat(best.qx, best.qy, best.qz, best.qw));
    out.t = game_vec_to_xform_axes(best.tx, best.ty, best.tz);
    return true;
}

std::vector<GmdLayoutChild>
parse_gmd_layout_children(const std::vector<uint8_t>& bytes)
{
    std::vector<GmdLayoutChild> out;
    static constexpr const char* kMarkers[] = {
        "Prop.Layout.Instance.",
        "Light.Layout.Instance.",
        "Environment.Layout.Instance.",
    };
    size_t pos = 0;
    while (pos < bytes.size()) {
        auto best_it = bytes.end();
        const char* best_marker = nullptr;
        for (const char* marker : kMarkers) {
            const size_t marker_len = std::strlen(marker);
            if (pos + marker_len >= bytes.size()) continue;
            const auto it = std::search(
                bytes.begin() +
                    static_cast<std::vector<uint8_t>::difference_type>(pos),
                bytes.end(),
                marker,
                marker + marker_len);
            if (it != bytes.end() &&
                (best_it == bytes.end() || it < best_it))
            {
                best_it = it;
                best_marker = marker;
            }
        }
        const auto it = best_it;
        if (it == bytes.end()) break;
        (void)best_marker;
        const size_t start =
            static_cast<size_t>(std::distance(bytes.begin(), it));
        size_t str_end = start;
        while (str_end < bytes.size() && bytes[str_end] != 0) {
            ++str_end;
        }
        if (str_end >= bytes.size()) break;

        std::string raw(reinterpret_cast<const char*>(&bytes[start]),
                        str_end - start);
        size_t payload_start = str_end + 1;
        size_t payload_end = std::min(bytes.size(), payload_start + 160);
        for (size_t s = payload_start; s + 4 <= payload_end; ++s) {
            if (bytes[s + 0] == 0xff && bytes[s + 1] == 0xff &&
                bytes[s + 2] == 0xff && bytes[s + 3] == 0xff)
            {
                payload_end = s;
                break;
            }
        }

        GmdLayoutChild child;
        child.raw_path = raw;
        child.asset_key = gmd_asset_key_from_raw_path(raw);
        child.offset = start;
        if (!child.asset_key.empty() &&
            parse_gmd_payload_transform(
                bytes, payload_start, payload_end, child.local))
        {
            out.push_back(std::move(child));
        }
        pos = str_end + 1;
    }
    return out;
}

bool is_gdb_authored_level_shell_model(
    const std::string& model_path,
    const std::unordered_set<std::string>& authored_level_model_paths)
{
    const std::string p = lower_slash(model_path);
    if (authored_level_model_paths.find(p) ==
        authored_level_model_paths.end())
    {
        return false;
    }

    return p.find("/buildings/") != std::string::npos ||
           p.find("/structures/") != std::string::npos;
}

bool is_gdb_shell_audit_model(const std::string& model_path)
{
    const std::string p = lower_slash(model_path);
    return p.find("/buildings/") != std::string::npos ||
           p.find("/structures/") != std::string::npos;
}

bool is_gdb_static_prop_reject_model(const std::string& model_path)
{
    const std::string p = lower_slash(model_path);
    return p.find("art/characters/") == 0 ||
           p.find("/art/characters/") != std::string::npos ||
           p.find("/characters/heros/") != std::string::npos;
}

bool is_gdb_unique_entity_shell_model(const std::string& model_path)
{
    const std::string p = lower_slash(model_path);
    if (p.find("/buildings/") == std::string::npos &&
        p.find("/structures/") == std::string::npos)
    {
        return false;
    }

    return p.find("/exterior.mdl") != std::string::npos ||
           p.find("/interior.mdl") != std::string::npos ||
           p.find("bs_market_gatehouse") != std::string::npos ||
           p.find("bs_market_clocktower") != std::string::npos ||
           p.find("bs_market_platform") != std::string::npos ||
           p.find("bs_market_largeshop") != std::string::npos ||
           p.find("bs_market_smallshop") != std::string::npos ||
           p.find("bs_market_generalshop") != std::string::npos ||
           p.find("bs_market_tavern") != std::string::npos ||
           p.find("bs_market_tarotstall") != std::string::npos ||
           p.find("bs_townhouse") != std::string::npos;
}

bool is_implausible_container_shell_model(const std::string& model_path)
{
    const std::string p = lower_slash(model_path);
    return p.find("bridge") != std::string::npos ||
           p.find("facade") != std::string::npos ||
           p.find("clocktower") != std::string::npos ||
           p.find("/buildings/") != std::string::npos ||
           p.find("/exterior.mdl") != std::string::npos ||
           p.find("/interior.mdl") != std::string::npos;
}

bool compact_key_is_or_numbered(const std::string& key, const char* base)
{
    const size_t n = std::strlen(base);
    if (key == base) return true;
    if (key.size() <= n || key.compare(0, n, base) != 0) {
        return false;
    }
    for (size_t i = n; i < key.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(key[i]))) {
            return false;
        }
    }
    return true;
}

bool compact_key_is_or_variant(const std::string& key, const char* base)
{
    if (compact_key_is_or_numbered(key, base)) return true;
    const size_t n = std::strlen(base);
    if (key.size() <= n + 1 || key.compare(0, n, base) != 0 ||
        key[n] != 'v')
    {
        return false;
    }
    for (size_t i = n + 1; i < key.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(key[i]))) {
            return false;
        }
    }
    return true;
}

bool is_bad_market_helper_substitution(const std::string& entity_key,
                                       const std::string& raw_key,
                                       const std::string& model_path)
{
    const std::string model_key = compact_match_key(model_path);

    const bool sign_entity =
        entity_key.find("sign") != std::string::npos ||
        raw_key.find("sign") != std::string::npos;
    const bool door_entity =
        entity_key.find("door") != std::string::npos ||
        raw_key.find("door") != std::string::npos;

    const bool general_store_building =
        compact_key_is_or_numbered(entity_key, "generalstore") ||
        compact_key_is_or_numbered(raw_key, "objectbuildinggeneralstore") ||
        compact_key_is_or_numbered(raw_key, "newobjectbuildinggeneralstore");
    if (model_key.find("bsmarketgeneralshop") != std::string::npos &&
        (model_key.find("exterior") != std::string::npos ||
         model_key.find("interior") != std::string::npos) &&
        !general_store_building)
    {
        return true;
    }
    if (general_store_building && !sign_entity &&
        model_key.find("signgeneralstore") != std::string::npos)
    {
        return true;
    }

    const bool tavern_building =
        entity_key.find("bsmarkettavern") != std::string::npos ||
        entity_key.find("markettavern") != std::string::npos ||
        raw_key.find("objectbuildingbsmarkettavern") != std::string::npos ||
        raw_key.find("newobjectbuildingbsmarkettavern") != std::string::npos;
    if (model_key.find("bsmarkettavern") != std::string::npos &&
        !tavern_building)
    {
        return true;
    }
    if (tavern_building && !sign_entity &&
        (model_key.find("botavernsign") != std::string::npos ||
         model_key.find("tavernsign") != std::string::npos))
    {
        return true;
    }

    const bool tarot_stall_building =
        entity_key.find("tarotstall") != std::string::npos ||
        raw_key.find("tarotstall") != std::string::npos;
    if (tarot_stall_building && !door_entity &&
        model_key.find("tarotstalldoors") != std::string::npos)
    {
        return true;
    }

    const bool market_stall_building =
        compact_key_is_or_variant(entity_key, "bsopenstall") ||
        compact_key_is_or_variant(entity_key, "bsmarketopenstall") ||
        compact_key_is_or_variant(entity_key, "bsmarketstall") ||
        raw_key.find("objectbuildingbsopenstall") != std::string::npos ||
        raw_key.find("objectbuildingbsmarketstall") != std::string::npos ||
        raw_key.find("newobjectbuildingbsmarketstall") != std::string::npos;
    const bool bs_market_stall_shell =
        (model_key.find("bsmarketmarketstall") != std::string::npos ||
         model_key.find("bsmarketopenstall") != std::string::npos ||
         model_key.find("bsopenstall") != std::string::npos) &&
        model_key.find("esashopmarketstall") == std::string::npos;
    if (bs_market_stall_shell && !market_stall_building) {
        return true;
    }
    if ((general_store_building || tavern_building ||
         market_stall_building || tarot_stall_building) &&
        (model_key.find("bstownhouse") != std::string::npos ||
         model_key.find("townhouse") != std::string::npos))
    {
        return true;
    }

    return false;
}

bool is_unindexed_shell_fallback_entity(const std::string& entity_key,
                                        const std::string& raw_key)
{
    const std::string text = entity_key + " " + raw_key;
    auto has = [&](const char* needle) {
        return text.find(needle) != std::string::npos;
    };

    if (has("canopy") || has("counter") || has("stairsfloor") ||
        has("door") || has("sign"))
    {
        return false;
    }

    return has("objectbuilding") ||
           has("newobjectbuilding") ||
           has("bsmarkettavern") ||
           has("generalstore") ||
           has("generalshop") ||
           has("largeshop") ||
           has("smallshop") ||
           has("townhouse") ||
           has("slumstreethouse") ||
           has("gatehouse") ||
           has("clocktower");
}

std::string hex_u32(uint32_t v)
{
    std::ostringstream os;
    os << "0x" << std::uppercase << std::hex
       << std::setw(8) << std::setfill('0') << v;
    return os.str();
}

std::string gdb_shell_sample_text(
    const Gdb::Placement& p,
    const std::string& model_path)
{
    std::ostringstream os;
    os << (p.entity_name.empty() ? "<unnamed>" : p.entity_name)
       << " parent=" << hex_u32(p.parent_hash);
    if (p.model_path_hash != 0) {
        os << " modelHash=" << hex_u32(p.model_path_hash);
    }
    os << " pos=(" << p.x << ", " << p.y << ", " << p.z << ")"
       << " model=" << model_path;
    return os.str();
}

std::string gdb_instance_key(
    const Gdb::Placement& p,
    const std::string& model_path)
{
    auto q = [](float v) -> long long {
        if (!std::isfinite(v)) return 0;
        return static_cast<long long>(std::llround(v * 100.0f));
    };
    std::ostringstream os;
    os << lower_slash(model_path) << '|'
       << std::hex << p.hash_a << '|' << p.parent_hash << std::dec << '|'
       << p.entity_name << '|'
       << q(p.x) << ',' << q(p.y) << ',' << q(p.z);
    return os.str();
}

std::string prop_instance_transform_key(
    const Level::PropInstance& inst,
    const std::string& model_path)
{
    auto q = [](float v) -> long long {
        if (!std::isfinite(v)) return 0;
        return static_cast<long long>(std::llround(v * 100.0f));
    };
    std::ostringstream os;
    os << lower_slash(model_path);
    for (int i = 0; i < 12; ++i) {
        os << '|' << q(inst.values[i]);
    }
    return os.str();
}

std::string companion_interior_path(const std::string& model_path)
{
    std::string p = lower_slash(model_path);
    const std::string suffix = "/exterior.mdl";
    if (p.size() < suffix.size() ||
        p.compare(p.size() - suffix.size(), suffix.size(), suffix) != 0)
    {
        return {};
    }
    p.replace(p.size() - suffix.size(), suffix.size(), "/interior.mdl");
    return p;
}

std::string companion_exterior_path(const std::string& model_path)
{
    std::string p = lower_slash(model_path);
    const std::string suffix = "/interior.mdl";
    if (p.size() < suffix.size() ||
        p.compare(p.size() - suffix.size(), suffix.size(), suffix) != 0)
    {
        return {};
    }
    p.replace(p.size() - suffix.size(), suffix.size(), "/exterior.mdl");
    return p;
}

std::string house_facade_companion_exterior_path(const std::string& model_path)
{
    std::string p = lower_slash(model_path);
    struct Map {
        const char* facade;
        const char* shell;
    };
    static const Map maps[] = {
        { "bs_townhouse_basic_facade_mid", "bs_townhouse_basic" },
        { "bs_townhouse_basic_facade",     "bs_townhouse_basic" },
        { "bs_townhouse_basic_facade_snow_v2", "bs_townhouse_basic_snow_v2" },
        { "bs_townhouse_v1_facade_mid",    "bs_townhouse_v1" },
        { "bs_townhouse_v1_facade",        "bs_townhouse_v1" },
        { "bs_townhouse_v1_facade_snow",   "bs_townhouse_v1_snow" },
        { "bs_townhouse_v2_facade_mid",    "bs_townhouse_v2" },
        { "bs_townhouse_v2_facade",        "bs_townhouse_v2" },
        { "bs_townhouse_v2_facade_snow",   "bs_townhouse_v2_snow" },
        { "bs_townhouse_v3_facade_snow",   "bs_townhouse_v3_snow" },
        { "bs_townhouse_v1_snow",           "bs_townhouse_v1_snow" },
        { "bs_townhouse_v2_snow",           "bs_townhouse_v2_snow" },
        { "bs_townhouse_v3_snow",           "bs_townhouse_v3_snow" },
    };
    for (const Map& map : maps) {
        const std::string needle =
            std::string("/buildings/dotxsi/") + map.facade + "/" +
            map.facade + ".mdl";
        const size_t pos = p.find(needle);
        if (pos == std::string::npos) continue;

        const std::string exterior =
            std::string("/buildings/dotxsi/") + map.shell + "/" +
            map.shell + "/exterior.mdl";
        p.replace(pos, needle.size(), exterior);
        return p;
    }
    return {};
}

std::string shop_facade_companion_exterior_path(const std::string& model_path)
{
    std::string p = lower_slash(model_path);
    struct Map {
        const char* facade;
        const char* shell;
    };
    static const Map maps[] = {
        {"bs_market_largeshop_facade_mid", "bs_market_largeshop"},
        {"bs_market_largeshop_facade",     "bs_market_largeshop"},
        {"bs_market_smallshop_facade_mid", "bs_market_smallshop"},
        {"bs_market_smallshop_facade",     "bs_market_smallshop"},
        {"bs_market_generalshop_facade_mid", "bs_market_generalshop"},
        {"bs_market_generalshop_facade",     "bs_market_generalshop"},
        {"bs_market_tavern_facade_mid", "bs_market_tavern"},
        {"bs_market_tavern_facade",     "bs_market_tavern"},
    };
    for (const Map& map : maps) {
        const std::string needle =
            std::string("/buildings/dotxsi/") + map.facade + "/" +
            map.facade + ".mdl";
        const size_t pos = p.find(needle);
        if (pos == std::string::npos) continue;

        const std::string exterior =
            std::string("/buildings/dotxsi/") + map.shell + "/" +
            map.shell + "/exterior.mdl";
        p.replace(pos, needle.size(), exterior);
        return p;
    }
    return {};
}

std::string gdb_entity_key(std::string s)
{
    static const char* prefixes[] = {
        "NewObjectBuilding", "ObjectBuilding",
        "NewObjectFurniture", "ObjectFurniture",
        "NewObjectStatic", "ObjectStatic",
        "NewObject", "Object",
        "New"
    };
    for (const char* pfx : prefixes) {
        const size_t n = std::strlen(pfx);
        if (s.size() > n && s.compare(0, n, pfx) == 0) {
            s = s.substr(n);
            break;
        }
    }
    return compact_match_key(s);
}

bool is_gdb_landmark_name(const std::string& entity_name)
{
    const std::string key = gdb_entity_key(entity_name);
    if (key.empty()) return false;
    const char* needles[] = {
        "bridge",
        "clocktower",
        "grandfatherclock",
        "wallclock",
        "dockarch",
        "gatehouse",
        "lockgate",
        "walltower",
        "wallgate",
        "archway",
        "guardpost",
        "marketstairs",
        "scaffoldingstairs",
        "castlearch",
        "dockswall",
        "oilamp",
        "oillantern",
        "statue",
    };
    for (const char* needle : needles) {
        if (key.find(needle) != std::string::npos) return true;
    }
    return false;
}

bool bytes_contain_be_u32(const std::vector<uint8_t>& bytes, uint32_t value)
{
    const uint8_t a = uint8_t(value >> 24);
    const uint8_t b = uint8_t(value >> 16);
    const uint8_t c = uint8_t(value >> 8);
    const uint8_t d = uint8_t(value);
    for (size_t i = 0; i + 4 <= bytes.size(); ++i) {
        if (bytes[i] == a && bytes[i + 1] == b &&
            bytes[i + 2] == c && bytes[i + 3] == d) {
            return true;
        }
    }
    return false;
}

std::string hex32_for_log(uint32_t value)
{
    std::ostringstream os;
    os << "0x" << std::hex << std::uppercase
       << std::setw(8) << std::setfill('0') << value;
    return os.str();
}

void log_curated_hashlist_miss(const std::string& entity_name,
                               uint32_t parent_hash,
                               const char* target_model_path)
{
    static std::mutex logged_mutex;
    static std::unordered_set<std::string> logged_keys;

    std::string key = hex32_for_log(parent_hash) + "|" +
                      gdb_entity_key(entity_name) + "|" +
                      (target_model_path ? target_model_path : "");
    {
        std::lock_guard<std::mutex> lock(logged_mutex);
        if (!logged_keys.insert(key).second) return;
    }

    OutputLog::warn(
        "GDB hashlist: curated model target missing in streaming candidates; "
        "parent=" + hex32_for_log(parent_hash) +
        " entity='" + entity_name +
        "' target='" + (target_model_path ? target_model_path : "") + "'");
}

std::string resolve_streaming_bnk_path(const std::string& vfs_stream_path)
{
    std::string wanted_leaf =
        std::filesystem::path(vfs_stream_path).filename().string();
    std::transform(wanted_leaf.begin(), wanted_leaf.end(),
                   wanted_leaf.begin(), ::tolower);

    auto leaf_matches = [&](const std::string& mounted_leaf_lower) {
        if (mounted_leaf_lower == wanted_leaf) return true;
        if (mounted_leaf_lower.size() <= wanted_leaf.size() + 1) return false;
        const size_t off = mounted_leaf_lower.size() - wanted_leaf.size();
        if (mounted_leaf_lower.compare(off, wanted_leaf.size(),
                                       wanted_leaf) != 0) return false;
        return mounted_leaf_lower[off - 1] == '_';
    };

    if (auto resolved = find_bnk_by_virtual_path(vfs_stream_path)) {
        return *resolved;
    }
    for (const auto& p : S.bnk_paths) {
        std::string leaf = std::filesystem::path(p).filename().string();
        std::transform(leaf.begin(), leaf.end(), leaf.begin(), ::tolower);
        if (leaf_matches(leaf)) return p;
    }
    for (const auto& p : S.nested_bnk_paths) {
        std::string leaf = std::filesystem::path(p).filename().string();
        std::transform(leaf.begin(), leaf.end(), leaf.begin(), ::tolower);
        if (leaf_matches(leaf)) return p;
    }
    return {};
}

std::vector<StreamingModelCandidate>
collect_streaming_model_candidates(const std::vector<std::string>& streaming_bnks)
{
    std::unordered_map<std::string, const FlatAssetEntry*> mdl_by_path;
    mdl_by_path.reserve(S.all_mdl_files.size());
    std::unordered_map<std::string, std::vector<const FlatAssetEntry*>> mdl_by_key;
    mdl_by_key.reserve(S.all_mdl_files.size());
    for (const auto& e : S.all_mdl_files) {
        mdl_by_path.emplace(lower_slash(e.full_path), &e);
        mdl_by_key[compact_match_key(model_name_from_path(e.full_path))]
            .push_back(&e);
    }
    auto choose_global_model = [&](const std::string& hint_path) {
        const std::string hint_lower = lower_slash(hint_path);
        if (auto exact = mdl_by_path.find(hint_lower); exact != mdl_by_path.end()) {
            return exact->second;
        }
        for (const auto& kv : mdl_by_path) {
            const std::string& model_path = kv.first;
            if (model_path.size() >= hint_lower.size() &&
                model_path.compare(model_path.size() - hint_lower.size(),
                                   hint_lower.size(),
                                   hint_lower) == 0) {
                return kv.second;
            }
        }

        const std::string hint_key =
            compact_match_key(model_name_from_path(hint_path));
        if (hint_key.empty()) return static_cast<const FlatAssetEntry*>(nullptr);

        auto choose_best = [](const std::vector<const FlatAssetEntry*>& hits) {
            const FlatAssetEntry* best = nullptr;
            int best_score = INT_MIN;
            for (const FlatAssetEntry* e : hits) {
                if (!e) continue;
                int score = 0;
                const std::string lower = lower_slash(e->full_path);
                if (lower.find("/globals_models.bnk") == std::string::npos) {
                    score += 500;
                }
                if (e->from_nested) score += 250;
                score -= int(std::min<size_t>(e->full_path.size(), 240));
                if (!best || score > best_score) {
                    best = e;
                    best_score = score;
                }
            }
            return best;
        };

        if (auto it = mdl_by_key.find(hint_key); it != mdl_by_key.end()) {
            return choose_best(it->second);
        }

        std::vector<const FlatAssetEntry*> fuzzy;
        for (const auto& kv : mdl_by_key) {
            const std::string& model_key = kv.first;
            if (model_key.size() < 5) continue;
            const bool related =
                model_key.find(hint_key) != std::string::npos ||
                hint_key.find(model_key) != std::string::npos;
            if (!related) continue;
            fuzzy.insert(fuzzy.end(), kv.second.begin(), kv.second.end());
        }
        return choose_best(fuzzy);
    };

    std::vector<StreamingModelCandidate> out;
    std::unordered_set<std::string> seen;
    for (const auto& vfs_path : streaming_bnks) {
        const std::string mounted = resolve_streaming_bnk_path(vfs_path);
        if (mounted.empty()) continue;
        try {
            const BnkCache::Entry bnk = BnkCache::get(mounted);
            const auto& files = bnk.reader->list_files();
            auto add_candidate = [&](std::string hint,
                                      bool from_gmd,
                                      int gmd_index) {
                std::string norm = lower_slash(hint);
                auto [seen_it, inserted] = seen.insert(norm);
                if (!inserted) {
                    if (from_gmd) {
                        for (auto& existing : out) {
                            if (lower_slash(existing.hint_path) == norm) {
                                existing.from_gmd = true;
                                existing.gmd_bnk_path = mounted;
                                existing.gmd_file_index = gmd_index;
                                break;
                            }
                        }
                    }
                    return;
                }

                StreamingModelCandidate c;
                c.hint_path = std::move(hint);
                c.hint_lower = norm;
                c.display_name = model_name_from_path(c.hint_path);
                c.key = compact_match_key(c.display_name);
                c.from_gmd = from_gmd;
                if (from_gmd) {
                    c.gmd_bnk_path = mounted;
                    c.gmd_file_index = gmd_index;
                }
                c.entry = choose_global_model(c.hint_path);
                if (c.entry) {
                    c.resolved_path = c.entry->full_path;
                }
                c.resolved_lower = lower_slash(c.resolved_path);
                c.path_key =
                    compact_match_key(c.hint_path + " " + c.resolved_path);
                out.push_back(std::move(c));
            };

            for (size_t file_i = 0; file_i < files.size(); ++file_i) {
                const auto& f = files[file_i];
                std::string lower = lower_slash(f.name);
                if (lower.size() >= 8 &&
                    lower.compare(lower.size() - 8, 8, ".mdl.gmd") == 0) {
                    std::string mdl = f.name;
                    mdl.resize(mdl.size() - 4);
                    add_candidate(std::move(mdl), true, int(file_i));
                    continue;
                }
                if (lower.size() >= 4 &&
                    lower.compare(lower.size() - 4, 4, ".hkx") == 0) {
                    std::string mdl = f.name;
                    mdl.resize(mdl.size() - 4);
                    mdl += ".mdl";
                    add_candidate(std::move(mdl), false, -1);
                }
            }
        } catch (...) {
        }
    }
    return out;
}

int streaming_model_score(const std::string& entity_name,
                          const StreamingModelCandidate& c)
{
    const std::string entity_key = gdb_entity_key(entity_name);
    if (entity_key.empty() || c.key.empty()) return INT_MIN;

    auto has = [&](const char* needle) {
        return entity_key.find(needle) != std::string::npos;
    };
    auto cand_has = [&](const char* needle) {
        return c.key.find(needle) != std::string::npos;
    };
    const std::string& path_key = c.path_key;
    auto cand_path_has = [&](const char* needle) {
        return path_key.find(needle) != std::string::npos;
    };
    auto key_is_or_numbered = [&](const char* base) {
        const size_t n = std::strlen(base);
        if (entity_key == base) return true;
        if (entity_key.size() <= n ||
            entity_key.compare(0, n, base) != 0)
        {
            return false;
        }
        for (size_t i = n; i < entity_key.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(entity_key[i]))) {
                return false;
            }
        }
        return true;
    };
    auto key_is_or_variant = [&](const char* base) {
        if (key_is_or_numbered(base)) return true;
        const size_t n = std::strlen(base);
        if (entity_key.size() <= n + 1 ||
            entity_key.compare(0, n, base) != 0 ||
            entity_key[n] != 'v')
        {
            return false;
        }
        for (size_t i = n + 1; i < entity_key.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(entity_key[i]))) {
                return false;
            }
        }
        return true;
    };

    if (key_is_or_numbered("generalstore") &&
        (cand_has("signgeneralstore") || cand_path_has("signgeneralstore")))
    {
        return INT_MIN;
    }

    int score = INT_MIN;

    const bool bare_general_store = key_is_or_numbered("generalstore");
    const bool market_tavern_shell =
        key_is_or_numbered("bsmarkettavern") ||
        key_is_or_numbered("markettavern");
    const bool market_openstall_shell =
        key_is_or_variant("bsopenstall") ||
        key_is_or_variant("bsmarketopenstall");
    const bool market_stall_shell =
        key_is_or_variant("bsmarketstall") ||
        key_is_or_variant("marketstall") ||
        key_is_or_numbered("bstarotstall") ||
        key_is_or_numbered("tarotstall");

    auto path_has_any = [&](std::initializer_list<const char*> needles) {
        for (const char* needle : needles) {
            if (cand_path_has(needle)) return true;
        }
        return false;
    };
    if ((bare_general_store || market_tavern_shell ||
         market_openstall_shell || market_stall_shell) &&
        path_has_any({"bstownhouse", "townhouse"}))
    {
        return INT_MIN;
    }
    auto same_variant_bonus = [&](int base_score) {
        int adjusted = base_score;
        for (const char* v :
             {"v1", "v2", "v3", "v4", "v5", "v6"})
        {
            if (entity_key.find(v) != std::string::npos &&
                path_key.find(v) != std::string::npos)
            {
                adjusted += 500;
            }
        }
        return adjusted;
    };

    if (bare_general_store) {
        return INT_MIN;
    }
    if (market_tavern_shell) {
        return INT_MIN;
    }
    if (market_openstall_shell) {
        if (cand_path_has("esashopmarketstall") ||
            cand_path_has("signstall"))
        {
            return INT_MIN;
        }
        if (cand_path_has("bsopenstall") ||
            cand_path_has("bsmarketopenstall") ||
            cand_path_has("openstall"))
        {
            score = std::max(score, same_variant_bonus(18500));
        }
    }
    if (market_stall_shell) {
        if (cand_path_has("esashopmarketstall") ||
            cand_path_has("signstall"))
        {
            return INT_MIN;
        }
        if (entity_key.find("tarotstall") != std::string::npos &&
            cand_path_has("tarotstall"))
        {
            score = std::max(score, same_variant_bonus(19000));
        } else if ((cand_path_has("bsmarketstall") ||
                    cand_path_has("marketstall")) &&
                   !cand_path_has("esashopmarketstall"))
        {
            score = std::max(score, same_variant_bonus(18500));
        }
    }

    if (entity_key == c.key) {
        score = 12000;
    } else if (c.key.find(entity_key) != std::string::npos) {
        score = 9000 + int(entity_key.size());
    } else if (entity_key.find(c.key) != std::string::npos) {
        score = 7000 + int(c.key.size());
    }

    struct Alias { const char* entity; const char* model; int score; };
    static const Alias aliases[] = {
        { "smallwallpost",         "stonewallmediumpostspiked",     15000 },
        { "wallpost",              "stonewallmediumpostspiked",     14500 },
        { "smallwallstraight",     "stonewallmediumstraightspiked", 15000 },
        { "smallwallcurved",       "stonewallmediumcurvedspiked",   15000 },
        { "smallwallcorner",       "stonewallmediumcurvedspiked",   14500 },
        { "smallwallbroken",       "stonewallmediumbrokenspiked",   15000 },
        { "shelflong",             "esashelflong",                  15000 },
        { "woodenbucket",          "esabucketwooden",               15000 },
        { "lightsceiling",         "bslightceiling",                15000 },
        { "lightfixingceiling",    "bslightceiling",                15000 },
        { "candleholder",          "bscandleholder",                14000 },
        { "grainsack",             "esasackgrain",                  14500 },
        { "shippingcrate",         "esashippingcrate",              14500 },
        { "weaponrackwallmulti",   "esashopweaponswallrackmulti",   15000 },
        { "weaponrackwallsingle",  "esashopweaponswallracksingle",  15000 },
        { "weaponrack",            "esashopweaponrack",             13500 },
        { "booksgroup",            "esabooksblock",                 13000 },
        { "pubtable",              "esatabletavern",                14500 },
        { "largesquareultradecorative","esaftableultradecorative",  13800 },
        { "largesquareupgradeable","esaftabledecorative",           12500 },
        { "standardultradecorative","esaftableultradecorative",     13600 },
        { "standardupgradeable",   "esaftabledecorative",           12300 },
        { "bookcaseultradecorative","esafbookcaseultradecorative",  15000 },
        { "bookcaseworn",          "esafbookcaseworn",              14500 },
        { "dresserupgradeable",    "esafdresserultradecorative",    13000 },
        { "kitchensinkupgradeable", "esakitchensink",               12000 },
        { "buildingsalesign",      "buildingsalesign",              14000 },
        { "bsmarketbridge",        "bsmarketbridge",                16000 },
        { "marketbridge",          "bsmarketbridge",                15800 },
        { "bridge",                "bsmarketbridge",                12000 },
        { "bsmarketclocktower",    "bsmarketclocktower",            16000 },
        { "marketclocktower",      "bsmarketclocktower",            15800 },
        { "clocktower",            "bsmarketclocktower",            14500 },
        { "grandfatherclock",      "bsgrandfatherclock",            15500 },
        { "wallclock",             "bswallclock",                   15500 },
        { "bsmarketdockarch",      "bsmarketdocksarch",             15000 },
        { "dockarch",              "bsmarketdocksarch",             14500 },
        { "bsmarketarchway",       "bsmarketarchway",               15000 },
        { "archway",               "bsmarketarchway",               13000 },
        { "bsmarketgatehouse",     "bsmarketgatehouse",             15000 },
        { "bsgatehouse",           "bsmarketgatehouse",             14500 },
        { "bsmarketlockgate",      "bsmarketlockgates",             15000 },
        { "lockgate",              "bsmarketlockgates",             14000 },
        { "bsmarketwalltower",     "bsmarketwalltower",             15000 },
        { "walltower",             "bsmarketwalltower",             13500 },
        { "bsmarketwallgate",      "bsmarketwallgate",              15000 },
        { "closedgate",            "bsmarketwallgate",              13000 },
        { "guardpost",             "bsmarketguardpost",             14500 },
        { "marketstairs",          "bsmarketstairs",                14500 },
        { "generalstorestairsfloor","bsmarketgeneralshopstairsfloor",14500 },
        { "generalshopstairsfloor", "bsmarketgeneralshopstairsfloor",14500 },
        { "bsopenstall",           "openstall",                     14500 },
        { "openstall",             "openstall",                     14000 },
        { "bsmarketstall",         "bsmarketstall",                 14500 },
        { "marketstall",           "marketstall",                   14000 },
        { "tarotstall",            "tarotstall",                    15000 },
        { "scaffoldingstairs",     "bsmarketscaffoldingstairs",     14500 },
        { "scaffoldstairs",        "bsmarketscaffoldingstairs",     14500 },
        { "scaffoldstraight",      "bsmarketscaffoldingstraight",   14000 },
        { "marketwalljoiner",      "bsmarketwallbuffer",            13500 },
        { "walljoiner",            "bsmarketwallbuffer",            13000 },
        { "castlearch",            "bsmarketcastlearch",            14500 },
        { "dockswall",             "bsmarketdockswall",             14500 },
        { "dockwall",              "bsmarketdockswall",             14500 },
        { "bsdockwall",            "bsmarketdockswall",             14500 },
        { "slumswall",             "bsslumsthinwallv1",             14000 },
        { "slumsthinwall",         "bsslumsthinwallv1",             14500 },
        { "windowsmallarched",     "esasmarchedwin",                14000 },
        { "smallarchedwin",        "esasmarchedwin",                14000 },
        { "smarchedwin",           "esasmarchedwin",                14000 },
        { "marketdocksjetty",      "bsmarketdocksjetty",            14000 },
        { "docksjetty",            "bsmarketdocksjetty",            13500 },
        { "docksplatform",         "bsmarketdocksplatform",         13500 },
        { "dockscrane",            "bsmarketdockscrane",            13500 },
        { "oillanternsingle",      "bscemetaryoillampsingle",       13000 },
        { "oillampsingle",         "bscemetaryoillampsingle",       13000 },
        { "statue",                "okstatuedolphinv1",             12000 },
        { "cellarlargeroom",       "cellarlargeroom",               15000 },
        { "cellarsmallroom",       "cellarsmallroom",               15000 },
        { "bsmarkettownhousesmall", "bstownhousebasicfacademid",    13500 },
        { "bwsmarkettownhousesmall","bstownhousebasicfacademid",    13500 },
        { "townhousev1",           "bstownhousev1facademid",        14000 },
        { "townhousev2",           "bstownhousev2facademid",        14000 },
        { "townhousev3",           "bstownhousev3exterior",         14500 },
    };
    for (const auto& a : aliases) {
        if (has(a.entity) && (cand_has(a.model) || cand_path_has(a.model))) {
            score = std::max(score, a.score);
        }
    }

    if (score == INT_MIN) return score;
    if (c.entry) score += 500;
    if (c.from_gmd) score += 150;
    if (has("facademid") && path_key.find("facademid") != std::string::npos) {
        score += 500;
    }
    if (has("facade") && path_key.find("facade") != std::string::npos) {
        score += 150;
    }
    return score - int(std::min<size_t>(c.hint_path.size(), 200));
}

const StreamingModelCandidate*
choose_streaming_model_for_gdb(const std::string& entity_name,
                               const std::vector<StreamingModelCandidate>& candidates,
                               int* out_score,
                               uint32_t parent_hash)
{
    auto path_suffix_matches = [](const std::string& path,
                                  const std::string& target) {
        if (path.empty() || target.empty()) return false;
        if (path == target) return true;
        return path.size() > target.size() &&
               path.compare(path.size() - target.size(),
                            target.size(), target) == 0 &&
               (path[path.size() - target.size() - 1] == '/' ||
                path[path.size() - target.size() - 1] == '\\');
    };

    auto choose_curated_override =
        [&](const char* target_model_path, int* score_out) {
            if (!target_model_path || !*target_model_path) {
                return static_cast<const StreamingModelCandidate*>(nullptr);
            }
            const std::string target_lower = lower_slash(target_model_path);
            const std::string target_key =
                compact_match_key(model_name_from_path(target_model_path));
            const bool generic_shell_target =
                target_key == "exterior" || target_key == "interior";
            const StreamingModelCandidate* best = nullptr;
            int best_score = INT_MIN;
            for (const auto& c : candidates) {
                int score = INT_MIN;
                const std::string& resolved_lower = c.resolved_lower;
                const std::string& hint_lower = c.hint_lower;
                if (resolved_lower == target_lower) {
                    score = std::max(score, 50000);
                } else if (path_suffix_matches(resolved_lower, target_lower)) {
                    score = std::max(score, 49250);
                }
                if (hint_lower == target_lower) {
                    score = std::max(score, 49000);
                } else if (path_suffix_matches(hint_lower, target_lower)) {
                    score = std::max(score, 48250);
                }
                if (!target_key.empty() && !generic_shell_target) {
                    if (c.key == target_key) {
                        score = std::max(score, 46000);
                    }
                }
                if (score == INT_MIN) continue;
                if (c.entry) score += 500;
                if (c.from_gmd) score += 150;
                score -= int(std::min<size_t>(c.hint_path.size(), 200));
                if (!best || score > best_score) {
                    best = &c;
                    best_score = score;
                }
            }
            if (score_out) *score_out = best_score;
            return best;
        };

    const std::string entity_key = gdb_entity_key(entity_name);
    const char* curated_model =
        GdbModelHashlist::LookupParentHash(parent_hash);
    if (!curated_model) {
        curated_model = GdbModelHashlist::LookupEntityKey(entity_key);
    }
    if (curated_model && *curated_model) {
        if (const StreamingModelCandidate* curated =
                choose_curated_override(curated_model, out_score)) {
            return curated;
        }
        log_curated_hashlist_miss(entity_name, parent_hash, curated_model);
        if (out_score) *out_score = INT_MIN;
        return nullptr;
    }

    const StreamingModelCandidate* best = nullptr;
    int best_score = INT_MIN;
    for (const auto& c : candidates) {
        const int score = streaming_model_score(entity_name, c);
        if (!best || score > best_score) {
            best = &c;
            best_score = score;
        }
    }
    if (out_score) *out_score = best_score;
    return (best_score >= 6500) ? best : nullptr;
}
