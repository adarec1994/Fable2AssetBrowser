#define IMGUI_DEFINE_MATH_OPERATORS
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "ImGuiFileDialog.h"
#include "../resource.h"
#include "State.h"
#include "UI_Main.h"
#include "UI_Panels.h"
#include "Progress.h"
#include "HexView.h"
#include "files.h"
#include <string>
#include <mutex>
#include <algorithm>

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
    if (g_pSwapChain) {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_pd3dDeviceContext) {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
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

int main() {
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

    build_theme();

    S.last_dir = load_last_dir();

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            if (msg.message == WM_QUIT) done = true;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        draw_main(hwnd, g_pd3dDevice);
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

        draw_hex_window(g_pd3dDevice);

        ImGui::Render();
        const float clear_color[4] = {0.10f, 0.10f, 0.10f, 1.0f};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    S.exiting = true;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return 0;
}