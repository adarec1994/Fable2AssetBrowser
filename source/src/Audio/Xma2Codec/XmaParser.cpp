#include "XmaParser.h"

#include <cstring>

namespace Xma {

XmaParser::Result XmaParser::parse(const uint8_t* buf, std::size_t buf_size) {
    Result r;
    if (buf_size && (buf_size % 2048) == 0) {
        const int nb_packets = int(buf_size / 2048);
        int duration = 0;
        for (int p = 0; p < nb_packets; ++p) {
            if (skip_packets_ == 0) {
                duration       += buf[p * 2048] * 128;
                skip_packets_   = buf[p * 2048 + 3] + 1;
            }
            --skip_packets_;
        }
        r.duration  = duration;
        r.key_frame = duration != 0;
    }
    return r;
}

namespace {

inline void wr_u16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
}
inline void wr_u32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}
inline uint32_t read_u32be(const uint8_t* b) {
    return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) |
           (uint32_t(b[2]) <<  8) |  uint32_t(b[3]);
}

uint32_t default_channel_mask(int channels) {
    switch (channels) {
        case 1: return 0x00000004;
        case 2: return 0x00000003;
        case 4: return 0x00000033;
        case 6: return 0x0000003F;
        case 8: return 0x000000FF;
        default:
            return channels >= 32 ? 0xFFFFFFFFu
                                  : (uint32_t(1) << channels) - 1u;
    }
}

}  // namespace

bool xma_synthesize_extradata(const XmaSynthInput& in,
                              std::array<uint8_t, 34>& out) {
    if (in.channels <= 0) return false;

    uint16_t num_streams = uint16_t((in.channels + 1) / 2);
    if (num_streams == 0) num_streams = 1;
    uint32_t channel_mask         = default_channel_mask(in.channels);
    uint32_t samples_encoded      = 0;
    uint32_t bytes_per_block      = 0x10000;
    uint32_t block_count          = 0;

    if (in.xma2_chunk && in.xma2_size >= 36) {
        const uint8_t* xp = in.xma2_chunk;
        const uint8_t num_streams_ch = xp[1];
        if (num_streams_ch >= 1 && num_streams_ch <= 8) {
            num_streams = uint16_t(num_streams_ch);
        }
        samples_encoded = read_u32be(xp + 16);
        if (in.xma2_size >= 28) {
            const uint32_t maybe_bpb = read_u32be(xp + 24);
            if (maybe_bpb >= 0x800 && maybe_bpb <= 0x100000) {
                bytes_per_block = maybe_bpb;
            }
        }
    }

    if (block_count == 0 && bytes_per_block > 0) {
        block_count = uint32_t((in.data_size + bytes_per_block - 1) /
                               bytes_per_block);
        if (block_count == 0) block_count = 1;
    }
    if (samples_encoded == 0) {
        samples_encoded = uint32_t(uint64_t(block_count) * 512u * 32u);
        if (samples_encoded == 0) samples_encoded = 0x7FFFFFFFu;
    }

    std::memset(out.data(), 0, out.size());
    wr_u16(out.data() +  0, num_streams);
    wr_u32(out.data() +  2, channel_mask);
    wr_u32(out.data() +  6, samples_encoded);
    wr_u32(out.data() + 10, bytes_per_block);
    wr_u32(out.data() + 14, 0);
    wr_u32(out.data() + 18, samples_encoded);
    wr_u32(out.data() + 22, 0);
    wr_u32(out.data() + 26, 0);
    out[30] = 0;
    out[31] = 4;
    wr_u16(out.data() + 32, uint16_t(block_count));
    return true;
}

}  // namespace Xma
