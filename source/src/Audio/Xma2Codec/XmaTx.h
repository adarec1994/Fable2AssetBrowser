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

}
