static void mat4_identity(float* m) { memset(m, 0, 16 * sizeof(float)); m[0] = m[5] = m[10] = m[15] = 1.0f; }
static void mat4_perspective(float* m, float fov, float aspect, float znear, float zfar) {
    float f = 1.0f / tanf(fov * 0.5f);
    memset(m, 0, 16 * sizeof(float));
    m[0] = f / aspect; m[5] = f; m[10] = (zfar + znear) / (znear - zfar); m[11] = -1.0f; m[14] = (2.0f * zfar * znear) / (znear - zfar);
}
static void mat4_lookat(float* m, float ex, float ey, float ez, float cx, float cy, float cz, float ux, float uy, float uz) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float fl = sqrtf(fx * fx + fy * fy + fz * fz); fx /= fl; fy /= fl; fz /= fl;
    float sx = fy * uz - fz * uy, sy = fz * ux - fx * uz, sz = fx * uy - fy * ux;
    float sl = sqrtf(sx * sx + sy * sy + sz * sz); sx /= sl; sy /= sl; sz /= sl;
    float uux = sy * fz - sz * fy, uuy = sz * fx - sx * fz, uuz = sx * fy - sy * fx;
    m[0] = sx; m[4] = sy; m[8] = sz; m[12] = -(sx * ex + sy * ey + sz * ez);
    m[1] = uux; m[5] = uuy; m[9] = uuz; m[13] = -(uux * ex + uuy * ey + uuz * ez);
    m[2] = -fx; m[6] = -fy; m[10] = -fz; m[14] = (fx * ex + fy * ey + fz * ez);
    m[3] = 0; m[7] = 0; m[11] = 0; m[15] = 1;
}
static void mat4_rotateX(float* m, float angle) { mat4_identity(m); float c = cosf(angle), s = sinf(angle); m[5] = c; m[6] = s; m[9] = -s; m[10] = c; }
static void mat4_translate(float* m, float x, float y, float z) { mat4_identity(m); m[12] = x; m[13] = y; m[14] = z; }
static void mat4_mult(float* out, const float* a, const float* b) {
    float tmp[16];
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) tmp[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] + a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
    memcpy(out, tmp, 16 * sizeof(float));
}
