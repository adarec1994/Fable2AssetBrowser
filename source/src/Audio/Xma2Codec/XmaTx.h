// Native C++ port of libavutil/tx.{h,c} (the float inverse MDCT path
// that wmaprodec.c uses). The current implementation is the naive
// O(N^2) direct formula; an FFT-based fast path can replace run()
// without changing the API.
//
// Derived from FFmpeg (LGPL 2.1+).

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace Xma {

class Mdct {
public:
    int  init_inverse(int len, float scale);
    void run(float* out, const float* in, std::ptrdiff_t stride = sizeof(float));
    int  length() const { return len_; }

private:
    int   len_ = 0;
    float scale_ = 1.0f;
    std::vector<float> twiddles_;
    std::vector<float> exptab_;
    std::vector<int>   revtab_;
};

}  // namespace Xma
