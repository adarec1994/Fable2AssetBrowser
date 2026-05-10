#include "IconFont.h"
#include "Splashscreen.h"
#include "imgui.h"
#include <vector>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include "../../resource.h"
#endif

namespace IconFont {

namespace {

bool g_loaded = false;
float g_loaded_size_px = 0.0f;

std::vector<unsigned char> g_roboto_bytes;
std::vector<unsigned char> g_fa_bytes;

constexpr float kDefaultFontSizePx = 17.0f;

#ifdef _WIN32
bool load_rcdata(int resource_id, std::vector<unsigned char>& out) {
    HMODULE mod = GetModuleHandleA(nullptr);
    HRSRC h = FindResourceA(mod, MAKEINTRESOURCEA(resource_id), (LPCSTR)RT_RCDATA);
    if (!h) return false;
    DWORD sz = SizeofResource(mod, h);
    if (!sz) return false;
    HGLOBAL g = LoadResource(mod, h);
    if (!g) return false;
    void* p = LockResource(g);
    if (!p) return false;
    out.assign((const unsigned char*)p, (const unsigned char*)p + sz);
    return true;
}
#endif

}

bool load_atlas_at_size(float size_px) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

#ifdef _WIN32

    if (g_roboto_bytes.empty()) {
        load_rcdata(IDR_ROBOTO_FONT, g_roboto_bytes);
    }
    if (g_fa_bytes.empty()) {
        load_rcdata(IDR_FA_FONT, g_fa_bytes);
    }

    if (g_roboto_bytes.empty()) {
        io.Fonts->AddFontDefault();
    } else {
        ImFontConfig roboto_cfg;
        roboto_cfg.FontDataOwnedByAtlas = false;
        roboto_cfg.PixelSnapH = true;
        io.Fonts->AddFontFromMemoryTTF(
            g_roboto_bytes.data(), (int)g_roboto_bytes.size(),
            size_px, &roboto_cfg, io.Fonts->GetGlyphRangesDefault());
    }

    if (!g_fa_bytes.empty()) {
        static const ImWchar icon_ranges[] = { 0xF000, 0xF8FF, 0 };
        ImFontConfig fa_cfg;
        fa_cfg.MergeMode = true;
        fa_cfg.PixelSnapH = true;
        fa_cfg.GlyphMinAdvanceX = size_px - 1.0f;
        fa_cfg.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(
            g_fa_bytes.data(), (int)g_fa_bytes.size(),
            size_px, &fa_cfg, icon_ranges);
    }
#else
    io.Fonts->AddFontDefault();
#endif

    g_loaded_size_px = size_px;
    return true;
}

bool ensure_loaded() {
    if (g_loaded) return true;
    bool ok = load_atlas_at_size(kDefaultFontSizePx);
    g_loaded = ok;
    return ok;
}

bool reload_at_size(float size_px) {
    if (size_px < 8.0f)  size_px = 8.0f;
    if (size_px > 48.0f) size_px = 48.0f;
    if (g_loaded && size_px == g_loaded_size_px) return true;
    bool ok = load_atlas_at_size(size_px);
    g_loaded = ok;
    return ok;
}

}

void Splashscreen_init_icon_font_at_startup() {
    IconFont::ensure_loaded();
}
