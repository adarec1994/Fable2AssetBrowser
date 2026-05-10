#define IMGUI_DEFINE_MATH_OPERATORS
#include "Splashscreen.h"
#include "LogoMap.h"
#include "SplashSparkles.h"
#include "IconFont.h"
#include "../Audio/play_audio.h"
#include "../ISO/IsoMount.h"
#include "../Utilities/State.h"
#include "../Utilities/Files.h"
#include "../UI/UI_Panels.h"
#include "imgui.h"
#include "ImGuiFileDialog.h"
#include <filesystem>
#include <string>
#include <vector>
#include <cstdio>

void save_audio_muted(bool muted);

#ifdef _WIN32
#include <d3d11.h>
#include <windows.h>
#include "../../resource.h"
#endif

#include "stb_image.h"

namespace Splash {

namespace {

#ifdef _WIN32
ID3D11ShaderResourceView* g_splash_texture = nullptr;
ID3D11ShaderResourceView* g_logo_texture = nullptr;
ID3D11ShaderResourceView* g_sparkle_textures[8] = {nullptr};
#else
unsigned int g_splash_texture = 0;
unsigned int g_logo_texture = 0;
unsigned int g_sparkle_textures[8] = {0};
#endif
int  g_splash_width = 0, g_splash_height = 0;
int  g_logo_width   = 0, g_logo_height   = 0;

float g_splash_scroll_offset = 0.0f;
float g_splash_time_elapsed = 0.0f;
constexpr float kFadeInDelay    = 5.0f;
constexpr float kFadeInDuration = 2.0f;

std::string g_status_msg;

#ifdef _WIN32

bool load_resource_bytes(int resource_id, std::vector<unsigned char>& out) {
    HMODULE mod = GetModuleHandleA(nullptr);
    HRSRC hRes = FindResourceA(mod, MAKEINTRESOURCEA(resource_id), (LPCSTR)RT_RCDATA);
    if (!hRes) return false;
    DWORD size = SizeofResource(mod, hRes);
    HGLOBAL hData = LoadResource(mod, hRes);
    if (!hData) return false;
    void* p = LockResource(hData);
    if (!p || size == 0) return false;
    out.assign((const unsigned char*)p, (const unsigned char*)p + size);
    return true;
}

bool create_d3d_texture(ID3D11Device* device, const unsigned char* rgba, int w, int h,
                        ID3D11ShaderResourceView** out_srv) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* tex = nullptr;
    D3D11_SUBRESOURCE_DATA sub{};
    sub.pSysMem = rgba; sub.SysMemPitch = (UINT)(w * 4);
    if (FAILED(device->CreateTexture2D(&desc, &sub, &tex)) || !tex) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = 1;
    HRESULT hr = device->CreateShaderResourceView(tex, &sv, out_srv);
    tex->Release();
    return SUCCEEDED(hr);
}

bool load_texture_from_resource(ID3D11Device* device, int resource_id,
                                ID3D11ShaderResourceView** out_srv,
                                int* out_w, int* out_h) {
    if (*out_srv) return true;
    std::vector<unsigned char> bytes;
    if (!load_resource_bytes(resource_id, bytes)) return false;
    int w, h, channels;
    unsigned char* rgba = stbi_load_from_memory(bytes.data(), (int)bytes.size(),
                                                 &w, &h, &channels, 4);
    if (!rgba) return false;
    bool ok = create_d3d_texture(device, rgba, w, h, out_srv);
    stbi_image_free(rgba);
    if (ok) { *out_w = w; *out_h = h; }
    return ok;
}

void load_textures_if_needed(ID3D11Device* device) {
    load_texture_from_resource(device, IDR_SPLASH_PNG, &g_splash_texture, &g_splash_width, &g_splash_height);
    load_texture_from_resource(device, IDR_LOGO_PNG,   &g_logo_texture,   &g_logo_width,   &g_logo_height);
    int dummy_w, dummy_h;
    for (int i = 0; i < 8; ++i) {
        load_texture_from_resource(device, IDR_SPARKLE_1 + i, &g_sparkle_textures[i], &dummy_w, &dummy_h);
    }
}
#else

bool LoadTextureFromFile(const char* filename, unsigned int* out_tex, int* out_w, int* out_h) {
    int width, height, channels;
    unsigned char* image_data = stbi_load(filename, &width, &height, &channels, 4);
    if (!image_data) return false;
    glGenTextures(1, out_tex);
    glBindTexture(GL_TEXTURE_2D, *out_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    *out_w = width; *out_h = height;
    stbi_image_free(image_data);
    return true;
}

void load_textures_if_needed() {
    if (!g_splash_texture)
        LoadTextureFromFile("include/image/splash_pano.png", &g_splash_texture, &g_splash_width, &g_splash_height);
    if (!g_logo_texture)
        LoadTextureFromFile("include/image/f2_logo.png", &g_logo_texture, &g_logo_width, &g_logo_height);
    for (int i = 0; i < 8; ++i) {
        if (!g_sparkle_textures[i]) {
            char path[64];
            std::snprintf(path, sizeof(path), "include/image/sparkle_%d.png", i + 1);
            int w, h;
            LoadTextureFromFile(path, &g_sparkle_textures[i], &w, &h);
        }
    }
}
#endif

std::string pick_browse_base_dir() {
    if (!S.last_dir.empty()
        && std::filesystem::exists(S.last_dir)
        && std::filesystem::is_directory(S.last_dir))
    {
        return S.last_dir;
    }
    return ".";
}

void handle_dialog_pick() {
    std::string file_path = ImGuiFileDialog::Instance()->GetFilePathName();
    std::string current   = ImGuiFileDialog::Instance()->GetCurrentPath();

    auto ends_with_lower = [](const std::string& s, const char* suffix) {
        size_t n = strlen(suffix);
        if (s.size() < n) return false;
        for (size_t i = 0; i < n; ++i) {
            char a = (char)tolower((unsigned char)s[s.size() - n + i]);
            if (a != suffix[i]) return false;
        }
        return true;
    };

    if (!file_path.empty() && std::filesystem::is_regular_file(file_path)
        && ends_with_lower(file_path, ".iso"))
    {

        std::string err;
        if (!ISO::IsoMount::instance().mount(file_path, &err)) {
            g_status_msg = "ISO mount failed: " + err;
            return;
        }
        open_iso_logic(file_path);
        g_status_msg.clear();
        return;
    }

    std::string folder;
    if (std::filesystem::is_directory(file_path)) folder = file_path;
    else if (!current.empty() && std::filesystem::is_directory(current)) folder = current;

    if (!folder.empty()) {
        open_folder_logic(folder);
        g_status_msg.clear();
    } else {
        g_status_msg = "No folder or .iso selected.";
    }
}

void draw_dialogs() {
    ImVec2 vp = ImGui::GetMainViewport()->WorkSize;
    ImVec2 minSize(680, 440);
    ImVec2 maxSize(vp.x * 0.9f, vp.y * 0.9f);
    if (ImGuiFileDialog::Instance()->Display("PickPath",
            ImGuiWindowFlags_NoCollapse, minSize, maxSize))
    {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            handle_dialog_pick();
        }
        ImGuiFileDialog::Instance()->Close();
    }
}

void open_picker() {
    IGFD::FileDialogConfig cfg;
    cfg.path = pick_browse_base_dir();

    ImGuiFileDialog::Instance()->OpenDialog("PickPath",
        "Select Fable 2 folder or .iso", ".iso,((.*))", cfg);
}

}

#ifdef _WIN32
void draw(ID3D11Device* device) {
    load_textures_if_needed(device);
#else
void draw(GLFWwindow* /*window*/) {
    load_textures_if_needed();
#endif
    float dt = ImGui::GetIO().DeltaTime;
    g_splash_time_elapsed += dt;

    ImVec2 window_pos = ImGui::GetWindowPos();
    ImVec2 window_size = ImGui::GetWindowSize();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    if (g_splash_texture && g_splash_width > 0) {
        float scale = window_size.y / (float)g_splash_height;
        float scaled_width = g_splash_width * scale;
        const float scroll_speed = 20.0f;
        g_splash_scroll_offset += dt * scroll_speed;
        if (g_splash_scroll_offset > scaled_width) g_splash_scroll_offset -= scaled_width;
        float start_x = window_pos.x - g_splash_scroll_offset;
        for (float x = start_x; x < window_pos.x + window_size.x; x += scaled_width) {
            ImVec2 p_min(x, window_pos.y);
            ImVec2 p_max(x + scaled_width, window_pos.y + window_size.y);
#ifdef _WIN32
            draw_list->AddImage((ImTextureID)g_splash_texture, p_min, p_max, ImVec2(0,0), ImVec2(1,1));
#else
            draw_list->AddImage((ImTextureID)(intptr_t)g_splash_texture, p_min, p_max, ImVec2(0,0), ImVec2(1,1));
#endif
        }
    }

    float logo_x = 0, logo_y = 0, scaled_w = 0, scaled_h = 0;
    if (g_logo_texture && g_logo_width > 0) {
        float logo_scale = window_size.x / (g_logo_width * 1.5f);
        scaled_w = g_logo_width * logo_scale;
        scaled_h = g_logo_height * logo_scale;
        logo_x = window_pos.x + (window_size.x - scaled_w) * 0.5f;
        logo_y = window_pos.y + window_size.y * 0.33f - scaled_h * 0.5f;

        update_and_draw_sparkles(draw_list, g_sparkle_textures,
                                 logo_x, logo_y, scaled_w, scaled_h, dt);

        ImVec2 logo_min(logo_x, logo_y);
        ImVec2 logo_max(logo_x + scaled_w, logo_y + scaled_h);
#ifdef _WIN32
        draw_list->AddImage((ImTextureID)g_logo_texture, logo_min, logo_max);
#else
        draw_list->AddImage((ImTextureID)(intptr_t)g_logo_texture, logo_min, logo_max);
#endif
    }

    {
        const float btn_size = 44.0f;
        const float pad      = 16.0f;
        ImGui::SetCursorScreenPos(ImVec2(window_pos.x + pad,
                                         window_pos.y + window_size.y - btn_size - pad));
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0.55f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f,0.25f,0.25f,0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.40f,0.40f,0.40f,0.95f));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1,1,1,1));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, btn_size * 0.5f);

        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 1));

        bool muted = BackgroundAudio::instance().is_muted();
        const char* label = muted ? ICON_FA_VOLUME_XMARK : ICON_FA_VOLUME_HIGH;
        if (ImGui::Button(label, ImVec2(btn_size, btn_size))) {
            BackgroundAudio::instance().toggle_mute();
            save_audio_muted(BackgroundAudio::instance().is_muted());
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", muted ? "Unmute music" : "Mute music");
        }

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(4);
    }

    ImGui::SetCursorScreenPos(window_pos);
    if (ImGui::InvisibleButton("##splash_click", window_size)) {
        open_picker();
    }

    {
        float text_alpha = 1.0f;
        if (g_splash_time_elapsed < kFadeInDelay + kFadeInDuration) {
            float fade_progress = (g_splash_time_elapsed - kFadeInDelay) / kFadeInDuration;
            text_alpha = (fade_progress > 1.0f) ? 1.0f
                       : (fade_progress < 0.0f ? 0.0f : fade_progress);
        }

        const char* hint = "Click anywhere to browse to a Fable 2 folder or .iso";
        ImVec2 text_size = ImGui::CalcTextSize(hint);
        float logo_scale = (g_logo_width > 0) ? window_size.x / (g_logo_width * 1.5f) : 1.0f;
        float scaled_height = (g_logo_height > 0 ? g_logo_height : 200) * logo_scale;
        float logo_bottom = window_pos.y + window_size.y * 0.33f + scaled_height * 0.5f;
        float text_x = window_pos.x + (window_size.x - text_size.x) * 0.5f;
        float text_y = logo_bottom - 10.0f;

        const float padding = 10.0f;
        ImVec2 backdrop_min(text_x - padding, text_y - padding);
        ImVec2 backdrop_max(text_x + text_size.x + padding, text_y + text_size.y + padding);
        draw_list->AddRectFilled(backdrop_min, backdrop_max,
                                 IM_COL32(0, 0, 0, (int)(text_alpha * 180)), 4.0f);
        draw_list->AddText(ImVec2(text_x, text_y),
                           IM_COL32(255, 255, 255, (int)(text_alpha * 255)), hint);

        if (!g_status_msg.empty()) {
            ImVec2 msg_size = ImGui::CalcTextSize(g_status_msg.c_str());
            float msg_x = window_pos.x + (window_size.x - msg_size.x) * 0.5f;
            float msg_y = text_y + text_size.y + 16.0f;
            ImVec2 mb_min(msg_x - padding, msg_y - padding);
            ImVec2 mb_max(msg_x + msg_size.x + padding, msg_y + msg_size.y + padding);
            draw_list->AddRectFilled(mb_min, mb_max, IM_COL32(50, 0, 0, 220), 4.0f);
            draw_list->AddText(ImVec2(msg_x, msg_y),
                               IM_COL32(255, 220, 220, 255), g_status_msg.c_str());
        }
    }

    draw_dialogs();
}

void release_resources() {
#ifdef _WIN32
    if (g_splash_texture) { g_splash_texture->Release(); g_splash_texture = nullptr; }
    if (g_logo_texture)   { g_logo_texture->Release();   g_logo_texture   = nullptr; }
    for (int i = 0; i < 8; ++i) {
        if (g_sparkle_textures[i]) { g_sparkle_textures[i]->Release(); g_sparkle_textures[i] = nullptr; }
    }
#else
    if (g_splash_texture) { glDeleteTextures(1, &g_splash_texture); g_splash_texture = 0; }
    if (g_logo_texture)   { glDeleteTextures(1, &g_logo_texture);   g_logo_texture   = 0; }
    for (int i = 0; i < 8; ++i) {
        if (g_sparkle_textures[i]) { glDeleteTextures(1, &g_sparkle_textures[i]); g_sparkle_textures[i] = 0; }
    }
#endif
}

}
