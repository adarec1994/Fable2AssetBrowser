#include "XmaWmaCore.h"

#include <array>
#include <cmath>
#include <mutex>
#include <vector>

#include "XmaBitstream.h"

namespace Xma {

const std::array<uint16_t, 25> wma_critical_freqs = {
      100,   200,  300,  400,  510,  630,   770,   920,
     1080,  1270, 1480, 1720, 2000, 2320,  2700,  3150,
     3700,  4400, 5300, 6400, 7700, 9500, 12000, 15500,
    24500,
};

int wma_get_frame_len_bits(int sample_rate, int version, unsigned decode_flags) {
    int frame_len_bits;
    if      (sample_rate <= 16000)                                frame_len_bits = 9;
    else if (sample_rate <= 22050 || (sample_rate <= 32000 && version == 1))
                                                                  frame_len_bits = 10;
    else if (sample_rate <= 48000 || version < 3)                 frame_len_bits = 11;
    else if (sample_rate <= 96000)                                frame_len_bits = 12;
    else                                                          frame_len_bits = 13;

    if (version == 3) {
        const unsigned tmp = decode_flags & 0x6u;
        if      (tmp == 0x2) ++frame_len_bits;
        else if (tmp == 0x4) --frame_len_bits;
        else if (tmp == 0x6) frame_len_bits -= 2;
    }
    return frame_len_bits;
}

int wma_total_gain_to_bits(int total_gain) {
    if      (total_gain < 15) return 13;
    else if (total_gain < 32) return 12;
    else if (total_gain < 40) return 11;
    else if (total_gain < 45) return 10;
    else                       return  9;
}

namespace {

constexpr int kMinLog2 = 5;
constexpr int kMaxLog2 = 13;

struct SineWindowTables {
    std::array<std::vector<float>, kMaxLog2 + 1> windows;
    std::array<std::once_flag,     kMaxLog2 + 1> once;
};

SineWindowTables& tables() {
    static SineWindowTables t;
    return t;
}

void fill_window(float* window, int n) {
    const double scale = 3.14159265358979323846 / (2.0 * double(n));
    for (int i = 0; i < n; ++i) {
        window[i] = std::sin((double(i) + 0.5) * scale);
    }
}

}  // namespace

void wma_init_sine_window(int index) {
    if (index < kMinLog2 || index > kMaxLog2) return;
    auto& t = tables();
    std::call_once(t.once[index], [&] {
        const int n = 1 << index;
        t.windows[index].resize(n);
        fill_window(t.windows[index].data(), n);
    });
}

const float* wma_sine_window(int index) {
    if (index < kMinLog2 || index > kMaxLog2) return nullptr;
    wma_init_sine_window(index);
    return tables().windows[index].data();
}

unsigned wma_get_large_val(GetBits& gb) {
    int n_bits = 8;
    if (gb.read_1()) {
        n_bits += 8;
        if (gb.read_1()) {
            n_bits += 8;
            if (gb.read_1()) n_bits += 7;
        }
    }
    return gb.read_long(n_bits);
}

}  // namespace Xma
