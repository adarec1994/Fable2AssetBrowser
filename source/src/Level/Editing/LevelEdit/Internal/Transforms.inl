void quat_mul(const float a[4], const float b[4], float out[4]) {
    out[0] = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
    out[1] = a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0];
    out[2] = a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3];
    out[3] = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
}

void quat_axis(const float axis[3], float deg, float out[4]) {
    const float h = deg * kDegToRad * 0.5f;
    const float s = std::sin(h);
    out[0] = axis[0] * s;
    out[1] = axis[1] * s;
    out[2] = axis[2] * s;
    out[3] = std::cos(h);
}

void euler_engine_to_preview_quat(const float rot_deg[3], float out[4]) {
    const float ax[3] = { 1, 0, 0 };
    const float ay[3] = { 0, 0, 1 };
    const float az[3] = { 0, 1, 0 };
    float qx[4], qy[4], qz[4], t[4];
    quat_axis(ax, rot_deg[0], qx);
    quat_axis(ay, rot_deg[1], qy);
    quat_axis(az, rot_deg[2], qz);
    quat_mul(qy, qx, t);
    quat_mul(qz, t, out);
}
