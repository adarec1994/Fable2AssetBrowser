#pragma once
#include <windows.h>
#include <string>
#include <thread>
#include <atomic>

class BackgroundAudio {
public:
    static BackgroundAudio& instance() {
        static BackgroundAudio inst;
        return inst;
    }

    void start(const std::string& wav_path);
    void stop();
    void toggle_mute();
    bool is_muted() const { return muted.load(); }
    void set_muted(bool m);

private:
    BackgroundAudio() = default;
    ~BackgroundAudio() { stop(); }
    
    std::thread audio_thread;
    std::atomic<bool> running{false};
    std::atomic<bool> muted{false};
    std::string audio_path;
    
    void audio_loop();
};