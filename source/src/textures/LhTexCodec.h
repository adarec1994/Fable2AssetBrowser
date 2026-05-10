#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// File-backed logger — writes to "tex_errors.log" next to the running exe.
// Thread-safe; opens lazily on first call. Use these instead of std::cerr.
// ---------------------------------------------------------------------------
void log_line(const std::string& msg);
void log_tagged(const char* tag, const std::string& msg);

// Lionhead BC1 codec — port of sub_82B8C1C8 in Fable 2 default.xex.
//
// Decodes a single compressed mip body into raw little-endian BC1 bytes.
//
// Body layout (one continuous bit-stream, BE u32 words, LSB-first):
//   16 bits  mip width (mw)
//   16 bits  mip height (mh)
//   256 * 8  Huffman code-length bytes for the index-byte alphabet
//    62 * 8  Huffman code-length bytes for the per-block opcode alphabet
//   122 * 8  Huffman code-length bytes for the RGB565 delta alphabet
//   ...      bit-coded payload (one entry per 4x4 block)
//
// On success returns true and fills `out` with `(mw/4) * (mh/4) * 8` bytes
// of raw little-endian BC1 (suitable for hand-off to a standard BC1 decoder).
//
// On failure returns false; an explanatory message is written to `err` and
// also printed via std::cerr.
bool lh_decode_compressed_mip(const uint8_t* body, size_t body_size,
                              int& out_width, int& out_height,
                              std::vector<uint8_t>& out_bc1,
                              std::string* err = nullptr,
                              bool comp11_layout = false);

// Lionhead BC1 codec (CompFlag=11 variant) — port of sub_82B8C900.
// Same trees, same per-block decode, same index byte transpose as comp=1.
// The ONLY difference: comp=11 always decodes 4 index symbols every block,
// while comp=1 skips index decoding when c0==c1 (solid block). If you don't
// match that behavior, the bit-stream falls out of sync and the decoder
// walks past the end of the body. Internally calls lh_decode_compressed_mip
// with the comp11_layout flag set.
inline bool lh_decode_compressed_mip_v11(const uint8_t* body, size_t body_size,
                                         int& out_width, int& out_height,
                                         std::vector<uint8_t>& out_bc1,
                                         std::string* err = nullptr) {
    return lh_decode_compressed_mip(body, body_size, out_width, out_height,
                                    out_bc1, err, /*comp11_layout=*/true);
}

// Lionhead BC4/BC5 variant codec — port of sub_82B8D010 in Fable 2.
//
// Handles CompFlag = 2, 3, 4. Currently CompFlag = 3 is the only one
// observed in the wild (PixelFormat=40 BC5 normal maps). See
// `docs/CODEC.md` "variant_2_3_4" section for the full spec.
//
// `mode`:
//   1 = output packed-3-bit indices (4×4 = 6 bytes per block)
//   2 = output BC4 alpha block (a0, a1, 6 index bytes — what BC5 wants)
//   3+ = output tiled stride pattern (used by some non-block formats)
//
// `body` / `body_size` is the compressed body for ONE channel. For BC5
// the dispatcher calls this twice (X then Y) and concatenates results.
//
// **NOT YET IMPLEMENTED** — currently returns false with a "comp=3 codec
// not yet ported" error. Implementation tracked in docs/STATE.md.
bool lh_decode_variant_2_3_4(const uint8_t* body, size_t body_size,
                             int mode, int width, int height,
                             std::vector<uint8_t>& out_bytes,
                             std::string* err = nullptr);
