std::string fbx_clean_name(std::string s) {
    if (s.empty()) return "clip";
    for (char& ch : s) {
        const unsigned char c = (unsigned char)ch;
        if (!std::isalnum(c) && ch != '_' && ch != '-' && ch != '.') {
            ch = '_';
        }
    }
    return s;
}

void quat_to_euler_deg(double qx, double qy, double qz, double qw,
                       double& rx, double& ry, double& rz) {
    const double sinr_cosp = 2.0 * (qw * qx + qy * qz);
    const double cosr_cosp = 1.0 - 2.0 * (qx * qx + qy * qy);
    rx = std::atan2(sinr_cosp, cosr_cosp);

    const double sinp = 2.0 * (qw * qy - qz * qx);
    ry = std::abs(sinp) >= 1.0
        ? std::copysign(3.14159265358979323846 / 2.0, sinp)
        : std::asin(sinp);

    const double siny_cosp = 2.0 * (qw * qz + qx * qy);
    const double cosy_cosp = 1.0 - 2.0 * (qy * qy + qz * qz);
    rz = std::atan2(siny_cosp, cosy_cosp);

    const double k = 180.0 / 3.14159265358979323846;
    rx *= k;
    ry *= k;
    rz *= k;
}

void fbx_root_quat(double& qx, double& qy, double& qz, double& qw) {
    constexpr double kS2 = 0.70710678118654752440;
    const double x = qx;
    const double y = qy;
    const double z = qz;
    const double w = qw;
    qx = kS2 * (x + w);
    qy = kS2 * (y - z);
    qz = kS2 * (z + y);
    qw = kS2 * (w - x);
}

int64_t fbx_time(float seconds) {
    return (int64_t)std::llround((double)seconds * 46186158000.0);
}
