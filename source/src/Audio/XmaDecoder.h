#pragma once
//
// XmaDecoder — Xbox 360 XMA2 -> PCM decoder using FFmpeg's libavcodec.
//
// Compiled in only when the project finds FFmpeg under external/ffmpeg/
// at CMake configure time (sets F2_HAVE_FFMPEG=1). Without it, the
// implementation is a stub that returns false with an explanatory error.
//

#include <cstdint>
#include <string>
#include <vector>

namespace XmaDecoder {

// Decode an XMA2 RIFF/WAVE buffer into interleaved int16 PCM.
//
// Accepts the RAW bytes as found in the BNK — i.e. it tolerates the leading
// "xma\0" magic prefix and broken RIFF size fields.
//
// On success: sample_rate / channels are filled, pcm holds the full decoded
// stream interleaved L,R,L,R,... and the function returns true.
//
// On failure: returns false and (if err_out non-null) puts a short reason
// string in *err_out.
bool decode_xma_to_pcm(const std::vector<uint8_t>& wav_bytes,
                      std::vector<int16_t>& pcm_out,
                      int& sample_rate,
                      int& channels,
                      std::string* err_out = nullptr);

// Convenience: decode an XMA2 RIFF/WAVE file into a standard PCM RIFF/WAVE
// at `out_path`. Used by the "Extract WAV" button so users get a usable
// .wav file instead of the raw XMA2 archive payload.
bool decode_xma_wav_file_to_pcm_wav(const std::vector<uint8_t>& src_bytes,
                                    const std::string& out_path,
                                    std::string* err_out = nullptr);

} // namespace XmaDecoder
