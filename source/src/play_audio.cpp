#include "play_audio.h"
#include <mmsystem.h>
#include <fstream>

#pragma comment(lib, "winmm.lib")

void BackgroundAudio::start(const std::string& wav_path) {
    stop();
    
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    std::string exe_dir = exe_path;
    size_t last_slash = exe_dir.find_last_of("\\/");
    if (last_slash != std::string::npos) {
        exe_dir = exe_dir.substr(0, last_slash + 1);
    }

    audio_path = exe_dir + "..\\" + wav_path;
    running = true;

    audio_thread = std::thread([this]() { audio_loop(); });
}

void BackgroundAudio::stop() {
    if (running) {
        running = false;
        PlaySound(NULL, NULL, 0);
        if (audio_thread.joinable()) {
            audio_thread.join();
        }
    }
}

void BackgroundAudio::toggle_mute() {
    bool new_state = !muted.load();
    set_muted(new_state);
}

void BackgroundAudio::set_muted(bool m) {
    bool was_muted = muted.load();
    muted = m;

    if (m && !was_muted) {
        PlaySound(NULL, NULL, 0);
    } else if (!m && was_muted && running && !audio_path.empty()) {
        PlaySoundA(audio_path.c_str(), NULL, SND_FILENAME | SND_ASYNC | SND_LOOP | SND_NODEFAULT);
    }
}

void BackgroundAudio::audio_loop() {
    while (running) {
        if (!muted.load() && !audio_path.empty()) {
            std::ifstream test(audio_path, std::ios::binary);
            if (!test.good()) {
                running = false;
                break;
            }
            test.close();

            if (!PlaySoundA(audio_path.c_str(), NULL, SND_FILENAME | SND_ASYNC | SND_LOOP | SND_NODEFAULT)) {
                running = false;
                break;
            }

            while (running) {
                Sleep(100);
            }
        } else {
            Sleep(100);
        }
    }
}