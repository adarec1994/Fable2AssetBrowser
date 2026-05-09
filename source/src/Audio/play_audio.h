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

    // Start looping playback of the WAV bytes embedded in the exe under
    // the given RCDATA resource id. The resource is loaded once into
    // an internal buffer and replayed via PlaySoundA(SND_MEMORY|SND_LOOP).
    // Returns false if the resource couldn't be loaded.
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
    std::vector<unsigned char> wav_bytes;  // owned, kept alive while playing
};

#else
// Stub for Linux - no audio support
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
