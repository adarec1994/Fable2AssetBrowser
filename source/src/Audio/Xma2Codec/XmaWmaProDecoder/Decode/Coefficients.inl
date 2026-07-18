int run_level_decode(WmaProState* s, const Vlc& vlc, const float* level_table,
                     const uint16_t* run_table, int version, float* ptr,
                     int offset, int num_coefs, int block_len,
                     int frame_len_bits, int coef_nb_bits) {
    const uint32_t coef_mask = uint32_t(block_len - 1);
    for (; offset < num_coefs; ++offset) {
        const int code = vlc.get(s->gb, 0);
        if (code > 1) {
            offset += run_table[code];
            const int sign = int(s->gb.read_1()) - 1;
            uint32_t lvl_u = float2int(level_table[code]);
            lvl_u ^= unsigned(sign) & 0x80000000u;
            ptr[offset & coef_mask] = int2float(lvl_u);
        } else if (code == 1) {
            break;
        } else {
            int level;
            if (!version) {
                level = int(s->gb.read(coef_nb_bits));
                offset += int(s->gb.read(frame_len_bits));
            } else {
                level = int(wma_get_large_val(s->gb));
                if (s->gb.read_1()) {
                    if (s->gb.read_1()) {
                        if (s->gb.read_1()) return -1;
                        else offset += int(s->gb.read(frame_len_bits)) + 4;
                    } else {
                        offset += int(s->gb.read(2)) + 1;
                    }
                }
            }
            const int sign = int(s->gb.read_1()) - 1;
            ptr[offset & coef_mask] = float((level ^ sign) - sign);
        }
    }
    return offset > num_coefs ? -1 : 0;
}

int decode_coeffs(WmaProState* s, int c) {
    static const uint32_t fval_tab[16] = {
        0x00000000, 0x3f800000, 0x40000000, 0x40400000,
        0x40800000, 0x40a00000, 0x40c00000, 0x40e00000,
        0x41000000, 0x41100000, 0x41200000, 0x41300000,
        0x41400000, 0x41500000, 0x41600000, 0x41700000,
    };
    ProChannel* ci = &s->channel[c];
    int rl_mode = 0, cur_coeff = 0, num_zeros = 0;
    const int vlctable = int(s->gb.read_1());
    const Vlc& vlc = vlctable ? pro_tables().coef1_vlc : pro_tables().coef0_vlc;
    const uint16_t* run   = vlctable ? coef1_run   : coef0_run;
    const float*    level = vlctable ? coef1_level : coef0_level;

    while ((s->transmit_num_vec_coeffs || !rl_mode) &&
           (cur_coeff + 3 < ci->num_vec_coeffs)) {
        uint32_t vals[4];
        unsigned idx = unsigned(pro_tables().vec4_vlc.get(s->gb, VEC4MAXDEPTH));
        if (int(idx) < 0) {
            for (int i = 0; i < 4; i += 2) {
                idx = unsigned(pro_tables().vec2_vlc.get(s->gb, VEC2MAXDEPTH));
                if (int(idx) < 0) {
                    uint32_t v0 = unsigned(pro_tables().vec1_vlc.get(s->gb, VEC1MAXDEPTH));
                    if (v0 == HUFF_VEC1_SIZE - 1) v0 += wma_get_large_val(s->gb);
                    uint32_t v1 = unsigned(pro_tables().vec1_vlc.get(s->gb, VEC1MAXDEPTH));
                    if (v1 == HUFF_VEC1_SIZE - 1) v1 += wma_get_large_val(s->gb);
                    vals[i    ] = float2int(float(v0));
                    vals[i + 1] = float2int(float(v1));
                } else {
                    vals[i    ] = fval_tab[idx >> 4];
                    vals[i + 1] = fval_tab[idx & 0xF];
                }
            }
        } else {
            vals[0] = fval_tab[idx >> 12];
            vals[1] = fval_tab[(idx >> 8) & 0xF];
            vals[2] = fval_tab[(idx >> 4) & 0xF];
            vals[3] = fval_tab[idx & 0xF];
        }
        for (int i = 0; i < 4; ++i) {
            if (vals[i]) {
                const uint32_t sign = unsigned(s->gb.read_1()) - 1u;
                const uint32_t raw  = vals[i] ^ (sign << 31);
                ci->coeffs[cur_coeff] = int2float(raw);
                num_zeros = 0;
            } else {
                ci->coeffs[cur_coeff] = 0;
                rl_mode |= (++num_zeros > (s->subframe_len >> 8));
            }
            ++cur_coeff;
        }
    }

    if (cur_coeff < s->subframe_len) {
        std::memset(&ci->coeffs[cur_coeff], 0,
                    sizeof(float) * std::size_t(s->subframe_len - cur_coeff));

        return run_level_decode(s, vlc, level, run, 1,
                                ci->coeffs, cur_coeff, s->subframe_len,
                                s->subframe_len, s->esc_len, 0);
    }
    return 0;
}

int decode_scale_factors(WmaProState* s) {
    for (int i = 0; i < s->channels_for_cur_subframe; ++i) {
        const int c = s->channel_indexes_for_cur_subframe[i];
        ProChannel& ch = s->channel[c];
        ch.scale_factors = ch.saved_scale_factors[!ch.scale_factor_idx];
        int* sf_end = ch.scale_factors + s->num_bands;

        if (ch.reuse_sf) {
            const int8_t* sf_offsets = s->sf_offsets[s->table_idx][ch.table_idx];
            for (int b = 0; b < s->num_bands; ++b)
                ch.scale_factors[b] =
                    ch.saved_scale_factors[ch.scale_factor_idx][*sf_offsets++];
        }

        if (!ch.cur_subframe || s->gb.read_1()) {
            if (!ch.reuse_sf) {
                ch.scale_factor_step = int8_t(s->gb.read(2) + 1);
                int val = 45 / ch.scale_factor_step;
                for (int* sf = ch.scale_factors; sf < sf_end; ++sf) {
                    val += pro_tables().sf_vlc.get(s->gb, SCALEMAXDEPTH);
                    *sf = val;
                }
            } else {
                for (int j = 0; j < s->num_bands; ++j) {
                    const int idx = pro_tables().sf_rl_vlc.get(s->gb, SCALERLMAXDEPTH);
                    int skip, val, sign;
                    if (!idx) {
                        const uint32_t code = s->gb.read(14);
                        val  = int(code >> 6);
                        sign = int(code & 1) - 1;
                        skip = int((code & 0x3f) >> 1);
                    } else if (idx == 1) {
                        break;
                    } else {
                        skip = scale_rl_run[idx];
                        val  = scale_rl_level[idx];
                        sign = int(s->gb.read_1()) - 1;
                    }
                    j += skip;
                    if (j >= s->num_bands) return -1;
                    ch.scale_factors[j] += (val ^ sign) - sign;
                }
            }
            ch.scale_factor_idx = !ch.scale_factor_idx;
            ch.table_idx = s->table_idx;
            ch.reuse_sf  = 1;
        }

        ch.max_scale_factor = ch.scale_factors[0];
        for (int* sf = ch.scale_factors + 1; sf < sf_end; ++sf)
            ch.max_scale_factor = std::max(ch.max_scale_factor, *sf);
    }
    return 0;
}
