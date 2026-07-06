#ifdef _WIN32
#include "AutoPilot.h"

#include "State.h"
#include "../Level/LevelLoader.h"
#include "../UI/ModelPreview.h"
#include "../UI/UI_Panels.h"
#include "../UI/Layout/LoadingScreen.h"
#include "../UI/OutputLog.h"
#include "../textures/export/TextureExport.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

extern bool g_mp_vista_only;
extern ModelPreview g_mp;

namespace {

enum class Stage {
    Idle,
    OpenRoot,
    WaitIndex,
    OpenLevel,
    WaitLevel,
    Settle,
    Capture,
    Done,
};

Stage       g_stage = Stage::Idle;
std::string g_level_query;
std::string g_shot_path;
bool        g_auto_exit = false;
bool        g_vista_only = false;
float       g_time_of_day = -1.0f;   // hours 0..24; <0 = leave untouched
int         g_settle_frames = 150;
int         g_countdown = 0;
int         g_timeout_frames = 0;
bool        g_capture_now = false;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

void finish(const std::string& msg, bool ok) {
    if (ok) OutputLog::success("autopilot: " + msg);
    else    OutputLog::error("autopilot: " + msg);
    g_stage = Stage::Done;
    if (g_auto_exit) PostQuitMessage(ok ? 0 : 1);
}

}

void AutoPilot_Init() {
    for (int i = 1; i < __argc; ++i) {
        const std::string arg = __argv[i];
        auto value = [&](const char* key) -> const char* {
            const size_t n = std::strlen(key);
            if (arg.compare(0, n, key) == 0 && arg.size() > n &&
                arg[n] == '=') {
                return arg.c_str() + n + 1;
            }
            return nullptr;
        };
        if (const char* v = value("--autoload")) g_level_query = lower(v);
        else if (const char* v = value("--autoshot")) g_shot_path = v;
        else if (const char* v = value("--autowait"))
            g_settle_frames = std::max(1, std::atoi(v));
        else if (const char* v = value("--autotime"))
            g_time_of_day = float(std::atof(v));
        else if (arg == "--autoexit") g_auto_exit = true;
        else if (arg == "--vistaonly") g_vista_only = true;
    }
    if (!g_level_query.empty()) {
        g_stage = Stage::OpenRoot;
        g_timeout_frames = 60 * 60 * 5;
        OutputLog::info("autopilot: will load level matching '" +
                        g_level_query + "'" +
                        (g_shot_path.empty()
                             ? std::string()
                             : " and capture to " + g_shot_path));
    }
}

void AutoPilot_Tick() {
    if (g_stage == Stage::Idle || g_stage == Stage::Done) return;
    if (--g_timeout_frames <= 0) {
        finish("timed out", false);
        return;
    }

    switch (g_stage) {
    case Stage::OpenRoot: {
        if (!S.root_dir.empty()) {
            g_stage = Stage::WaitIndex;
            break;
        }
        if (S.last_dir.empty() ||
            !std::filesystem::is_directory(S.last_dir)) {
            finish("no saved root directory to open", false);
            break;
        }
        open_folder_logic(S.last_dir);
        if (S.root_dir.empty()) {
            finish("failed to open root " + S.last_dir, false);
            break;
        }
        g_stage = Stage::WaitIndex;
        break;
    }
    case Stage::WaitIndex: {
        if (UI::loading_in_progress()) break;
        if (S.all_level_files.empty()) break;
        g_countdown = 10;
        g_stage = Stage::OpenLevel;
        break;
    }
    case Stage::OpenLevel: {
        if (--g_countdown > 0) break;
        const FlatAssetEntry* best = nullptr;
        for (const auto& e : S.all_level_files) {
            const std::string full =
                lower(e.full_path.empty() ? e.name : e.full_path);
            if (full.find(g_level_query) == std::string::npos) continue;
            if (!best) best = &e;
            if (full.find("mainlevel") != std::string::npos) {
                best = &e;
                break;
            }
        }
        if (!best) {
            finish("no level matches '" + g_level_query + "'", false);
            break;
        }
        OutputLog::info("autopilot: opening level " +
                        (best->full_path.empty() ? best->name
                                                 : best->full_path));
        Level::OpenAsync(*best);
        g_countdown = 30;
        g_stage = Stage::WaitLevel;
        break;
    }
    case Stage::WaitLevel: {
        if (--g_countdown > 0) break;
        if (Level::IsAsyncLoadInProgress()) break;
        if (g_pending_terrain_load.load()) break;
        if (S.show_progress.load()) break;
        if (g_vista_only) {
            g_mp_vista_only = true;
            S.show_adjacent_terrain = true;
            // Elevated orbit so the surrounding vista ring is framed.
            S.cam_yaw   = 0.7f;
            S.cam_pitch = 0.65f;
            S.cam_dist  = 1.6f;
        }
        if (g_time_of_day >= 0.0f) {
            g_mp.time_of_day_override = true;
            g_mp.time_of_day_override_value =
                std::clamp(g_time_of_day / 24.0f, 0.0f, 1.0f);
        }
        g_countdown = g_settle_frames;
        g_stage = Stage::Settle;
        break;
    }
    case Stage::Settle: {
        if (--g_countdown > 0) break;
        if (g_shot_path.empty()) {
            finish("level loaded (no capture requested)", true);
            break;
        }
        g_capture_now = true;
        g_stage = Stage::Capture;
        break;
    }
    default:
        break;
    }
}

void AutoPilot_Capture(ID3D11Device* device,
                       ID3D11DeviceContext* context,
                       IDXGISwapChain* swapchain) {
    if (!g_capture_now) return;
    g_capture_now = false;

    ID3D11Texture2D* back = nullptr;
    if (FAILED(swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                    (void**)&back)) || !back) {
        finish("could not get backbuffer", false);
        return;
    }
    D3D11_TEXTURE2D_DESC desc = {};
    back->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    ID3D11Texture2D* staging = nullptr;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging)) ||
        !staging) {
        back->Release();
        finish("could not create staging texture", false);
        return;
    }
    context->CopyResource(staging, back);
    back->Release();

    D3D11_MAPPED_SUBRESOURCE map = {};
    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
        staging->Release();
        finish("could not map staging texture", false);
        return;
    }

    const int w = (int)desc.Width;
    const int h = (int)desc.Height;
    const bool bgra = (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                       desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
    std::vector<uint8_t> rgba((size_t)w * h * 4);
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = (const uint8_t*)map.pData +
                             (size_t)y * map.RowPitch;
        uint8_t* dst = rgba.data() + (size_t)y * w * 4;
        if (bgra) {
            for (int x = 0; x < w; ++x) {
                dst[x * 4 + 0] = src[x * 4 + 2];
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4 + 0];
                dst[x * 4 + 3] = 0xFF;
            }
        } else {
            std::memcpy(dst, src, (size_t)w * 4);
            for (int x = 0; x < w; ++x) dst[x * 4 + 3] = 0xFF;
        }
    }
    context->Unmap(staging, 0);
    staging->Release();

    if (tex_export_png(g_shot_path, rgba.data(), w, h)) {
        finish("captured " + std::to_string(w) + "x" + std::to_string(h) +
               " to " + g_shot_path, true);
    } else {
        finish("failed to write " + g_shot_path, false);
    }
}

#endif
