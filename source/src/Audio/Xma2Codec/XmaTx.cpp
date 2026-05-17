// XmaTx.cpp — naive but correct inverse float MDCT.
//
// FFmpeg's lavu_tx AV_TX_FLOAT_MDCT (inverse) as wmaprodec uses it:
//   - input  = `len` real coefficients (wmaprodec fills tmp[0..len-1])
//   - output = `len` real samples (= central N of the unrolled 2N MDCT)
//
// Formula:
//   y[m] = sum_{k=0..N-1} X[k] * cos((pi/N) * (m + 0.5 + N) * (k + 0.5))
//
// O(N^2) per transform — slow but correct. Replaceable with an FFT-based
// fast path later.

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
    return 0;
}

void Mdct::run(float* out, const float* in, std::ptrdiff_t stride) {
    if (!out || !in || len_ <= 0) return;
    const int N    = len_;
    const int step = int(stride / std::ptrdiff_t(sizeof(float)));
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

}  // namespace Xma
