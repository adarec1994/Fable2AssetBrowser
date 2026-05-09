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

// ImGui's font atlas keeps a pointer to the TTF byte buffer for the
// lifetime of the atlas (when FontDataOwnedByAtlas is false), so we
// keep our copies alive for the whole program.
std::vector<unsigned char> g_roboto_bytes;
std::vector<unsigned char> g_fa_bytes;

// Sizes — Roboto sets the body text size; FA is rendered at the same
// size so icons line up with running text without nudges.
constexpr float kBaseFontSizePx = 15.0f;

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

} // anonymous

bool ensure_loaded() {
    if (g_loaded) return true;
    ImGuiIO& io = ImGui::GetIO();

#ifdef _WIN32
    // 1. Load Roboto as the BASE font (not merged). This becomes ImGui's
    //    default — every subsequent UI call uses Roboto unless it pushes
    //    a different font.
    if (!load_rcdata(IDR_ROBOTO_FONT, g_roboto_bytes)) {
        // Last-resort fallback so the UI still renders if the resource
        // is missing for some reason.
        if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();
    } else {
        ImFontConfig roboto_cfg;
        roboto_cfg.FontDataOwnedByAtlas = false; // we own g_roboto_bytes
        roboto_cfg.PixelSnapH = true;
        ImFont* roboto = io.Fonts->AddFontFromMemoryTTF(
            g_roboto_bytes.data(), (int)g_roboto_bytes.size(),
            kBaseFontSizePx, &roboto_cfg, io.Fonts->GetGlyphRangesDefault());
        (void)roboto;
        // First-added font becomes io.Fonts->Fonts[0] = the default.
    }

    // 2. Merge FontAwesome 6 Solid icons on top of the base font so
    //    icons in the [F000..F8FF] range render with the same size and
    //    baseline as Roboto.
    if (!load_rcdata(IDR_FA_FONT, g_fa_bytes)) {
        // Without FA the UI still renders, just without icons.
        g_loaded = true;
        return true;
    }

    static const ImWchar icon_ranges[] = { 0xF000, 0xF8FF, 0 };
    ImFontConfig fa_cfg;
    fa_cfg.MergeMode = true;
    fa_cfg.PixelSnapH = true;
    fa_cfg.GlyphMinAdvanceX = 14.0f;       // keep icons roughly mono-spaced
    fa_cfg.FontDataOwnedByAtlas = false;   // we own g_fa_bytes
    ImFont* fa = io.Fonts->AddFontFromMemoryTTF(
        g_fa_bytes.data(), (int)g_fa_bytes.size(),
        kBaseFontSizePx, &fa_cfg, icon_ranges);
    if (!fa) return false;
#else
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();
#endif

    // The atlas rebuild happens automatically on the next ImGui frame's
    // first font-using draw. Don't force Build() here — if we're inside
    // a frame the atlas is already in flight and rebuilding crashes the
    // renderer backend.
    g_loaded = true;
    return true;
}

} // namespace IconFont

// Public entry called from main.cpp at startup, before NewFrame.
void Splashscreen_init_icon_font_at_startup() {
    IconFont::ensure_loaded();
}
