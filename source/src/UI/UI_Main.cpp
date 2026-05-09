#define IMGUI_DEFINE_MATH_OPERATORS
#include "UI_Main.h"
#include "../Utilities/State.h"
#include "../Utilities/Utils.h"
#include "../Utilities/Files.h"
#include "../Utilities/Progress.h"
#include "UI_Panels.h"
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
#ifndef _WIN32
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
ModelPreview g_mp;
static bool g_mp_initialized = false;
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
#ifdef _WIN32
static bool LoadTextureFromFile(const char* filename, ID3D11Device* device, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height) {
    int width, height, channels;
    unsigned char* image_data = stbi_load(filename, &width, &height, &channels, 4);
    if (image_data == NULL) return false;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    ID3D11Texture2D* pTexture = NULL;
    D3D11_SUBRESOURCE_DATA subResource{};
    subResource.pSysMem = image_data;
    subResource.SysMemPitch = desc.Width * 4;
    subResource.SysMemSlicePitch = 0;
    device->CreateTexture2D(&desc, &subResource, &pTexture);
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;
    device->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
    pTexture->Release();
    *out_width = width;
    *out_height = height;
    stbi_image_free(image_data);
    return true;
}
#else
static bool LoadTextureFromFile(const char* filename, unsigned int* out_texture, int* out_width, int* out_height) {
    int width, height, channels;
    unsigned char* image_data = stbi_load(filename, &width, &height, &channels, 4);
    if (image_data == NULL) return false;
    unsigned int texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    *out_texture = texture;
    *out_width = width;
    *out_height = height;
    stbi_image_free(image_data);
    return true;
}
#endif
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
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    float dt = ImGui::GetIO().DeltaTime;
    if (S.root_dir.empty()) {
#ifdef _WIN32
        Splash::draw(device);
#else
        Splash::draw(window);
#endif
    } else {
        ImVec2 window_pos = ImGui::GetWindowPos();
        ImVec2 window_size = ImGui::GetWindowSize();

        if (g_mp.has_model) {
            if (!g_mp_initialized) {
#ifdef _WIN32
                MP_Init(device, g_mp, (int)window_size.x, (int)window_size.y);
#else
                MP_Init(g_mp, (int)window_size.x, (int)window_size.y);
#endif
                g_mp_initialized = true;
            }
#ifdef _WIN32
            MP_Resize(device, g_mp, (int)window_size.x, (int)window_size.y);
            MP_Render(device, g_mp, g_flycam);
#else
            MP_Resize(g_mp, (int)window_size.x, (int)window_size.y);
            MP_Render(g_mp, g_flycam);
#endif
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
#ifdef _WIN32
            if (g_mp.srv) {
                draw_list->AddImage((ImTextureID)g_mp.srv, window_pos, ImVec2(window_pos.x + window_size.x, window_pos.y + window_size.y));
            }
#else
            if (g_mp.color_tex) {
                draw_list->AddImage((ImTextureID)(intptr_t)g_mp.color_tex, window_pos, ImVec2(window_pos.x + window_size.x, window_pos.y + window_size.y));
            }
#endif
            if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemHovered()) {
                handle_flycam_input(dt);
            }
            ImGui::SetCursorPos(ImVec2(10, 10));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 0.8f));
            ImGui::BeginChild("controls_hint", ImVec2(220, 140), true);
            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Camera Controls");
            ImGui::Separator();
            ImGui::Text("W/S - Forward/Back");
            ImGui::Text("A/D - Strafe Left/Right");
            ImGui::Text("Q/E - Down/Up");
            ImGui::Text("Right-Click - Look");
            ImGui::Text("ESC - Close Model");
            ImGui::EndChild();
            ImGui::PopStyleColor();
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                MP_Release(g_mp);
                g_mp.has_model = false;
                g_mp_initialized = false;
            }
        }
    }
    ImGui::End();

    if (!S.bnk_paths.empty()) {
        ImGuiViewport* viewport = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 10, viewport->WorkPos.y + 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(900, viewport->WorkSize.y - 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(600, 300), ImVec2(viewport->WorkSize.x - 50, viewport->WorkSize.y));

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 0.95f));

        if (ImGui::Begin("Asset Browser", nullptr, ImGuiWindowFlags_None)) {
            ImGui::BeginChild("left_panel_wrap", ImVec2(360, 0), false);
#ifdef _WIN32
            draw_left_panel(device);
#else
            draw_left_panel();
#endif
            ImGui::EndChild();
            ImGui::SameLine();
#ifdef _WIN32
            draw_right_panel(device);
#else
            draw_right_panel();
#endif
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }

#ifdef _WIN32
    if (S.pending_texture_load) {
        S.pending_texture_load = false;
        if (S.texture_window_srv) {
            S.texture_window_srv->Release();
            S.texture_window_srv = nullptr;
        }
        if (S.pending_texture_w > 0 && S.pending_texture_h > 0 && !S.pending_texture_rgba.empty()) {
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
#else
    if (S.pending_texture_load) {
        S.pending_texture_load = false;
        if (S.texture_window_gl) {
            glDeleteTextures(1, &S.texture_window_gl);
            S.texture_window_gl = 0;
        }
        if (S.pending_texture_w > 0 && S.pending_texture_h > 0 && !S.pending_texture_rgba.empty()) {
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
#endif

    if (S.show_texture_window) {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->GetCenter().x - 200, viewport->GetCenter().y - 150), ImGuiCond_FirstUseEver);

        bool is_error = (S.texture_window_width == 0 || S.texture_window_height == 0);
        if (is_error) {
            ImGui::SetNextWindowSize(ImVec2(500, 100), ImGuiCond_FirstUseEver);
        } else {
            ImGui::SetNextWindowSize(ImVec2((float)S.texture_window_width + 20, (float)S.texture_window_height + 60), ImGuiCond_FirstUseEver);
        }

        std::string title = is_error ? "Texture Preview" : ("Texture: " + std::filesystem::path(S.texture_window_name).filename().string());
        if (ImGui::Begin(title.c_str(), &S.show_texture_window, ImGuiWindowFlags_NoCollapse)) {
            if (is_error) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", S.texture_window_name.c_str());
            } else {
                ImGui::Text("%dx%d", S.texture_window_width, S.texture_window_height);
                ImGui::Separator();

                ImVec2 avail = ImGui::GetContentRegionAvail();
                float scale = std::min(avail.x / (float)S.texture_window_width, avail.y / (float)S.texture_window_height);
                float disp_w = S.texture_window_width * scale;
                float disp_h = S.texture_window_height * scale;

#ifdef _WIN32
                if (S.texture_window_srv) {
                    ImGui::Image((ImTextureID)S.texture_window_srv, ImVec2(disp_w, disp_h));
                }
#else
                if (S.texture_window_gl) {
                    ImGui::Image((ImTextureID)(intptr_t)S.texture_window_gl, ImVec2(disp_w, disp_h));
                }
#endif
            }
        }
        ImGui::End();

        if (!S.show_texture_window) {
#ifdef _WIN32
            if (S.texture_window_srv) {
                S.texture_window_srv->Release();
                S.texture_window_srv = nullptr;
            }
#else
            if (S.texture_window_gl) {
                glDeleteTextures(1, &S.texture_window_gl);
                S.texture_window_gl = 0;
            }
#endif
        }
    }
}