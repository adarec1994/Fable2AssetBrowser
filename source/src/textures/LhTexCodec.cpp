#include "LhTexCodec.h"
#include <sstream>
#include <deque>
#include <cstring>
#include <cstdint>

namespace {

struct OpEntry { int32_t op, dx, dy; };
static const OpEntry kOpTable[62] = {
    { 0, -1,  0}, { 0, -2,  0}, { 0, -3,  0}, { 0, -4,  0}, { 0, -5,  0},
    { 0,  0, -1}, { 0, -1, -1}, { 0, -2, -1}, { 0, -3, -1}, { 0, -4, -1}, { 0, -5, -1},
    { 0,  0, -2}, { 0, -1, -2}, { 0, -2, -2}, { 0, -3, -2},
    { 0,  0, -3}, { 0, -1, -3}, { 0, -2, -3},
    { 0,  0, -4}, { 0, -1, -4},
    { 0,  0, -5}, { 0, -1, -5},
    { 1, -1,  0}, { 1, -2,  0}, { 1, -3,  0}, { 1, -4,  0},
    { 1,  0, -1}, { 1, -1, -1}, { 1, -2, -1}, { 1, -3, -1}, { 1, -4, -1},
    { 1,  0, -2}, { 1, -1, -2}, { 1, -2, -2},
    { 1,  0, -3}, { 1, -1, -3},
    { 1,  0, -4}, { 1, -1, -4},
    { 2, -1,  0}, { 2, -2,  0}, { 2, -3,  0}, { 2, -4,  0},
    { 2,  0, -1}, { 2, -1, -1}, { 2, -2, -1}, { 2, -3, -1}, { 2, -4, -1},
    { 2,  0, -2}, { 2, -1, -2}, { 2, -2, -2},
    { 2,  0, -3}, { 2, -1, -3},
    { 2,  0, -4}, { 2, -1, -4},
    { 3, -1,  0}, { 3, -2,  0},
    { 3,  0, -1}, { 3, -1, -1}, { 3, -2, -1},
    { 3,  0, -2}, { 3, -1, -2},
    { 4,  0,  0},
};

struct DeltaEntry { int32_t dr, dg, db; };
static const DeltaEntry kDeltaTable[122] = {
    { -3, -5, -3}, { -3, -5, -2}, { -3, -5, -1},
    { -3, -3, -3}, { -3, -3, -2}, { -3, -3, -1},
    { -2, -5, -3}, { -2, -5, -2}, { -2, -5, -1}, { -2, -5,  0},
    { -2, -3, -3}, { -2, -3, -2}, { -2, -3, -1}, { -2, -3,  0},
    { -2, -1, -2}, { -2, -1, -1}, { -2, -1,  0},
    { -2,  0, -2}, { -2,  0, -1}, { -2,  0,  0},
    { -2,  1, -2}, { -2,  1, -1}, { -2,  1,  0},
    { -1, -5, -3}, { -1, -5, -2}, { -1, -5, -1}, { -1, -5,  0},
    { -1, -3, -3}, { -1, -3, -2}, { -1, -3, -1}, { -1, -3,  0}, { -1, -3,  1},
    { -1, -1, -2}, { -1, -1, -1}, { -1, -1,  0}, { -1, -1,  1},
    { -1,  0, -2}, { -1,  0, -1}, { -1,  0,  0}, { -1,  0,  1},
    { -1,  1, -2}, { -1,  1, -1}, { -1,  1,  0}, { -1,  1,  1},
    { -1,  3, -1}, { -1,  3,  0}, { -1,  3,  1},
    {  0, -5, -2}, {  0, -5, -1}, {  0, -5,  0},
    {  0, -3, -2}, {  0, -3, -1}, {  0, -3,  0}, {  0, -3,  1},
    {  0, -1, -2}, {  0, -1, -1}, {  0, -1,  0}, {  0, -1,  1}, {  0, -1,  2},
    {  0,  0, -2}, {  0,  0, -1}, {  0,  0,  1}, {  0,  0,  2},
    {  0,  1, -2}, {  0,  1, -1}, {  0,  1,  0}, {  0,  1,  1}, {  0,  1,  2},
    {  0,  3, -1}, {  0,  3,  0}, {  0,  3,  1}, {  0,  3,  2},
    {  0,  5,  0}, {  0,  5,  1}, {  0,  5,  2},
    {  1, -3, -1}, {  1, -3,  0}, {  1, -3,  1},
    {  1, -1, -1}, {  1, -1,  0}, {  1, -1,  1}, {  1, -1,  2},
    {  1,  0, -1}, {  1,  0,  0}, {  1,  0,  1}, {  1,  0,  2},
    {  1,  1, -1}, {  1,  1,  0}, {  1,  1,  1}, {  1,  1,  2},
    {  1,  3, -1}, {  1,  3,  0}, {  1,  3,  1}, {  1,  3,  2}, {  1,  3,  3},
    {  1,  5,  0}, {  1,  5,  1}, {  1,  5,  2}, {  1,  5,  3},
    {  2, -1,  0}, {  2, -1,  1}, {  2, -1,  2},
    {  2,  0,  0}, {  2,  0,  1}, {  2,  0,  2},
    {  2,  1,  0}, {  2,  1,  1}, {  2,  1,  2},
    {  2,  3,  0}, {  2,  3,  1}, {  2,  3,  2}, {  2,  3,  3},
    {  2,  5,  0}, {  2,  5,  1}, {  2,  5,  2}, {  2,  5,  3},
    {  3,  3,  1}, {  3,  3,  2}, {  3,  3,  3},
    {  3,  5,  1}, {  3,  5,  2}, {  3,  5,  3},
};

static_assert(sizeof(kOpTable)/sizeof(kOpTable[0]) == 62, "OP_TABLE size");
static_assert(sizeof(kDeltaTable)/sizeof(kDeltaTable[0]) == 122, "DELTA_TABLE size");

struct BitReader {
    const uint8_t* data;
    size_t bytes;
    size_t cur_bit;

    explicit BitReader(const uint8_t* d, size_t n) : data(d), bytes(n), cur_bit(0) {}

    inline uint32_t word(size_t idx) const {
        size_t off = idx * 4;
        if (off + 4 > bytes) {

            uint32_t v = 0;
            for (int i = 0; i < 4 && off + i < bytes; ++i) {
                v |= ((uint32_t)data[off + i]) << (24 - 8*i);
            }
            return v;
        }
        return ((uint32_t)data[off]   << 24) |
               ((uint32_t)data[off+1] << 16) |
               ((uint32_t)data[off+2] <<  8) |
               ((uint32_t)data[off+3]);
    }

    uint32_t read(int n) {
        uint32_t mask = (n == 32) ? 0xFFFFFFFFu : ((1u << n) - 1u);
        size_t bit_off  = cur_bit & 31;
        size_t word_idx = cur_bit >> 5;
        uint32_t v;
        if (bit_off + (size_t)n > 32) {
            uint32_t lo = word(word_idx);
            uint32_t hi = word(word_idx + 1);
            v = ((hi << (32 - bit_off)) | (lo >> bit_off)) & mask;
        } else {
            v = (word(word_idx) >> bit_off) & mask;
        }
        cur_bit += (size_t)n;
        return v;
    }
};

static inline uint32_t decode_freq_byte(uint8_t b) {
    return ((uint32_t)(b & 0x0F)) << ((b >> 4) & 0x0F);
}

struct HuffNode {
    uint32_t freq = 0;
    int      sym  = -1;
    HuffNode* left  = nullptr;
    HuffNode* right = nullptr;
};

class HuffArena {
public:
    HuffNode* alloc() {
        nodes_.emplace_back();
        return &nodes_.back();
    }
private:
    std::deque<HuffNode> nodes_;
};

struct LhHeap {
    std::vector<HuffNode*> h;

    void push(HuffNode* n) {
        h.push_back(n);
        size_t i = h.size() - 1;
        while (i > 0) {
            size_t p = (i - 1) >> 1;
            if (h[p]->freq < h[i]->freq) break;
            std::swap(h[p], h[i]);
            i = p;
        }
    }

    HuffNode* pop() {
        HuffNode* r = h.front();
        HuffNode* last = h.back();
        h.pop_back();
        if (!h.empty()) {
            h[0] = last;
            size_t i = 0;
            const size_t n = h.size();
            for (;;) {
                size_t l = 2*i + 1;
                size_t s;
                if (l >= n) break;
                size_t rr = l + 1;
                if (rr >= n) {
                    s = l;
                } else {

                    s = (h[rr]->freq >= h[l]->freq) ? l : rr;
                }
                if (h[i]->freq < h[s]->freq) break;
                std::swap(h[i], h[s]);
                i = s;
            }
        }
        return r;
    }

    size_t size() const { return h.size(); }
};

static HuffNode* build_tree(HuffArena& arena, const uint32_t* freqs, size_t count) {
    LhHeap heap;
    heap.h.reserve(count * 2 + 1);
    for (size_t s = 0; s < count; ++s) {
        HuffNode* n = arena.alloc();
        n->freq = freqs[s];
        n->sym  = (int)s;
        heap.push(n);
    }
    while (heap.size() > 1) {
        HuffNode* a = heap.pop();
        HuffNode* b = heap.pop();
        HuffNode* p = arena.alloc();
        p->freq  = a->freq + b->freq;
        p->left  = a;
        p->right = b;
        heap.push(p);
    }
    return heap.size() ? heap.pop() : nullptr;
}

static inline int huff_decode(BitReader& br, const HuffNode* root) {
    const HuffNode* n = root;
    while (n && n->sym < 0) {
        uint32_t bit = br.read(1);
        n = bit ? n->right : n->left;
    }
    return n ? n->sym : -1;
}

static inline int clamp_int(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint16_t apply_delta(uint16_t c565, const DeltaEntry& d) {
    int r = clamp_int(((c565 >> 11) & 0x1F) + d.dr, 0, 31);
    int g = clamp_int(((c565 >>  5) & 0x3F) + d.dg, 0, 63);
    int b = clamp_int(( c565        & 0x1F) + d.db, 0, 31);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

}

bool lh_decode_compressed_mip(const uint8_t* body, size_t body_size,
                              int& out_width, int& out_height,
                              std::vector<uint8_t>& out_bc1,
                              std::string* err,
                              bool comp11_layout)
{
    auto fail = [&](const std::string& msg) -> bool {
        if (err) *err = msg;
        return false;
    };

    if (body_size < 444) {
        return fail("body too small (<" + std::to_string(444) + " bytes): " + std::to_string(body_size));
    }

    BitReader br(body, body_size);
    int mw = (int)br.read(16);
    int mh = (int)br.read(16);

    if (mw <= 0 || mh <= 0 || mw > 8192 || mh > 8192) {
        std::ostringstream os;
        os << "implausible mip dimensions: " << mw << "x" << mh;
        return fail(os.str());
    }
    if ((mw % 4) != 0 || (mh % 4) != 0) {
        std::ostringstream os;
        os << "mip dimensions not 4-aligned: " << mw << "x" << mh;
        return fail(os.str());
    }

    out_width  = mw;
    out_height = mh;

    uint32_t freq_idx[256];
    uint32_t freq_op [62];
    uint32_t freq_del[122];
    for (int i = 0; i < 256; ++i) freq_idx[i] = decode_freq_byte((uint8_t)br.read(8));
    for (int i = 0; i <  62; ++i) freq_op [i] = decode_freq_byte((uint8_t)br.read(8));
    for (int i = 0; i < 122; ++i) freq_del[i] = decode_freq_byte((uint8_t)br.read(8));

    HuffArena arena_idx;
    HuffArena arena_op;
    HuffArena arena_del;

    const HuffNode* tree_idx = build_tree(arena_idx, freq_idx, 256);
    const HuffNode* tree_op  = build_tree(arena_op,  freq_op,   62);
    const HuffNode* tree_del = build_tree(arena_del, freq_del, 122);

    if (!tree_idx || !tree_op || !tree_del) {
        return fail("Huffman tree build failed");
    }

    const int bw = mw / 4;
    const int bh = mh / 4;
    const size_t total_blocks = (size_t)bw * (size_t)bh;
    out_bc1.assign(total_blocks * 8, 0);

    auto get_block_endpoints = [&](int rx, int ry, uint16_t& c0, uint16_t& c1) {

        if (rx < 0 || ry < 0 || rx >= bw || ry >= bh) {
            c0 = 0; c1 = 0;
            return;
        }
        const uint8_t* p = out_bc1.data() + (size_t)(ry * bw + rx) * 8;
        c0 = (uint16_t)(p[0] | (p[1] << 8));
        c1 = (uint16_t)(p[2] | (p[3] << 8));
    };

    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            int op_idx = huff_decode(br, tree_op);
            if (op_idx < 0 || op_idx >= 62) {
                std::ostringstream os;
                os << "op_idx out of range at block (" << bx << "," << by << "): " << op_idx;
                return fail(os.str());
            }
            const OpEntry& opent = kOpTable[op_idx];

            uint16_t c0, c1;
            if (opent.op == 4) {

                c0 = (uint16_t)br.read(16);
                c1 = (uint16_t)br.read(16);
            } else {
                get_block_endpoints(bx + opent.dx, by + opent.dy, c0, c1);

                if (opent.op == 1 || opent.op == 3) {
                    int d = huff_decode(br, tree_del);
                    if (d < 0 || d >= 122) {
                        std::ostringstream os;
                        os << "delta idx out of range (c0) at block (" << bx << "," << by << "): " << d;
                        return fail(os.str());
                    }
                    c0 = apply_delta(c0, kDeltaTable[d]);
                }
                if (opent.op == 2 || opent.op == 3) {
                    int d = huff_decode(br, tree_del);
                    if (d < 0 || d >= 122) {
                        std::ostringstream os;
                        os << "delta idx out of range (c1) at block (" << bx << "," << by << "): " << d;
                        return fail(os.str());
                    }
                    c1 = apply_delta(c1, kDeltaTable[d]);
                }
                if (c0 < c1) { uint16_t t = c0; c0 = c1; c1 = t; }
            }

            uint8_t* outb = out_bc1.data() + (size_t)(by * bw + bx) * 8;
            outb[0] = (uint8_t)(c0 & 0xFF);
            outb[1] = (uint8_t)(c0 >> 8);
            outb[2] = (uint8_t)(c1 & 0xFF);
            outb[3] = (uint8_t)(c1 >> 8);

            const bool always_read_indices = comp11_layout;
            if (c0 == c1 && !always_read_indices) {
                outb[4] = outb[5] = outb[6] = outb[7] = 0;
            } else {
                int B0 = huff_decode(br, tree_idx);
                int B1 = huff_decode(br, tree_idx);
                int B2 = huff_decode(br, tree_idx);
                int B3 = huff_decode(br, tree_idx);
                if (B0 < 0 || B1 < 0 || B2 < 0 || B3 < 0) {
                    std::ostringstream os;
                    os << "idx decode failed at block (" << bx << "," << by << ")";
                    return fail(os.str());
                }

                outb[4] = (uint8_t)(((B1 & 0xF0))        | (((B0 >> 4) & 0x0F)));
                outb[5] = (uint8_t)((((B1 & 0x0F) << 4)) | ( (B0       & 0x0F)));
                outb[6] = (uint8_t)(((B3 & 0xF0))        | (((B2 >> 4) & 0x0F)));
                outb[7] = (uint8_t)((((B3 & 0x0F) << 4)) | ( (B2       & 0x0F)));
            }
        }
    }

    if (br.cur_bit > body_size * 8 + 32) {
        std::ostringstream os;
        os << "ran past end of body during decode: bit=" << br.cur_bit
           << " of " << (body_size * 8);
        return fail(os.str());
    }

    return true;
}

namespace {

static uint8_t g_dq6to8[64];

static uint8_t g_dq4to8[16];

static uint8_t g_q8to4[256];

static bool g_variant_tables_init = false;

static void init_variant_tables() {
    if (g_variant_tables_init) return;
    for (int v = 0; v < 64; ++v) {
        g_dq6to8[v] = (uint8_t)((v * 255 + 31) / 63);
    }
    for (int v = 0; v < 16; ++v) {
        g_dq4to8[v] = (uint8_t)((v * 255 + 7) / 15);
    }
    for (int v = 0; v < 256; ++v) {
        g_q8to4[v] = (uint8_t)((v * 15 + 127) / 255);
    }
    g_variant_tables_init = true;
}

static inline int clamp6(int v) {
    return v < 0 ? 0 : (v > 0x3F ? 0x3F : v);
}

}

bool lh_decode_variant_2_3_4(const uint8_t* body, size_t body_size,
                             int mode, int width, int height,
                             std::vector<uint8_t>& out_bytes,
                             std::string* err) {
    auto fail = [&](const std::string& msg) -> bool {
        if (err) *err = msg;
        return false;
    };

    init_variant_tables();

    if (body_size < 32) return fail("variant_2_3_4: body too small");
    if (width <= 0 || height <= 0 || (width % 4) != 0 || (height % 4) != 0)
        return fail("variant_2_3_4: invalid dimensions");

    BitReader br(body, body_size);
    /*int mw =*/ (void)br.read(16);
    /*int mh =*/ (void)br.read(16);
    int mode_flag = (int)br.read(4);
    (void)br.read(8);

    auto read_freqs = [&](uint32_t* freqs, int n) {
        for (int i = 0; i < n; ++i) {
            uint8_t b = (uint8_t)br.read(8);
            freqs[i] = decode_freq_byte(b);
        }
    };
    uint32_t freqA[64], freqB[32], freqC[64], freqOp[32];
    read_freqs(freqA, 64);
    read_freqs(freqB, 32);
    read_freqs(freqC, 64);
    read_freqs(freqOp, 32);

    HuffArena arena;
    HuffNode* tree_A  = build_tree(arena, freqA, 64);
    HuffNode* tree_B  = build_tree(arena, freqB, 32);
    HuffNode* tree_C  = build_tree(arena, freqC, 64);
    HuffNode* tree_op = build_tree(arena, freqOp, 32);

    if (!tree_A || !tree_B || !tree_C || !tree_op)
        return fail("variant_2_3_4: tree build failed");

    const int bx_count = width  / 4;
    const int by_count = height / 4;
    const size_t blocks = (size_t)bx_count * (size_t)by_count;

    out_bytes.assign(blocks * 8, 0);

    int run_remaining = 0;
    int last_op_type = -1;
    uint8_t last_v20 = 0;

    for (int by = 0; by < by_count; ++by) {
        for (int bx = 0; bx < bx_count; ++bx) {
            int op_type;
            uint8_t v20 = 0;

            if (run_remaining > 0) {
                --run_remaining;
                op_type = last_op_type;
                v20 = last_v20;
            } else {
                int op_sym = huff_decode(br, tree_op);
                if (op_sym < 0) return fail("variant_2_3_4: op tree decode failed");
                int count = op_sym & 7;
                op_type   = op_sym >> 3;
                int extra = (count > 0) ? (int)br.read(count) : 0;
                run_remaining = (1 << count) + extra - 1;

                if (op_type == 2) {
                    int bits = (mode_flag == 1) ? (int)br.read(4) : (int)br.read(8);
                    if (mode == 1) {

                        v20 = (uint8_t)((bits & 0xF) | ((bits & 0xF) << 4));
                    } else {

                        if (mode_flag == 1) {
                            v20 = g_dq4to8[bits & 0xF];
                        } else {
                            v20 = (uint8_t)(bits & 0xFF);
                        }
                    }
                }
                last_op_type = op_type;
                last_v20 = v20;
            }

            uint8_t* outb = out_bytes.data() + (size_t)(by * bx_count + bx) * 8;

            switch (op_type) {
                case 0: {
                    for (int i = 0; i < 8; ++i) outb[i] = 0;
                    break;
                }
                case 1: {
                    for (int i = 0; i < 8; ++i) outb[i] = 0xFF;
                    break;
                }
                case 2: {
                    if (mode == 2) {

                        outb[0] = v20; outb[1] = v20;
                        outb[2] = outb[3] = outb[4] = outb[5] = outb[6] = outb[7] = 0;
                    } else {
                        for (int i = 0; i < 8; ++i) outb[i] = v20;
                    }
                    break;
                }
                case 3: {

                    int sym_A = huff_decode(br, tree_A);
                    int sym_B = huff_decode(br, tree_B);
                    if (sym_A < 0 || sym_B < 0) return fail("variant_2_3_4: A/B tree decode failed");

                    int a0 = g_dq6to8[clamp6(sym_A + sym_B)];
                    int a1 = g_dq6to8[clamp6(sym_A - sym_B)];

                    uint8_t indices[16] = {0};
                    for (int iter = 0; iter < 4; ++iter) {
                        int sC1 = huff_decode(br, tree_C);
                        int sC2 = huff_decode(br, tree_C);
                        if (sC1 < 0 || sC2 < 0)
                            return fail("variant_2_3_4: C tree decode failed");
                        indices[iter * 4 + 0] = (uint8_t)(sC1 & 7);
                        indices[iter * 4 + 1] = (uint8_t)((sC1 >> 3) & 7);
                        indices[iter * 4 + 2] = (uint8_t)(sC2 & 7);
                        indices[iter * 4 + 3] = (uint8_t)((sC2 >> 3) & 7);
                    }

                    if (mode == 2) {

                        outb[0] = (uint8_t)a0;
                        outb[1] = (uint8_t)a1;
                        uint64_t packed = 0;
                        for (int i = 0; i < 16; ++i) {
                            packed |= ((uint64_t)(indices[i] & 7)) << (i * 3);
                        }
                        outb[2] = (uint8_t)(packed       & 0xFF);
                        outb[3] = (uint8_t)((packed >> 8)  & 0xFF);
                        outb[4] = (uint8_t)((packed >> 16) & 0xFF);
                        outb[5] = (uint8_t)((packed >> 24) & 0xFF);
                        outb[6] = (uint8_t)((packed >> 32) & 0xFF);
                        outb[7] = (uint8_t)((packed >> 40) & 0xFF);
                    } else {

                        outb[0] = (uint8_t)a0;
                        outb[1] = (uint8_t)a1;
                        for (int i = 2; i < 8; ++i) outb[i] = 0;
                    }
                    break;
                }
                default:
                    return fail("variant_2_3_4: invalid op_type");
            }
        }
    }

    return true;
}
