#include "AudioPlayer.h"
#include "XmaDecoder.h"
#include "../Utilities/Files.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <vector>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

namespace AudioPlayer {

namespace {

struct PlayerState {
    std::atomic<bool> initialized{false};
    ma_device device{};

    std::mutex source_mutex;
    std::vector<int16_t> pcm;
    int sample_rate = 0;
    int channels = 0;
    std::string display_name;

    static constexpr int kWaveformSlots = 1024;
    std::vector<float> wf_low;
    std::vector<float> wf_high;

    std::atomic<uint64_t> cursor_frames{0};
    std::atomic<bool> playing{false};
    std::atomic<bool> loop{false};
    std::atomic<float> volume{0.85f};
};

PlayerState& state() {
    static PlayerState s;
    return s;
}

void audio_data_callback(ma_device* dev, void* output, const void* input, ma_uint32 frame_count) {
    (void)input;
    auto& S = state();
    int16_t* out = static_cast<int16_t*>(output);
    int channels = (int)dev->playback.channels;

    std::memset(out, 0, sizeof(int16_t) * frame_count * channels);

    if (!S.playing.load(std::memory_order_relaxed)) return;

    std::lock_guard<std::mutex> lk(S.source_mutex);
    if (S.pcm.empty() || S.channels <= 0 || S.sample_rate <= 0) return;

    const int src_ch = S.channels;
    const uint64_t total_frames = (uint64_t)(S.pcm.size() / (size_t)src_ch);
    if (total_frames == 0) return;

    uint64_t cursor = S.cursor_frames.load(std::memory_order_relaxed);
    const float gain = S.volume.load(std::memory_order_relaxed);

    for (ma_uint32 i = 0; i < frame_count; ++i) {
        if (cursor >= total_frames) {
            if (S.loop.load(std::memory_order_relaxed)) {
                cursor = 0;
            } else {
                S.playing.store(false, std::memory_order_relaxed);
                break;
            }
        }
        const int16_t* src = &S.pcm[(size_t)(cursor * (uint64_t)src_ch)];
        int16_t* dst = &out[(size_t)i * (size_t)channels];

        if (src_ch == channels) {
            for (int c = 0; c < channels; ++c) {
                int v = (int)((float)src[c] * gain);
                if (v >  32767) v =  32767;
                if (v < -32768) v = -32768;
                dst[c] = (int16_t)v;
            }
        } else if (src_ch == 1 && channels >= 2) {
            int v = (int)((float)src[0] * gain);
            if (v >  32767) v =  32767;
            if (v < -32768) v = -32768;
            for (int c = 0; c < channels; ++c) dst[c] = (int16_t)v;
        } else if (src_ch >= 2 && channels == 1) {

            int v = ((int)src[0] + (int)src[1]) / 2;
            v = (int)((float)v * gain);
            if (v >  32767) v =  32767;
            if (v < -32768) v = -32768;
            dst[0] = (int16_t)v;
        } else {

            const int ncopy = std::min(src_ch, channels);
            for (int c = 0; c < ncopy; ++c) {
                int v = (int)((float)src[c] * gain);
                if (v >  32767) v =  32767;
                if (v < -32768) v = -32768;
                dst[c] = (int16_t)v;
            }
        }
        ++cursor;
    }

    S.cursor_frames.store(cursor, std::memory_order_relaxed);
}

uint32_t rd_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint16_t rd_u16_le(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

bool decode_pcm_wav(const std::vector<uint8_t>& buf,
                    std::vector<int16_t>& pcm_out,
                    int& sample_rate,
                    int& channels) {
    if (buf.size() < 44) return false;

    size_t base = 0;
    if (buf.size() >= 4 && buf[0] == 'x' && buf[1] == 'm' && buf[2] == 'a' && buf[3] == 0) {
        base = 4;
    }
    if (buf.size() < base + 12) return false;
    if (std::memcmp(&buf[base], "RIFF", 4) != 0) return false;
    if (std::memcmp(&buf[base + 8], "WAVE", 4) != 0) return false;

    size_t pos = base + 12;
    int format = 0;
    int bits = 0;
    int rate = 0;
    int ch = 0;
    const uint8_t* data_ptr = nullptr;
    size_t data_size = 0;

    while (pos + 8 <= buf.size()) {
        uint32_t id = rd_u32_le(&buf[pos]);
        uint32_t sz = rd_u32_le(&buf[pos + 4]);
        size_t data_start = pos + 8;
        size_t data_end = data_start + sz;
        if (data_end > buf.size()) data_end = buf.size();

        if (id == 0x20746d66 ) {
            if (sz >= 16) {
                format = rd_u16_le(&buf[data_start + 0]);
                ch     = rd_u16_le(&buf[data_start + 2]);
                rate   = (int)rd_u32_le(&buf[data_start + 4]);
                bits   = rd_u16_le(&buf[data_start + 14]);
            }
        } else if (id == 0x61746164 ) {
            data_ptr = &buf[data_start];
            data_size = data_end - data_start;
            break;
        }
        pos = data_end + (sz & 1u);
    }

    if (!data_ptr || data_size == 0) return false;
    if (rate <= 0 || ch <= 0) return false;

    if (format != 1 && format != 3) return false;

    sample_rate = rate;
    channels = ch;

    if (format == 1 && bits == 16) {
        const size_t n_samples = data_size / 2;
        pcm_out.resize(n_samples);
        std::memcpy(pcm_out.data(), data_ptr, n_samples * 2);
        return true;
    }
    if (format == 1 && bits == 8) {
        const size_t n_samples = data_size;
        pcm_out.resize(n_samples);
        for (size_t i = 0; i < n_samples; ++i) {
            int v = (int)data_ptr[i] - 128;
            pcm_out[i] = (int16_t)(v << 8);
        }
        return true;
    }
    if (format == 1 && bits == 24) {
        const size_t n_samples = data_size / 3;
        pcm_out.resize(n_samples);
        for (size_t i = 0; i < n_samples; ++i) {
            int v = (int)data_ptr[i * 3 + 0] | ((int)data_ptr[i * 3 + 1] << 8) | ((int)(int8_t)data_ptr[i * 3 + 2] << 16);
            pcm_out[i] = (int16_t)(v >> 8);
        }
        return true;
    }
    if (format == 1 && bits == 32) {
        const size_t n_samples = data_size / 4;
        pcm_out.resize(n_samples);
        for (size_t i = 0; i < n_samples; ++i) {
            int32_t v = (int32_t)rd_u32_le(&data_ptr[i * 4]);
            pcm_out[i] = (int16_t)(v >> 16);
        }
        return true;
    }
    if (format == 3 && bits == 32) {
        const size_t n_samples = data_size / 4;
        pcm_out.resize(n_samples);
        for (size_t i = 0; i < n_samples; ++i) {
            float f;
            std::memcpy(&f, &data_ptr[i * 4], 4);
            if (f >  1.0f) f =  1.0f;
            if (f < -1.0f) f = -1.0f;
            pcm_out[i] = (int16_t)(f * 32767.0f);
        }
        return true;
    }
    return false;
}

}

bool ensure_initialized() {
    auto& S = state();
    if (S.initialized.load()) return true;

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_s16;
    cfg.playback.channels = 2;
    cfg.sampleRate        = 48000;
    cfg.dataCallback      = audio_data_callback;

    if (ma_device_init(nullptr, &cfg, &S.device) != MA_SUCCESS) {
        return false;
    }
    if (ma_device_start(&S.device) != MA_SUCCESS) {
        ma_device_uninit(&S.device);
        return false;
    }
    S.initialized.store(true);
    return true;
}

void shutdown() {
    auto& S = state();

    bool was = S.initialized.exchange(false);
    if (!was) return;

    S.playing.store(false);

    try {
        ma_device_stop(&S.device);
        ma_device_uninit(&S.device);
    } catch (...) {  }

    S.pcm.clear();
    S.wf_low.clear();
    S.wf_high.clear();
    S.sample_rate = 0;
    S.channels = 0;
    S.display_name.clear();
}

bool load_wav_bytes(const std::vector<uint8_t>& bytes,
                    const std::string& display_name,
                    std::string* err_out) {
    if (!ensure_initialized()) {
        if (err_out) *err_out = "audio device init failed";
        return false;
    }

    std::vector<int16_t> pcm;
    int rate = 0, ch = 0;

    if (!decode_pcm_wav(bytes, pcm, rate, ch)) {

        std::string xma_err;
        if (!XmaDecoder::decode_xma_to_pcm(bytes, pcm, rate, ch, &xma_err)) {
            if (err_out) {
                *err_out = "Unsupported WAV format. ";
                *err_out += xma_err;
            }
            return false;
        }
    }

    std::vector<float> wf_low(PlayerState::kWaveformSlots, 0.0f);
    std::vector<float> wf_high(PlayerState::kWaveformSlots, 0.0f);
    if (!pcm.empty() && ch > 0) {
        size_t total_frames = pcm.size() / (size_t)ch;
        if (total_frames > 0) {
            for (int slot = 0; slot < PlayerState::kWaveformSlots; ++slot) {
                size_t s0 = (size_t)((uint64_t)slot * total_frames / PlayerState::kWaveformSlots);
                size_t s1 = (size_t)((uint64_t)(slot + 1) * total_frames / PlayerState::kWaveformSlots);
                if (s1 <= s0) s1 = s0 + 1;
                if (s1 > total_frames) s1 = total_frames;
                int16_t lo = 0, hi = 0;
                for (size_t f = s0; f < s1; ++f) {

                    int sum = 0;
                    for (int c = 0; c < ch; ++c) sum += pcm[f * (size_t)ch + (size_t)c];
                    int v = sum / ch;
                    if (v < lo) lo = (int16_t)v;
                    if (v > hi) hi = (int16_t)v;
                }
                wf_low[slot]  = (float)lo / 32768.0f;
                wf_high[slot] = (float)hi / 32767.0f;
            }
        }
    }

    auto& S = state();
    {
        std::lock_guard<std::mutex> lk(S.source_mutex);
        S.pcm = std::move(pcm);
        S.sample_rate = rate;
        S.channels = ch;
        S.display_name = display_name;
        S.wf_low = std::move(wf_low);
        S.wf_high = std::move(wf_high);
    }
    S.cursor_frames.store(0);
    S.playing.store(false);

    if ((int)S.device.sampleRate != rate) {
        ma_device_uninit(&S.device);
        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format   = ma_format_s16;
        cfg.playback.channels = (ch >= 2) ? 2u : 1u;
        cfg.sampleRate        = (ma_uint32)rate;
        cfg.dataCallback      = audio_data_callback;
        if (ma_device_init(nullptr, &cfg, &S.device) != MA_SUCCESS) {
            S.initialized.store(false);
            if (err_out) *err_out = "device reinit failed";
            return false;
        }
        ma_device_start(&S.device);
    }
    return true;
}

bool load_wav_path(const std::string& path, std::string* err_out) {
    auto bytes = read_all_bytes(std::filesystem::path(path));
    if (bytes.empty()) {
        if (err_out) *err_out = "could not read file";
        return false;
    }
    return load_wav_bytes(std::vector<uint8_t>(bytes.begin(), bytes.end()),
                          std::filesystem::path(path).filename().string(),
                          err_out);
}

bool has_source() {
    auto& S = state();
    std::lock_guard<std::mutex> lk(S.source_mutex);
    return !S.pcm.empty() && S.sample_rate > 0 && S.channels > 0;
}

static void rewind_if_finished() {
    auto& S = state();
    std::lock_guard<std::mutex> lk(S.source_mutex);
    if (S.channels <= 0 || S.pcm.empty()) return;
    uint64_t total = (uint64_t)(S.pcm.size() / (size_t)S.channels);
    if (S.cursor_frames.load() >= total) {
        S.cursor_frames.store(0);
    }
}

void play() {
    if (!has_source()) return;
    rewind_if_finished();
    state().playing.store(true);
}

void pause() { state().playing.store(false); }

void toggle_play() {
    auto& S = state();
    if (S.playing.load()) {
        S.playing.store(false);
        return;
    }
    if (!has_source()) return;
    rewind_if_finished();
    S.playing.store(true);
}

void stop() {
    auto& S = state();
    S.playing.store(false);
    S.cursor_frames.store(0);
}

void seek_fraction(float t) {
    auto& S = state();
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    std::lock_guard<std::mutex> lk(S.source_mutex);
    if (S.channels <= 0 || S.pcm.empty()) return;
    uint64_t total = (uint64_t)(S.pcm.size() / (size_t)S.channels);
    S.cursor_frames.store((uint64_t)((double)t * (double)total));
}

void set_volume(float v) {
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    state().volume.store(v);
}
float get_volume() { return state().volume.load(); }

void set_loop(bool loop) { state().loop.store(loop); }
bool get_loop() { return state().loop.load(); }

bool is_playing() { return state().playing.load(); }

double get_time_sec() {
    auto& S = state();
    std::lock_guard<std::mutex> lk(S.source_mutex);
    if (S.sample_rate <= 0) return 0.0;
    return (double)S.cursor_frames.load() / (double)S.sample_rate;
}

double get_duration_sec() {
    auto& S = state();
    std::lock_guard<std::mutex> lk(S.source_mutex);
    if (S.sample_rate <= 0 || S.channels <= 0 || S.pcm.empty()) return 0.0;
    uint64_t total = (uint64_t)(S.pcm.size() / (size_t)S.channels);
    return (double)total / (double)S.sample_rate;
}

std::string get_display_name() {
    auto& S = state();
    std::lock_guard<std::mutex> lk(S.source_mutex);
    return S.display_name;
}

int get_sample_rate() {
    auto& S = state();
    std::lock_guard<std::mutex> lk(S.source_mutex);
    return S.sample_rate;
}

int get_channels() {
    auto& S = state();
    std::lock_guard<std::mutex> lk(S.source_mutex);
    return S.channels;
}

int get_waveform_peaks(float* low_out, float* high_out, int max_slots) {
    auto& S = state();
    std::lock_guard<std::mutex> lk(S.source_mutex);
    if (S.wf_low.empty() || S.wf_high.empty()) return 0;
    int n = std::min(max_slots, (int)S.wf_low.size());
    for (int i = 0; i < n; ++i) {
        low_out[i]  = S.wf_low[i];
        high_out[i] = S.wf_high[i];
    }
    return n;
}

}
