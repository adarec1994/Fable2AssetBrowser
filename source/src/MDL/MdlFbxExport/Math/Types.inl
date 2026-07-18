struct IdGen {
    int64_t next = 1000000000ll;
    int64_t make() { return next++; }
};

struct Mat4 {
    double m[16];
    static Mat4 identity() {
        Mat4 r{};
        for (int i = 0; i < 16; ++i) r.m[i] = 0.0;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0;
        return r;
    }
    static Mat4 multiply(const Mat4& a, const Mat4& b) {
        Mat4 r{};
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                double s = 0;
                for (int k = 0; k < 4; ++k)
                    s += a.m[i*4 + k] * b.m[k*4 + j];
                r.m[i*4 + j] = s;
            }
        }
        return r;
    }

    std::vector<double> as_column_major() const {
        std::vector<double> out(16);
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                out[j*4 + i] = m[i*4 + j];
        return out;
    }
    static Mat4 from_trs_quat(double qx, double qy, double qz, double qw,
                              double tx, double ty, double tz,
                              double sx, double sy, double sz) {

        const double xx = qx*qx, yy = qy*qy, zz = qz*qz;
        const double xy = qx*qy, xz = qx*qz, yz = qy*qz;
        const double wx = qw*qx, wy = qw*qy, wz = qw*qz;
        Mat4 r{};
        r.m[0]  = (1 - 2*(yy + zz)) * sx;
        r.m[1]  = (2*(xy - wz))     * sy;
        r.m[2]  = (2*(xz + wy))     * sz;
        r.m[3]  = tx;
        r.m[4]  = (2*(xy + wz))     * sx;
        r.m[5]  = (1 - 2*(xx + zz)) * sy;
        r.m[6]  = (2*(yz - wx))     * sz;
        r.m[7]  = ty;
        r.m[8]  = (2*(xz - wy))     * sx;
        r.m[9]  = (2*(yz + wx))     * sy;
        r.m[10] = (1 - 2*(xx + yy)) * sz;
        r.m[11] = tz;
        r.m[12] = 0; r.m[13] = 0; r.m[14] = 0; r.m[15] = 1;
        return r;
    }
    Mat4 inverse_or_identity() const {

        Mat4 inv{};
        const double* a = m;
        inv.m[0]  =  a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] +
                     a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
        inv.m[4]  = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] -
                     a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
        inv.m[8]  =  a[4]*a[9]*a[15]  - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] +
                     a[8]*a[7]*a[13]  + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
        inv.m[12] = -a[4]*a[9]*a[14]  + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] -
                     a[8]*a[6]*a[13]  - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
        inv.m[1]  = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] -
                     a[9]*a[3]*a[14]  - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
        inv.m[5]  =  a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] +
                     a[8]*a[3]*a[14]  + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
        inv.m[9]  = -a[0]*a[9]*a[15]  + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] -
                     a[8]*a[3]*a[13]  - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
        inv.m[13] =  a[0]*a[9]*a[14]  - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] +
                     a[8]*a[2]*a[13]  + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
        inv.m[2]  =  a[1]*a[6]*a[15]  - a[1]*a[7]*a[14]  - a[5]*a[2]*a[15] +
                     a[5]*a[3]*a[14]  + a[13]*a[2]*a[7]  - a[13]*a[3]*a[6];
        inv.m[6]  = -a[0]*a[6]*a[15]  + a[0]*a[7]*a[14]  + a[4]*a[2]*a[15] -
                     a[4]*a[3]*a[14]  - a[12]*a[2]*a[7]  + a[12]*a[3]*a[6];
        inv.m[10] =  a[0]*a[5]*a[15]  - a[0]*a[7]*a[13]  - a[4]*a[1]*a[15] +
                     a[4]*a[3]*a[13]  + a[12]*a[1]*a[7]  - a[12]*a[3]*a[5];
        inv.m[14] = -a[0]*a[5]*a[14]  + a[0]*a[6]*a[13]  + a[4]*a[1]*a[14] -
                     a[4]*a[2]*a[13]  - a[12]*a[1]*a[6]  + a[12]*a[2]*a[5];
        inv.m[3]  = -a[1]*a[6]*a[11]  + a[1]*a[7]*a[10]  + a[5]*a[2]*a[11] -
                     a[5]*a[3]*a[10]  - a[9]*a[2]*a[7]   + a[9]*a[3]*a[6];
        inv.m[7]  =  a[0]*a[6]*a[11]  - a[0]*a[7]*a[10]  - a[4]*a[2]*a[11] +
                     a[4]*a[3]*a[10]  + a[8]*a[2]*a[7]   - a[8]*a[3]*a[6];
        inv.m[11] = -a[0]*a[5]*a[11]  + a[0]*a[7]*a[9]   + a[4]*a[1]*a[11] -
                     a[4]*a[3]*a[9]   - a[8]*a[1]*a[7]   + a[8]*a[3]*a[5];
        inv.m[15] =  a[0]*a[5]*a[10]  - a[0]*a[6]*a[9]   - a[4]*a[1]*a[10] +
                     a[4]*a[2]*a[9]   + a[8]*a[1]*a[6]   - a[8]*a[2]*a[5];
        double det = a[0]*inv.m[0] + a[1]*inv.m[4] + a[2]*inv.m[8] + a[3]*inv.m[12];
        if (std::abs(det) < 1e-20) return Mat4::identity();
        double inv_det = 1.0 / det;
        for (int i = 0; i < 16; ++i) inv.m[i] *= inv_det;
        return inv;
    }
};
