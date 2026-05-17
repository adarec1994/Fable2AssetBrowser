#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace XmaDecoder {

class StreamDecoder {
public:
    StreamDecoder();
    ~StreamDecoder();
    StreamDecoder(const StreamDecoder&) = delete;
    StreamDecoder& operator=(const StreamDecoder&) = delete;

    bool open(const std::vector<uint8_t>& bytes, std::string* err = nullptr);

    int      sample_rate() const;
    int      channels()    const;
    uint64_t total_frames() const;

    bool decode_chunk(std::vector<int16_t>& out);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

bool decode_xma_to_pcm(const std::vector<uint8_t>& wav_bytes,
                      std::vector<int16_t>& pcm_out,
                      int& sample_rate,
                      int& channels,
                      std::string* err_out = nullptr);

bool decode_xma_wav_file_to_pcm_wav(const std::vector<uint8_t>& src_bytes,
                                    const std::string& out_path,
                                    std::string* err_out = nullptr);

}
