#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

bool lh_decode_compressed_mip(const uint8_t* body, size_t body_size,
                              int& out_width, int& out_height,
                              std::vector<uint8_t>& out_bc1,
                              std::string* err = nullptr,
                              bool comp11_layout = false);

inline bool lh_decode_compressed_mip_v11(const uint8_t* body, size_t body_size,
                                         int& out_width, int& out_height,
                                         std::vector<uint8_t>& out_bc1,
                                         std::string* err = nullptr) {
    return lh_decode_compressed_mip(body, body_size, out_width, out_height,
                                    out_bc1, err, true);
}

bool lh_decode_variant_2_3_4(const uint8_t* body, size_t body_size,
                             int mode, int width, int height,
                             std::vector<uint8_t>& out_bytes,
                             std::string* err = nullptr);
