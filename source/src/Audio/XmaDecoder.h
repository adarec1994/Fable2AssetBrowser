#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XmaDecoder {

bool decode_xma_to_pcm(const std::vector<uint8_t>& wav_bytes,
                      std::vector<int16_t>& pcm_out,
                      int& sample_rate,
                      int& channels,
                      std::string* err_out = nullptr);

bool decode_xma_wav_file_to_pcm_wav(const std::vector<uint8_t>& src_bytes,
                                    const std::string& out_path,
                                    std::string* err_out = nullptr);

}
