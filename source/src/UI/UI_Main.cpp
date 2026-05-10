#define IMGUI_DEFINE_MATH_OPERATORS
#include "UI_Main.h"
#include "../Utilities/State.h"
#include "../Utilities/Utils.h"
#include "../Utilities/Files.h"
#include "../Utilities/Progress.h"
#include "UI_Panels.h"
#include "AudioPlayerWindow.h"
#include "Layout/LoadingScreen.h"
#include "Layout/MainLayout.h"
#include "../Splashscreen/Splashscreen.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "ImGuiFileDialog.h"
#include <filesystem>
#include <algorithm>
#include <vector>
#ifdef _WIN32
#include <d3d11.h>
#include <windows.h>
#endif
#include "ModelPreview.h"
#include "../textures/export/TextureExport.h"
#ifndef _WIN32
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
ModelPreview g_mp;
// Non-static so RenderPanel.cpp can reference it as extern. Tracks
// whether MP_Init has run for the current model.
bool g_mp_initialized = false;

// Apply the user's R/G/B/A display toggles in-place to a freshly-decoded
// RGBA buffer. Disabled colour channels are zeroed, disabled alpha is
// forced to 255 (opaque) so the image still shows. Fast-paths the
// common "all on" case so a typical preview pays nothing for the
// per-pixel walk. Called right before SRV creation in both the
// initial-load and mip/channel-change handlers below.
static void apply_tex_channel_mask(std::vector<uint8_t>& rgba) {
    if (S.tex_show_r && S.tex_show_g && S.tex_show_b && S.tex_show_a) return;
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (!S.tex_show_r) rgba[i + 0] = 0;
        if (!S.tex_show_g) rgba[i + 1] = 0;
        if (!S.tex_show_b) rgba[i + 2] = 0;
        if (!S.tex_show_a) rgba[i + 3] = 255;
    }
}
#ifdef _WIN32
static ID3D11ShaderResourceView* g_splash_texture = nullptr;
static ID3D11ShaderResourceView* g_logo_texture = nullptr;
static ID3D11ShaderResourceView* g_button_texture = nullptr;
static ID3D11ShaderResourceView* g_sparkle_textures[8] = {nullptr};
static HWND g_hwnd = nullptr;
#else
static unsigned int g_splash_texture = 0;
static unsigned int g_logo_texture = 0;
static unsigned int g_button_texture = 0;
static unsigned int g_sparkle_textures[8] = {0};
static GLFWwindow* g_glfw_window = nullptr;
#endif
static float g_splash_scroll_offset = 0.0f;
static int g_splash_width = 0;
static int g_splash_height = 0;
static int g_logo_width = 0;
static int g_logo_height = 0;
static int g_button_width = 0;
static int g_button_height = 0;
static int g_sparkle_widths[8] = {0};
static int g_sparkle_heights[8] = {0};
static float g_splash_time_elapsed = 0.0f;
static const float g_fade_in_delay = 5.0f;
static const float g_fade_in_duration = 2.0f;
static const char* g_logo_map[] = {
"                                                                                                                                              i                                                         ",
"                                                                                                                                              iBPi                                                      ",
"           rYrrrvvvLLvvvrrrrrrrrrvsJuqBI                                                                                                       iBBBBZPPSIuUuJjuukSVUU vqusUVVXbqkjUISkuvi               ",
"           LBBBBQgRbjuKMBBQBBQBBBBBdKJvBi                                                                                                        UQQZqIusrvsuVdgRPugBvSBBBBPQBuYBBBBBBBBBBQi            ",
"            iDBkvui iSRBBBBQQQBBBBQMBB BU                                                                                                          vPRBBQgVbBBBJiiPQRu iPBvbIi XQMEqkJvvvUgBv           ",
"              BivsiiBBPjrii   iivSZirBrBs       sSPPQBMu          Jvrrvvvvriiiiiii          isrrvvvvrrrri           UvrvLsYvrrrrrrvvvsId                ivuKqQuQsivMI    iBiqUiuBI           i           ",
"              Bivu iBu               QkBL     rBvr gQgQBBv        QBBZgkvudgBBBBBBBgi      iBBBgMMgBBBBBBQu        BBgsuvLJVdRQZBBBMRjsI                    dirrivRv     BLks vQr                       ",
"              BvvkiiBL               PDBv    iB LBYiBi  ir         ugii vQBRgEEDBQqBBP       MUi  gRbSkuJuEu        dI   IQBQgDZEDgQZUrRi                   QiiiivRv     Brqk rQr                       ",
"              BuSZriBv               ZBgi    B uBBBivS              Qvr MZi     rbiv QU      Iqi iQu                rqi idbi      irisKgi                   RrvrvvRL     Qisu vQr                       ",
"              BkvS  Bj               BqV    B vBIkBQ QX             Bir Zu      uv   QRi     udi iBv                vqi idv          ugZ                    QiviiiQL    iQiuv vBr                       ",
"              BvigBPBZEDRQMPui      iiLi   BiiBP  kBPiQL            Bii Rdi iijgv vKRZv      UKi iBv                vqi  BX          UII                    RivvirBL    iQiJu rQr                       ",
"              BriJvi YSdMBBBBBBQi         BL BX    PBI Bi          iBri qBBBBBMukggRBP       IXiivBv         qk     vP JuVjKMMMPr    ivi                    givrirMY    igiJL vQr                       ",
"              Bvvr  BBBREqVkUVPQBX       gP BBSiii  PBsiBi          Bi  ggusuSERBBBZEBBr     IUi iBv          RB    vDii vKgQMQBBQv                         QiiiiiRs    iBiYv vQr                       ",
"              BurkiiBSi          ii     bR  uVdBBBBbvRBsiB         iQi  ZX       iUBXDiBv    Sk   Bv           BE    Rvi ZDLiiiiivji                        M ri iQs    iBivv rQr                       ",
"              BrrSiiBs                 dg ikQBMdXVSPqVBgirB         Qri gU        iP i vBi   Usi iQv           Uqu   ZPv uk                                 Z rv iQs    iDirr iQr                       ",
"              Bviui BU                gB iBBki        iRKvVB        BiVuBBi   irkDBv   MBr   Jv irBJ        u rIqX   iBBqLBv           r                    b rv  Qs    iQivi iEi                       ",
"              BviJi BU               BS  BQi          rP viqBi     Mk IQkuVSXRBBBbi iUBBk   iQrirYKPKUuuuKBBQVu KK    iQBbudgkriiiiiLPPvi                   Riii iMv     R iZ vBr                       ",
"              Bvvi iBu             iQY  iSBgv        JBuvuJjgBv   BBXqqKUjJkKKXPbDBBBgr   vBBXdIkuuqEDBgbEZQBSvEP     iqgPuIVPgQQgRQbUji      r           VV iviUBJ    uK iMBBBBRPji                   ",
"              Bvr  uBs           iISiiKBBBQZIi        rMBQQRgQgi  iXQQRMRQQQRMMggZPkvi      iZQQQQRRRMMgggDEZDQBBb       iUZQQQQQQQgqsi        iMBDIjJuIqqZv  iiikbQJ rBJ        iJXgBRBSi              ",
"              Brr  BQr      irvubZuuBBBdvi                                                                                    iiii               uQBBBBQgPbdggDMggZdBUqDdQQggZPPbgDZZbPBBBBi            ",
"             SBr  XBq       rQBBMRBBPr                                                                                                             irvsuUkkIIVIkUuuuUsirkkkkUkkUUuuUkVKPdgBBk           ",
"             Bj  kBMi         iLUJi                                                                                                                                                       ivgJ          ",
"            Qg  ZBMi                                                                                                                                                                                    ",
"           dBisBBPi                                                                                                                                                                                     ",
"          BBEBBDr                                                                                                                                                                                       ",
"        vQPKMXi                                                                                                                                                                                         ",
"          ii                                                                                                                                                                                           "
};
static std::vector<std::pair<float, float>> g_letter_positions;
static bool g_letter_positions_built = false;
static void build_letter_positions() {
    if (g_letter_positions_built) return;
    for (int y = 0; y < 27; ++y) {
        const char* line = g_logo_map[y];
        for (int x = 0; x < 200 && line[x] != '\0'; ++x) {
            if (line[x] != ' ') {
                g_letter_positions.push_back({(float)x / 200.0f, (float)y / 27.0f});
            }
        }
    }
    g_letter_positions_built = true;
}
struct Sparkle {
    float x, y;
    float start_y;
    int texture_index;
    float life_time;
    float max_life;
    bool active;
    bool falls;
};
static Sparkle g_sparkles[400];
static bool g_sparkles_initialized = false;
// All splash assets (logo, panorama, sparkles) are embedded in the exe as
// RCDATA on Windows; the loaders live in Splashscreen.cpp. Older disk-loading
// helpers were removed so there are no runtime file dependencies.
static void handle_flycam_input(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    bool w_pressed = ImGui::IsKeyDown(ImGuiKey_W);
    bool s_pressed = ImGui::IsKeyDown(ImGuiKey_S);
    bool a_pressed = ImGui::IsKeyDown(ImGuiKey_A);
    bool d_pressed = ImGui::IsKeyDown(ImGuiKey_D);
    bool q_pressed = ImGui::IsKeyDown(ImGuiKey_Q);
    bool e_pressed = ImGui::IsKeyDown(ImGuiKey_E);
    float mouse_dx = 0.0f;
    float mouse_dy = 0.0f;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        g_flycam.is_looking = true;
        g_flycam.saved_mouse_x = io.MousePos.x;
        g_flycam.saved_mouse_y = io.MousePos.y;
#ifdef _WIN32
        ShowCursor(FALSE);
#else
        if (g_glfw_window) {
            glfwSetInputMode(g_glfw_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
#endif
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        g_flycam.is_looking = false;
#ifdef _WIN32
        ShowCursor(TRUE);
        SetCursorPos((int)g_flycam.saved_mouse_x, (int)g_flycam.saved_mouse_y);
#else
        if (g_glfw_window) {
            glfwSetInputMode(g_glfw_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            glfwSetCursorPos(g_glfw_window, g_flycam.saved_mouse_x, g_flycam.saved_mouse_y);
        }
#endif
    }
    if (g_flycam.is_looking) {
#ifdef _WIN32
        POINT cur;
        GetCursorPos(&cur);
        mouse_dx = (float)(cur.x - (int)g_flycam.saved_mouse_x);
        mouse_dy = (float)(cur.y - (int)g_flycam.saved_mouse_y);
        SetCursorPos((int)g_flycam.saved_mouse_x, (int)g_flycam.saved_mouse_y);
#else
        mouse_dx = io.MouseDelta.x;
        mouse_dy = io.MouseDelta.y;
#endif
    }
    FlyCam_Update(g_flycam, dt, w_pressed, s_pressed, a_pressed, d_pressed, q_pressed, e_pressed, mouse_dx, mouse_dy);
}

// External entry point so the render panel (in src/UI/Layout/) can drive
// the camera while the model is embedded in its child window. Same logic
// as the in-file static — kept as a thin wrapper so we don't have to make
// the camera state public.
void render_panel_handle_flycam(float dt) {
    handle_flycam_input(dt);
}

#ifdef _WIN32
void draw_main(HWND hwnd, ID3D11Device* device) {
    g_hwnd = hwnd;
#else
void draw_main(GLFWwindow* window) {
    g_glfw_window = window;
#endif
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    // Only carry the MenuBar flag when we're past the splash. The flag
    // unconditionally reserves a strip at the top of the window even
    // when no menu is drawn — that strip would steal pixels from the
    // splash artwork.
    ImGuiWindowFlags main_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!S.root_dir.empty()) main_flags |= ImGuiWindowFlags_MenuBar;
    ImGui::Begin("##main", nullptr, main_flags);
    float dt = ImGui::GetIO().DeltaTime;

    // ---- Top menu bar ------------------------------------------------------
    // Only shown after splash — otherwise it sits awkwardly above the
    // "click anywhere" splash. File > Quit posts WM_QUIT so the main loop
    // exits through the same path as clicking the X. Settings is a
    // dropdown carrying the show-paths / dev-mode toggles + font slider
    // inline; no separate popup window.
    if (!S.root_dir.empty()) {
        extern void settings_save();
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Quit", "Alt+F4")) {
#ifdef _WIN32
                    PostQuitMessage(0);
#else
                    if (g_glfw_window) glfwSetWindowShouldClose(g_glfw_window, GLFW_TRUE);
#endif
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Settings")) {
                if (ImGui::Checkbox("Show file paths in tree tooltips", &S.show_paths)) {
                    S.hide_tooltips = !S.show_paths;
                    settings_save();
                }
                if (ImGui::Checkbox("Developer mode", &S.dev_mode)) {
                    settings_save();
                }
                ImGui::TextDisabled(
                    S.dev_mode
                        ? "Tooltips/overlays show file size, BNK source, and full paths."
                        : "Only the filename and basic info are shown.");

                ImGui::Separator();
                ImGui::TextUnformatted("Font size");
                bool changed = false;
                ImGui::SetNextItemWidth(180);
                if (ImGui::SliderFloat("##font_size_slider", &S.pending_font_size,
                                       10.0f, 28.0f, "%.0f px")) {
                    changed = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80);
                if (ImGui::InputFloat("##font_size_input", &S.pending_font_size,
                                      1.0f, 1.0f, "%.0f")) {
                    if (S.pending_font_size < 8.0f)  S.pending_font_size = 8.0f;
                    if (S.pending_font_size > 48.0f) S.pending_font_size = 48.0f;
                    changed = true;
                }
                if (changed) {
                    S.font_size_dirty = true;
                    settings_save();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
    }

    // ---- Top-level dispatch ------------------------------------------------
    // 1. No data picked → splash screen.
    // 2. Data picked + tree still building → loading screen (full-window).
    // 3. Data picked + tree ready → main 2-column layout. The render
    //    panel inside the layout owns model preview / texture display.
    if (S.root_dir.empty()) {
#ifdef _WIN32
        Splash::draw(device);
#else
        Splash::draw(window);
#endif
    } else if (UI::loading_in_progress() || S.bnk_paths.empty()) {
        // Either the tree is still being built, or we don't have any
        // BNKs scanned yet (e.g. the open thread hasn't finished). Show
        // the loading screen rather than a half-populated layout.
        UI::draw_loading_screen();
    } else {
#ifdef _WIN32
        UI::draw_main_layout(device);
#else
        UI::draw_main_layout();
#endif
    }
    ImGui::End();

#ifdef _WIN32
    if (S.pending_texture_load) {
        S.pending_texture_load = false;
        if (S.texture_window_srv) {
            S.texture_window_srv->Release();
            S.texture_window_srv = nullptr;
        }
        if (S.pending_texture_w > 0 && S.pending_texture_h > 0 && !S.pending_texture_rgba.empty()) {
            apply_tex_channel_mask(S.pending_texture_rgba);
            S.texture_window_srv = create_srv_from_rgba(device, S.pending_texture_w, S.pending_texture_h, S.pending_texture_rgba);
            S.texture_window_width = S.pending_texture_w;
            S.texture_window_height = S.pending_texture_h;
        } else {
            S.texture_window_width = 0;
            S.texture_window_height = 0;
        }
        S.show_texture_window = true;
        S.pending_texture_rgba.clear();
    }

    // Mip / channel re-decode path — the texture preview's overlay
    // sets pending_texture_mip_change for both arrow clicks AND R/G/B/A
    // checkbox flips. Re-decodes from the cached blob (no BNK round
    // trip), applies the channel mask, and swaps the SRV in-place.
    // No-op if we don't have a blob (e.g. last load failed).
    if (S.pending_texture_mip_change.exchange(false)) {
        if (!S.texture_blob.empty() && S.tex_info_ok) {
            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            bool has_alpha = false;
            if (decode_tex_to_rgba(S.texture_blob, rgba, w, h, &has_alpha,
                                   S.texture_mip_index)) {
                apply_tex_channel_mask(rgba);
                if (S.texture_window_srv) {
                    S.texture_window_srv->Release();
                    S.texture_window_srv = nullptr;
                }
                if (w > 0 && h > 0 && !rgba.empty()) {
                    S.texture_window_srv = create_srv_from_rgba(device, w, h, rgba);
                    S.texture_window_width  = w;
                    S.texture_window_height = h;
                }
            }
        }
    }
#else
    if (S.pending_texture_load) {
        S.pending_texture_load = false;
        if (S.texture_window_gl) {
            glDeleteTextures(1, &S.texture_window_gl);
            S.texture_window_gl = 0;
        }
        if (S.pending_texture_w > 0 && S.pending_texture_h > 0 && !S.pending_texture_rgba.empty()) {
            apply_tex_channel_mask(S.pending_texture_rgba);
            S.texture_window_gl = create_gl_texture_from_rgba(S.pending_texture_w, S.pending_texture_h, S.pending_texture_rgba.data());
            S.texture_window_width = S.pending_texture_w;
            S.texture_window_height = S.pending_texture_h;
        } else {
            S.texture_window_width = 0;
            S.texture_window_height = 0;
        }
        S.show_texture_window = true;
        S.pending_texture_rgba.clear();
    }

    if (S.pending_texture_mip_change.exchange(false)) {
        if (!S.texture_blob.empty() && S.tex_info_ok) {
            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            bool has_alpha = false;
            if (decode_tex_to_rgba(S.texture_blob, rgba, w, h, &has_alpha,
                                   S.texture_mip_index)) {
                apply_tex_channel_mask(rgba);
                if (S.texture_window_gl) {
                    glDeleteTextures(1, &S.texture_window_gl);
                    S.texture_window_gl = 0;
                }
                if (w > 0 && h > 0 && !rgba.empty()) {
                    S.texture_window_gl = create_gl_texture_from_rgba(w, h, rgba.data());
                    S.texture_window_width  = w;
                    S.texture_window_height = h;
                }
            }
        }
    }
#endif

    // The floating "Texture Preview" ImGui window is gone — textures
    // now render into the central RenderPanel via S.texture_window_srv.
    // The pending_texture_load handler above creates that SRV; the
    // panel paints it.

    // Drive the MDL/TEX preview-load state machines. They used to live
    // inside draw_right_panel; that function is no longer called by the
    // new layout, so we run the handlers from here instead.
#ifdef _WIN32
    process_pending_loads(device);
#else
    process_pending_loads();
#endif

    // Drive the texture-export ImGuiFileDialog. No-op when no export
    // is pending. Runs after the rest of the UI so the dialog renders
    // on top of everything.
    tex_export_drive();

    // (Settings used to live in a floating window here; it's now a
    // dropdown directly off the menu bar.)

    // In-app audio player (only renders when a source is loaded).
    UI::draw_audio_player_window();
}