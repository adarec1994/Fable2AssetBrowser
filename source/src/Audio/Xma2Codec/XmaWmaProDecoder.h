#pragma once

#include <memory>
#include "XmaSupport.h"

namespace Xma {

constexpr int kWmaProMaxChannels       = 8;
constexpr int kWmaProMaxStreams        = 8;
constexpr int kWmaProChannelsPerStream = 2;
constexpr int kXmaMaxChannels          = kWmaProMaxStreams * kWmaProChannelsPerStream;

class WmaProDecoder {
public:
    WmaProDecoder();
    ~WmaProDecoder();
    WmaProDecoder(const WmaProDecoder&) = delete;
    WmaProDecoder& operator=(const WmaProDecoder&) = delete;

    int init(CodecContext& ctx);
    int decode_packet(const Packet& pkt, Frame& out, bool& got_frame);
    int flush(Frame& out, bool& got_frame);

    bool packet_done() const;
    bool packet_loss() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
