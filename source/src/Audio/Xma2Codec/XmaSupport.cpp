#include "XmaSupport.h"

#include <algorithm>
#include <bitset>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <share.h>
#endif

namespace Xma {

namespace {
constexpr uint64_t default_masks[9] = {
    0,
    0x00000004,
    0x00000003,
    0x00000007,
    0x00000033,
    0x00000037,
    0x0000003F,
    0x0000063F,
    0x000000FF,
};
}  // namespace

int channel_layout_popcount(uint64_t mask) {
    return int(std::bitset<64>(mask).count());
}

void channel_layout_default(ChannelLayout& layout, int nb_channels) {
    layout.nb_channels = nb_channels;
    if (nb_channels >= 0 && nb_channels <= 8) {
        layout.mask = default_masks[nb_channels];
        layout.order = nb_channels == 0 ? ChannelOrder::Unspec
                                        : ChannelOrder::Native;
    } else {
        layout.mask = nb_channels >= 64 ? ~uint64_t(0)
                                        : (uint64_t(1) << nb_channels) - 1;
        layout.order = ChannelOrder::Native;
    }
}

void channel_layout_from_mask(ChannelLayout& layout, uint64_t mask) {
    layout.mask = mask;
    layout.nb_channels = channel_layout_popcount(mask);
    layout.order = mask ? ChannelOrder::Native : ChannelOrder::Unspec;
}

void Frame::allocate(int channels, int samples) {
    nb_channels = channels;
    nb_samples  = samples;
    for (int c = 0; c < channels && c < int(planes.size()); ++c) {
        planes[c].assign(samples, 0.0f);
    }
}

void Frame::clear() {
    for (auto& p : planes) p.clear();
    nb_samples = 0;
    nb_channels = 0;
    sample_rate = 0;
}

void fltp_to_s16_interleaved(const float* const* planes,
                             int16_t* dst,
                             int nb_samples,
                             int nb_channels) {
    constexpr float kScale = 32767.0f;
    for (int i = 0; i < nb_samples; ++i) {
        for (int c = 0; c < nb_channels; ++c) {
            float v = planes[c][i] * kScale;
            v = std::max(-32768.0f, std::min(32767.0f, v));
            dst[i * nb_channels + c] = int16_t(v);
        }
    }
}

namespace {

std::mutex& log_mutex() {
    static std::mutex m;
    return m;
}

bool& log_reset_flag() {
    static bool b = false;
    return b;
}

std::string exe_dir() {
#if defined(_WIN32)
    char buf[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::string s(buf, buf + n);
        const auto pos = s.find_last_of("\\/");
        if (pos != std::string::npos) return s.substr(0, pos);
    }
#endif
    return ".";
}

FILE* open_log() {
    static FILE* fp = nullptr;
    static bool tried_open = false;
    if (log_reset_flag()) {
        if (fp) { std::fclose(fp); fp = nullptr; }
        log_reset_flag() = false;
        tried_open = false;
    }
    if (!fp && !tried_open) {
        tried_open = true;
        const std::string path = exe_dir() + "/xma_debug.log";
#if defined(_WIN32)
        fp = _fsopen(path.c_str(), "wb", _SH_DENYNO);
#else
        fp = std::fopen(path.c_str(), "wb");
#endif
        if (fp) {
            std::fprintf(fp, "==== xma_debug.log opened ====\n");
            std::fflush(fp);
        }
    }
    return fp;
}

}  // namespace

int g_log_packet_budget = 0;
int g_log_frame_budget = 0;
int g_log_subframe_budget = 0;

void log_reset() {
    std::lock_guard<std::mutex> g(log_mutex());
    log_reset_flag() = true;
    g_log_packet_budget   = 6;
    g_log_frame_budget    = 6;
    g_log_subframe_budget = 30;
}

void log_msg(const char* fmt, ...) {
    std::lock_guard<std::mutex> g(log_mutex());
    FILE* fp = open_log();
    if (!fp) return;
#if defined(_WIN32)
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::fprintf(fp, "[%02u:%02u:%02u.%03u] ",
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
#else
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    std::fprintf(fp, "[%02d:%02d:%02d] ", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
#endif
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(fp, fmt, ap);
    va_end(ap);
    if (!fmt || !*fmt || fmt[std::strlen(fmt) - 1] != '\n') std::fputc('\n', fp);
    std::fflush(fp);
}

void log_hexdump(const char* label, const void* data, std::size_t n) {
    std::lock_guard<std::mutex> g(log_mutex());
    FILE* fp = open_log();
    if (!fp) return;
    std::fprintf(fp, "  %s (%zu bytes):", label, n);
    const auto* b = static_cast<const uint8_t*>(data);
    const std::size_t cap = std::min<std::size_t>(n, 64);
    for (std::size_t i = 0; i < cap; ++i) {
        if ((i % 16) == 0) std::fprintf(fp, "\n    ");
        std::fprintf(fp, "%02X ", b[i]);
    }
    if (n > cap) std::fprintf(fp, "...");
    std::fputc('\n', fp);
    std::fflush(fp);
}

void log_flush() {
    std::lock_guard<std::mutex> g(log_mutex());
    if (FILE* fp = open_log()) std::fflush(fp);
}

}  // namespace Xma
