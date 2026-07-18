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
