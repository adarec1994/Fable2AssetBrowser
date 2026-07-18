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
