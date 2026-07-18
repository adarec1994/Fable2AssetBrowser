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
