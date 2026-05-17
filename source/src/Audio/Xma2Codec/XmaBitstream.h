// Native C++ port of FFmpeg's BE non-cached bitstream reader
// (get_bits.h), BE bit writer (put_bits.h), and the table-driven
// canonical-Huffman VLC decoder (vlc.h) used by wmaprodec.c.
//
// Derived from FFmpeg (LGPL 2.1+); see Archive/audio/libavcodec/*.h.

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace Xma {

class GetBits {
public:
    void  init(const uint8_t* buffer, int byte_size);

    unsigned read(int n);
    unsigned read_long(int n);
    unsigned read_1();
    unsigned show(int n) const;
    int      read_signed(int n);
    void     skip(int n);
    int      tell() const  { return index_; }
    int      bits_left() const { return size_in_bits_ - index_; }
    void     align_to_byte();

private:
    const uint8_t* buffer_ = nullptr;
    int            index_ = 0;
    int            size_in_bits_ = 0;
    int            size_in_bits_plus8_ = 0;
};

class PutBits {
public:
    void  init(uint8_t* buffer, int byte_size);
    void  write(int n, unsigned value);
    void  write_long(int n, uint64_t value);
    void  flush();
    int   tell() const { return bit_index_; }

private:
    uint8_t* buffer_ = nullptr;
    int      bit_index_ = 0;
    int      size_in_bits_ = 0;
    uint64_t bit_buf_ = 0;
    int      bit_left_ = 64;
};

// VlcElem.sym is int32_t — sub-table offsets for long WMA Pro codebooks
// (e.g. HUFF_SCALE_RL_MAXBITS=21 with VLCBITS=9) exceed int16 range.
struct VlcElem {
    int32_t sym;
    int16_t len;
};

class Vlc {
public:
    int bits = 0;
    std::vector<VlcElem> table;

    int init_from_lengths(int nb_bits, int nb_codes,
                          const int8_t* lens, int lens_stride,
                          const void*   symbols, int symbols_stride, int symbols_elem_size,
                          int offset, int flags);

    int init_sparse(int nb_bits, int nb_codes,
                    const void* bits_in, int bits_wrap, int bits_size,
                    const void* codes,   int codes_wrap, int codes_size,
                    const void* symbols, int symbols_wrap, int symbols_size,
                    int flags);

    int get(GetBits& gb, int depth) const;

    void clear() { table.clear(); bits = 0; }
};

constexpr int kVlcInitUseStatic      = 1;
constexpr int kVlcInitStaticOverlong = 2 | kVlcInitUseStatic;
constexpr int kVlcInitInputLE        = 4;
constexpr int kVlcInitOutputLE       = 8;

}  // namespace Xma
