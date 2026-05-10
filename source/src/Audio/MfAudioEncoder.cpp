// MfAudioEncoder — Windows Media Foundation PCM→MP3 / PCM→AAC.
//
// Implementation: MFSinkWriter for both formats. The path's extension
// drives the muxer choice (`.mp3` → MP3 stream sink, `.m4a` / `.mp4`
// → MP4/AAC), so we don't hand-roll container packing here. Each call
// goes through:
//
//   1. MFStartup
//   2. MFCreateSinkWriterFromURL(path)
//   3. AddStream(output_type)         — MP3 / AAC profile
//   4. SetInputMediaType(stream, pcm_type)
//   5. BeginWriting / WriteSample / Finalize
//   6. MFShutdown
//
// One sample buffer carries the entire PCM payload — Fable 2 audio
// clips top out around a few minutes of stereo, so a few-MB sample
// is well inside MF's tolerance and avoids the timestamp bookkeeping
// you'd need for chunked writes. The sample's duration is set
// explicitly to (frames / sample_rate) seconds in 100-ns units so
// the muxer doesn't wait on a clock reference.
//
// All COM objects are managed by ComPtr (wrl/client.h) — release
// is automatic on scope exit, including the early-returns on error.

#include "MfAudioEncoder.h"

#ifdef _WIN32

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>

extern "C" {
// libswresample is already linked (XmaDecoder uses it for the
// XMA→PCM channel/rate normalisation) — reuse it here for the
// PCM→encoder rate adaptation. The MF MP3 encoder only accepts
// 32 / 44.1 / 48 kHz input; the AAC encoder only accepts 44.1 /
// 48 kHz. Fable 2 audio decodes at a wider variety of native
// rates (16k, 22.05k, 24k, …) so we resample before handing the
// buffer to SinkWriter.
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
}

#include <cstring>
#include <cstdio>

using Microsoft::WRL::ComPtr;

namespace MfAudio {

namespace {

// RAII wrapper around MFStartup / MFShutdown. MF calls fail outright
// if MFStartup hasn't run on the current thread; bracketing each
// encode call with this keeps the lifetimes predictable and avoids
// a long-running global init we'd have to tear down at exit.
struct MfScope {
    HRESULT hr;
    MfScope() : hr(MFStartup(MF_VERSION)) {}
    ~MfScope() { if (SUCCEEDED(hr)) MFShutdown(); }
};

// UTF-8 → wide for the SinkWriter URL. MF takes wide strings; our
// caller passes UTF-8 because that's the project-wide convention.
std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
                        w.data(), n);
    return w;
}

// Build the input PCM media type. MF wants every audio attribute set
// — leaving any of these out yields MF_E_INVALIDMEDIATYPE on
// SetInputMediaType. The bytes-per-sample / block-align math is
// trivial but easy to typo, so it lives here once.
HRESULT make_pcm_input_type(int sample_rate, int channels,
                            ComPtr<IMFMediaType>& out) {
    HRESULT hr = MFCreateMediaType(&out);
    if (FAILED(hr)) return hr;
    out->SetGUID  (MF_MT_MAJOR_TYPE,           MFMediaType_Audio);
    out->SetGUID  (MF_MT_SUBTYPE,              MFAudioFormat_PCM);
    out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,        16);
    out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,     (UINT32)sample_rate);
    out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,           (UINT32)channels);
    out->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,
                   (UINT32)(channels * 2));
    out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                   (UINT32)(sample_rate * channels * 2));
    out->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    return S_OK;
}

// Wrap the entire PCM payload in a single IMFSample. The buffer is
// memcpy'd into MF-allocated memory because the caller's vector
// could move/realloc out from under us (it doesn't here, but the
// pattern stays defensive).
HRESULT make_sample(const int16_t* pcm, size_t n_samples,
                    int sample_rate, int channels,
                    ComPtr<IMFSample>& out) {
    const size_t pcm_bytes = n_samples * sizeof(int16_t);
    ComPtr<IMFMediaBuffer> buf;
    HRESULT hr = MFCreateMemoryBuffer((DWORD)pcm_bytes, &buf);
    if (FAILED(hr)) return hr;
    BYTE* dst = nullptr;
    DWORD max_len = 0, cur_len = 0;
    hr = buf->Lock(&dst, &max_len, &cur_len);
    if (FAILED(hr)) return hr;
    std::memcpy(dst, pcm, pcm_bytes);
    buf->Unlock();
    buf->SetCurrentLength((DWORD)pcm_bytes);

    hr = MFCreateSample(&out);
    if (FAILED(hr)) return hr;
    out->AddBuffer(buf.Get());

    // Duration in 100-ns units. MF's SinkWriter uses this to decide
    // when to flush; without it the file ends up empty.
    LONGLONG dur_100ns = (LONGLONG)(
        (double)n_samples / (double)channels /
        (double)sample_rate * 1.0e7);
    out->SetSampleTime(0);
    out->SetSampleDuration(dur_100ns);
    return S_OK;
}

// Pick an MF AAC AVG_BYTES_PER_SECOND constant for the given channel
// count. The AAC encoder only accepts a fixed set of bitrate values:
// 12000 / 16000 / 20000 / 24000 bytes-per-second (≈ 96 / 128 / 160 /
// 192 kbps). Anything outside that list yields MF_E_INVALIDMEDIATYPE.
UINT32 aac_avg_bps(int channels) {
    return (channels >= 2) ? 24000u : 16000u;   // 192k stereo, 128k mono
}

// MF MP3 encoder accepts a similar fixed set of bitrates expressed
// the same way. 24000 = 192 kbps; 16000 = 128 kbps.
UINT32 mp3_avg_bps(int channels) {
    return (channels >= 2) ? 24000u : 16000u;
}

// Pick a target sample rate that's actually accepted by the encoder
// MFT we're about to feed. Anything outside the lists below yields
// MF_E_INVALIDMEDIATYPE on SetInputMediaType — which is the bug the
// user hit on every Fable 2 .wav at 22050 / 24000 / 16000 Hz.
//
// Strategy: pick the smallest accepted rate that's ≥ `source_rate`,
// so we never lose detail. If even the highest accepted rate is
// below the source we fall back to that (extremely unlikely — Fable 2
// caps out at 48 kHz). For aac we omit 32 kHz; the MF AAC encoder's
// PCM input list is 44.1 / 48 kHz only.
int pick_target_rate(int source_rate, bool aac) {
    static constexpr int mp3_rates[] = { 32000, 44100, 48000 };
    static constexpr int aac_rates[] = { 44100, 48000 };
    const int* rates  = aac ? aac_rates : mp3_rates;
    const int  count  = aac ? 2 : 3;
    for (int i = 0; i < count; ++i) {
        if (rates[i] >= source_rate) return rates[i];
    }
    return rates[count - 1];
}

// Resample interleaved s16 PCM `pcm` from `in_rate` to `target_rate`
// via libswresample. No-op fast-path when the rates already match
// (just copies the buffer reference). Channel count is preserved —
// the encoder accepts whatever the source had so we don't downmix.
bool resample_pcm_s16(const std::vector<int16_t>& pcm,
                      int in_rate, int channels, int target_rate,
                      std::vector<int16_t>& out,
                      std::string* err) {
    if (in_rate == target_rate) {
        out = pcm;
        return true;
    }
    AVChannelLayout layout;
    av_channel_layout_default(&layout, channels);

    SwrContext* swr = nullptr;
    if (swr_alloc_set_opts2(
            &swr,
            &layout, AV_SAMPLE_FMT_S16, target_rate,
            &layout, AV_SAMPLE_FMT_S16, in_rate,
            0, nullptr) < 0 || swr_init(swr) < 0) {
        if (swr) swr_free(&swr);
        if (err) *err = "swresample setup failed";
        return false;
    }

    const int64_t in_samples = (int64_t)pcm.size() / channels;
    // Conservative output size — av_rescale_rnd with AV_ROUND_UP
    // gives us a couple of frames of headroom over the strict ratio
    // so swr_convert never returns AVERROR_INVALIDDATA from a short
    // destination buffer.
    const int64_t out_samples_max = av_rescale_rnd(
        swr_get_delay(swr, in_rate) + in_samples,
        target_rate, in_rate, AV_ROUND_UP);
    out.assign((size_t)(out_samples_max * channels), 0);

    const uint8_t* in_data[1]  = { (const uint8_t*)pcm.data() };
    uint8_t*       out_data[1] = { (uint8_t*)out.data() };
    const int got = swr_convert(
        swr,
        out_data, (int)out_samples_max,
        in_data,  (int)in_samples);

    swr_free(&swr);
    if (got < 0) {
        out.clear();
        if (err) *err = "swr_convert failed";
        return false;
    }
    out.resize((size_t)got * (size_t)channels);
    return true;
}

// Common encode loop body — given a configured output media type,
// run the SinkWriter pipeline. Pulled out so the MP3 and AAC
// front-ends share the bulk of the COM dance.
bool encode_with_output_type(const std::vector<int16_t>& pcm,
                             int sample_rate, int channels,
                             const std::string& out_path_utf8,
                             const ComPtr<IMFMediaType>& out_type,
                             std::string* err) {
    auto fail = [&](const char* where, HRESULT hr) -> bool {
        if (err) {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "%s failed (HRESULT 0x%08lX)",
                          where, (unsigned long)hr);
            *err = buf;
        }
        return false;
    };

    if (pcm.empty() || sample_rate <= 0 || channels <= 0)
        return fail("input check", E_INVALIDARG);

    MfScope mf;
    if (FAILED(mf.hr)) return fail("MFStartup", mf.hr);

    ComPtr<IMFSinkWriter> writer;
    HRESULT hr = MFCreateSinkWriterFromURL(
        widen(out_path_utf8).c_str(), nullptr, nullptr, &writer);
    if (FAILED(hr)) return fail("MFCreateSinkWriterFromURL", hr);

    DWORD stream_idx = 0;
    hr = writer->AddStream(out_type.Get(), &stream_idx);
    if (FAILED(hr)) return fail("AddStream", hr);

    ComPtr<IMFMediaType> in_type;
    hr = make_pcm_input_type(sample_rate, channels, in_type);
    if (FAILED(hr)) return fail("make_pcm_input_type", hr);
    hr = writer->SetInputMediaType(stream_idx, in_type.Get(), nullptr);
    if (FAILED(hr)) return fail("SetInputMediaType", hr);

    hr = writer->BeginWriting();
    if (FAILED(hr)) return fail("BeginWriting", hr);

    ComPtr<IMFSample> sample;
    hr = make_sample(pcm.data(), pcm.size(), sample_rate, channels, sample);
    if (FAILED(hr)) return fail("make_sample", hr);

    hr = writer->WriteSample(stream_idx, sample.Get());
    if (FAILED(hr)) return fail("WriteSample", hr);

    hr = writer->Finalize();
    if (FAILED(hr)) return fail("Finalize", hr);
    return true;
}

} // anonymous

bool encode_pcm_to_mp3(const std::vector<int16_t>& pcm,
                       int sample_rate, int channels,
                       const std::string& out_path_utf8,
                       std::string* err) {
    // Step 1 — adapt the input rate to one the MF MP3 encoder
    // accepts. Source-rate-passthrough is the fast path; anything
    // else gets bumped up via swresample. Without this step rates
    // like 22050 (common in Fable 2 dialogue) yield
    // MF_E_INVALIDMEDIATYPE from SetInputMediaType.
    const int target_rate = pick_target_rate(sample_rate, /*aac=*/false);
    const std::vector<int16_t>* effective = &pcm;
    std::vector<int16_t> resampled;
    if (target_rate != sample_rate) {
        if (!resample_pcm_s16(pcm, sample_rate, channels, target_rate,
                              resampled, err)) {
            return false;
        }
        effective = &resampled;
    }

    // MF's MP3 encoder expects MFAudioFormat_MP3 with an explicit
    // bitrate. The decoder side uses the same subtype GUID.
    ComPtr<IMFMediaType> out_type;
    HRESULT hr = MFCreateMediaType(&out_type);
    if (FAILED(hr)) {
        if (err) *err = "MFCreateMediaType failed";
        return false;
    }
    out_type->SetGUID  (MF_MT_MAJOR_TYPE,           MFMediaType_Audio);
    out_type->SetGUID  (MF_MT_SUBTYPE,              MFAudioFormat_MP3);
    out_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,           (UINT32)channels);
    out_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,     (UINT32)target_rate);
    out_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                        mp3_avg_bps(channels));
    return encode_with_output_type(*effective, target_rate, channels,
                                   out_path_utf8, out_type, err);
}

bool encode_pcm_to_aac(const std::vector<int16_t>& pcm,
                       int sample_rate, int channels,
                       const std::string& out_path_utf8,
                       std::string* err) {
    // Same input-rate adaptation as the MP3 path. AAC's accepted
    // list is even narrower (44.1 / 48 only — no 32) so most non-
    // standard sources end up resampled.
    const int target_rate = pick_target_rate(sample_rate, /*aac=*/true);
    const std::vector<int16_t>* effective = &pcm;
    std::vector<int16_t> resampled;
    if (target_rate != sample_rate) {
        if (!resample_pcm_s16(pcm, sample_rate, channels, target_rate,
                              resampled, err)) {
            return false;
        }
        effective = &resampled;
    }

    // AAC output type per the MS docs:
    //   - subtype = MFAudioFormat_AAC
    //   - bits-per-sample = 16  (input bit depth, not output)
    //   - profile-level-indication = 0x29 (AAC-LC, the safe default)
    //   - payload-type = 0       (raw AAC frames, MP4 muxer wraps them)
    ComPtr<IMFMediaType> out_type;
    HRESULT hr = MFCreateMediaType(&out_type);
    if (FAILED(hr)) {
        if (err) *err = "MFCreateMediaType failed";
        return false;
    }
    out_type->SetGUID  (MF_MT_MAJOR_TYPE,           MFMediaType_Audio);
    out_type->SetGUID  (MF_MT_SUBTYPE,              MFAudioFormat_AAC);
    out_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,        16);
    out_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,     (UINT32)target_rate);
    out_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,           (UINT32)channels);
    out_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                        aac_avg_bps(channels));
    out_type->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE,             0);
    out_type->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
    return encode_with_output_type(*effective, target_rate, channels,
                                   out_path_utf8, out_type, err);
}

} // namespace MfAudio

#else  // !_WIN32

namespace MfAudio {
bool encode_pcm_to_mp3(const std::vector<int16_t>&,
                       int, int, const std::string&,
                       std::string* err) {
    if (err) *err = "MP3 encoding is Windows-only in this build";
    return false;
}
bool encode_pcm_to_aac(const std::vector<int16_t>&,
                       int, int, const std::string&,
                       std::string* err) {
    if (err) *err = "AAC encoding is Windows-only in this build";
    return false;
}
} // namespace MfAudio

#endif // _WIN32
