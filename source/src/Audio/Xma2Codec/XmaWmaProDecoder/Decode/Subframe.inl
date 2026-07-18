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
