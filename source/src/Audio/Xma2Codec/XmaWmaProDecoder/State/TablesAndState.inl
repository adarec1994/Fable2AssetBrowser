constexpr int MAX_SUBFRAMES = 32;
constexpr int MAX_BANDS     = 29;
constexpr int MAX_FRAMESIZE = 32768;

constexpr int WMAPRO_BLOCK_MIN_BITS = 6;
constexpr int WMAPRO_BLOCK_MAX_BITS = 13;
constexpr int WMAPRO_BLOCK_MIN_SIZE = 1 << WMAPRO_BLOCK_MIN_BITS;
constexpr int WMAPRO_BLOCK_MAX_SIZE = 1 << WMAPRO_BLOCK_MAX_BITS;
constexpr int WMAPRO_BLOCK_SIZES    = WMAPRO_BLOCK_MAX_BITS - WMAPRO_BLOCK_MIN_BITS + 1;

constexpr int VLCBITS       = 9;
constexpr int SCALEVLCBITS  = 8;
constexpr int VEC4MAXDEPTH  = (HUFF_VEC4_MAXBITS  + VLCBITS - 1) / VLCBITS;
constexpr int VEC2MAXDEPTH  = (HUFF_VEC2_MAXBITS  + VLCBITS - 1) / VLCBITS;
constexpr int VEC1MAXDEPTH  = (HUFF_VEC1_MAXBITS  + VLCBITS - 1) / VLCBITS;
constexpr int SCALEMAXDEPTH = (HUFF_SCALE_MAXBITS + SCALEVLCBITS - 1) / SCALEVLCBITS;
constexpr int SCALERLMAXDEPTH = (HUFF_SCALE_RL_MAXBITS + VLCBITS - 1) / VLCBITS;

constexpr int AV_INPUT_BUFFER_PADDING_SIZE = 64;

struct ProTables {
    Vlc sf_vlc;
    Vlc sf_rl_vlc;
    Vlc vec4_vlc;
    Vlc vec2_vlc;
    Vlc vec1_vlc;
    Vlc coef0_vlc;
    Vlc coef1_vlc;
    std::array<float, 33> sin64{};
};

ProTables& pro_tables() {
    static ProTables t;
    return t;
}

void init_static_once() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        auto& t = pro_tables();
        t.sf_vlc.init_from_lengths(SCALEVLCBITS, HUFF_SCALE_SIZE,
                                   reinterpret_cast<const int8_t*>(&scale_table[0][1]), 2,
                                   &scale_table[0][0], 2, 1, -60, 0);
        t.sf_rl_vlc.init_from_lengths(VLCBITS, HUFF_SCALE_RL_SIZE,
                                      reinterpret_cast<const int8_t*>(&scale_rl_table[0][1]), 2,
                                      &scale_rl_table[0][0], 2, 1, 0, 0);
        t.vec4_vlc.init_from_lengths(VLCBITS, HUFF_VEC4_SIZE,
                                     reinterpret_cast<const int8_t*>(vec4_lens), 1,
                                     vec4_syms, 2, 2, -1, 0);
        t.vec2_vlc.init_from_lengths(VLCBITS, HUFF_VEC2_SIZE,
                                     reinterpret_cast<const int8_t*>(&vec2_table[0][1]), 2,
                                     &vec2_table[0][0], 2, 1, -1, 0);
        t.vec1_vlc.init_from_lengths(VLCBITS, HUFF_VEC1_SIZE,
                                     reinterpret_cast<const int8_t*>(&vec1_table[0][1]), 2,
                                     &vec1_table[0][0], 2, 1, 0, 0);
        t.coef0_vlc.init_from_lengths(VLCBITS, HUFF_COEF0_SIZE,
                                      reinterpret_cast<const int8_t*>(coef0_lens), 1,
                                      coef0_syms, 2, 2, 0, 0);
        t.coef1_vlc.init_from_lengths(VLCBITS, HUFF_COEF1_SIZE,
                                      reinterpret_cast<const int8_t*>(&coef1_table[0][1]), 2,
                                      &coef1_table[0][0], 2, 1, 0, 0);
        for (int i = 0; i < 33; ++i)
            t.sin64[i] = float(std::sin(double(i) * 3.14159265358979323846 / 64.0));
        for (int i = WMAPRO_BLOCK_MIN_BITS; i <= WMAPRO_BLOCK_MAX_BITS; ++i)
            wma_init_sine_window(i);
    });
}

struct ProChannel {
    int16_t  prev_block_len = 0;
    uint8_t  transmit_coefs = 0;
    uint8_t  num_subframes  = 0;
    uint16_t subframe_len[MAX_SUBFRAMES]    = {};
    uint16_t subframe_offset[MAX_SUBFRAMES] = {};
    uint8_t  cur_subframe   = 0;
    uint16_t decoded_samples = 0;
    uint8_t  grouped        = 0;
    int      quant_step     = 0;
    int8_t   reuse_sf       = 0;
    int8_t   scale_factor_step = 0;
    int      max_scale_factor  = 0;
    int      saved_scale_factors[2][MAX_BANDS] = {};
    int8_t   scale_factor_idx  = 0;
    int*     scale_factors     = nullptr;
    uint8_t  table_idx         = 0;
    float*   coeffs            = nullptr;
    uint16_t num_vec_coeffs    = 0;
    std::array<float, WMAPRO_BLOCK_MAX_SIZE + WMAPRO_BLOCK_MAX_SIZE / 2> out{};
};

struct ProChannelGrp {
    uint8_t num_channels = 0;
    int8_t  transform = 0;
    int8_t  transform_band[MAX_BANDS] = {};
    float   decorrelation_matrix[kWmaProMaxChannels * kWmaProMaxChannels] = {};
    float*  channel_data[kWmaProMaxChannels] = {};
};

struct WmaProState {
    CodecContext* avctx = nullptr;
    std::array<uint8_t, MAX_FRAMESIZE + AV_INPUT_BUFFER_PADDING_SIZE> frame_data{};
    PutBits      pb;
    Mdct         tx[WMAPRO_BLOCK_SIZES];
    std::array<float, WMAPRO_BLOCK_MAX_SIZE> tmp{};
    const float* windows[WMAPRO_BLOCK_SIZES] = {};

    uint32_t decode_flags = 0;
    uint8_t  len_prefix = 0;
    uint8_t  dynamic_range_compression = 0;
    uint8_t  bits_per_sample = 16;
    uint16_t samples_per_frame = 0;
    uint16_t trim_start = 0;
    uint16_t trim_end   = 0;
    uint16_t log2_frame_size = 0;
    int8_t   lfe_channel = -1;
    uint8_t  max_num_subframes = 0;
    uint8_t  subframe_len_bits = 0;
    uint8_t  max_subframe_len_bit = 0;
    uint16_t min_samples_per_subframe = 0;
    int8_t   num_sfb[WMAPRO_BLOCK_SIZES] = {};
    int16_t  sfb_offsets[WMAPRO_BLOCK_SIZES][MAX_BANDS] = {};
    int8_t   sf_offsets[WMAPRO_BLOCK_SIZES][WMAPRO_BLOCK_SIZES][MAX_BANDS] = {};
    int16_t  subwoofer_cutoffs[WMAPRO_BLOCK_SIZES] = {};

    GetBits  pgb;
    int      next_packet_start = 0;
    uint8_t  packet_offset = 0;
    uint8_t  packet_sequence_number = 0;
    int      num_saved_bits = 0;
    int      frame_offset = 0;
    int      subframe_offset = 0;
    uint8_t  packet_loss = 1;
    uint8_t  packet_done = 0;
    uint8_t  eof_done = 0;

    uint32_t frame_num = 0;
    GetBits  gb;
    int      buf_bit_size = 0;
    uint8_t  drc_gain = 0;
    int8_t   skip_frame = 1;
    int8_t   parsed_all_subframes = 0;
    uint8_t  skip_packets = 0;

    int16_t  subframe_len = 0;
    int8_t   nb_channels = 0;
    int8_t   channels_for_cur_subframe = 0;
    int8_t   channel_indexes_for_cur_subframe[kWmaProMaxChannels] = {};
    int8_t   num_bands = 0;
    int8_t   transmit_num_vec_coeffs = 0;
    int16_t* cur_sfb_offsets = nullptr;
    uint8_t  table_idx = 0;
    int8_t   esc_len = 0;

    uint8_t       num_chgroups = 0;
    ProChannelGrp chgroup[kWmaProMaxChannels];
    ProChannel    channel[kWmaProMaxChannels];
};

struct FloatFifo {
    std::deque<float> q;
    void write(const float* in, int n) {
        for (int i = 0; i < n; ++i) q.push_back(in[i]);
    }
    int read(float* out, int n) {
        const int got = std::min(n, int(q.size()));
        for (int i = 0; i < got; ++i) { out[i] = q.front(); q.pop_front(); }
        return got;
    }
    int size() const { return int(q.size()); }
    void clear() { q.clear(); }
};

struct XmaState {
    WmaProState xma[kWmaProMaxStreams];
    Frame frames[kWmaProMaxStreams];
    int current_stream = 0;
    int num_streams = 0;
    FloatFifo samples[2][kWmaProMaxStreams];
    int start_channel[kWmaProMaxStreams] = {};
    int trim_start = 0;
    int trim_end = 0;
    int flushed = 0;
};
