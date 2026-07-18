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
