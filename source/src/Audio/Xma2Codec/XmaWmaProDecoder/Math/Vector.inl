inline void vector_fmul_scalar(float* dst, const float* src, float c, int n) {
    for (int i = 0; i < n; ++i) dst[i] = src[i] * c;
}

inline void vector_fmul_window(float* dst, const float* src0,
                               const float* src1, const float* win, int len) {
    for (int k = 0; k < len; ++k) {
        const float s0  = src0[k];
        const float s1  = src1[len - 1 - k];
        const float wlo = win[k];
        const float whi = win[2 * len - 1 - k];
        dst[k]               = s0 * whi - s1 * wlo;
        dst[2 * len - 1 - k] = s0 * wlo + s1 * whi;
    }
}

inline float ff_exp10(double v) { return float(std::pow(10.0, v)); }

inline uint32_t float2int(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}
inline float int2float(uint32_t u) {
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

inline int av_log2(unsigned v) {
    int r = -1;
    while (v) { ++r; v >>= 1; }
    return r;
}

inline int av_clip(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

int get_rate(CodecContext* avctx) {
    if (avctx->codec_id != CodecId::WmaPro) {
        if (avctx->sample_rate > 44100) return 48000;
        else if (avctx->sample_rate > 32000) return 44100;
        else if (avctx->sample_rate > 24000) return 32000;
        return 24000;
    }
    return avctx->sample_rate;
}
