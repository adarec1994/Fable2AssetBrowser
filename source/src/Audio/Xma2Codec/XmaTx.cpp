#include "XmaTx.h"

#include <cmath>
#include <cstring>

namespace Xma {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

int Mdct::init_inverse(int len, float scale) {
    if (len <= 0 || (len & (len - 1)) != 0) return -1;
    len_   = len;
    scale_ = scale;
    twiddles_.clear();
    exptab_.clear();
    revtab_.clear();
    constexpr int kCacheMaxLen = 1024;
    if (len > kCacheMaxLen) return 0;
    twiddles_.assign(std::size_t(len) * std::size_t(len), 0.0f);
    const double inv_N = kPi / double(len);
    for (int m = 0; m < len; ++m) {
        const double phase_m = (double(m) + 0.5) + double(len);
        for (int k = 0; k < len; ++k) {
            const double c = std::cos(inv_N * phase_m * (double(k) + 0.5));
            twiddles_[std::size_t(m) * std::size_t(len) + std::size_t(k)] =
                float(c) * scale;
        }
    }
    return 0;
}

void Mdct::run(float* out, const float* in, std::ptrdiff_t stride) {
    if (!out || !in || len_ <= 0) return;
    const int N    = len_;
    const int step = int(stride / std::ptrdiff_t(sizeof(float)));
    if (int(twiddles_.size()) == N * N && step == 1) {
        for (int m = 0; m < N; ++m) {
            const float* row = &twiddles_[std::size_t(m) * std::size_t(N)];
            float acc = 0.0f;
            for (int k = 0; k < N; ++k) {
                acc += in[k] * row[k];
            }
            out[m] = acc;
        }
        return;
    }
    const double inv_N = kPi / double(N);
    for (int m = 0; m < N; ++m) {
        double acc = 0.0;
        const double phase_m = (double(m) + 0.5) + double(N);
        for (int k = 0; k < N; ++k) {
            const double c = std::cos(inv_N * phase_m * (double(k) + 0.5));
            acc += double(in[k * step]) * c;
        }
        out[m] = float(acc) * scale_;
    }
}

}
