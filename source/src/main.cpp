#define IMGUI_DEFINE_MATH_OPERATORS
#ifdef _WIN32
#pragma comment(linker, "/subsystem:windows /entry:mainCRTStartup")
#endif
#ifdef _WIN32
#include <windows.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi.h>
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <shlobj.h>
#include "Audio/play_audio.h"
#include "Audio/AudioPlayer.h"
#include "../resource.h"
#else
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>
#endif
#include "imgui.h"
#include "ImGuiFileDialog.h"
#include "Utilities/State.h"
#include "UI/UI_Main.h"
#include "UI/UI_Panels.h"
#include "Utilities/Progress.h"
#include "UI/HexView.h"
#include "Splashscreen/Splashscreen.h"
#include "Utilities/Files.h"
#include "Splashscreen/IconFont.h"
#include <string>
#include <mutex>
#include <map>
#include <algorithm>
#include <fstream>
static std::string get_config_path() {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path) == S_OK) {
        std::string config_dir = std::string(path) + "\\Fable2AssetBrowser";
        CreateDirectoryA(config_dir.c_str(), NULL);
        return config_dir + "\\config.ini";
    }
    return "config.ini";
#else
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : ".";
    }
    std::string config_dir = std::string(home) + "/.config/Fable2AssetBrowser";
    mkdir(config_dir.c_str(), 0755);
    return config_dir + "/config.ini";
#endif
}
// Tiny key=value config store. The file lives in
// %APPDATA%\Fable2AssetBrowser\config.ini (Windows) or
// ~/.config/Fable2AssetBrowser/config.ini (other). Order is irrelevant;
// missing keys fall back to the defaults baked into State.h.
namespace {
std::map<std::string, std::string> read_config_kv() {
    std::map<std::string, std::string> kv;
    std::ifstream f(get_config_path());
    if (!f.is_open()) return kv;
    std::string line;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return kv;
}
void write_config_kv(const std::map<std::string, std::string>& kv) {
    std::ofstream f(get_config_path(), std::ios::trunc);
    if (!f.is_open()) return;
    for (auto& [k, v] : kv) f << k << "=" << v << "\n";
}
} // namespace

static bool load_audio_muted() {
    auto kv = read_config_kv();
    auto it = kv.find("audio_muted");
    return it != kv.end() && it->second == "1";
}

// Non-static so Splashscreen can persist the mute state when its FA-icon
// button is clicked.
void save_audio_muted(bool muted) {
    auto kv = read_config_kv();
    kv["audio_muted"] = muted ? "1" : "0";
    write_config_kv(kv);
}

// Read the user-visible settings from disk into S. Called once at startup
// after the State singleton is alive.
void settings_load() {
    auto kv = read_config_kv();
    if (auto it = kv.find("show_paths"); it != kv.end()) {
        S.show_paths = (it->second != "0");
        S.hide_tooltips = !S.show_paths;
    }
    if (auto it = kv.find("dev_mode"); it != kv.end()) {
        S.dev_mode = (it->second == "1");
    }
    if (auto it = kv.find("font_size"); it != kv.end()) {
        try {
            float v = std::stof(it->second);
            if (v < 8.0f)  v = 8.0f;
            if (v > 48.0f) v = 48.0f;
            S.font_size = v;
            S.pending_font_size = v;
        } catch (...) {}
    }
}

// Persist the current S.* settings. Called from the Settings dropdown
// whenever a value changes. Cheap (small file, infrequent edits).
void settings_save() {
    auto kv = read_config_kv();
    kv["show_paths"] = S.show_paths ? "1" : "0";
    kv["dev_mode"]   = S.dev_mode ? "1" : "0";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f", S.font_size_dirty ? S.pending_font_size : S.font_size);
    kv["font_size"] = buf;
    write_config_kv(kv);
}
#ifdef _WIN32
static ID3D11Device *g_pd3dDevice = nullptr;
static ID3D11DeviceContext *g_pd3dDeviceContext = nullptr;
static IDXGISwapChain *g_pSwapChain = nullptr;
static ID3D11RenderTargetView *g_mainRenderTargetView = nullptr;
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
static void CreateRenderTarget() {
    ID3D11Texture2D *pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_ID3D11Texture2D, (void **) &pBackBuffer);
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    if (pBackBuffer) pBackBuffer->Release();
}
static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}
static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL levels[1] = {D3D_FEATURE_LEVEL_11_0};
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
                                      D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel,
                                      &g_pd3dDeviceContext) != S_OK)
        return false;
    CreateRenderTarget();
    return true;
}
static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return 1;
    switch (msg) {
        case WM_SIZE:
            if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
                CleanupRenderTarget();
                g_pSwapChain->ResizeBuffers(0, (UINT) LOWORD(lParam), (UINT) HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTarget();
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}
#else
static GLFWwindow* g_window = nullptr;
static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}
#endif
static void build_theme() {
    auto &s = ImGui::GetStyle();
    s.WindowRounding = 12.0f;
    s.FrameRounding = 8.0f;
    s.ChildRounding = 10.0f;
    s.PopupRounding = 10.0f;
    s.GrabRounding = 8.0f;
    s.TabRounding = 8.0f;
    s.ScrollbarRounding = 8.0f;
    s.WindowBorderSize = 0.0f;
}
#ifdef _WIN32
int main() {
    // FFmpeg (libavcodec / libavutil / libswresample) is now linked
    // statically into the exe — no DLLs to extract or DLL search-path
    // games at startup. XmaDecoder calls into avcodec directly.
    HINSTANCE hInstance = GetModuleHandle(nullptr);
    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = (HICON) LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_ICON1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                  GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON) LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_ICON1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                    GetSystemMetrics(SM_CYICON), 0);
    wc.lpszClassName = "BNKWndClass";
    RegisterClassExA(&wc);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "Fable 2 Asset Browser", WS_OVERLAPPEDWINDOW, 100, 100, 1100, 680,
                                NULL, NULL, wc.hInstance, NULL);
    HICON big = (HICON) LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_ICON1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                   GetSystemMetrics(SM_CYICON), 0);
    HICON sml = (HICON) LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_ICON1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYICON), 0);
    SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM) big);
    SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM) sml);
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassA(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    // Load the FontAwesome icon font BEFORE any frame is rendered. Adding
    // fonts mid-frame would crash because the DX11 backend has already
    // built/bound the font atlas texture by then.
    Splashscreen_init_icon_font_at_startup();
#else
int main() {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    g_window = glfwCreateWindow(1100, 680, "Fable 2 Asset Browser", nullptr, nullptr);
    if (!g_window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1);
    if (glewInit() != GLEW_OK) {
        fprintf(stderr, "Failed to initialize GLEW\n");
        return 1;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(g_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
#endif
    build_theme();
    S.last_dir = load_last_dir();

    // Pull persisted user-visible settings (show_paths, dev_mode,
    // font_size) from config.ini into S. Has to happen before the
    // first NewFrame so the font-size value is honored on initial
    // atlas build.
    settings_load();
    if (S.font_size != 17.0f) {
        // User had a non-default font size persisted — rebuild the atlas
        // at that size before the first frame so we never flash at the
        // default.
        IconFont::reload_at_size(S.font_size);
        S.pending_font_size = S.font_size;
    }
#ifdef _WIN32
    bool audio_muted = load_audio_muted();
    BackgroundAudio::instance().set_muted(audio_muted);
    // Splash music is embedded as RCDATA — no sidecar file needed.
    BackgroundAudio::instance().start_from_resource(IDR_MENU_INTERLUDE_WAV);
#endif
    bool done = false;
    while (!done) {
#ifdef _WIN32
        MSG msg;
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            if (msg.message == WM_QUIT) done = true;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (done) break;

        // Apply pending font-size change BEFORE NewFrame. The atlas
        // bitmap rebuilds on the CPU here, then the DX11 backend's
        // GPU font texture is invalidated so it gets re-uploaded on
        // the next call to ImGui_ImplDX11_NewFrame.
        if (S.font_size_dirty) {
            S.font_size_dirty = false;
            S.font_size = S.pending_font_size;
            IconFont::reload_at_size(S.font_size);
#ifdef _WIN32
            ImGui_ImplDX11_InvalidateDeviceObjects();
#endif
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
#else
        glfwPollEvents();
        if (glfwWindowShouldClose(g_window)) done = true;
        if (done) break;
        if (S.font_size_dirty) {
            S.font_size_dirty = false;
            S.font_size = S.pending_font_size;
            IconFont::reload_at_size(S.font_size);
            ImGui_ImplOpenGL3_DestroyDeviceObjects();
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
#endif
        ImGui::NewFrame();
#ifdef _WIN32
        draw_main(hwnd, g_pd3dDevice);
        // (The legacy "S"/"M" mute button used to live here. The splash
        // module now owns the mute button — see Splashscreen.cpp — using
        // FontAwesome icons. We still need to stop the audio when the
        // user has loaded a Fable 2 root.)
        if (!S.root_dir.empty()) {
            static bool audio_stopped = false;
            if (!audio_stopped) {
                BackgroundAudio::instance().stop();
                audio_stopped = true;
            }
        }
#else
        draw_main(g_window);
#endif
        draw_folder_dialog();
        if (S.show_progress.load()) ImGui::OpenPopup("progress_win");
        if (S.show_error) {
            ImGui::OpenPopup("error_modal");
            S.show_error = false;
        }
        if (S.show_completion) {
            ImGui::OpenPopup("completion_modal");
            S.show_completion = false;
        }
        ImGuiViewport *vp = ImGui::GetMainViewport();
        float w = std::clamp(vp->WorkSize.x * 0.6f, 520.0f, 900.0f);
        const ImGuiStyle &st = ImGui::GetStyle();
        float line = ImGui::GetTextLineHeightWithSpacing();
        float h = st.WindowPadding.y * 2.0f + line + st.ItemSpacing.y + (line * 2.0f + 6.0f) + st.ItemSpacing.y +
                  ImGui::GetFrameHeight() + st.ItemSpacing.y + ImGui::GetFrameHeight() + 12.0f;
        ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(vp->WorkPos + ImVec2((vp->WorkSize.x - w) * 0.5f, (vp->WorkSize.y - h) * 0.5f),
                                ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("progress_win", nullptr,
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
            int total, current;
            std::string label;
            {
                std::lock_guard<std::mutex> lk(S.progress_mutex);
                total = S.progress_total;
                current = S.progress_current;
                label = S.progress_label;
            }
            ImGui::Text("%d/%d", current, std::max(1, total));
            float wrap_w = ImGui::GetContentRegionAvail().x;
            std::string two = label;
            if (ImGui::CalcTextSize(two.c_str()).x > wrap_w) {
                size_t mid = two.size() / 2;
                auto fits = [&](size_t pos) {
                    std::string a = two.substr(0, pos), b = two.substr(pos + 1);
                    return ImGui::CalcTextSize(a.c_str()).x <= wrap_w && ImGui::CalcTextSize(b.c_str()).x <= wrap_w;
                };
                size_t cand = std::string::npos;
                size_t l1 = two.rfind('\\', mid), l2 = two.rfind('/', mid);
                if (l1 != std::string::npos || l2 != std::string::npos) cand = std::max(
                                                                            l1 == std::string::npos ? 0 : l1,
                                                                            l2 == std::string::npos ? 0 : l2);
                if (cand != std::string::npos && fits(cand)) two.insert(cand + 1, "\n");
                else {
                    size_t r1 = two.find('\\', mid), r2 = two.find('/', mid);
                    size_t r = std::min(r1 == std::string::npos ? two.size() : r1,
                                        r2 == std::string::npos ? two.size() : r2);
                    if (r != two.size() && fits(r)) two.insert(r + 1, "\n");
                    else two.insert(mid, "\n");
                }
            }
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_w);
            ImGui::BeginChild("progress_label", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 2.0f + 6.0f), false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::TextUnformatted(two.c_str());
            ImGui::EndChild();
            ImGui::PopTextWrapPos();
            float frac = total > 0 ? (float) current / (float) total : 1.0f;
            ImGui::ProgressBar(frac, ImVec2(-1, 0));
            ImGui::Dummy(ImVec2(0, 6));
            if (ImGui::Button("Cancel", ImVec2(-1, 0))) {
                S.cancel_requested = true;
                progress_done();
                show_completion_box("Extraction cancelled.");
            }
            if (!S.show_progress.load()) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGuiViewport *vp2 = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp2->WorkPos + vp2->WorkSize * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("error_modal", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoTitleBar)) {
            ImGui::TextColored(ImVec4(1, 0.47f, 0.47f, 1), "Error");
            ImGui::Separator();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
            ImGui::TextUnformatted(S.error_text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0, 10));
            if (ImGui::Button("Close", ImVec2(-1, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGuiViewport *vp3 = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp3->WorkPos + vp3->WorkSize * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("completion_modal", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoTitleBar)) {
            ImGui::TextColored(ImVec4(0.47f, 1, 0.47f, 1), "Operation Status");
            ImGui::Separator();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
            ImGui::TextUnformatted(S.completion_text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0, 10));
            if (ImGui::Button("OK", ImVec2(-1, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
#ifdef _WIN32
        draw_hex_window(g_pd3dDevice);
#else
        draw_hex_window();
#endif
        ImGui::Render();
#ifdef _WIN32
        const float clear_color[4] = {0.10f, 0.10f, 0.10f, 1.0f};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
#else
        int display_w, display_h;
        glfwGetFramebufferSize(g_window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.10f, 0.10f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(g_window);
#endif
    }
    S.exiting = true;
    AudioPlayer::shutdown();
#ifdef _WIN32
    BackgroundAudio::instance().stop();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
#else
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
#endif
    ImGui::DestroyContext();
#ifdef _WIN32
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
#else
    glfwDestroyWindow(g_window);
    glfwTerminate();
#endif
    return 0;
}