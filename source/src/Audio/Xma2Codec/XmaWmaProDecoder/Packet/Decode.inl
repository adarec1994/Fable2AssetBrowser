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
