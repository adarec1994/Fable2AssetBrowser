#include "XmaWmaProDecoder.h"
#include "XmaBitstream.h"
#include "XmaSupport.h"
#include "XmaTx.h"
#include "XmaWmaCore.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace Xma {

namespace {

#include "wmaprodata.h"

constexpr int MAX_SUBFRAMES = 32;
constexpr int MAX_BANDS     = 29;
constexpr int MAX_FRAMESIZE = 32768;

constexpr int WMAPRO_BLOCK_MIN_BITS = 6;
constexpr int WMAPRO_BLOCK_MAX_BITS = 13;
constexpr int WMAPRO_BLOCK_MIN_SIZE = 1 << WMAPRO_BLOCK_MIN_BITS;
constexpr int WMAPRO_BLOCK_MAX_SIZE = 1 << WMAPRO_BLOCK_MAX_BITS;
constexpr int WMAPRO_BLOCK_SIZES    = WMAPRO_BLOCK_MAX_BITS - WMAPRO_BLOCK_MIN_BITS + 1;

constexpr int VLCBITS       = 9;
constexpr int SCALEVLCBITS  = 8;
constexpr int VEC4MAXDEPTH  = (HUFF_VEC4_MAXBITS  + VLCBITS - 1) / VLCBITS;
constexpr int VEC2MAXDEPTH  = (HUFF_VEC2_MAXBITS  + VLCBITS - 1) / VLCBITS;
constexpr int VEC1MAXDEPTH  = (HUFF_VEC1_MAXBITS  + VLCBITS - 1) / VLCBITS;
constexpr int SCALEMAXDEPTH = (HUFF_SCALE_MAXBITS + SCALEVLCBITS - 1) / SCALEVLCBITS;
constexpr int SCALERLMAXDEPTH = (HUFF_SCALE_RL_MAXBITS + VLCBITS - 1) / VLCBITS;

constexpr int AV_INPUT_BUFFER_PADDING_SIZE = 64;

struct ProTables {
    Vlc sf_vlc;
    Vlc sf_rl_vlc;
    Vlc vec4_vlc;
    Vlc vec2_vlc;
    Vlc vec1_vlc;
    Vlc coef0_vlc;
    Vlc coef1_vlc;
    std::array<float, 33> sin64{};
};

ProTables& pro_tables() {
    static ProTables t;
    return t;
}

void init_static_once() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        auto& t = pro_tables();
        t.sf_vlc.init_from_lengths(SCALEVLCBITS, HUFF_SCALE_SIZE,
                                   reinterpret_cast<const int8_t*>(&scale_table[0][1]), 2,
                                   &scale_table[0][0], 2, 1, -60, 0);
        t.sf_rl_vlc.init_from_lengths(VLCBITS, HUFF_SCALE_RL_SIZE,
                                      reinterpret_cast<const int8_t*>(&scale_rl_table[0][1]), 2,
                                      &scale_rl_table[0][0], 2, 1, 0, 0);
        t.vec4_vlc.init_from_lengths(VLCBITS, HUFF_VEC4_SIZE,
                                     reinterpret_cast<const int8_t*>(vec4_lens), 1,
                                     vec4_syms, 2, 2, -1, 0);
        t.vec2_vlc.init_from_lengths(VLCBITS, HUFF_VEC2_SIZE,
                                     reinterpret_cast<const int8_t*>(&vec2_table[0][1]), 2,
                                     &vec2_table[0][0], 2, 1, -1, 0);
        t.vec1_vlc.init_from_lengths(VLCBITS, HUFF_VEC1_SIZE,
                                     reinterpret_cast<const int8_t*>(&vec1_table[0][1]), 2,
                                     &vec1_table[0][0], 2, 1, 0, 0);
        t.coef0_vlc.init_from_lengths(VLCBITS, HUFF_COEF0_SIZE,
                                      reinterpret_cast<const int8_t*>(coef0_lens), 1,
                                      coef0_syms, 2, 2, 0, 0);
        t.coef1_vlc.init_from_lengths(VLCBITS, HUFF_COEF1_SIZE,
                                      reinterpret_cast<const int8_t*>(&coef1_table[0][1]), 2,
                                      &coef1_table[0][0], 2, 1, 0, 0);
        for (int i = 0; i < 33; ++i)
            t.sin64[i] = float(std::sin(double(i) * 3.14159265358979323846 / 64.0));
        for (int i = WMAPRO_BLOCK_MIN_BITS; i <= WMAPRO_BLOCK_MAX_BITS; ++i)
            wma_init_sine_window(i);
    });
}

struct ProChannel {
    int16_t  prev_block_len = 0;
    uint8_t  transmit_coefs = 0;
    uint8_t  num_subframes  = 0;
    uint16_t subframe_len[MAX_SUBFRAMES]    = {};
    uint16_t subframe_offset[MAX_SUBFRAMES] = {};
    uint8_t  cur_subframe   = 0;
    uint16_t decoded_samples = 0;
    uint8_t  grouped        = 0;
    int      quant_step     = 0;
    int8_t   reuse_sf       = 0;
    int8_t   scale_factor_step = 0;
    int      max_scale_factor  = 0;
    int      saved_scale_factors[2][MAX_BANDS] = {};
    int8_t   scale_factor_idx  = 0;
    int*     scale_factors     = nullptr;
    uint8_t  table_idx         = 0;
    float*   coeffs            = nullptr;
    uint16_t num_vec_coeffs    = 0;
    std::array<float, WMAPRO_BLOCK_MAX_SIZE + WMAPRO_BLOCK_MAX_SIZE / 2> out{};
};

struct ProChannelGrp {
    uint8_t num_channels = 0;
    int8_t  transform = 0;
    int8_t  transform_band[MAX_BANDS] = {};
    float   decorrelation_matrix[kWmaProMaxChannels * kWmaProMaxChannels] = {};
    float*  channel_data[kWmaProMaxChannels] = {};
};

struct WmaProState {
    CodecContext* avctx = nullptr;
    std::array<uint8_t, MAX_FRAMESIZE + AV_INPUT_BUFFER_PADDING_SIZE> frame_data{};
    PutBits      pb;
    Mdct         tx[WMAPRO_BLOCK_SIZES];
    std::array<float, WMAPRO_BLOCK_MAX_SIZE> tmp{};
    const float* windows[WMAPRO_BLOCK_SIZES] = {};

    uint32_t decode_flags = 0;
    uint8_t  len_prefix = 0;
    uint8_t  dynamic_range_compression = 0;
    uint8_t  bits_per_sample = 16;
    uint16_t samples_per_frame = 0;
    uint16_t trim_start = 0;
    uint16_t trim_end   = 0;
    uint16_t log2_frame_size = 0;
    int8_t   lfe_channel = -1;
    uint8_t  max_num_subframes = 0;
    uint8_t  subframe_len_bits = 0;
    uint8_t  max_subframe_len_bit = 0;
    uint16_t min_samples_per_subframe = 0;
    int8_t   num_sfb[WMAPRO_BLOCK_SIZES] = {};
    int16_t  sfb_offsets[WMAPRO_BLOCK_SIZES][MAX_BANDS] = {};
    int8_t   sf_offsets[WMAPRO_BLOCK_SIZES][WMAPRO_BLOCK_SIZES][MAX_BANDS] = {};
    int16_t  subwoofer_cutoffs[WMAPRO_BLOCK_SIZES] = {};

    GetBits  pgb;
    int      next_packet_start = 0;
    uint8_t  packet_offset = 0;
    uint8_t  packet_sequence_number = 0;
    int      num_saved_bits = 0;
    int      frame_offset = 0;
    int      subframe_offset = 0;
    uint8_t  packet_loss = 1;
    uint8_t  packet_done = 0;
    uint8_t  eof_done = 0;

    uint32_t frame_num = 0;
    GetBits  gb;
    int      buf_bit_size = 0;
    uint8_t  drc_gain = 0;
    int8_t   skip_frame = 1;
    int8_t   parsed_all_subframes = 0;
    uint8_t  skip_packets = 0;

    int16_t  subframe_len = 0;
    int8_t   nb_channels = 0;
    int8_t   channels_for_cur_subframe = 0;
    int8_t   channel_indexes_for_cur_subframe[kWmaProMaxChannels] = {};
    int8_t   num_bands = 0;
    int8_t   transmit_num_vec_coeffs = 0;
    int16_t* cur_sfb_offsets = nullptr;
    uint8_t  table_idx = 0;
    int8_t   esc_len = 0;

    uint8_t       num_chgroups = 0;
    ProChannelGrp chgroup[kWmaProMaxChannels];
    ProChannel    channel[kWmaProMaxChannels];
};

struct FloatFifo {
    std::deque<float> q;
    void write(const float* in, int n) {
        for (int i = 0; i < n; ++i) q.push_back(in[i]);
    }
    int read(float* out, int n) {
        const int got = std::min(n, int(q.size()));
        for (int i = 0; i < got; ++i) { out[i] = q.front(); q.pop_front(); }
        return got;
    }
    int size() const { return int(q.size()); }
    void clear() { q.clear(); }
};

struct XmaState {
    WmaProState xma[kWmaProMaxStreams];
    Frame frames[kWmaProMaxStreams];
    int current_stream = 0;
    int num_streams = 0;
    FloatFifo samples[2][kWmaProMaxStreams];
    int start_channel[kWmaProMaxStreams] = {};
    int trim_start = 0;
    int trim_end = 0;
    int flushed = 0;
};

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

int decode_init_stream(WmaProState* s, CodecContext* avctx, int num_stream) {
    init_static_once();

    s->avctx = avctx;
    s->pb.init(s->frame_data.data(), MAX_FRAMESIZE);
    avctx->sample_fmt = SampleFmt::Fltp;

    const uint8_t* edata_ptr = avctx->extradata.data();
    unsigned int channel_mask = 0;

    if (avctx->codec_id == CodecId::Xma2 && avctx->extradata_size == 34) {
        s->decode_flags    = 0x10d6;
        s->bits_per_sample = 16;
        channel_mask       = 0;
        if ((num_stream + 1) * kWmaProChannelsPerStream > avctx->ch_layout.nb_channels)
            s->nb_channels = 1;
        else
            s->nb_channels = 2;
    } else if (avctx->codec_id == CodecId::Xma2) {
        s->decode_flags    = 0x10d6;
        s->bits_per_sample = 16;
        channel_mask       = 0;
        s->nb_channels     = edata_ptr[32 + ((edata_ptr[0] == 3) ? 0 : 8) + 4 * num_stream + 0];
    } else if (avctx->codec_id == CodecId::Xma1) {
        s->decode_flags    = 0x10d6;
        s->bits_per_sample = 16;
        channel_mask       = 0;
        s->nb_channels     = edata_ptr[8 + 20 * num_stream + 17];
    } else if (avctx->codec_id == CodecId::WmaPro && avctx->extradata_size >= 18) {
        s->decode_flags    = read_u16le(edata_ptr + 14);
        channel_mask       = read_u32le(edata_ptr + 2);
        s->bits_per_sample = read_u16le(edata_ptr);
        s->nb_channels     = channel_mask ? channel_layout_popcount(channel_mask)
                                          : avctx->ch_layout.nb_channels;
        if (s->bits_per_sample > 32 || s->bits_per_sample < 1) return -1;
    } else {
        return -1;
    }

    s->log2_frame_size = uint16_t(av_log2(avctx->block_align) + 4);
    if (s->log2_frame_size > 25) return -1;

    s->skip_frame  = 1;
    s->packet_loss = 1;
    s->len_prefix  = (s->decode_flags & 0x40) ? 1 : 0;

    int bits = 0;
    if (avctx->codec_id == CodecId::WmaPro) {
        bits = wma_get_frame_len_bits(avctx->sample_rate, 3, s->decode_flags);
        if (bits > WMAPRO_BLOCK_MAX_BITS) return -1;
        s->samples_per_frame = uint16_t(1 << bits);
    } else {
        s->samples_per_frame = 512;
    }

    const int log2_max_num_subframes = (s->decode_flags & 0x38) >> 3;
    s->max_num_subframes = uint8_t(1 << log2_max_num_subframes);
    if (s->max_num_subframes == 16 || s->max_num_subframes == 4)
        s->max_subframe_len_bit = 1;
    s->subframe_len_bits = uint8_t(av_log2(log2_max_num_subframes) + 1);

    const int num_possible_block_sizes = log2_max_num_subframes + 1;
    s->min_samples_per_subframe = s->samples_per_frame / s->max_num_subframes;
    s->dynamic_range_compression = (s->decode_flags & 0x80) ? 1 : 0;

    if (s->max_num_subframes > MAX_SUBFRAMES) return -1;
    if (s->min_samples_per_subframe < WMAPRO_BLOCK_MIN_SIZE) return -1;
    if (s->nb_channels <= 0) return -1;
    if (avctx->codec_id != CodecId::WmaPro && s->nb_channels > kWmaProChannelsPerStream) return -1;
    if (s->nb_channels > kWmaProMaxChannels || s->nb_channels > avctx->ch_layout.nb_channels) return -1;

    for (int i = 0; i < s->nb_channels; i++)
        s->channel[i].prev_block_len = s->samples_per_frame;

    s->lfe_channel = -1;
    if (channel_mask & 8) {
        unsigned int mask;
        for (mask = 1; mask < 16; mask <<= 1) {
            if (channel_mask & mask) ++s->lfe_channel;
        }
    }

    for (int i = 0; i < num_possible_block_sizes; ++i) {
        const int subframe_len = s->samples_per_frame >> i;
        int band = 1;
        const int rate = get_rate(avctx);
        s->sfb_offsets[i][0] = 0;
        int x;
        for (x = 0; x < MAX_BANDS - 1 && s->sfb_offsets[i][band - 1] < subframe_len; ++x) {
            int offset = (subframe_len * 2 * critical_freq[x]) / rate + 2;
            offset &= ~3;
            if (offset > s->sfb_offsets[i][band - 1])
                s->sfb_offsets[i][band++] = int16_t(offset);
            if (offset >= subframe_len) break;
        }
        s->sfb_offsets[i][band - 1] = int16_t(subframe_len);
        s->num_sfb[i] = int8_t(band - 1);
        if (s->num_sfb[i] <= 0) return -1;
    }

    for (int i = 0; i < num_possible_block_sizes; ++i) {
        for (int b = 0; b < s->num_sfb[i]; ++b) {
            const int offset = ((s->sfb_offsets[i][b] + s->sfb_offsets[i][b + 1] - 1) << i) >> 1;
            for (int x = 0; x < num_possible_block_sizes; ++x) {
                int v = 0;
                while ((s->sfb_offsets[x][v + 1] << x) < offset) {
                    v++;
                    if (v >= MAX_BANDS) return -1;
                }
                s->sf_offsets[i][x][b] = int8_t(v);
            }
        }
    }

    for (int i = 0; i < WMAPRO_BLOCK_SIZES; ++i) {
        const float scale = float(1.0 / double(1 << (WMAPRO_BLOCK_MIN_BITS + i - 1))
                                  / double(int64_t(1) << (s->bits_per_sample - 1)));
        if (s->tx[i].init_inverse(1 << (WMAPRO_BLOCK_MIN_BITS + i), scale) < 0) return -1;
    }

    for (int i = 0; i < WMAPRO_BLOCK_SIZES; ++i) {
        const int win_idx = WMAPRO_BLOCK_MAX_BITS - i;
        s->windows[WMAPRO_BLOCK_SIZES - i - 1] = wma_sine_window(win_idx);
    }

    for (int i = 0; i < num_possible_block_sizes; ++i) {
        const int block_size = s->samples_per_frame >> i;
        const int cutoff = (440 * block_size + 3LL * (avctx->sample_rate >> 1) - 1) / avctx->sample_rate;
        s->subwoofer_cutoffs[i] = int16_t(av_clip(cutoff, 4, block_size));
    }

    if (avctx->codec_id == CodecId::WmaPro) {
        if (channel_mask) {
            channel_layout_from_mask(avctx->ch_layout, channel_mask);
        } else {
            avctx->ch_layout.order = ChannelOrder::Unspec;
        }
    }
    return 0;
}

int decode_subframe_length(WmaProState* s, int offset) {
    int frame_len_shift = 0;
    if (offset == s->samples_per_frame - s->min_samples_per_subframe)
        return s->min_samples_per_subframe;
    if (s->gb.bits_left() < 1) return -1;
    if (s->max_subframe_len_bit) {
        if (s->gb.read_1())
            frame_len_shift = 1 + s->gb.read(s->subframe_len_bits - 1);
    } else {
        frame_len_shift = s->gb.read(s->subframe_len_bits);
    }
    const int subframe_len = s->samples_per_frame >> frame_len_shift;
    if (subframe_len < s->min_samples_per_subframe ||
        subframe_len > s->samples_per_frame) return -1;
    return subframe_len;
}

int decode_tilehdr(WmaProState* s) {
    uint16_t num_samples[kWmaProMaxChannels] = {};
    uint8_t  contains_subframe[kWmaProMaxChannels] = {};
    int channels_for_cur_subframe = s->nb_channels;
    int fixed_channel_layout = 0;
    int min_channel_len = 0;
    for (int c = 0; c < s->nb_channels; ++c) s->channel[c].num_subframes = 0;
    if (s->max_num_subframes == 1 || s->gb.read_1()) fixed_channel_layout = 1;

    do {
        for (int c = 0; c < s->nb_channels; ++c) {
            if (num_samples[c] == min_channel_len) {
                if (fixed_channel_layout || channels_for_cur_subframe == 1 ||
                    (min_channel_len == s->samples_per_frame - s->min_samples_per_subframe))
                    contains_subframe[c] = 1;
                else
                    contains_subframe[c] = uint8_t(s->gb.read_1());
            } else {
                contains_subframe[c] = 0;
            }
        }
        const int subframe_len = decode_subframe_length(s, min_channel_len);
        if (subframe_len <= 0) return -1;
        min_channel_len += subframe_len;
        for (int c = 0; c < s->nb_channels; ++c) {
            ProChannel* chan = &s->channel[c];
            if (contains_subframe[c]) {
                if (chan->num_subframes >= MAX_SUBFRAMES) return -1;
                chan->subframe_len[chan->num_subframes] = uint16_t(subframe_len);
                num_samples[c] += subframe_len;
                ++chan->num_subframes;
                if (num_samples[c] > s->samples_per_frame) return -1;
            } else if (num_samples[c] <= min_channel_len) {
                if (num_samples[c] < min_channel_len) {
                    channels_for_cur_subframe = 0;
                    min_channel_len = num_samples[c];
                }
                ++channels_for_cur_subframe;
            }
        }
    } while (min_channel_len < s->samples_per_frame);

    for (int c = 0; c < s->nb_channels; ++c) {
        int offset = 0;
        for (int i = 0; i < s->channel[c].num_subframes; ++i) {
            s->channel[c].subframe_offset[i] = uint16_t(offset);
            offset += s->channel[c].subframe_len[i];
        }
    }
    return 0;
}

void decode_decorrelation_matrix(WmaProState* s, ProChannelGrp* chgroup) {
    int offset = 0;
    int8_t rotation_offset[kWmaProMaxChannels * kWmaProMaxChannels] = {};
    std::memset(chgroup->decorrelation_matrix, 0,
                std::size_t(s->nb_channels) * std::size_t(s->nb_channels) * sizeof(float));
    for (int i = 0; i < chgroup->num_channels * (chgroup->num_channels - 1) >> 1; ++i)
        rotation_offset[i] = int8_t(s->gb.read(6));
    for (int i = 0; i < chgroup->num_channels; ++i)
        chgroup->decorrelation_matrix[chgroup->num_channels * i + i] =
            s->gb.read_1() ? 1.0f : -1.0f;
    for (int i = 1; i < chgroup->num_channels; ++i) {
        for (int x = 0; x < i; ++x) {
            for (int y = 0; y < i + 1; ++y) {
                const float v1 = chgroup->decorrelation_matrix[x * chgroup->num_channels + y];
                const float v2 = chgroup->decorrelation_matrix[i * chgroup->num_channels + y];
                const int n = rotation_offset[offset + x];
                float sinv, cosv;
                if (n < 32) {
                    sinv = pro_tables().sin64[n];
                    cosv = pro_tables().sin64[32 - n];
                } else {
                    sinv =  pro_tables().sin64[64 - n];
                    cosv = -pro_tables().sin64[n - 32];
                }
                chgroup->decorrelation_matrix[y + x * chgroup->num_channels] = v1 * sinv - v2 * cosv;
                chgroup->decorrelation_matrix[y + i * chgroup->num_channels] = v1 * cosv + v2 * sinv;
            }
        }
        offset += i;
    }
}

int decode_channel_transform(WmaProState* s) {
    s->num_chgroups = 0;
    if (s->nb_channels > 1) {
        int remaining_channels = s->channels_for_cur_subframe;
        if (s->gb.read_1()) return -1;
        for (s->num_chgroups = 0;
             remaining_channels && s->num_chgroups < s->channels_for_cur_subframe;
             ++s->num_chgroups) {
            ProChannelGrp* chgroup = &s->chgroup[s->num_chgroups];
            float** channel_data = chgroup->channel_data;
            chgroup->num_channels = 0;
            chgroup->transform = 0;

            if (remaining_channels > 2) {
                for (int i = 0; i < s->channels_for_cur_subframe; ++i) {
                    const int channel_idx = s->channel_indexes_for_cur_subframe[i];
                    if (!s->channel[channel_idx].grouped && s->gb.read_1()) {
                        ++chgroup->num_channels;
                        s->channel[channel_idx].grouped = 1;
                        *channel_data++ = s->channel[channel_idx].coeffs;
                    }
                }
            } else {
                chgroup->num_channels = uint8_t(remaining_channels);
                for (int i = 0; i < s->channels_for_cur_subframe; ++i) {
                    const int channel_idx = s->channel_indexes_for_cur_subframe[i];
                    if (!s->channel[channel_idx].grouped)
                        *channel_data++ = s->channel[channel_idx].coeffs;
                    s->channel[channel_idx].grouped = 1;
                }
            }

            if (chgroup->num_channels == 2) {
                if (s->gb.read_1()) {
                    if (s->gb.read_1()) return -1;
                } else {
                    chgroup->transform = 1;
                    if (s->nb_channels == 2) {
                        chgroup->decorrelation_matrix[0] =  1.0f;
                        chgroup->decorrelation_matrix[1] = -1.0f;
                        chgroup->decorrelation_matrix[2] =  1.0f;
                        chgroup->decorrelation_matrix[3] =  1.0f;
                    } else {
                        chgroup->decorrelation_matrix[0] =  0.70703125f;
                        chgroup->decorrelation_matrix[1] = -0.70703125f;
                        chgroup->decorrelation_matrix[2] =  0.70703125f;
                        chgroup->decorrelation_matrix[3] =  0.70703125f;
                    }
                }
            } else if (chgroup->num_channels > 2) {
                if (s->gb.read_1()) {
                    chgroup->transform = 1;
                    if (s->gb.read_1()) {
                        decode_decorrelation_matrix(s, chgroup);
                    } else {
                        if (chgroup->num_channels > 6) {
                        } else {
                            std::memcpy(chgroup->decorrelation_matrix,
                                        default_decorrelation[chgroup->num_channels],
                                        std::size_t(chgroup->num_channels) *
                                        std::size_t(chgroup->num_channels) * sizeof(float));
                        }
                    }
                }
            }

            if (chgroup->transform) {
                if (!s->gb.read_1()) {
                    for (int i = 0; i < s->num_bands; ++i)
                        chgroup->transform_band[i] = int8_t(s->gb.read_1());
                } else {
                    std::memset(chgroup->transform_band, 1, std::size_t(s->num_bands));
                }
            }
            remaining_channels -= chgroup->num_channels;
        }
    }
    return 0;
}

int run_level_decode(WmaProState* s, const Vlc& vlc, const float* level_table,
                     const uint16_t* run_table, int version, float* ptr,
                     int offset, int num_coefs, int block_len,
                     int frame_len_bits, int coef_nb_bits) {
    const uint32_t coef_mask = uint32_t(block_len - 1);
    for (; offset < num_coefs; ++offset) {
        const int code = vlc.get(s->gb, 0);
        if (code > 1) {
            offset += run_table[code];
            const int sign = int(s->gb.read_1()) - 1;
            uint32_t lvl_u = float2int(level_table[code]);
            lvl_u ^= unsigned(sign) & 0x80000000u;
            ptr[offset & coef_mask] = int2float(lvl_u);
        } else if (code == 1) {
            break;
        } else {
            int level;
            if (!version) {
                level = int(s->gb.read(coef_nb_bits));
                offset += int(s->gb.read(frame_len_bits));
            } else {
                level = int(wma_get_large_val(s->gb));
                if (s->gb.read_1()) {
                    if (s->gb.read_1()) {
                        if (s->gb.read_1()) return -1;
                        else offset += int(s->gb.read(frame_len_bits)) + 4;
                    } else {
                        offset += int(s->gb.read(2)) + 1;
                    }
                }
            }
            const int sign = int(s->gb.read_1()) - 1;
            ptr[offset & coef_mask] = float((level ^ sign) - sign);
        }
    }
    return offset > num_coefs ? -1 : 0;
}

int decode_coeffs(WmaProState* s, int c) {
    static const uint32_t fval_tab[16] = {
        0x00000000, 0x3f800000, 0x40000000, 0x40400000,
        0x40800000, 0x40a00000, 0x40c00000, 0x40e00000,
        0x41000000, 0x41100000, 0x41200000, 0x41300000,
        0x41400000, 0x41500000, 0x41600000, 0x41700000,
    };
    ProChannel* ci = &s->channel[c];
    int rl_mode = 0, cur_coeff = 0, num_zeros = 0;
    const int vlctable = int(s->gb.read_1());
    const Vlc& vlc = vlctable ? pro_tables().coef1_vlc : pro_tables().coef0_vlc;
    const uint16_t* run   = vlctable ? coef1_run   : coef0_run;
    const float*    level = vlctable ? coef1_level : coef0_level;

    while ((s->transmit_num_vec_coeffs || !rl_mode) &&
           (cur_coeff + 3 < ci->num_vec_coeffs)) {
        uint32_t vals[4];
        unsigned idx = unsigned(pro_tables().vec4_vlc.get(s->gb, VEC4MAXDEPTH));
        if (int(idx) < 0) {
            for (int i = 0; i < 4; i += 2) {
                idx = unsigned(pro_tables().vec2_vlc.get(s->gb, VEC2MAXDEPTH));
                if (int(idx) < 0) {
                    uint32_t v0 = unsigned(pro_tables().vec1_vlc.get(s->gb, VEC1MAXDEPTH));
                    if (v0 == HUFF_VEC1_SIZE - 1) v0 += wma_get_large_val(s->gb);
                    uint32_t v1 = unsigned(pro_tables().vec1_vlc.get(s->gb, VEC1MAXDEPTH));
                    if (v1 == HUFF_VEC1_SIZE - 1) v1 += wma_get_large_val(s->gb);
                    vals[i    ] = float2int(float(v0));
                    vals[i + 1] = float2int(float(v1));
                } else {
                    vals[i    ] = fval_tab[idx >> 4];
                    vals[i + 1] = fval_tab[idx & 0xF];
                }
            }
        } else {
            vals[0] = fval_tab[idx >> 12];
            vals[1] = fval_tab[(idx >> 8) & 0xF];
            vals[2] = fval_tab[(idx >> 4) & 0xF];
            vals[3] = fval_tab[idx & 0xF];
        }
        for (int i = 0; i < 4; ++i) {
            if (vals[i]) {
                const uint32_t sign = unsigned(s->gb.read_1()) - 1u;
                const uint32_t raw  = vals[i] ^ (sign << 31);
                ci->coeffs[cur_coeff] = int2float(raw);
                num_zeros = 0;
            } else {
                ci->coeffs[cur_coeff] = 0;
                rl_mode |= (++num_zeros > (s->subframe_len >> 8));
            }
            ++cur_coeff;
        }
    }

    if (cur_coeff < s->subframe_len) {
        std::memset(&ci->coeffs[cur_coeff], 0,
                    sizeof(float) * std::size_t(s->subframe_len - cur_coeff));

        return run_level_decode(s, vlc, level, run, 1,
                                ci->coeffs, cur_coeff, s->subframe_len,
                                s->subframe_len, s->esc_len, 0);
    }
    return 0;
}

int decode_scale_factors(WmaProState* s) {
    for (int i = 0; i < s->channels_for_cur_subframe; ++i) {
        const int c = s->channel_indexes_for_cur_subframe[i];
        ProChannel& ch = s->channel[c];
        ch.scale_factors = ch.saved_scale_factors[!ch.scale_factor_idx];
        int* sf_end = ch.scale_factors + s->num_bands;

        if (ch.reuse_sf) {
            const int8_t* sf_offsets = s->sf_offsets[s->table_idx][ch.table_idx];
            for (int b = 0; b < s->num_bands; ++b)
                ch.scale_factors[b] =
                    ch.saved_scale_factors[ch.scale_factor_idx][*sf_offsets++];
        }

        if (!ch.cur_subframe || s->gb.read_1()) {
            if (!ch.reuse_sf) {
                ch.scale_factor_step = int8_t(s->gb.read(2) + 1);
                int val = 45 / ch.scale_factor_step;
                for (int* sf = ch.scale_factors; sf < sf_end; ++sf) {
                    val += pro_tables().sf_vlc.get(s->gb, SCALEMAXDEPTH);
                    *sf = val;
                }
            } else {
                for (int j = 0; j < s->num_bands; ++j) {
                    const int idx = pro_tables().sf_rl_vlc.get(s->gb, SCALERLMAXDEPTH);
                    int skip, val, sign;
                    if (!idx) {
                        const uint32_t code = s->gb.read(14);
                        val  = int(code >> 6);
                        sign = int(code & 1) - 1;
                        skip = int((code & 0x3f) >> 1);
                    } else if (idx == 1) {
                        break;
                    } else {
                        skip = scale_rl_run[idx];
                        val  = scale_rl_level[idx];
                        sign = int(s->gb.read_1()) - 1;
                    }
                    j += skip;
                    if (j >= s->num_bands) return -1;
                    ch.scale_factors[j] += (val ^ sign) - sign;
                }
            }
            ch.scale_factor_idx = !ch.scale_factor_idx;
            ch.table_idx = s->table_idx;
            ch.reuse_sf  = 1;
        }

        ch.max_scale_factor = ch.scale_factors[0];
        for (int* sf = ch.scale_factors + 1; sf < sf_end; ++sf)
            ch.max_scale_factor = std::max(ch.max_scale_factor, *sf);
    }
    return 0;
}

void inverse_channel_transform(WmaProState* s) {
    for (int i = 0; i < s->num_chgroups; ++i) {
        if (!s->chgroup[i].transform) continue;
        float data[kWmaProMaxChannels];
        const int num_channels = s->chgroup[i].num_channels;
        float** ch_data = s->chgroup[i].channel_data;
        float** ch_end  = ch_data + num_channels;
        const int8_t* tb = s->chgroup[i].transform_band;
        int16_t* sfb;
        for (sfb = s->cur_sfb_offsets;
             sfb < s->cur_sfb_offsets + s->num_bands; ++sfb) {
            if (*tb++ == 1) {
                for (int y = sfb[0]; y < std::min<int>(sfb[1], s->subframe_len); ++y) {
                    const float* mat = s->chgroup[i].decorrelation_matrix;
                    const float* data_end = data + num_channels;
                    float* dp = data;
                    for (float** ch = ch_data; ch < ch_end; ++ch) *dp++ = (*ch)[y];
                    for (float** ch = ch_data; ch < ch_end; ++ch) {
                        float sum = 0;
                        dp = data;
                        while (dp < data_end) sum += *dp++ * *mat++;
                        (*ch)[y] = sum;
                    }
                }
            } else if (s->nb_channels == 2) {
                const int len = std::min<int>(sfb[1], s->subframe_len) - sfb[0];
                vector_fmul_scalar(ch_data[0] + sfb[0], ch_data[0] + sfb[0], 181.0f / 128.0f, len);
                vector_fmul_scalar(ch_data[1] + sfb[0], ch_data[1] + sfb[0], 181.0f / 128.0f, len);
            }
        }
    }
}

void wmapro_window(WmaProState* s) {
    for (int i = 0; i < s->channels_for_cur_subframe; ++i) {
        const int c = s->channel_indexes_for_cur_subframe[i];
        ProChannel& ch = s->channel[c];
        int winlen = ch.prev_block_len;
        float* start = ch.coeffs - (winlen >> 1);
        if (s->subframe_len < winlen) {
            start += (winlen - s->subframe_len) >> 1;
            winlen = s->subframe_len;
        }
        const int win_idx = av_log2(winlen) - WMAPRO_BLOCK_MIN_BITS;
        if (win_idx < 0 || win_idx >= WMAPRO_BLOCK_SIZES) continue;
        const float* window = s->windows[win_idx];
        if (!window) continue;
        const float* buf_lo = ch.out.data();
        const float* buf_hi = buf_lo + ch.out.size();
        if (start < buf_lo || start + winlen > buf_hi) continue;
        winlen >>= 1;
        vector_fmul_window(start, start, start + winlen, window, winlen);
        ch.prev_block_len = int16_t(s->subframe_len);
    }
}

int decode_subframe(WmaProState* s) {
    int offset = s->samples_per_frame;
    int subframe_len = s->samples_per_frame;
    int total_samples   = s->samples_per_frame * s->nb_channels;
    int transmit_coeffs = 0;
    int cur_subwoofer_cutoff;
    const int subframe_log_start_bits = s->gb.tell();

    s->subframe_offset = s->gb.tell();

    for (int i = 0; i < s->nb_channels; ++i) {
        s->channel[i].grouped = 0;
        if (offset > s->channel[i].decoded_samples) {
            offset = s->channel[i].decoded_samples;
            subframe_len = s->channel[i].subframe_len[s->channel[i].cur_subframe];
        }
    }

    s->channels_for_cur_subframe = 0;
    for (int i = 0; i < s->nb_channels; ++i) {
        const int cur_subframe = s->channel[i].cur_subframe;
        total_samples -= s->channel[i].decoded_samples;
        if (offset == s->channel[i].decoded_samples &&
            subframe_len == s->channel[i].subframe_len[cur_subframe]) {
            total_samples -= s->channel[i].subframe_len[cur_subframe];
            s->channel[i].decoded_samples += s->channel[i].subframe_len[cur_subframe];
            s->channel_indexes_for_cur_subframe[s->channels_for_cur_subframe] = int8_t(i);
            ++s->channels_for_cur_subframe;
        }
    }
    if (!total_samples) s->parsed_all_subframes = 1;
    if (subframe_len <= 0 || subframe_len > s->samples_per_frame) return -1;
    {
        const int log2_idx = av_log2(s->samples_per_frame / subframe_len);
        if (log2_idx < 0 || log2_idx >= WMAPRO_BLOCK_SIZES) return -1;
        s->table_idx = uint8_t(log2_idx);
    }
    s->num_bands       = s->num_sfb[s->table_idx];
    s->cur_sfb_offsets = s->sfb_offsets[s->table_idx];
    cur_subwoofer_cutoff = s->subwoofer_cutoffs[s->table_idx];

    offset += s->samples_per_frame >> 1;
    const int out_capacity = int(s->channel[0].out.size());
    if (offset < 0 || offset + subframe_len > out_capacity) return -1;
    for (int i = 0; i < s->channels_for_cur_subframe; ++i) {
        const int c = s->channel_indexes_for_cur_subframe[i];
        s->channel[c].coeffs = &s->channel[c].out[offset];
    }

    s->subframe_len = int16_t(subframe_len);
    s->esc_len = int8_t(av_log2(s->subframe_len - 1) + 1);

    if (s->gb.read_1()) {
        int num_fill_bits = int(s->gb.read(2));
        if (!num_fill_bits) {
            const int len = int(s->gb.read(4));
            num_fill_bits = (len ? int(s->gb.read(len)) : 0) + 1;
        }
        if (num_fill_bits >= 0) {
            if (s->gb.tell() + num_fill_bits > s->num_saved_bits) return -1;
            s->gb.skip(num_fill_bits);
        }
    }

    if (s->gb.read_1()) {
        return -1;
    }
    if (decode_channel_transform(s) < 0) {
        return -1;
    }
    for (int i = 0; i < s->channels_for_cur_subframe; ++i) {
        const int c = s->channel_indexes_for_cur_subframe[i];
        s->channel[c].transmit_coefs = uint8_t(s->gb.read_1());
        if (s->channel[c].transmit_coefs) transmit_coeffs = 1;
    }
    if (transmit_coeffs) {
        int step;
        int quant_step = 90 * s->bits_per_sample >> 4;
        s->transmit_num_vec_coeffs = int8_t(s->gb.read_1());
        if (s->transmit_num_vec_coeffs) {
            const int num_bits = av_log2((s->subframe_len + 3) / 4) + 1;
            for (int i = 0; i < s->channels_for_cur_subframe; ++i) {
                const int c = s->channel_indexes_for_cur_subframe[i];
                const int num_vec_coeffs = int(s->gb.read(num_bits)) << 2;
                if (num_vec_coeffs > s->subframe_len) {
                    return -1;
                }
                s->channel[c].num_vec_coeffs = uint16_t(num_vec_coeffs);
            }
        } else {
            for (int i = 0; i < s->channels_for_cur_subframe; ++i) {
                const int c = s->channel_indexes_for_cur_subframe[i];
                s->channel[c].num_vec_coeffs = uint16_t(s->subframe_len);
            }
        }
        step = s->gb.read_signed(6);
        quant_step += step;
        if (step == -32 || step == 31) {
            const int sign = (step == 31) - 1;
            int quant = 0;
            while (s->gb.tell() + 5 < s->num_saved_bits) {
                step = int(s->gb.read(5));
                if (step != 31) break;
                quant += 31;
            }
            quant_step += ((quant + step) ^ sign) - sign;
        }
        if (s->channels_for_cur_subframe == 1) {
            s->channel[s->channel_indexes_for_cur_subframe[0]].quant_step = quant_step;
        } else {
            const int modifier_len = int(s->gb.read(3));
            for (int i = 0; i < s->channels_for_cur_subframe; ++i) {
                const int c = s->channel_indexes_for_cur_subframe[i];
                s->channel[c].quant_step = quant_step;
                if (s->gb.read_1()) {
                    if (modifier_len)
                        s->channel[c].quant_step += int(s->gb.read(modifier_len)) + 1;
                    else
                        ++s->channel[c].quant_step;
                }
            }
        }
        if (decode_scale_factors(s) < 0) {
            return -1;
        }
    }

    for (int i = 0; i < s->channels_for_cur_subframe; ++i) {
        const int c = s->channel_indexes_for_cur_subframe[i];
        if (s->channel[c].transmit_coefs && s->gb.tell() < s->num_saved_bits) {
            const int crc = decode_coeffs(s, c);
        } else {
            std::memset(s->channel[c].coeffs, 0, sizeof(float) * std::size_t(subframe_len));
        }
    }

    if (transmit_coeffs) {
        const int tx_idx = av_log2(subframe_len) - WMAPRO_BLOCK_MIN_BITS;
        if (tx_idx < 0 || tx_idx >= WMAPRO_BLOCK_SIZES) return -1;
        Mdct& tx = s->tx[tx_idx];
        inverse_channel_transform(s);
        for (int i = 0; i < s->channels_for_cur_subframe; ++i) {
            const int c = s->channel_indexes_for_cur_subframe[i];
            const int* sf = s->channel[c].scale_factors;
            if (c == s->lfe_channel) {
                std::memset(&s->tmp[cur_subwoofer_cutoff], 0,
                            sizeof(float) * std::size_t(subframe_len - cur_subwoofer_cutoff));
            }
            for (int b = 0; b < s->num_bands; ++b) {
                const int end = std::min<int>(s->cur_sfb_offsets[b + 1], s->subframe_len);
                const int exp = s->channel[c].quant_step -
                                (s->channel[c].max_scale_factor - *sf++) *
                                s->channel[c].scale_factor_step;
                const float quant = ff_exp10(double(exp) / 20.0);
                const int start = s->cur_sfb_offsets[b];
                vector_fmul_scalar(&s->tmp[start], s->channel[c].coeffs + start, quant, end - start);
            }
            tx.run(s->channel[c].coeffs, s->tmp.data());
        }
    }

    wmapro_window(s);

    for (int i = 0; i < s->channels_for_cur_subframe; ++i) {
        const int c = s->channel_indexes_for_cur_subframe[i];
        if (s->channel[c].cur_subframe >= s->channel[c].num_subframes) {
            return -1;
        }
        ++s->channel[c].cur_subframe;
    }
    return 0;
}

int decode_frame(WmaProState* s, Frame& frame, bool& got_frame) {
    GetBits& gb = s->gb;
    int len = 0;
    if (s->len_prefix) len = int(gb.read(s->log2_frame_size));
    if (decode_tilehdr(s) != 0) {
        s->packet_loss = 1;
        return 0;
    }
    if (s->nb_channels > 1 && gb.read_1()) {
        if (gb.read_1()) {
            for (int i = 0; i < s->nb_channels * s->nb_channels; ++i) gb.skip(4);
        }
    }
    if (s->dynamic_range_compression) s->drc_gain = uint8_t(gb.read(8));
    if (gb.read_1()) {
        if (gb.read_1()) s->trim_start = uint16_t(gb.read(av_log2(s->samples_per_frame * 2)));
        if (gb.read_1()) s->trim_end   = uint16_t(gb.read(av_log2(s->samples_per_frame * 2)));
    } else {
        s->trim_start = s->trim_end = 0;
    }
    s->parsed_all_subframes = 0;
    for (int i = 0; i < s->nb_channels; ++i) {
        s->channel[i].decoded_samples = 0;
        s->channel[i].cur_subframe = 0;
        s->channel[i].reuse_sf = 0;
    }
    int subf_iter = 0;
    while (!s->parsed_all_subframes) {
        if (decode_subframe(s) < 0) {
            s->packet_loss = 1;
            return 0;
        }
        if (++subf_iter > 64) {
            s->packet_loss = 1;
            return 0;
        }
    }

    if (frame.nb_channels < s->nb_channels) frame.allocate(s->nb_channels, s->samples_per_frame);
    for (int i = 0; i < s->nb_channels; ++i) {
        if (int(frame.planes[i].size()) < s->samples_per_frame)
            frame.planes[i].resize(s->samples_per_frame);
        std::memcpy(frame.planes[i].data(), s->channel[i].out.data(),
                    sizeof(float) * std::size_t(s->samples_per_frame));
    }
    for (int i = 0; i < s->nb_channels; ++i) {
        std::memmove(&s->channel[i].out[0],
                     &s->channel[i].out[s->samples_per_frame],
                     (sizeof(float) * std::size_t(s->samples_per_frame)) >> 1);
    }
    frame.nb_samples = s->samples_per_frame;
                    if (v < mn) mn = v;
                    if (v > mx) mx = v;
                    const float av = v < 0 ? -v : v;
                    if (av > abs_max) abs_max = av;
                    if (v != 0.0f) ++nonzero;
                }
            }
        }
    }

    if (s->skip_frame) {
        s->skip_frame = 0;
        got_frame = false;
    } else {
        got_frame = true;
    }

    if (s->len_prefix) {
        const int actual = gb.tell() - s->frame_offset;
        const int expected = len - 2;
        if (actual != expected) {
            const int target = s->frame_offset + len - 1;
            if (target > gb.tell()) gb.skip(target - gb.tell());
            else                    s->packet_loss = 1;
        } else {
            gb.skip(len - actual - 1);
        }
    } else {
        while (gb.tell() < s->num_saved_bits && gb.read_1() == 0) {}
    }
    int more_frames = int(gb.read_1());
    ++s->frame_num;
    return more_frames;
}

int remaining_bits(WmaProState* s, GetBits& gb) {
    return s->buf_bit_size - gb.tell();
}

void copy_bits_to_pb(WmaProState* s, GetBits& gb, int len) {
    while (len >= 24) {
        s->pb.write(24, gb.read(24));
        len -= 24;
    }
    while (len >= 8) {
        s->pb.write(8, gb.read(8));
        len -= 8;
    }
    if (len > 0) s->pb.write(len, gb.read(len));
}

void save_bits(WmaProState* s, GetBits& gb, int len, int append) {
    int buflen;
    if (!append) {
        s->frame_offset   = gb.tell() & 7;
        s->num_saved_bits = s->frame_offset;
        s->pb.init(s->frame_data.data(), MAX_FRAMESIZE);
        buflen = (s->num_saved_bits + len + 7) >> 3;
    } else {
        buflen = (s->pb.tell() + len + 7) >> 3;
    }
    if (len <= 0 || buflen > MAX_FRAMESIZE) {
        s->packet_loss = 1;
        return;
    }
    s->num_saved_bits += len;
    if (!append) {

        s->pb.write(s->frame_offset, 0);
        copy_bits_to_pb(s, gb, len);
    } else {
        int align = 8 - (gb.tell() & 7);
        align = std::min(align, len);
        s->pb.write(align, gb.read(align));
        len -= align;
        copy_bits_to_pb(s, gb, len);
    }
    s->pb.flush();
    s->gb.init(s->frame_data.data(), (s->num_saved_bits + 7) >> 3);
    s->gb.skip(s->frame_offset);
}

int decode_packet_stream(WmaProState* s, const Packet& avpkt,
                         Frame& frame, bool& got_frame) {
    GetBits& gb = s->pgb;
    const uint8_t* buf = avpkt.data;
    int buf_size = avpkt.size;
    got_frame = false;
    const bool entering_new = (s->packet_done || s->packet_loss);
    if (!buf_size) {
        s->packet_done = 0;
        if (s->eof_done) return 0;
        if (frame.nb_channels < s->nb_channels) frame.allocate(s->nb_channels, s->samples_per_frame);
        for (int i = 0; i < s->nb_channels; ++i) {
            if (int(frame.planes[i].size()) < s->samples_per_frame)
                frame.planes[i].resize(s->samples_per_frame);
            std::memset(frame.planes[i].data(), 0,
                        sizeof(float) * std::size_t(s->samples_per_frame));
            std::memcpy(frame.planes[i].data(), s->channel[i].out.data(),
                        (sizeof(float) * std::size_t(s->samples_per_frame)) >> 1);
        }
        s->eof_done = 1;
        s->packet_done = 1;
        got_frame = true;
        return 0;
    }

    if (s->packet_done || s->packet_loss) {
        s->packet_done = 0;
        if (s->avctx->codec_id == CodecId::WmaPro && buf_size < s->avctx->block_align) {
            s->packet_loss = 1;
            return -1;
        }
        if (s->avctx->codec_id == CodecId::WmaPro) {
            s->next_packet_start = buf_size - s->avctx->block_align;
            buf_size = s->avctx->block_align;
        } else {
            s->next_packet_start = buf_size - std::min(buf_size, s->avctx->block_align);
            buf_size = std::min(buf_size, s->avctx->block_align);
        }
        s->buf_bit_size = buf_size << 3;
        gb.init(buf, buf_size);

        int packet_sequence_number;
        int dbg_num_frames = 0;
        if (s->avctx->codec_id != CodecId::Xma2) {
            packet_sequence_number = int(gb.read(4));
            gb.skip(2);
        } else {
            dbg_num_frames = int(gb.read(6));
            packet_sequence_number = 0;
        }
        const int num_bits_prev_frame = int(gb.read(s->log2_frame_size));
        if (s->avctx->codec_id != CodecId::WmaPro) {
            gb.skip(3);
            s->skip_packets = uint8_t(gb.read(8));
        }
        if (s->avctx->codec_id == CodecId::WmaPro && !s->packet_loss &&
            ((s->packet_sequence_number + 1) & 0xF) != packet_sequence_number) {
            s->packet_loss = 1;
        }
        s->packet_sequence_number = uint8_t(packet_sequence_number);

        if (num_bits_prev_frame > 0) {
            int remaining_packet_bits = s->buf_bit_size - gb.tell();
            int nbpf = num_bits_prev_frame;
            if (nbpf >= remaining_packet_bits) {
                nbpf = remaining_packet_bits;
                s->packet_done = 1;
            }
            save_bits(s, gb, nbpf, 1);
            if (!s->packet_loss) decode_frame(s, frame, got_frame);
        }
        if (s->packet_loss) {
            s->num_saved_bits = 0;
            s->packet_loss    = 0;
        }
    } else {
        if (avpkt.size < s->next_packet_start) {
            s->packet_loss = 1;
            return -1;
        }
        s->buf_bit_size = (avpkt.size - s->next_packet_start) << 3;
        gb.init(avpkt.data, avpkt.size - s->next_packet_start);
        gb.skip(s->packet_offset);
        int frame_size;
        if (s->len_prefix && remaining_bits(s, gb) > s->log2_frame_size &&
            (frame_size = int(gb.show(s->log2_frame_size))) &&
            frame_size <= remaining_bits(s, gb)) {
            save_bits(s, gb, frame_size, 0);
            if (!s->packet_loss)
                s->packet_done = uint8_t(!decode_frame(s, frame, got_frame));
        } else if (!s->len_prefix && s->num_saved_bits > s->gb.tell()) {
            s->packet_done = uint8_t(!decode_frame(s, frame, got_frame));
        } else {
            s->packet_done = 1;
        }
    }

    if (remaining_bits(s, gb) < 0) s->packet_loss = 1;
    if (s->packet_done && !s->packet_loss && remaining_bits(s, gb) > 0)
        save_bits(s, gb, remaining_bits(s, gb), 0);
    s->packet_offset = uint8_t(gb.tell() & 7);
    if (s->packet_loss) return -1;
    return gb.tell() >> 3;
}

}

struct WmaProDecoder::Impl {
    CodecContext* ctx = nullptr;
    XmaState x;
    int      pending_channels = 0;
    bool     is_xma = false;

    int init(CodecContext& c) {
        ctx = &c;
        is_xma = (c.codec_id == CodecId::Xma1) || (c.codec_id == CodecId::Xma2);

        if (is_xma) {
            c.block_align = 2048;
            if (c.ch_layout.nb_channels <= 0 || c.extradata_size == 0) return -1;
            if (c.codec_id == CodecId::Xma2 && c.extradata_size == 34) {
                const uint32_t channel_mask = read_u32le(c.extradata.data() + 2);
                if (channel_mask) channel_layout_from_mask(c.ch_layout, channel_mask);
                else              c.ch_layout.order = ChannelOrder::Unspec;
                x.num_streams = read_u16le(c.extradata.data());
            } else if (c.codec_id == CodecId::Xma2 && c.extradata_size >= 2) {
                x.num_streams = c.extradata[1];
            } else if (c.codec_id == CodecId::Xma1 && c.extradata_size >= 4) {
                x.num_streams = c.extradata[4];
            } else {
                return -1;
            }
            if (c.ch_layout.nb_channels > kXmaMaxChannels ||
                x.num_streams > kWmaProMaxStreams || x.num_streams <= 0) return -1;
            int start_channels = 0;
            for (int i = 0; i < x.num_streams; ++i) {
                if (decode_init_stream(&x.xma[i], &c, i) < 0) return -1;
                x.start_channel[i] = start_channels;
                start_channels += x.xma[i].nb_channels;
            }
            pending_channels = c.ch_layout.nb_channels;
            return 0;
        } else {
            if (!c.block_align) return -1;
            return decode_init_stream(&x.xma[0], &c, 0);
        }
    }

    int decode_packet_impl(const Packet& pkt, Frame& out, bool& got_frame) {
        got_frame = false;
        out.clear();
        if (!is_xma) {
            out.allocate(x.xma[0].nb_channels, x.xma[0].samples_per_frame);
            const int ret = decode_packet_stream(&x.xma[0], pkt, out, got_frame);
            if (got_frame) out.sample_rate = ctx->sample_rate;
            return ret;
        }

        bool stream_got = false;
        int  stream_ret = 0;
        if (!x.xma[x.current_stream].eof_done) {
            x.frames[x.current_stream].allocate(x.xma[x.current_stream].nb_channels,
                                               x.xma[x.current_stream].samples_per_frame);
            stream_ret = decode_packet_stream(&x.xma[x.current_stream], pkt,
                                              x.frames[x.current_stream], stream_got);
        }

        bool eof = pkt.size == 0;
        if (eof) {
            for (int i = 0; i < x.num_streams; ++i) {
                if (!x.xma[i].eof_done) {
                    Packet empty{};
                    bool g = false;
                    decode_packet_stream(&x.xma[i], empty, x.frames[i], g);
                    stream_got |= g;
                }
                eof = eof && (x.xma[i].eof_done != 0);
            }
        }

        if (stream_got) {
            const int n = x.frames[x.current_stream].nb_samples;
            x.samples[0][x.current_stream].write(
                x.frames[x.current_stream].planes[0].data(), n);
            if (x.xma[x.current_stream].nb_channels > 1) {
                x.samples[1][x.current_stream].write(
                    x.frames[x.current_stream].planes[1].data(), n);
            }
        }

        if (x.xma[x.current_stream].packet_done || x.xma[x.current_stream].packet_loss) {
            int nb_samples = INT_MAX;
            if (x.xma[x.current_stream].skip_packets != 0) {
                int min0 = x.xma[0].skip_packets;
                int min1 = 0;
                for (int i = 1; i < x.num_streams; ++i) {
                    if (x.xma[i].skip_packets < min0) { min0 = x.xma[i].skip_packets; min1 = i; }
                }
                x.current_stream = min1;
            }
            for (int i = 0; i < x.num_streams; ++i) {
                x.xma[i].skip_packets = uint8_t(std::max(0, int(x.xma[i].skip_packets) - 1));
                nb_samples = std::min(nb_samples, x.samples[0][i].size());
            }
            if (!eof && pkt.size) nb_samples -= std::min(nb_samples, 4096);
            if ((nb_samples > 0 || eof || !pkt.size) && !x.flushed) {
                out.allocate(ctx->ch_layout.nb_channels, nb_samples);
                for (int i = 0; i < x.num_streams; ++i) {
                    const int start_ch = x.start_channel[i];
                    x.samples[0][i].read(out.planes[start_ch + 0].data(), nb_samples);
                    if (x.xma[i].nb_channels > 1)
                        x.samples[1][i].read(out.planes[start_ch + 1].data(), nb_samples);
                }
                out.sample_rate = ctx->sample_rate;
                got_frame = nb_samples > 0;
            }
        }
        return stream_ret;
    }

    int flush_impl(Frame& out, bool& got_frame) {
        out.clear();
        got_frame = false;
        if (!is_xma) return 0;
        int nb_samples = INT_MAX;
        for (int i = 0; i < x.num_streams; ++i)
            nb_samples = std::min(nb_samples, x.samples[0][i].size());
        if (nb_samples <= 0) return 0;
        out.allocate(ctx->ch_layout.nb_channels, nb_samples);
        for (int i = 0; i < x.num_streams; ++i) {
            const int start_ch = x.start_channel[i];
            x.samples[0][i].read(out.planes[start_ch + 0].data(), nb_samples);
            if (x.xma[i].nb_channels > 1)
                x.samples[1][i].read(out.planes[start_ch + 1].data(), nb_samples);
        }
        out.sample_rate = ctx->sample_rate;
        got_frame = true;
        return 0;
    }
};

WmaProDecoder::WmaProDecoder() : impl_(std::make_unique<Impl>()) {}
WmaProDecoder::~WmaProDecoder() = default;

int WmaProDecoder::init(CodecContext& ctx) { return impl_->init(ctx); }

int WmaProDecoder::decode_packet(const Packet& pkt, Frame& out, bool& got_frame) {
    return impl_->decode_packet_impl(pkt, out, got_frame);
}

int WmaProDecoder::flush(Frame& out, bool& got_frame) {
    return impl_->flush_impl(out, got_frame);
}

bool WmaProDecoder::packet_done() const {
    const auto& st = impl_->x.xma[impl_->is_xma ? impl_->x.current_stream : 0];
    return st.packet_done != 0;
}

bool WmaProDecoder::packet_loss() const {
    const auto& st = impl_->x.xma[impl_->is_xma ? impl_->x.current_stream : 0];
    return st.packet_loss != 0;
}

}
