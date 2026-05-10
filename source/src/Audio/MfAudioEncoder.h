// MfAudioEncoder — PCM s16 → MP3 / AAC via Windows Media Foundation.
//
// Why Media Foundation: our prebuilt libavcodec.lib was compiled
// decoders-only (XMA1/XMA2/WMAPro), so the FFmpeg encoder path can't
// link in MP3 or AAC without rebuilding the dependency. MF ships
// MP3 and AAC encoders with every modern Windows install (Windows 8+
// for MP3) and is already partly linked through mfplat / mfuuid /
// strmiids — adding the SinkWriter (mfreadwrite.lib) gets us the rest.
//
// The two functions below take a PCM s16 buffer (interleaved stereo
// or mono — anything MF accepts), feed it through MFSinkWriter, and
// land an MP3 or M4A (AAC-in-MP4) file at the given path. Both block
// on the calling thread; callers route them through their own worker
// pattern. Sample rate and channel count come from the source XMA;
// MF picks an encoder bitrate that matches.
//
// Windows-only by design — there's a `#ifdef _WIN32` stub on the
// non-Windows branch so the rest of the codebase keeps building.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MfAudio {

// Encode PCM s16 LE samples to MP3 at `out_path_utf8`. Returns true
// on success; on failure writes a human-readable reason into `err`
// (if non-null) and leaves any partial file on disk for inspection.
//
// `pcm` is interleaved (LRLRLR…) for stereo, plain mono for channels=1.
// Sample rate / channel count must match what the decoder produced.
// The encoded bitrate is hard-coded to 192 kbps stereo / 128 kbps mono
// — solid quality for archival, no bitrate dialog needed.
bool encode_pcm_to_mp3(const std::vector<int16_t>& pcm,
                       int sample_rate, int channels,
                       const std::string& out_path_utf8,
                       std::string* err);

// Encode PCM s16 LE samples to AAC, wrapped in an MP4 container,
// at `out_path_utf8`. The path SHOULD end in `.m4a` or `.mp4` —
// SinkWriter selects the muxer from the extension and an unrecognised
// one yields E_FAIL on creation. Returns true on success.
//
// AAC bitrate is fixed at 192 kbps stereo / 128 kbps mono (same
// reasoning as MP3 above).
bool encode_pcm_to_aac(const std::vector<int16_t>& pcm,
                       int sample_rate, int channels,
                       const std::string& out_path_utf8,
                       std::string* err);

} // namespace MfAudio
