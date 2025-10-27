#define IMGUI_DEFINE_MATH_OPERATORS
#include "UI_Main.h"
#include "State.h"
#include "Utils.h"
#include "files.h"
#include "Progress.h"
#include "UI_Panels.h"
#include "BNKCore.cpp"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "ImGuiFileDialog.h"
#include <filesystem>
#include <algorithm>
#include <vector>
#include "ModelPreview.h"
#include <d3d11.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

ModelPreview g_mp;

static ID3D11ShaderResourceView* g_splash_texture = nullptr;
static ID3D11ShaderResourceView* g_logo_texture = nullptr;
static ID3D11ShaderResourceView* g_button_texture = nullptr;
static ID3D11ShaderResourceView* g_sparkle_textures[8] = {nullptr};
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
"              BivsiiBBPjrii   iivSZirBrBs       sSPPQBMu          Jvrrvvvriiiiiii          isrrvvvvrrrri           UvrvLsYvrrrrrrvvvsId                ivuKqQuQsivMI    iBiqUiuBI           i           ",
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

void refresh_file_table() { S.selected_file_index = -1; }

void pick_bnk(const std::string &path) {
    S.selected_bnk = path;
    S.selected_nested_temp_path.clear();
    S.files.clear();
    S.file_filter.clear();
    BNKReader reader(path);
    const auto &fe = reader.list_files();
    S.files.reserve(fe.size());
    for (size_t i = 0; i < fe.size(); ++i) S.files.push_back({(int) i, fe[i].name, fe[i].uncompressed_size});

    std::sort(S.files.begin(), S.files.end(), [](const BNKItemUI &a, const BNKItemUI &b) {
        std::string x = std::filesystem::path(a.name).filename().string();
        std::string y = std::filesystem::path(b.name).filename().string();
        std::transform(x.begin(), x.end(), x.begin(), ::tolower);
        std::transform(y.begin(), y.end(), y.begin(), ::tolower);
        return x < y;
    });

    refresh_file_table();
}

void open_folder_logic(const std::string &sel) {
    if (sel.empty()) {
        show_error_box("No folder selected");
        return;
    }
    if (!std::filesystem::exists(sel)) {
        show_error_box(std::string("Folder does not exist: ") + sel);
        return;
    }
    if (!std::filesystem::is_directory(sel)) {
        show_error_box(std::string("Selected path is not a directory: ") + sel);
        return;
    }
    S.root_dir = sel;
    S.last_dir = sel;
    save_last_dir(sel);
    try {
        S.bnk_paths = scan_bnks_recursive(sel);
        if (S.bnk_paths.empty()) S.bnk_paths = find_bnks(sel);

        S.adb_paths = scan_adbs_recursive(sel);
    } catch (...) {
        show_error_box("Error searching for BNK files");
        return;
    }
    if (S.bnk_paths.empty()) {
        show_error_box(
            std::string("No .bnk files found in:\n") + sel + std::string(
                "\n\nPlease select a folder containing Fable 2 BNK files."));
        return;
    }
    std::sort(S.bnk_paths.begin(), S.bnk_paths.end(), [](const std::string &a, const std::string &b) {
        std::string A = std::filesystem::path(a).filename().string(), B = std::filesystem::path(b).filename().string();
        std::transform(A.begin(), A.end(), A.begin(), ::tolower);
        std::transform(B.begin(), B.end(), B.begin(), ::tolower);
        return A < B;
    });

    std::sort(S.adb_paths.begin(), S.adb_paths.end(), [](const std::string &a, const std::string &b) {
        std::string A = std::filesystem::path(a).filename().string(), B = std::filesystem::path(b).filename().string();
        std::transform(A.begin(), A.end(), A.begin(), ::tolower);
        std::transform(B.begin(), B.end(), B.begin(), ::tolower);
        return A < B;
    });

    S.selected_bnk.clear();
    S.files.clear();
    refresh_file_table();
}

void draw_main(HWND hwnd, ID3D11Device* device) {
    ImGuiViewport *vp = ImGui::GetMainViewport();
    const float inset = 8.0f;
    ImGui::SetNextWindowPos(vp->WorkPos + ImVec2(inset, inset));
    ImGui::SetNextWindowSize(vp->WorkSize - ImVec2(inset * 2, inset * 2));
    ImGui::Begin("Fable 2 Asset Browser", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (S.root_dir.empty()) {
        if (!g_splash_texture) {
            LoadTextureFromFile("../include/image/splash_pano.png", device, &g_splash_texture, &g_splash_width, &g_splash_height);
        }
        if (!g_logo_texture) {
            LoadTextureFromFile("../include/image/f2_logo.png", device, &g_logo_texture, &g_logo_width, &g_logo_height);
        }

        for (int i = 0; i < 8; ++i) {
            if (!g_sparkle_textures[i]) {
                std::string path = "../include/image/sparkle_" + std::to_string(i + 1) + ".png";
                LoadTextureFromFile(path.c_str(), device, &g_sparkle_textures[i], &g_sparkle_widths[i], &g_sparkle_heights[i]);
            }
        }

        g_splash_time_elapsed += ImGui::GetIO().DeltaTime;

        float alpha = 0.0f;
        if (g_splash_time_elapsed > g_fade_in_delay) {
            float fade_progress = (g_splash_time_elapsed - g_fade_in_delay) / g_fade_in_duration;
            alpha = (fade_progress > 1.0f) ? 1.0f : fade_progress;
        }

        if (g_splash_texture && alpha > 0.0f) {
            ImVec2 window_pos = ImGui::GetWindowPos();
            ImVec2 window_size = ImGui::GetWindowSize();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            float aspect = (float)g_splash_height / (float)g_splash_width;
            float display_height = window_size.y;
            float display_width = display_height / aspect;

            if (display_width < window_size.x) {
                display_width = window_size.x;
                display_height = display_width * aspect;
            }

            g_splash_scroll_offset += ImGui::GetIO().DeltaTime * 20.0f;
            if (g_splash_scroll_offset >= display_width) {
                g_splash_scroll_offset -= display_width;
            }

            float x1 = window_pos.x - g_splash_scroll_offset;
            float x2 = x1 + display_width;

            ImU32 col = IM_COL32(255, 255, 255, (int)(alpha * 255));

            ImVec2 p1_min(x1, window_pos.y);
            ImVec2 p1_max(x1 + display_width, window_pos.y + display_height);
            draw_list->AddImage((ImTextureID)g_splash_texture, p1_min, p1_max, ImVec2(0,0), ImVec2(1,1), col);

            ImVec2 p2_min(x2, window_pos.y);
            ImVec2 p2_max(x2 + display_width, window_pos.y + display_height);
            draw_list->AddImage((ImTextureID)g_splash_texture, p2_min, p2_max, ImVec2(0,0), ImVec2(1,1), col);
        }

        if (g_logo_texture) {
            ImVec2 window_pos = ImGui::GetWindowPos();
            ImVec2 window_size = ImGui::GetWindowSize();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            float logo_scale = window_size.x / (g_logo_width * 1.5f);
            float scaled_width = g_logo_width * logo_scale;
            float scaled_height = g_logo_height * logo_scale;

            float logo_x = window_pos.x + (window_size.x - scaled_width) * 0.5f;
            float logo_y = window_pos.y + window_size.y * 0.33f - scaled_height * 0.5f;

            build_letter_positions();

            if (!g_sparkles_initialized) {
                for (int i = 0; i < 400; ++i) {
                    if (g_letter_positions.empty()) break;

                    int pos_index = rand() % g_letter_positions.size();
                    float norm_x = g_letter_positions[pos_index].first;
                    float norm_y = g_letter_positions[pos_index].second - 0.08f;

                    float jitter_x = (((float)rand() / RAND_MAX) - 0.5f) * 0.08f;
                    float jitter_y = (((float)rand() / RAND_MAX) - 0.5f) * 0.06f;

                    g_sparkles[i].x = logo_x + (norm_x + jitter_x) * scaled_width;
                    g_sparkles[i].start_y = logo_y + (norm_y + jitter_y) * scaled_height;
                    g_sparkles[i].y = g_sparkles[i].start_y;
                    g_sparkles[i].texture_index = rand() % 8;
                    g_sparkles[i].life_time = ((float)rand() / RAND_MAX) * (0.4f + ((float)rand() / RAND_MAX) * 0.4f);
                    g_sparkles[i].max_life = 0.4f + ((float)rand() / RAND_MAX) * 0.4f;
                    g_sparkles[i].active = true;
                    g_sparkles[i].falls = (rand() % 100) < 70;
                }
                g_sparkles_initialized = true;
            }

            for (int i = 0; i < 400; ++i) {
                if (!g_sparkles[i].active) continue;

                g_sparkles[i].life_time += ImGui::GetIO().DeltaTime;
                if (g_sparkles[i].life_time >= g_sparkles[i].max_life) {
                    g_sparkles[i].life_time = 0.0f;

                    if (!g_letter_positions.empty()) {
                        int pos_index = rand() % g_letter_positions.size();
                        float norm_x = g_letter_positions[pos_index].first;
                        float norm_y = g_letter_positions[pos_index].second - 0.08f;

                        float jitter_x = (((float)rand() / RAND_MAX) - 0.5f) * 0.08f;
                        float jitter_y = (((float)rand() / RAND_MAX) - 0.5f) * 0.06f;

                        g_sparkles[i].x = logo_x + (norm_x + jitter_x) * scaled_width;
                        g_sparkles[i].start_y = logo_y + (norm_y + jitter_y) * scaled_height;
                        g_sparkles[i].y = g_sparkles[i].start_y;
                        g_sparkles[i].texture_index = rand() % 8;
                        g_sparkles[i].max_life = 0.4f + ((float)rand() / RAND_MAX) * 0.4f;
                        g_sparkles[i].falls = (rand() % 100) < 70;
                    }
                }

                float phase = g_sparkles[i].life_time / g_sparkles[i].max_life;

                if (g_sparkles[i].falls) {
                    float fall_distance = 30.0f;
                    g_sparkles[i].y = g_sparkles[i].start_y + (phase * fall_distance);
                } else {
                    g_sparkles[i].y = g_sparkles[i].start_y;
                }

                int tex_idx = g_sparkles[i].texture_index;
                if (!g_sparkle_textures[tex_idx]) continue;

                float sparkle_alpha = 0.0f;
                if (phase < 0.5f) {
                    sparkle_alpha = phase * 2.0f;
                } else {
                    sparkle_alpha = (1.0f - phase) * 2.0f;
                }

                sparkle_alpha *= 0.7f;

                float sparkle_size = 32.0f;
                ImU32 col = IM_COL32(255, 255, 255, (int)(sparkle_alpha * 255));

                float center_x = g_sparkles[i].x;
                float center_y = g_sparkles[i].y;
                float half_size = sparkle_size * 0.5f;

                ImVec2 sp_min(center_x - half_size, center_y - half_size);
                ImVec2 sp_max(center_x + half_size, center_y + half_size);
                draw_list->AddImage((ImTextureID)g_sparkle_textures[tex_idx], sp_min, sp_max, ImVec2(0,0), ImVec2(1,1), col);

                float glow_size = sparkle_size * 1.3f;
                float glow_half = glow_size * 0.5f;
                ImU32 glow_col = IM_COL32(255, 255, 200, (int)(sparkle_alpha * 100));
                ImVec2 glow_min(center_x - glow_half, center_y - glow_half);
                ImVec2 glow_max(center_x + glow_half, center_y + glow_half);
                draw_list->AddImage((ImTextureID)g_sparkle_textures[tex_idx], glow_min, glow_max, ImVec2(0,0), ImVec2(1,1), glow_col);
            }

            ImVec2 logo_min(logo_x, logo_y);
            ImVec2 logo_max(logo_x + scaled_width, logo_y + scaled_height);
            draw_list->AddImage((ImTextureID)g_logo_texture, logo_min, logo_max);
        }

        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("splash_click_area", avail);
        if (ImGui::IsItemClicked()) {
            IGFD::FileDialogConfig cfg;
            std::string base = (!S.last_dir.empty() && std::filesystem::exists(S.last_dir) &&
                                std::filesystem::is_directory(S.last_dir))
                                   ? S.last_dir
                                   : ".";
            cfg.path = base.c_str();
            ImGuiFileDialog::Instance()->OpenDialog("PickDir", "Select Fable 2 Directory", nullptr, cfg);
        }
        draw_folder_dialog();
    } else {
        ImGui::BeginChild("browser_group", ImVec2(0, 0), false);
        ImGui::BeginGroup();
        ImGui::BeginChild("left_panel_wrap", ImVec2(360, 0), false);
        draw_left_panel(device);
        ImGui::EndChild();
        ImGui::SameLine();
        draw_right_panel(device);
        ImGui::EndGroup();
        ImGui::EndChild();
    }
    ImGui::End();
}