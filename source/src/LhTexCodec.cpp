#include "LhTexCodec.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <deque>
#include <mutex>
#include <ctime>
#include <cstring>
#include <cstdint>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

// ---------------------------------------------------------------------------
// File-backed logger — writes "tex_errors.log" next to the running exe.
// Lazily opens on first call. Thread-safe via a small mutex.
// ---------------------------------------------------------------------------
namespace {

std::mutex g_log_mutex;
std::ofstream g_log_file;
bool g_log_open_attempted = false;

std::filesystem::path exe_directory() {
#ifdef _WIN32
    char buf[MAX_PATH * 2] = {0};
    DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return std::filesystem::current_path();
    return std::filesystem::path(buf).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

void ensure_log_open() {
    if (g_log_open_attempted) return;
    g_log_open_attempted = true;
    auto path = exe_directory() / "tex_errors.log";
    g_log_file.open(path, std::ios::out | std::ios::app);
    if (g_log_file.is_open()) {
        std::time_t t = std::time(nullptr);
        char ts[32] = {0};
        std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", std::localtime(&t));
        g_log_file << "\n--- session started " << ts << " ---\n";
        g_log_file.flush();
    } else {
        std::cerr << "[Logger] failed to open log at " << path.string()
                  << " — falling back to stderr only\n";
    }
}

std::string current_timestamp() {
    std::time_t t = std::time(nullptr);
    char ts[32] = {0};
    std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", std::localtime(&t));
    return ts;
}

} // namespace

void log_line(const std::string& msg) {
    std::lock_guard<std::mutex> lk(g_log_mutex);
    ensure_log_open();
    std::string line = current_timestamp() + " " + msg + "\n";
    if (g_log_file.is_open()) {
        g_log_file << line;
        g_log_file.flush();
    }
    // Also still echo to stderr — harmless when running headless on Windows
    // (no console attached) and useful when launched from a terminal.
    std::cerr << line;
}

void log_tagged(const char* tag, const std::string& msg) {
    std::ostringstream os;
    os << "[" << (tag ? tag : "?") << "] " << msg;
    log_line(os.str());
}

namespace {

// ---------------------------------------------------------------------------
// OP_TABLE (62 entries × {op, dx, dy})  —  data @ 0x83316898 in default.xex
// Generated from verified Python decoder (see tex_decode.py).
// ---------------------------------------------------------------------------
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
    { 4,  0,  0},  // 61: literal
};

// ---------------------------------------------------------------------------
// DELTA_TABLE (122 entries × {dr, dg, db})  —  data @ 0x833162E0
// Generated from verified Python decoder.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Bit reader — BE u32 words, LSB-first within each word.
// ---------------------------------------------------------------------------
struct BitReader {
    const uint8_t* data;
    size_t bytes;
    size_t cur_bit;

    explicit BitReader(const uint8_t* d, size_t n) : data(d), bytes(n), cur_bit(0) {}

    inline uint32_t word(size_t idx) const {
        size_t off = idx * 4;
        if (off + 4 > bytes) {
            // pad with zeros if reading past end
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

// ---------------------------------------------------------------------------
// Frequency byte decode: freq = (b & 0xF) << ((b >> 4) & 0xF)
// ---------------------------------------------------------------------------
static inline uint32_t decode_freq_byte(uint8_t b) {
    return ((uint32_t)(b & 0x0F)) << ((b >> 4) & 0x0F);
}

// ---------------------------------------------------------------------------
// Huffman tree node + builder. Replicates the in-binary heap exactly:
//  - Push: sift-up swaps when parent_freq >= new_freq (LIFO on ties)
//  - Pop:  sift-down prefers LEFT child on tie
// ---------------------------------------------------------------------------
struct HuffNode {
    uint32_t freq = 0;
    int      sym  = -1;     // -1 for internal nodes
    HuffNode* left  = nullptr;
    HuffNode* right = nullptr;
};

// std::deque provides pointer stability across emplace_back, which we need
// because internal nodes hold pointers to children allocated earlier.
class HuffArena {
public:
    HuffNode* alloc() {
        nodes_.emplace_back();
        return &nodes_.back();
    }
private:
    std::deque<HuffNode> nodes_;
};

// Custom min-heap that exactly matches the binary's behavior.
struct LhHeap {
    std::vector<HuffNode*> h;

    void push(HuffNode* n) {
        h.push_back(n);
        size_t i = h.size() - 1;
        while (i > 0) {
            size_t p = (i - 1) >> 1;
            if (h[p]->freq < h[i]->freq) break;   // strictly smaller, stop
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
                    // Prefer LEFT on tie: pick left when right_freq >= left_freq
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

} // anonymous

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------
bool lh_decode_compressed_mip(const uint8_t* body, size_t body_size,
                              int& out_width, int& out_height,
                              std::vector<uint8_t>& out_bc1,
                              std::string* err)
{
    auto fail = [&](const std::string& msg) -> bool {
        log_tagged("LhTexCodec", msg);
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

    // ---------------------------------------------------------------------
    // Three frequency tables, packed in this order.
    // ---------------------------------------------------------------------
    uint32_t freq_idx[256];
    uint32_t freq_op [62];
    uint32_t freq_del[122];
    for (int i = 0; i < 256; ++i) freq_idx[i] = decode_freq_byte((uint8_t)br.read(8));
    for (int i = 0; i <  62; ++i) freq_op [i] = decode_freq_byte((uint8_t)br.read(8));
    for (int i = 0; i < 122; ++i) freq_del[i] = decode_freq_byte((uint8_t)br.read(8));

    // ---------------------------------------------------------------------
    // Build three Huffman trees.
    // ---------------------------------------------------------------------
    HuffArena arena_idx;
    HuffArena arena_op;
    HuffArena arena_del;

    const HuffNode* tree_idx = build_tree(arena_idx, freq_idx, 256);
    const HuffNode* tree_op  = build_tree(arena_op,  freq_op,   62);
    const HuffNode* tree_del = build_tree(arena_del, freq_del, 122);

    if (!tree_idx || !tree_op || !tree_del) {
        return fail("Huffman tree build failed");
    }

    // ---------------------------------------------------------------------
    // Per-block decode loop. Output is little-endian BC1.
    //   block layout: u16 c0, u16 c1, u32 idx32  (8 bytes)
    // ---------------------------------------------------------------------
    const int bw = mw / 4;
    const int bh = mh / 4;
    const size_t total_blocks = (size_t)bw * (size_t)bh;
    out_bc1.assign(total_blocks * 8, 0);  // pre-zeroed (matches in-binary alloc behavior)

    auto get_block_endpoints = [&](int rx, int ry, uint16_t& c0, uint16_t& c1) {
        // Out-of-bounds reads return (0, 0) — matches the C decoder reading
        // from the pre-zeroed output buffer.
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
                // LITERAL
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

            if (c0 == c1) {
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
                // 4 decoded symbols are 2x2 sub-blocks in Z-order:
                //   B0 = top-left, B1 = top-right, B2 = bottom-left, B3 = bottom-right.
                // Each byte holds 4 BC1 indices (2 bits each); low nibble = cols 0-1,
                // high nibble = cols 2-3. high nibble of Bn = row 0/2, low nibble = row 1/3.
                outb[4] = (uint8_t)(((B1 & 0xF0))        | (((B0 >> 4) & 0x0F)));
                outb[5] = (uint8_t)((((B1 & 0x0F) << 4)) | ( (B0       & 0x0F)));
                outb[6] = (uint8_t)(((B3 & 0xF0))        | (((B2 >> 4) & 0x0F)));
                outb[7] = (uint8_t)((((B3 & 0x0F) << 4)) | ( (B2       & 0x0F)));
            }
        }
    }

    // Tolerance for trailing bit-stream slack — the encoder pads to the
    // next 32-bit word boundary, so up to 31 unused bits at the end is fine.
    if (br.cur_bit > body_size * 8 + 32) {
        std::ostringstream os;
        os << "ran past end of body during decode: bit=" << br.cur_bit
           << " of " << (body_size * 8);
        return fail(os.str());
    }

    return true;
}

// ---------------------------------------------------------------------------
// lh_decode_variant_2_3_4 — STUB. See docs/CODEC.md "variant_2_3_4" for the
// full reverse-engineered spec; the port is tracked in docs/STATE.md.
// Returns false so callers fall through to comp=7 raw fallback gracefully.
// ---------------------------------------------------------------------------
bool lh_decode_variant_2_3_4(const uint8_t* /*body*/, size_t /*body_size*/,
                             int /*mode*/, int /*width*/, int /*height*/,
                             std::vector<uint8_t>& /*out_bytes*/,
                             std::string* err) {
    const char* msg = "lh_decode_variant_2_3_4: not yet ported "
                      "(see docs/CODEC.md for spec)";
    if (err) *err = msg;
    log_tagged("LhTexCodec", msg);
    return false;
}
