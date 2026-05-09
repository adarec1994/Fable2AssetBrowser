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
                              std::string* err = nullptr);
