#ifdef _WIN32
#include "play_audio.h"
#include "../../resource.h"

#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

namespace {

bool load_rcdata(int resource_id, std::vector<unsigned char>& out) {
    HMODULE mod = GetModuleHandleA(nullptr);
    HRSRC h = FindResourceA(mod, MAKEINTRESOURCEA(resource_id), (LPCSTR)RT_RCDATA);
    if (!h) return false;
    DWORD sz = SizeofResource(mod, h);
    if (!sz) return false;
    HGLOBAL g = LoadResource(mod, h);
    if (!g) return false;
    const void* p = LockResource(g);
    if (!p) return false;
    out.assign((const unsigned char*)p, (const unsigned char*)p + sz);
    return true;
}

}

bool BackgroundAudio::start_from_resource(int resource_id) {
    stop();

    if (!load_rcdata(resource_id, wav_bytes)) {
        return false;
    }

    running = true;

    if (!muted.load()) {
        PlaySoundA(reinterpret_cast<LPCSTR>(wav_bytes.data()), nullptr,
                   SND_MEMORY | SND_ASYNC | SND_LOOP | SND_NODEFAULT);
    }
    return true;
}

void BackgroundAudio::stop() {
    if (!running.exchange(false)) return;
    PlaySoundA(nullptr, nullptr, 0);
    wav_bytes.clear();
    wav_bytes.shrink_to_fit();
}

void BackgroundAudio::toggle_mute() {
    set_muted(!muted.load());
}

void BackgroundAudio::set_muted(bool m) {
    bool was = muted.exchange(m);
    if (m && !was) {

        PlaySoundA(nullptr, nullptr, 0);
    } else if (!m && was && running.load() && !wav_bytes.empty()) {
        PlaySoundA(reinterpret_cast<LPCSTR>(wav_bytes.data()), nullptr,
                   SND_MEMORY | SND_ASYNC | SND_LOOP | SND_NODEFAULT);
    }
}

#endif
