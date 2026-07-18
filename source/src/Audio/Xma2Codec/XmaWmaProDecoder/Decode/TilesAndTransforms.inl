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
