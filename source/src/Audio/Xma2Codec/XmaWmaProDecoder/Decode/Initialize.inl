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
