#ifdef _WIN32
#include "AutoPilot.h"

#include "State.h"
#include "../Level/LevelEdit.h"
#include "../Level/LevelLoader.h"
#include "../UI/ModelPreview.h"
#include "../UI/UI_Panels.h"
#include "../UI/Layout/LoadingScreen.h"
#include "../UI/OutputLog.h"
#include "../textures/export/TextureExport.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
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
std::string g_root_path;
std::string g_level_query;
std::string g_shot_path;
bool        g_auto_exit = false;
bool        g_vista_only = false;
bool        g_sky_shot = false;
bool        g_terrain_shot = false;
float       g_time_of_day = -1.0f;
float       g_pitch_offset = 0.0f;
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
        if (const char* v = value("--autoroot")) g_root_path = v;
        else if (const char* v = value("--autoload")) g_level_query = lower(v);
        else if (const char* v = value("--autoshot")) g_shot_path = v;
        else if (const char* v = value("--autowait"))
            g_settle_frames = std::max(1, std::atoi(v));
        else if (const char* v = value("--autotime"))
            g_time_of_day = float(std::atof(v));
        else if (const char* v = value("--autopitch"))
            g_pitch_offset = float(std::atof(v));
        else if (arg == "--autoexit") g_auto_exit = true;
        else if (arg == "--vistaonly") g_vista_only = true;
        else if (arg == "--skyshot") g_sky_shot = true;
        else if (arg == "--terrainshot") g_terrain_shot = true;
        else if (const char* v = value("--levprobe")) {
            std::string msg;
            const bool ok = LevelEdit::RunLevProbe(v, msg);
            if (ok) OutputLog::success("autopilot: " + msg);
            else    OutputLog::error("autopilot: " + msg);
            std::fprintf(ok ? stdout : stderr, "%s\n", msg.c_str());
            std::fflush(nullptr);
            ExitProcess(ok ? 0 : 1);
        }
        else if (const char* v = value("--levprobefloat")) {
            std::string msg;
            const bool ok = LevelEdit::RunLevProbeMode(v, true, msg);
            std::fprintf(ok ? stdout : stderr, "%s\n", msg.c_str());
            std::fflush(nullptr);
            ExitProcess(ok ? 0 : 1);
        }
        else if (const char* v = value("--fixstream")) {
            std::string msg;
            const bool ok = LevelEdit::RunStreamFix(v, msg);
            std::fprintf(ok ? stdout : stderr, "%s\n", msg.c_str());
            std::fflush(nullptr);
            ExitProcess(ok ? 0 : 1);
        }
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
        const std::string& root =
            g_root_path.empty() ? S.last_dir : g_root_path;
        if (root.empty() || !std::filesystem::is_directory(root)) {
            finish("no saved root directory to open", false);
            break;
        }
        open_folder_logic(root);
        if (S.root_dir.empty()) {
            finish("failed to open root " + root, false);
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

            S.cam_yaw   = 0.7f;
            S.cam_pitch = 0.65f;
            S.cam_dist  = 1.6f;
        }
        if (g_sky_shot) {

            g_mp_vista_only = false;
            g_flycam.pos[0] = g_mp.center[0];
            g_flycam.pos[1] = g_mp.center[1] +
                std::max(2.0f, g_mp.radius * 0.01f);
            g_flycam.pos[2] = g_mp.center[2];
            g_flycam.yaw = 0.0f;
            g_flycam.pitch = 0.55f;
            g_flycam.is_looking = false;
        }
        if (g_terrain_shot) {

            g_mp_vista_only = false;
            S.show_adjacent_terrain = false;
            g_mp.show_mist = false;
            g_mp.show_weather = false;
            g_mp.fx_show = false;
            g_mp.weather_mist_strength = 0.0f;
            g_mp.has_fog_theme = false;
            std::fill(std::begin(g_mp.fog_range),
                      std::end(g_mp.fog_range), 0.0f);
            std::fill(std::begin(g_mp.fog_density),
                      std::end(g_mp.fog_density), 0.0f);
            for (auto& key : g_mp.day_night_keyframes) {
                key.weather_mist_strength = 0.0f;
                key.has_fog_theme = false;
                std::fill(std::begin(key.fog_range),
                          std::end(key.fog_range), 0.0f);
                std::fill(std::begin(key.fog_density),
                          std::end(key.fog_density), 0.0f);
            }

            const MPPerMesh* primary_terrain = nullptr;
            for (const auto& mesh : g_mp.meshes) {
                if (!mesh.is_terrain ||
                    mesh.name.rfind("adjacent terrain", 0) == 0) {
                    continue;
                }
                primary_terrain = &mesh;
                break;
            }

            if (primary_terrain) {
                const float cx = primary_terrain->center[0];
                const float cy = primary_terrain->center[1];
                const float cz = primary_terrain->center[2];
                const float r = std::max(primary_terrain->radius, 1.0f);
                float camera_y = cy +
                    std::clamp(r * 0.15f, 8.0f, 80.0f);
                float min_cloud_y = std::numeric_limits<float>::max();
                auto consider_clouds = [&min_cloud_y](
                    int count, const float layers[4][4]) {
                    for (int i = 0; i < std::min(count, 4); ++i) {
                        if (layers[i][0] > 0.0f) {
                            min_cloud_y = std::min(min_cloud_y,
                                                   layers[i][1]);
                        }
                    }
                };
                consider_clouds(g_mp.cloud_layer_count, g_mp.cloud_layer);
                for (const auto& key : g_mp.day_night_keyframes) {
                    consider_clouds(key.cloud_layer_count, key.cloud_layer);
                }
                if (min_cloud_y < std::numeric_limits<float>::max()) {
                    camera_y = std::min(camera_y, min_cloud_y - 8.0f);
                    camera_y = std::max(camera_y, cy + 2.0f);
                }

                g_flycam.pos[0] = cx;
                g_flycam.pos[1] = camera_y;
                g_flycam.pos[2] = cz - r * 0.60f;
                const float dx = cx - g_flycam.pos[0];
                const float dy = cy - g_flycam.pos[1];
                const float dz = cz - g_flycam.pos[2];
                g_flycam.yaw = std::atan2(dx, dz);
                g_flycam.pitch = std::atan2(
                    dy, std::sqrt(dx * dx + dz * dz));
            } else {
                const float r = std::max(1.0f, g_mp.radius);
                g_flycam.pos[0] = g_mp.center[0];
                g_flycam.pos[1] = g_mp.center[1] + r * 0.01f;
                g_flycam.pos[2] = g_mp.center[2] - r * 0.35f;
                g_flycam.yaw = 0.0f;
                g_flycam.pitch = -0.12f;
            }
            g_flycam.is_looking = false;
        }
        if (g_pitch_offset != 0.0f) {
            g_flycam.pitch = std::clamp(g_flycam.pitch + g_pitch_offset,
                                        -1.4f, 1.4f);
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
