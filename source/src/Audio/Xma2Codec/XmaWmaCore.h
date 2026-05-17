// Native C++ port of the shared WMA decoder helpers from
// libavcodec/wma.{h,c}, wma_common.{h,c}, wma_freqs.{h,c}, sinewin.{h,c}.
//
// Derived from FFmpeg (LGPL 2.1+).

#pragma once

#include <array>
#include <cstdint>

namespace Xma {

extern const std::array<uint16_t, 25> wma_critical_freqs;

int wma_get_frame_len_bits(int sample_rate, int version, unsigned decode_flags);
int wma_total_gain_to_bits(int total_gain);

void wma_init_sine_window(int index);
const float* wma_sine_window(int index);

class GetBits;
unsigned wma_get_large_val(GetBits& gb);

}  // namespace Xma
