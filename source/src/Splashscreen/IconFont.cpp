#include "IconFont.h"
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
// ImGui consumes the TTF bytes as long as the font atlas exists, so keep
// our copy alive for the whole program lifetime.
std::vector<unsigned char> g_ttf_bytes;

#ifdef _WIN32
bool load_ttf_from_resource(std::vector<unsigned char>& out) {
    HMODULE mod = GetModuleHandleA(nullptr);
    HRSRC hRes = FindResourceA(mod, MAKEINTRESOURCEA(IDR_FA_FONT), (LPCSTR)RT_RCDATA);
    if (!hRes) return false;
    DWORD size = SizeofResource(mod, hRes);
    HGLOBAL hData = LoadResource(mod, hRes);
    if (!hData) return false;
    void* p = LockResource(hData);
    if (!p || size == 0) return false;
    out.assign((const unsigned char*)p, (const unsigned char*)p + size);
    return true;
}
#endif

} // anonymous

bool ensure_loaded() {
    if (g_loaded) return true;
    ImGuiIO& io = ImGui::GetIO();

    // Make sure the default font exists first; we'll merge FA glyphs into it.
    if (io.Fonts->Fonts.empty()) {
        io.Fonts->AddFontDefault();
    }

#ifdef _WIN32
    if (!load_ttf_from_resource(g_ttf_bytes)) return false;
#else
    return false;  // non-Windows: not wired yet
#endif

    static const ImWchar icon_ranges[] = { 0xF000, 0xF8FF, 0 };
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.PixelSnapH = true;
    cfg.GlyphMinAdvanceX = 14.0f;     // keep the icon roughly mono-spaced
    cfg.FontDataOwnedByAtlas = false; // we keep ownership in g_ttf_bytes

    ImFont* fnt = io.Fonts->AddFontFromMemoryTTF(
        g_ttf_bytes.data(), (int)g_ttf_bytes.size(),
        14.0f, &cfg, icon_ranges);
    if (!fnt) return false;

    // The atlas needs to be rebuilt before the next frame for the new
    // glyphs to be available. The DX11 backend rebuilds lazily so we
    // call Build() explicitly to prevent a stale atlas this frame.
    io.Fonts->Build();
    g_loaded = true;
    return true;
}

} // namespace IconFont
