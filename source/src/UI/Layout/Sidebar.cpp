#include "Sidebar.h"

#include "imgui.h"

#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#include "../../../resource.h"
#include "stb_image.h"
#endif

#include <algorithm>
#include <vector>

namespace UI {

namespace {

#ifdef _WIN32
// Loaded once on first draw and kept until the process ends. We don't
// need an unload path — when the D3D device is torn down, the SRV
// pointer goes stale, but by that time we're exiting too.
ID3D11ShaderResourceView* g_logo_srv = nullptr;
int g_logo_w = 0;
int g_logo_h = 0;

bool load_rcdata(int resource_id, std::vector<unsigned char>& out) {
    HMODULE mod = GetModuleHandleA(nullptr);
    HRSRC h = FindResourceA(mod, MAKEINTRESOURCEA(resource_id), (LPCSTR)RT_RCDATA);
    if (!h) return false;
    HGLOBAL g = LoadResource(mod, h);
    if (!g) return false;
    DWORD sz = SizeofResource(mod, h);
    if (!sz) return false;
    const void* p = LockResource(g);
    if (!p) return false;
    out.assign((const unsigned char*)p, (const unsigned char*)p + sz);
    return true;
}

bool ensure_logo_loaded(ID3D11Device* device) {
    if (g_logo_srv) return true;
    if (!device)    return false;

    std::vector<unsigned char> bytes;
    if (!load_rcdata(IDR_LOGO_PNG, bytes)) return false;

    int w, h, channels;
    unsigned char* rgba = stbi_load_from_memory(bytes.data(), (int)bytes.size(),
                                                &w, &h, &channels, 4);
    if (!rgba) return false;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = rgba;
    init.SysMemPitch = (UINT)w * 4u;

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, &init, &tex);
    stbi_image_free(rgba);
    if (FAILED(hr) || !tex) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;

    hr = device->CreateShaderResourceView(tex, &srv_desc, &g_logo_srv);
    tex->Release();
    if (FAILED(hr)) return false;

    g_logo_w = w;
    g_logo_h = h;
    return true;
}
#endif // _WIN32

} // namespace

#ifdef _WIN32
void draw_sidebar(ID3D11Device* device) {
#else
void draw_sidebar() {
#endif
    // The sidebar is rendered into a child window the caller has already
    // sized; we just paint inside it.
    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Solid panel background with a subtle right-edge border to delimit
    // the column from the rest of the layout.
    dl->AddRectFilled(origin,
                      ImVec2(origin.x + region.x, origin.y + region.y),
                      IM_COL32(22, 24, 30, 255));
    dl->AddLine(ImVec2(origin.x + region.x - 0.5f, origin.y),
                ImVec2(origin.x + region.x - 0.5f, origin.y + region.y),
                IM_COL32(50, 55, 65, 255), 1.0f);

    // ---- Logo ---------------------------------------------------------------
#ifdef _WIN32
    bool have_logo = ensure_logo_loaded(device);
    if (have_logo && g_logo_srv && g_logo_w > 0 && g_logo_h > 0) {
        const float max_logo_w = region.x - 24.0f;
        const float max_logo_h = 110.0f;
        float scale = std::min(max_logo_w / (float)g_logo_w,
                               max_logo_h / (float)g_logo_h);
        if (scale > 1.0f) scale = 1.0f;
        float dw = (float)g_logo_w * scale;
        float dh = (float)g_logo_h * scale;
        float x  = origin.x + (region.x - dw) * 0.5f;
        float y  = origin.y + 16.0f;
        dl->AddImage((ImTextureID)g_logo_srv,
                     ImVec2(x, y), ImVec2(x + dw, y + dh));
    }
#endif

    // ---- Branding text -----------------------------------------------------
    auto centered_text = [&](float y_offset, const char* s, ImU32 col, float scale = 1.0f) {
        ImVec2 sz = ImGui::CalcTextSize(s);
        sz.x *= scale; sz.y *= scale;
        ImVec2 pos(origin.x + (region.x - sz.x) * 0.5f,
                   origin.y + y_offset);
        dl->AddText(ImGui::GetFont(),
                    ImGui::GetFontSize() * scale,
                    pos, col, s);
    };

    centered_text(140.0f, "Fable 2",        IM_COL32(235, 240, 250, 255), 1.30f);
    centered_text(166.0f, "Asset Browser",  IM_COL32(170, 180, 195, 255), 1.00f);

    // Bottom-anchored version label.
    {
        const char* ver = "v0.6";
        ImVec2 sz = ImGui::CalcTextSize(ver);
        ImVec2 pos(origin.x + (region.x - sz.x) * 0.5f,
                   origin.y + region.y - sz.y - 10.0f);
        dl->AddText(pos, IM_COL32(110, 120, 135, 255), ver);
    }
}

} // namespace UI
