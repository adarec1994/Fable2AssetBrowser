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
#include "About/AboutWindow.h"
#include "../Splashscreen/Splashscreen.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "ImGuiFileDialog.h"
#include <cmath>
#include <filesystem>
#include <algorithm>
#include <vector>
#ifdef _WIN32
#include <d3d11.h>
#include <windows.h>
#endif
#include "ModelPreview.h"
#include "../textures/export/TextureExport.h"
#include "../animations/AnimPlayer.h"
#include "../ISO/IsoMount.h"
#include "../ISO/IsoDump.h"
#include "OutputLog.h"
#ifndef _WIN32
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
ModelPreview g_mp;

bool g_mp_initialized = false;

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

static void handle_flycam_input(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    bool w_pressed = ImGui::IsKeyDown(S.key_forward);
    bool s_pressed = ImGui::IsKeyDown(S.key_back);
    bool a_pressed = ImGui::IsKeyDown(S.key_left);
    bool d_pressed = ImGui::IsKeyDown(S.key_right);
    bool q_pressed = ImGui::IsKeyDown(S.key_down);
    bool e_pressed = ImGui::IsKeyDown(S.key_up);
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

void render_panel_handle_flycam(float dt) {
    handle_flycam_input(dt);
}

static void draw_keybinds_window() {
    if (!S.show_keybinds_window) return;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
               vp->WorkPos.y + vp->WorkSize.y * 0.5f),
        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Appearing);

    if (!ImGui::Begin("Keybinds", &S.show_keybinds_window,
                      ImGuiWindowFlags_NoCollapse |
                      ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }

    extern void settings_save();
    struct KB { const char* label; ImGuiKey* k; };
    KB rows[] = {
        { "Move forward",   &S.key_forward       },
        { "Move back",      &S.key_back          },
        { "Move left",      &S.key_left          },
        { "Move right",     &S.key_right         },
        { "Move up",        &S.key_up            },
        { "Move down",      &S.key_down          },
        { "Bone rotate",    &S.key_rotate_mode   },
    };
    const int n = (int)(sizeof(rows) / sizeof(rows[0]));
    for (int i = 0; i < n; ++i) {
        ImGui::PushID(i);
        ImGui::TextUnformatted(rows[i].label);
        ImGui::SameLine(160);
        const bool capturing = (S.capturing_key_id == i);
        const char* keyname = ImGui::GetKeyName(*rows[i].k);
        std::string btn = capturing
            ? std::string("[press any key]")
            : (keyname && *keyname ? std::string(keyname)
                                   : std::string("<unset>"));
        if (ImGui::Button(btn.c_str(), ImVec2(180, 0))) {
            S.capturing_key_id = i;
        }
        ImGui::PopID();
    }

    if (S.capturing_key_id >= 0 && S.capturing_key_id < n) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            S.capturing_key_id = -1;
        } else {
            for (int k = ImGuiKey_NamedKey_BEGIN;
                 k < ImGuiKey_NamedKey_END; ++k)
            {
                ImGuiKey key = (ImGuiKey)k;
                if (key == ImGuiKey_LeftCtrl  || key == ImGuiKey_RightCtrl ||
                    key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift||
                    key == ImGuiKey_LeftAlt   || key == ImGuiKey_RightAlt  ||
                    key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper)
                    continue;
                if (ImGui::IsKeyPressed(key)) {
                    *rows[S.capturing_key_id].k = key;
                    S.capturing_key_id = -1;
                    settings_save();
                    break;
                }
            }
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Reset to defaults")) {
        S.key_forward       = ImGuiKey_W;
        S.key_back          = ImGuiKey_S;
        S.key_left          = ImGuiKey_D;
        S.key_right         = ImGuiKey_A;
        S.key_up            = ImGuiKey_E;
        S.key_down          = ImGuiKey_Q;
        S.key_rotate_mode   = ImGuiKey_R;
        settings_save();
    }

    ImGui::End();
}

#ifdef _WIN32
void draw_main(HWND hwnd, ID3D11Device* device) {
    g_hwnd = hwnd;
#else
void draw_main(GLFWwindow* window) {
    g_glfw_window = window;
#endif
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    const bool show_output_log =
        !S.root_dir.empty() && !UI::loading_in_progress() && !S.bnk_paths.empty();
    const float bottom_reserve =
        show_output_log ? OutputLog::reserved_bottom_height() : 0.0f;
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x,
                                    viewport->WorkSize.y - bottom_reserve));

    const bool show_menu_bar = show_output_log;
    ImGuiWindowFlags main_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (show_menu_bar) main_flags |= ImGuiWindowFlags_MenuBar;
    ImGui::Begin("##main", nullptr, main_flags);
    float dt = ImGui::GetIO().DeltaTime;

    if (show_menu_bar) {
        extern void settings_save();
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {

                if (S.dev_mode) {
                    if (ImGui::BeginMenu("Dump")) {
                        if (ISO::IsoMount::instance().is_mounted()) {
                            if (ImGui::MenuItem("BNK's")) {
                                ISO::dump_iso_contents();
                            }
                        } else {
                            ImGui::MenuItem("BNK's (ISO only)", nullptr,
                                            false, false);
                        }

                        if (!S.bnk_paths.empty() ||
                            !S.nested_bnk_paths.empty()) {
                            if (ImGui::MenuItem("BNK contents (raw)")) {
                                ISO::dump_bnk_contents();
                            }
                        } else {
                            ImGui::MenuItem(
                                "BNK contents (no BNKs indexed)",
                                nullptr, false, false);
                        }
                        if (!S.all_mdl_files.empty()) {
                            if (ImGui::MenuItem(".mdl")) {
                                ISO::dump_mdl_files();
                            }
                        } else {
                            ImGui::MenuItem(".mdl (no MDLs indexed)",
                                            nullptr, false, false);
                        }
                        if (!S.all_tex_files.empty()) {
                            if (ImGui::MenuItem(".tex")) {
                                ISO::dump_tex_files();
                            }
                        } else {
                            ImGui::MenuItem(".tex (no TEXs indexed)",
                                            nullptr, false, false);
                        }
                        if (!S.all_wav_files.empty()) {
                            if (ImGui::MenuItem(".wav")) {
                                ISO::dump_wav_files();
                            }
                        } else {
                            ImGui::MenuItem(".wav (no WAVs indexed)",
                                            nullptr, false, false);
                        }

                        if (!S.all_anim_files.empty()) {
                            ImGui::MenuItem(
                                (".anim (" +
                                 std::to_string(S.all_anim_files.size()) +
                                 " indexed — dump TBD)").c_str(),
                                nullptr, false, false);
                        } else {
                            ImGui::MenuItem(".anim (none indexed)",
                                            nullptr, false, false);
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::Separator();
                }
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

                ImGui::Separator();
                ImGui::TextUnformatted("Export location");
                ImGui::SetNextItemWidth(360);
                if (ImGui::InputText("##export_dir_input", &S.export_dir)) {
                    settings_save();
                }
                ImGui::SameLine();
                if (ImGui::Button("Browse##export_dir")) {
                    IGFD::FileDialogConfig cfg;
                    cfg.path = S.export_dir.empty() ? "." : S.export_dir;
                    ImGuiFileDialog::Instance()->OpenDialog(
                        "PickExportDir",
                        "Select export folder",
                        nullptr,
                        cfg);
                }

                ImGui::Separator();
                ImGui::TextUnformatted("MDL export texture format");
                ImGui::SetNextItemWidth(160);
                static const char* kTexFmts[] = { "DDS", "PNG", "JPG" };
                if (ImGui::BeginCombo("##mdl_tex_fmt",
                                      S.mdl_texture_export_format.c_str())) {
                    for (const char* opt : kTexFmts) {
                        bool selected =
                            (S.mdl_texture_export_format == opt);
                        if (ImGui::Selectable(opt, selected)) {
                            S.mdl_texture_export_format = opt;
                            settings_save();
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::Separator();
                ImGui::TextUnformatted("Flycam");
                if (ImGui::Checkbox("Invert X (look)",
                                    &S.cam_invert_x)) {
                    settings_save();
                }
                if (ImGui::Checkbox("Invert Y (look)",
                                    &S.cam_invert_y)) {
                    settings_save();
                }

                ImGui::Separator();
                if (ImGui::MenuItem("Keybinds...")) {
                    S.show_keybinds_window = true;
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("About")) {
                    About::open();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
    }

    if (S.root_dir.empty()) {
#ifdef _WIN32
        Splash::draw(device);
#else
        Splash::draw(window);
#endif
    } else if (UI::loading_in_progress() || S.bnk_paths.empty()) {

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
    About::draw(device);
#else
    About::draw();
#endif

    draw_keybinds_window();

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
        if (g_mp.has_model) {
            MP_Release(g_mp);
            g_mp.has_model = false;
            g_mp_initialized = false;
        }
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

#ifdef _WIN32
    process_pending_loads(device);
#else
    process_pending_loads();
#endif

    if (g_mp.has_model && g_mp.bone_count > 0) {
        Anim::global_player().tick(dt);
        Anim::global_player().apply_to_skeleton();
    } else if (Anim::global_player().clip()) {

        Anim::global_player().stop();
    }

    tex_export_drive();

    {
        ImVec2 vp = ImGui::GetMainViewport()->WorkSize;
        ImVec2 minSize(680, 440);
        ImVec2 maxSize(vp.x * 0.9f, vp.y * 0.9f);
        if (ImGuiFileDialog::Instance()->Display(
                "PickExportDir", ImGuiWindowFlags_NoCollapse, minSize, maxSize)) {
            if (ImGuiFileDialog::Instance()->IsOk()) {
                S.export_dir = ImGuiFileDialog::Instance()->GetCurrentPath();
                extern void settings_save();
                settings_save();
                OutputLog::info("Export location changed to " + S.export_dir);
            }
            ImGuiFileDialog::Instance()->Close();
        }
    }

    UI::draw_audio_player_window();

    if (show_output_log) {
        OutputLog::draw();
    }
}
