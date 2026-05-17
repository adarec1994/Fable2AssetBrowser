#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace Xma {

class XmaParser {
public:
    struct Result {
        int duration = 0;
        bool key_frame = false;
    };
    Result parse(const uint8_t* buf, std::size_t buf_size);

private:
    int skip_packets_ = 0;
};

struct XmaSynthInput {
    int      channels = 0;
    int      sample_rate = 0;
    int      block_align = 0;
    uint16_t fmt_format_tag = 0;
    const uint8_t* xma2_chunk = nullptr;
    std::size_t    xma2_size  = 0;
    std::size_t    data_size  = 0;
};

bool xma_synthesize_extradata(const XmaSynthInput& in,
                              std::array<uint8_t, 34>& out);

}
