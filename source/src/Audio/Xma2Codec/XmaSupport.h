// Native C++ port of the FFmpeg XMA/WMA Pro decoder pipeline.
// This unit replaces a handful of FFmpeg framework types
// (AVCodecContext, AVFrame, AVPacket, AVChannelLayout, AVSampleFormat)
// with the slim subset the WMA Pro decoder actually touches, plus
// float -> s16 conversion and a debug log writer.
//
// Derived from FFmpeg (LGPL 2.1+); see Archive/audio/libavcodec for the
// original sources.

#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace Xma {

constexpr int kMaxFramePadding = 64;

enum class CodecId : int {
    None = 0,
    WmaPro = 1,
    Xma1   = 2,
    Xma2   = 3,
};

enum class SampleFmt : int {
    None  = -1,
    S16   = 1,
    Fltp  = 8,
};

enum class ChannelOrder : int {
    Unspec = 0,
    Native = 1,
};

struct ChannelLayout {
    ChannelOrder order = ChannelOrder::Unspec;
    int          nb_channels = 0;
    uint64_t     mask = 0;
};

void channel_layout_default(ChannelLayout& layout, int nb_channels);
void channel_layout_from_mask(ChannelLayout& layout, uint64_t mask);
int  channel_layout_popcount(uint64_t mask);

struct Packet {
    const uint8_t* data = nullptr;
    int            size = 0;
};

struct Frame {
    std::array<std::vector<float>, 16> planes;
    int nb_samples = 0;
    int nb_channels = 0;
    int sample_rate = 0;

    void allocate(int channels, int samples);
    void clear();
};

struct CodecContext {
    CodecId       codec_id     = CodecId::None;
    int           sample_rate  = 0;
    SampleFmt     sample_fmt   = SampleFmt::None;
    ChannelLayout ch_layout;
    int           block_align  = 0;
    int           bit_rate     = 0;
    int           flags        = 0;
    int           debug        = 0;
    int64_t       frame_num    = 0;

    std::vector<uint8_t> extradata;
    int           extradata_size = 0;

    void* priv_data = nullptr;
    int   skip_samples = 0;
};

void fltp_to_s16_interleaved(const float* const* planes,
                             int16_t* dst,
                             int nb_samples,
                             int nb_channels);

inline int log2i(unsigned int v) {
    int r = -1;
    while (v) { ++r; v >>= 1; }
    return r;
}

inline uint16_t read_u16le(const void* p) {
    const auto* b = static_cast<const uint8_t*>(p);
    return uint16_t(b[0] | (b[1] << 8));
}
inline uint32_t read_u32le(const void* p) {
    const auto* b = static_cast<const uint8_t*>(p);
    return uint32_t(b[0]) | (uint32_t(b[1]) << 8) |
           (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
}

// ---------------------------------------------------------------------
// Debug log writer. Writes to xma_debug.log next to the running .exe.
// _fsopen(_SH_DENYNO) on Windows so external tools can read while we
// write. log_reset() truncates on next write and resets the per-decode
// log budgets below.
// ---------------------------------------------------------------------
void log_msg(const char* fmt, ...);
void log_hexdump(const char* label, const void* data, std::size_t n);
void log_flush();
void log_reset();

extern int g_log_packet_budget;
extern int g_log_frame_budget;
extern int g_log_subframe_budget;

}  // namespace Xma
