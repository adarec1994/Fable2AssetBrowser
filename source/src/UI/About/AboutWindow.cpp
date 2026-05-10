// About window — fixed-size popup, image-on-left / text-on-right
// layout. See AboutWindow.h for the contributor-facing contract.
//
// Everything you'd want to change about the visible content lives in
// the kAbout* constants near the top of this file:
//   - kAboutTitle           — text in the title bar
//   - kAboutDescription     — wrapped paragraph under the title
//   - kAboutEntries[]       — the key/value rows (version, authors, …)
//   - kAboutWindowWidth/Height — fixed window size in pixels
//   - kAboutImagePadding    — breathing room around the left-column image
//
// To change the image: drop a new PNG at include/image/about_image.png
// and rebuild. The .rc file ships it into the exe as RCDATA so we
// don't depend on a sibling file at runtime.

#define IMGUI_DEFINE_MATH_OPERATORS
#include "AboutWindow.h"

#include "imgui.h"

#ifdef _WIN32
#include <d3d11.h>
#include <windows.h>
#include <shellapi.h>
#include "../../../resource.h"
#endif

#include "stb_image.h"

#include <algorithm>
#include <cstring>
#include <vector>
#include <string>

namespace About {

// ---------------------------------------------------------------------------
// Editable content — change these, rebuild, and the dialog updates.
// Keep entries short; the right column is sized for two-line max values.
// ---------------------------------------------------------------------------

constexpr const char* kAboutTitle =
    "About Fable 2 Asset Browser";

constexpr const char* kAboutDescription =
    "Fable 2 Asset Browser is a tool for inspecting, previewing, and "
    "extracting assets from Fable II's data files. It supports both "
    "extracted folder layouts and mounted Xbox 360 ISO images, and "
    "includes decoders for the game's models, textures, audio, and "
    "Lua scripts. The project is open-source and under active "
    "development.";

struct AboutEntry {
    const char* label;   // bold left column (e.g. "Version:")
    const char* value;   // right column
    bool        is_url;  // render in link-blue + open in browser on click
};

// Order of these rows is the order they appear in the right-bottom block.
constexpr AboutEntry kAboutEntries[] = {
    { "Version:",       "1.0.0",                                false },
    { "Authors:",       "Matthew W",                             false },
    { "Special Thanks:", "H3x3r and the Fable modding discord",         false },
    { "Source Code:",
      "https://github.com/weak1337/Fable2AssetBrowser",         true  },
};

// The window itself auto-sizes (AlwaysAutoResize) so the user can't
// drag it bigger / smaller and content never clips. These bounds
// shape the two side-by-side columns:
//   - Image is aspect-fit into (kImageMaxWidth × kImageMaxHeight).
//     Portrait images come out narrow + tall, landscape ones wide +
//     short, but neither blows up the dialog.
//   - Right column width is measured each frame from the actual
//     key/value strings so the longest value (typically the source-
//     code URL) always has room — no need to bump a magic number
//     when an entry's text changes.
//   - kRightColumnMin keeps the description paragraph from going
//     pencil-thin if the entries happen to be very short.
constexpr float kImageMaxWidth   = 240.0f;
constexpr float kImageMaxHeight  = 360.0f;
constexpr float kRightColumnMin  = 380.0f;
constexpr float kAboutColumnGap  = 14.0f;

// ---------------------------------------------------------------------------
// Internal state — open flag + cached GPU image.
// ---------------------------------------------------------------------------

namespace {

bool g_open = false;

#ifdef _WIN32
ID3D11ShaderResourceView* g_image_srv = nullptr;
#endif
int  g_image_w = 0;
int  g_image_h = 0;

#ifdef _WIN32
// Pull a Windows RCDATA resource into a byte buffer. Same shape the
// splash uses; duplicated here so the About module can stand alone
// without depending on Splash internals.
bool load_resource_bytes(int resource_id, std::vector<unsigned char>& out) {
    HMODULE mod = GetModuleHandleA(nullptr);
    HRSRC hRes = FindResourceA(mod, MAKEINTRESOURCEA(resource_id),
                               (LPCSTR)RT_RCDATA);
    if (!hRes) return false;
    DWORD size = SizeofResource(mod, hRes);
    HGLOBAL hData = LoadResource(mod, hRes);
    if (!hData) return false;
    void* p = LockResource(hData);
    if (!p || size == 0) return false;
    out.assign((const unsigned char*)p,
               (const unsigned char*)p + size);
    return true;
}

bool create_d3d_texture(ID3D11Device* device, const unsigned char* rgba,
                       int w, int h,
                       ID3D11ShaderResourceView** out_srv) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w; desc.Height = h;
    desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* tex = nullptr;
    D3D11_SUBRESOURCE_DATA sub{};
    sub.pSysMem = rgba; sub.SysMemPitch = (UINT)(w * 4);
    if (FAILED(device->CreateTexture2D(&desc, &sub, &tex)) || !tex)
        return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = 1;
    HRESULT hr = device->CreateShaderResourceView(tex, &sv, out_srv);
    tex->Release();
    return SUCCEEDED(hr);
}

void load_image_if_needed(ID3D11Device* device) {
    if (g_image_srv || !device) return;
    std::vector<unsigned char> bytes;
    if (!load_resource_bytes(IDR_ABOUT_IMAGE, bytes)) return;
    int w = 0, h = 0, channels = 0;
    unsigned char* rgba = stbi_load_from_memory(
        bytes.data(), (int)bytes.size(), &w, &h, &channels, 4);
    if (!rgba) return;
    if (create_d3d_texture(device, rgba, w, h, &g_image_srv)) {
        g_image_w = w;
        g_image_h = h;
    }
    stbi_image_free(rgba);
}

void open_url(const char* url) {
    // ShellExecute with "open" picks the user's default browser. We
    // pass nullptr for hwnd because the call is fire-and-forget; the
    // browser launches on its own message pump.
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
}
#else
void load_image_if_needed() { /* image preview only on Win32 for now */ }
void open_url(const char* /*url*/) {}
#endif

// Compute the (size, offset) for the image inside `avail` while
// preserving aspect ratio and centering. If the image hasn't loaded
// yet (or the loader bailed), returns a zero-size rect — the caller
// just shows the placeholder.
ImVec2 fit_image_size(ImVec2 avail) {
    if (g_image_w <= 0 || g_image_h <= 0) return ImVec2(0, 0);
    float aw = avail.x;
    float ah = avail.y;
    float ar_img  = (float)g_image_w / (float)g_image_h;
    float ar_box  = aw / ah;
    if (ar_img > ar_box) {
        // Image is wider than the box — limit by width.
        return ImVec2(aw, aw / ar_img);
    } else {
        // Image is taller — limit by height.
        return ImVec2(ah * ar_img, ah);
    }
}

} // anonymous

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void open() {
    g_open = true;
}

#ifdef _WIN32
void draw(ID3D11Device* device) {
#else
void draw() {
#endif
    if (!g_open) return;

#ifdef _WIN32
    load_image_if_needed(device);
#else
    load_image_if_needed();
#endif

    // Center the dialog on first appearance. The window's actual
    // size is decided by AlwaysAutoResize below — we only set the
    // anchor point here so it appears in the middle of the host
    // window the first time the user opens it.
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
               vp->WorkPos.y + vp->WorkSize.y * 0.5f),
        ImGuiCond_Appearing,
        ImVec2(0.5f, 0.5f));

    // AlwaysAutoResize sizes the window to fit its contents on every
    // frame and removes the resize grip — so the user can't drag it
    // bigger or smaller, and nothing can clip. NoResize would also
    // remove the grip, but it doesn't shrink-to-fit, leaving the
    // empty bottom margin we saw before.
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoCollapse       |
        ImGuiWindowFlags_NoSavedSettings;

    if (!ImGui::Begin(kAboutTitle, &g_open, kFlags)) {
        ImGui::End();
        return;
    }

    // ---- Left column: image (aspect-fit, top-aligned) ----
    // BeginGroup grows naturally to fit whatever we draw inside it,
    // unlike BeginChild which needs an explicit size up front. That's
    // exactly what AlwaysAutoResize wants — the window measures
    // group sizes, picks the taller of the two side-by-side groups
    // for its height, sums their widths for its width.
    ImGui::BeginGroup();

#ifdef _WIN32
    if (g_image_srv) {
        ImVec2 fit = fit_image_size(ImVec2(kImageMaxWidth, kImageMaxHeight));
        if (fit.x > 0 && fit.y > 0) {
            ImGui::Image((ImTextureID)(intptr_t)g_image_srv, fit);
        }
    } else {
        // No image loaded — reserve the column slot with a Dummy so
        // the right column still aligns to a predictable left edge.
        ImGui::Dummy(ImVec2(kImageMaxWidth, kImageMaxHeight));
        ImGui::TextDisabled("(image unavailable)");
    }
#else
    ImGui::Dummy(ImVec2(kImageMaxWidth, kImageMaxHeight));
    ImGui::TextDisabled("(image preview is Win32-only)");
#endif

    ImGui::EndGroup();

    ImGui::SameLine(0.0f, kAboutColumnGap);

    // ---- Right column: description + key/value table ----
    ImGui::BeginGroup();

    // Pre-measure both columns from the entry strings so the right
    // group's width is known before we lay anything out. This is the
    // fix for the URL truncation we saw earlier — the value column
    // gets exactly the width its longest entry needs.
    float label_w = 0.0f;
    float value_w = 0.0f;
    for (const auto& e : kAboutEntries) {
        label_w = (std::max)(label_w, ImGui::CalcTextSize(e.label).x);
        value_w = (std::max)(value_w, ImGui::CalcTextSize(e.value).x);
    }
    label_w += 16.0f;   // gutter between label and value
    value_w += 24.0f;   // trailing pad so the URL doesn't kiss the cell edge
    float right_w = (std::max)(label_w + value_w, kRightColumnMin);

    // Wrap the paragraph to the same width as the key/value table so
    // the description and the entries share a consistent right edge.
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + right_w);
    ImGui::TextUnformatted(kAboutDescription);
    ImGui::PopTextWrapPos();

    ImGui::Dummy(ImVec2(0, 18));

    // Key/value table — labels fixed-width to the widest label, value
    // column stretches to fill the remainder of `right_w`. The
    // explicit outer width is what makes WidthStretch land at the
    // value-column size we measured above.
    if (ImGui::BeginTable("##about_kv", 2,
                          ImGuiTableFlags_SizingFixedFit |
                          ImGuiTableFlags_NoBordersInBody,
                          ImVec2(right_w, 0.0f))) {
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed,
                                label_w);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);

        for (const auto& e : kAboutEntries) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            // Approximation of "bold" — push the brightest white so
            // the label stands out against the slightly dimmer value
            // text. ImGui's bundled Roboto Regular doesn't have a
            // bold cut shipped in the atlas, and merging a second
            // weight just for this dialog isn't worth the complexity.
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImGui::TextUnformatted(e.label);
            ImGui::PopStyleColor();

            ImGui::TableSetColumnIndex(1);
            if (e.is_url) {
                // Link-styled clickable text. Selectable gives us
                // hover + click; we paint the text in link blue and
                // pop the colour back after the call.
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(0.40f, 0.65f, 1.0f, 1.0f));
                bool clicked = ImGui::Selectable(e.value, false,
                                                 ImGuiSelectableFlags_None);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted("Open in browser");
                    ImGui::EndTooltip();
                }
                if (clicked) open_url(e.value);
            } else {
                ImGui::TextUnformatted(e.value);
            }
        }
        ImGui::EndTable();
    }

    ImGui::EndGroup();

    ImGui::End();
}

void release_resources() {
#ifdef _WIN32
    if (g_image_srv) {
        g_image_srv->Release();
        g_image_srv = nullptr;
    }
#endif
    g_image_w = 0;
    g_image_h = 0;
}

} // namespace About
