#define IMGUI_DEFINE_MATH_OPERATORS
#include "UI_Main.h"
#include "../Utilities/State.h"
#include "../Utilities/Utils.h"
#include "../Utilities/Files.h"
#include "../Utilities/Progress.h"
#include "UI_Panels.h"
#include "Panels/ImportDialog.h"
#include "AudioPlayerWindow.h"
#include "Layout/LoadingScreen.h"
#include "Layout/MainLayout.h"
#include "About/AboutWindow.h"
#include "../Splashscreen/Splashscreen.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "ImGuiFileDialog.h"
#include <cmath>
#include <cctype>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <d3d11.h>
#include <windows.h>
#include "../../resource.h"
#endif
#include "ModelPreview.h"
#include "../textures/export/TextureExport.h"
#include "../animations/AnimPlayer.h"
#include "OutputLog.h"
#include "../Level/Editing/LevelEdit.h"
#include "../Level/Core/LevelLoader.h"
#include "../Level/Database/TextBank.h"
#include "../Quest/QuestInjection.h"
#include "../Entity/NpcAuthoring.h"
#include "../Utilities/GameBackup.h"
#include "../BNKCore.cpp"
#ifndef _WIN32
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
ModelPreview g_mp;

bool g_mp_initialized = false;

struct LevelOpThread {
    std::thread t;
    ~LevelOpThread() {
        if (t.joinable()) t.join();
    }
    void launch(std::thread&& nt) {
        if (t.joinable()) t.join();
        t = std::move(nt);
    }
};

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

#ifdef _WIN32
static bool load_resource_bytes(int resource_id, std::vector<unsigned char>& out) {
    out.clear();
    HMODULE module = GetModuleHandleW(nullptr);
    if (!module) return false;
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resource_id), MAKEINTRESOURCEW(10));
    if (!resource) return false;
    DWORD size = SizeofResource(module, resource);
    if (size == 0) return false;
    HGLOBAL handle = LoadResource(module, resource);
    if (!handle) return false;
    void* ptr = LockResource(handle);
    if (!ptr) return false;
    auto* first = static_cast<const unsigned char*>(ptr);
    out.assign(first, first + size);
    return true;
}

static std::filesystem::path normalized_zip_export_path(std::filesystem::path path) {
    if (!path.has_extension()) {
        path += ".zip";
        return path;
    }
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (ext != ".zip") path.replace_extension(".zip");
    return path;
}

static void export_embedded_blender_plugin_zip(const std::filesystem::path& selected_path) {
    if (selected_path.empty()) {
        OutputLog::error("Blender plugin export failed: no output path selected");
        return;
    }
    std::vector<unsigned char> bytes;
    if (!load_resource_bytes(IDR_FABLE_LEVEL_IMPORTER_ZIP, bytes)) {
        OutputLog::error("Blender plugin export failed: embedded addon resource is missing");
        return;
    }
    std::filesystem::path out_path = normalized_zip_export_path(selected_path);
    std::error_code ec;
    if (out_path.has_parent_path()) {
        std::filesystem::create_directories(out_path.parent_path(), ec);
        if (ec) {
            OutputLog::error("Blender plugin export failed: " + ec.message());
            return;
        }
    }
    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        OutputLog::error("Blender plugin export failed: could not open " + out_path.string());
        return;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        OutputLog::error("Blender plugin export failed while writing " + out_path.string());
        return;
    }
    OutputLog::success("Blender plugin exported: " + out_path.string());
}
#endif
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
    const bool keys_ok =
        !LevelEdit::Enabled() || g_flycam.is_looking;
    bool w_pressed = keys_ok && ImGui::IsKeyDown(S.key_forward);
    bool s_pressed = keys_ok && ImGui::IsKeyDown(S.key_back);
    bool a_pressed = keys_ok && ImGui::IsKeyDown(S.key_left);
    bool d_pressed = keys_ok && ImGui::IsKeyDown(S.key_right);
    bool q_pressed = keys_ok && ImGui::IsKeyDown(S.key_down);
    bool e_pressed = keys_ok && ImGui::IsKeyDown(S.key_up);
    float mouse_dx = 0.0f;
    float mouse_dy = 0.0f;

    auto begin_right_look = [&]() {
        g_flycam.is_looking = true;
        g_flycam.right_press_pending = false;


        g_flycam.right_drag_distance = std::max(
            g_flycam.right_drag_distance, 4.0f);
        g_flycam.saved_mouse_x = g_flycam.right_press_x;
        g_flycam.saved_mouse_y = g_flycam.right_press_y;
#ifdef _WIN32
        ShowCursor(FALSE);
#else
        if (g_glfw_window) {
            glfwSetInputMode(g_glfw_window, GLFW_CURSOR,
                             GLFW_CURSOR_DISABLED);
        }
#endif
    };

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        g_flycam.right_drag_distance = 0.0f;
        g_flycam.right_press_x = io.MousePos.x;
        g_flycam.right_press_y = io.MousePos.y;
        g_flycam.right_press_started = float(ImGui::GetTime());
        if (LevelEdit::Enabled()) {


            g_flycam.right_press_pending = true;
        } else {
            begin_right_look();
        }
    }

    if (g_flycam.right_press_pending) {
        const float dx = io.MousePos.x - g_flycam.right_press_x;
        const float dy = io.MousePos.y - g_flycam.right_press_y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        g_flycam.right_drag_distance = std::max(
            g_flycam.right_drag_distance, distance);
        constexpr float kRightDragThreshold = 4.0f;
        constexpr float kRightHoldSeconds = 0.18f;
        const float held_for = float(ImGui::GetTime()) -
            g_flycam.right_press_started;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
            (distance >= kRightDragThreshold ||
             held_for >= kRightHoldSeconds)) {
            begin_right_look();
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        g_flycam.right_press_pending = false;
        if (g_flycam.is_looking) {
            g_flycam.is_looking = false;
#ifdef _WIN32
            ShowCursor(TRUE);
            SetCursorPos((int)g_flycam.saved_mouse_x,
                         (int)g_flycam.saved_mouse_y);
#else
            if (g_glfw_window) {
                glfwSetInputMode(g_glfw_window, GLFW_CURSOR,
                                 GLFW_CURSOR_NORMAL);
                glfwSetCursorPos(g_glfw_window, g_flycam.saved_mouse_x,
                                 g_flycam.saved_mouse_y);
            }
#endif
        }
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
        g_flycam.right_drag_distance +=
            std::sqrt(mouse_dx * mouse_dx + mouse_dy * mouse_dy);

        
        
        if (io.MouseWheel != 0.0f) {
            g_flycam.move_speed *= std::pow(1.25f, io.MouseWheel);
            g_flycam.move_speed =
                std::clamp(g_flycam.move_speed, 0.1f, 5000.0f);
        }
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
                                    viewport->WorkSize.y));

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

                {
                    const bool import_busy = ImportDialog::Busy();
                    if (ImGui::BeginMenu("Import", !import_busy)) {
                        if (ImGui::MenuItem("Import .glb...")) {
                            ImportDialog::OpenGlb();
                        }
                        if (ImGui::MenuItem("Import Image...")) {
                            ImportDialog::OpenImage();
                        }
                        if (ImGui::MenuItem("Import Folder...")) {
                            ImportDialog::OpenFolder();
                        }
                        ImGui::EndMenu();
                    }
                    if (import_busy && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("An import is already running");
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Alt+F4")) {
#ifdef _WIN32
                    PostQuitMessage(0);
#else
                    if (g_glfw_window) glfwSetWindowShouldClose(g_glfw_window, GLFW_TRUE);
#endif
                }
                ImGui::EndMenu();
            }
            {
                const bool level_ready = LevelEdit::Available();
                if (ImGui::BeginMenu("Level", level_ready)) {
                    static LevelOpThread s_toggle_thread;
                    bool edit_on = LevelEdit::Enabled();
                    if (ImGui::MenuItem("Edit Level", nullptr, &edit_on,
                                        !LevelEdit::Saving())) {
                        if (!edit_on) {
                            std::string msg;
                            LevelEdit::SetEnabled(false, msg);
                            OutputLog::info("level edit: " + msg);
                        } else {
                            s_toggle_thread.launch(std::thread([] {
                                progress_open(100,
                                              "Enabling level editing...");
                                std::string msg;
                                const bool ok =
                                    LevelEdit::SetEnabled(true, msg);
                                progress_done();
                                if (ok) {
                                    OutputLog::success("level edit: " +
                                                       msg);
                                } else {
                                    OutputLog::error("level edit: " + msg);
                                }
                            }));
                        }
                    }
                    if (LevelEdit::Enabled()) {
                        const bool saving = LevelEdit::Saving();
                        const std::string save_label =
                            saving ? "Saving..."
                                   : (LevelEdit::Dirty() ? "Save Level*"
                                                         : "Save Level");
                        if (ImGui::MenuItem(save_label.c_str(), nullptr,
                                            false,
                                            LevelEdit::Dirty() && !saving)) {
                            static LevelOpThread s_save_thread;
                            s_save_thread.launch(std::thread([] {
                                progress_open(100, "Saving level...");
                                std::string msg;
                                const bool ok = LevelEdit::Save(msg);
                                progress_done();
                                if (ok) {
                                    OutputLog::success("level edit: " +
                                                       msg);
                                } else {
                                    OutputLog::error("level edit: " + msg);
                                }
                            }));
                        }
                    }
                    ImGui::EndMenu();
                }
                if (S.level_edit_guard_popup) {
                    ImGui::OpenPopup("Level Editing Active");
                    S.level_edit_guard_popup = false;
                }
                if (ImGui::BeginPopupModal("Level Editing Active", nullptr,
                                           ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextWrapped("%s.",
                                       S.level_edit_guard_message.c_str());
                    ImGui::TextDisabled(
                        "Turn off Edit Level (Level menu) to browse other "
                        "assets. Further attempts are noted in the output "
                        "log instead of this popup.");
                    ImGui::Spacing();
                    if (ImGui::Button("OK", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SetItemDefaultFocus();
                    ImGui::EndPopup();
                }
            }
            if (ImGui::BeginMenu("Settings")) {
                if (ImGui::Checkbox("Show file paths in tree tooltips", &S.show_paths)) {
                    S.hide_tooltips = !S.show_paths;
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

            if (ImGui::BeginMenu("Addons")) {
                if (ImGui::MenuItem("Export Blender Plugin")) {
#ifdef _WIN32
                    IGFD::FileDialogConfig cfg;
                    cfg.path = S.export_dir.empty() ? "." : S.export_dir;
                    cfg.fileName = "FableLevelImporter.zip";
                    cfg.flags = ImGuiFileDialogFlags_ConfirmOverwrite;
                    ImGuiFileDialog::Instance()->OpenDialog(
                        "ExportBlenderPluginZip",
                        "Export Blender Plugin",
                        ".zip",
                        cfg);
#else
                    OutputLog::error("Blender plugin export is only available in the Windows build");
#endif
                }
                ImGui::EndMenu();
            }

            GameBackup::DrawMainMenu();

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
        UI::draw_main_layout(device, bottom_reserve);
#else
        UI::draw_main_layout(bottom_reserve);
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

#ifdef _WIN32
    ImportDialog::Draw(device);
#else
    ImportDialog::Draw();
#endif

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

#ifdef _WIN32
    {
        ImVec2 vp = ImGui::GetMainViewport()->WorkSize;
        ImVec2 minSize(680, 440);
        ImVec2 maxSize(vp.x * 0.9f, vp.y * 0.9f);
        if (ImGuiFileDialog::Instance()->Display(
                "ExportBlenderPluginZip", ImGuiWindowFlags_NoCollapse, minSize, maxSize)) {
            if (ImGuiFileDialog::Instance()->IsOk()) {
                std::filesystem::path out_path = ImGuiFileDialog::Instance()->GetFilePathName();
                if (out_path.empty()) {
                    std::filesystem::path current = ImGuiFileDialog::Instance()->GetCurrentPath();
                    out_path = current / "FableLevelImporter.zip";
                }
                export_embedded_blender_plugin_zip(out_path);
            }
            ImGuiFileDialog::Instance()->Close();
        }
    }
#endif

    UI::draw_audio_player_window();

    if (show_output_log) {
        OutputLog::draw();
    }
}
