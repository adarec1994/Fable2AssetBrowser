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
#include "ModelPreview.h"
#include <d3d11.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

ModelPreview g_mp;

static ID3D11ShaderResourceView* g_splash_texture = nullptr;
static float g_splash_scroll_offset = 0.0f;
static int g_splash_width = 0;
static int g_splash_height = 0;

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

        if (g_splash_texture) {
            g_splash_scroll_offset += ImGui::GetIO().DeltaTime * 20.0f;
            if (g_splash_scroll_offset >= g_splash_width) {
                g_splash_scroll_offset -= g_splash_width;
            }

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

            float offset = -g_splash_scroll_offset;
            while (offset < window_size.x) {
                ImVec2 p_min(window_pos.x + offset, window_pos.y);
                ImVec2 p_max(window_pos.x + offset + display_width, window_pos.y + display_height);
                draw_list->AddImage((ImTextureID)g_splash_texture, p_min, p_max);
                offset += display_width;
            }
        }

        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 sz(320, 50);
        ImVec2 pos((avail.x - sz.x) * 0.5f, (avail.y - sz.y) * 0.33f);
        ImGui::SetCursorPos(pos);
        if (ImGui::Button("Select Fable 2 Directory", sz)) {
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