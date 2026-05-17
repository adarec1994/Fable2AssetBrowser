// XmaBitstream.cpp — BE non-cached bitstream reader, BE bit writer,
// and canonical-Huffman VLC decoder with multi-level dispatch.
//
// Derived from FFmpeg (LGPL 2.1+).

#include "XmaBitstream.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace Xma {

void GetBits::init(const uint8_t* buffer, int byte_size) {
    buffer_             = buffer;
    index_              = 0;
    size_in_bits_       = byte_size * 8;
    size_in_bits_plus8_ = size_in_bits_ + 8;
}

unsigned GetBits::read(int n) {
    if (n <= 0) return 0;
    if (n > 25) return unsigned(read_long(n));
    const int byte = index_ >> 3;
    const int bit  = index_ & 7;
    const int total_bytes = (size_in_bits_ + 7) >> 3;
    auto byte_at = [&](int i) -> uint32_t {
        return (i < total_bytes) ? uint32_t(buffer_[i]) : 0u;
    };
    uint32_t cache =
        (byte_at(byte) << 24) |
        (byte_at(byte + 1) << 16) |
        (byte_at(byte + 2) << 8) |
        (byte_at(byte + 3));
    cache <<= bit;
    cache >>= (32 - n);
    index_ = std::min(size_in_bits_plus8_, index_ + n);
    return cache;
}

unsigned GetBits::read_long(int n) {
    assert(n >= 0 && n <= 32);
    if (n == 0) return 0;
    if (n <= 25) return read(n);
    unsigned hi = read(16);
    return (hi << (n - 16)) | read(n - 16);
}

unsigned GetBits::read_1() {
    const unsigned idx  = unsigned(index_);
    uint8_t       byte  = buffer_[idx >> 3];
    byte <<= (idx & 7);
    byte >>= 7;
    if (index_ < size_in_bits_plus8_) index_++;
    return byte;
}

unsigned GetBits::show(int n) const {
    GetBits copy = *this;
    return copy.read(n);
}

int GetBits::read_signed(int n) {
    assert(n > 0 && n <= 25);
    int32_t v = int32_t(read(n));
    const int shift = 32 - n;
    return (v << shift) >> shift;
}

void GetBits::skip(int n) {
    index_ = std::min(size_in_bits_plus8_, index_ + n);
}

void GetBits::align_to_byte() {
    const int extra = (-index_) & 7;
    if (extra) skip(extra);
}

void PutBits::init(uint8_t* buffer, int byte_size) {
    buffer_       = buffer;
    bit_index_    = 0;
    size_in_bits_ = byte_size * 8;
    bit_buf_      = 0;
    bit_left_     = 64;
    if (buffer && byte_size > 0) std::memset(buffer, 0, std::size_t(byte_size));
}

void PutBits::write(int n, unsigned value) {
    if (n <= 0) return;
    for (int i = n - 1; i >= 0; --i) {
        if (bit_index_ >= size_in_bits_) return;
        if ((value >> i) & 1u) {
            const int byte = bit_index_ >> 3;
            const int bit_in_byte = 7 - (bit_index_ & 7);
            buffer_[byte] |= uint8_t(1u << bit_in_byte);
        }
        ++bit_index_;
    }
}

void PutBits::write_long(int n, uint64_t value) {
    if (n <= 0) return;
    for (int i = n - 1; i >= 0; --i) {
        if (bit_index_ >= size_in_bits_) return;
        if ((value >> i) & 1ull) {
            const int byte = bit_index_ >> 3;
            const int bit_in_byte = 7 - (bit_index_ & 7);
            buffer_[byte] |= uint8_t(1u << bit_in_byte);
        }
        ++bit_index_;
    }
}

void PutBits::flush() {
    bit_buf_  = 0;
    bit_left_ = 64;
}

namespace {

int read_symbol(const void* base, int stride, int elem_size,
                int index, int /*flags*/) {
    const uint8_t* p = static_cast<const uint8_t*>(base) + index * stride;
    if (elem_size == 1) {
        return int(*p);
    } else if (elem_size == 2) {
        return int(p[0]) | (int(p[1]) << 8);
    }
    return 0;
}

}  // namespace

int Vlc::init_from_lengths(int nb_bits, int nb_codes,
                           const int8_t* lens, int lens_stride,
                           const void*   symbols, int symbols_stride,
                           int symbols_elem_size,
                           int offset, int flags) {
    bits = nb_bits;
    const std::size_t root_size = std::size_t(1) << nb_bits;
    table.assign(root_size, VlcElem{0, 0});

    struct Entry { int len; int sym; int order; uint32_t code; };
    std::vector<Entry> entries;
    entries.reserve(std::size_t(nb_codes));
    for (int i = 0; i < nb_codes; ++i) {
        const int len = int(lens[i * lens_stride]);
        if (len <= 0) continue;
        int sym = i + offset;
        if (symbols) {
            sym = read_symbol(symbols, symbols_stride, symbols_elem_size, i, flags)
                  + offset;
        }
        entries.push_back({len, sym, i, 0});
    }

    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry& a, const Entry& b) {
                         if (a.len != b.len) return a.len < b.len;
                         return a.order < b.order;
                     });

    {
        uint32_t code = 0;
        int      prev_len = 0;
        for (auto& e : entries) {
            if (prev_len != 0 && e.len > prev_len) {
                code <<= (e.len - prev_len);
            }
            prev_len = e.len;
            e.code = code;
            code  += 1;
        }
    }

    // Per-prefix max residual: each prefix that has any multi-level
    // entry needs a sub-table sized for the widest residual under it.
    std::unordered_map<uint32_t, int> prefix_max_leaf;
    for (const auto& e : entries) {
        if (e.len > nb_bits) {
            const uint32_t prefix    = e.code >> (e.len - nb_bits);
            const int      leaf_bits = e.len - nb_bits;
            auto it = prefix_max_leaf.find(prefix);
            if (it == prefix_max_leaf.end() || it->second < leaf_bits)
                prefix_max_leaf[prefix] = leaf_bits;
        }
    }

    std::unordered_map<uint32_t, int> prefix_sub_off;
    for (const auto& [prefix, max_leaf] : prefix_max_leaf) {
        const int sub_off  = int(table.size());
        const int sub_size = 1 << max_leaf;
        table.resize(table.size() + std::size_t(sub_size), VlcElem{0, 0});
        table[prefix] = VlcElem{int32_t(sub_off), int16_t(-max_leaf)};
        prefix_sub_off[prefix] = sub_off;
    }

    for (const auto& e : entries) {
        if (e.len <= nb_bits) {
            const uint32_t fill = uint32_t(1) << (nb_bits - e.len);
            const uint32_t base = e.code << (nb_bits - e.len);
            if (base + fill > root_size) return -1;
            for (uint32_t k = 0; k < fill; ++k) {
                table[base + k] = VlcElem{int32_t(e.sym), int16_t(e.len)};
            }
        } else {
            const uint32_t prefix    = e.code >> (e.len - nb_bits);
            const int      sub_off   = prefix_sub_off[prefix];
            const int      sub_bits  = prefix_max_leaf[prefix];
            const int      leaf_bits = e.len - nb_bits;
            const uint32_t local     = e.code & ((1u << leaf_bits) - 1);
            const uint32_t fill      = uint32_t(1) << (sub_bits - leaf_bits);
            const uint32_t base      = local << (sub_bits - leaf_bits);
            for (uint32_t k = 0; k < fill; ++k) {
                table[sub_off + base + k] =
                    VlcElem{int32_t(e.sym), int16_t(leaf_bits)};
            }
        }
    }

    return 0;
}

int Vlc::init_sparse(int nb_bits, int nb_codes,
                     const void* bits_in, int bits_wrap, int bits_size,
                     const void* codes,   int codes_wrap, int codes_size,
                     const void* symbols, int symbols_wrap, int symbols_size,
                     int flags) {
    (void)codes; (void)codes_wrap; (void)codes_size;
    bits = nb_bits;

    std::vector<int8_t> len_arr;
    len_arr.resize(static_cast<std::size_t>(nb_codes));
    const uint8_t* lbase = static_cast<const uint8_t*>(bits_in);
    for (int i = 0; i < nb_codes; ++i) {
        if (bits_size == 1 || bits_size == 2) {
            len_arr[static_cast<std::size_t>(i)] =
                static_cast<int8_t>(lbase[i * bits_wrap]);
        } else {
            len_arr[static_cast<std::size_t>(i)] = 0;
        }
    }

    const int8_t* lens_ptr = len_arr.data();
    return init_from_lengths(nb_bits, nb_codes,
                             lens_ptr, 1,
                             symbols, symbols_wrap, symbols_size,
                             0, flags);
}

int Vlc::get(GetBits& gb, int /*depth*/) const {
    unsigned idx = gb.show(bits);
    if (idx >= table.size()) return -1;
    int code = table[idx].sym;
    int len  = table[idx].len;
    int level_bits = bits;
    int safety = 0;
    while (len < 0) {
        gb.skip(level_bits);
        level_bits = -len;
        unsigned idx2 = gb.show(level_bits) + code;
        if (idx2 >= table.size()) return -1;
        code = table[idx2].sym;
        len  = table[idx2].len;
        if (++safety > 4) return -1;
    }
    if (len > 0) gb.skip(len);
    return code;
}

}  // namespace Xma
