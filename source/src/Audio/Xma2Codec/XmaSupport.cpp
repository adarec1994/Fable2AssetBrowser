#include "XmaSupport.h"

#include <algorithm>
#include <bitset>

namespace Xma {

namespace {
constexpr uint64_t default_masks[9] = {
    0,
    0x00000004,
    0x00000003,
    0x00000007,
    0x00000033,
    0x00000037,
    0x0000003F,
    0x0000063F,
    0x000000FF,
};
}

int channel_layout_popcount(uint64_t mask) {
    return int(std::bitset<64>(mask).count());
}

void channel_layout_default(ChannelLayout& layout, int nb_channels) {
    layout.nb_channels = nb_channels;
    if (nb_channels >= 0 && nb_channels <= 8) {
        layout.mask = default_masks[nb_channels];
        layout.order = nb_channels == 0 ? ChannelOrder::Unspec
                                        : ChannelOrder::Native;
    } else {
        layout.mask = nb_channels >= 64 ? ~uint64_t(0)
                                        : (uint64_t(1) << nb_channels) - 1;
        layout.order = ChannelOrder::Native;
    }
}

void channel_layout_from_mask(ChannelLayout& layout, uint64_t mask) {
    layout.mask = mask;
    layout.nb_channels = channel_layout_popcount(mask);
    layout.order = mask ? ChannelOrder::Native : ChannelOrder::Unspec;
}

void Frame::allocate(int channels, int samples) {
    nb_channels = channels;
    nb_samples  = samples;
    for (int c = 0; c < channels && c < int(planes.size()); ++c) {
        planes[c].assign(samples, 0.0f);
    }
}

void Frame::clear() {
    for (auto& p : planes) p.clear();
    nb_samples = 0;
    nb_channels = 0;
    sample_rate = 0;
}

void fltp_to_s16_interleaved(const float* const* planes,
                             int16_t* dst,
                             int nb_samples,
                             int nb_channels) {
    constexpr float kScale = 32767.0f;
    for (int i = 0; i < nb_samples; ++i) {
        for (int c = 0; c < nb_channels; ++c) {
            float v = planes[c][i] * kScale;
            if (!(v == v) || v > 1e9f || v < -1e9f) v = 0.0f;
            v = std::max(-32768.0f, std::min(32767.0f, v));
            dst[i * nb_channels + c] = int16_t(v);
        }
    }
}

}
