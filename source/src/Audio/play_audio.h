#pragma once

#ifdef _WIN32
#include <windows.h>
#include <atomic>
#include <vector>

class BackgroundAudio {
public:
    static BackgroundAudio& instance() {
        static BackgroundAudio inst;
        return inst;
    }

    bool start_from_resource(int resource_id);

    void stop();
    void toggle_mute();
    bool is_muted() const { return muted.load(); }
    void set_muted(bool m);

private:
    BackgroundAudio() = default;
    ~BackgroundAudio() { stop(); }

    std::atomic<bool> running{false};
    std::atomic<bool> muted{false};
    std::vector<unsigned char> wav_bytes;
};

#else

class BackgroundAudio {
public:
    static BackgroundAudio& instance() {
        static BackgroundAudio inst;
        return inst;
    }
    bool start_from_resource(int) { return false; }
    void stop() {}
    void toggle_mute() { muted = !muted; }
    bool is_muted() const { return muted; }
    void set_muted(bool m) { muted = m; }

private:
    BackgroundAudio() = default;
    bool muted = false;
};
#endif
