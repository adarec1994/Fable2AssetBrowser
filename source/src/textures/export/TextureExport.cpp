// Texture-export dispatcher + UI workflow.
//
// Two responsibilities:
//   1. Dispatch tex_export_rgba() to the right format-specific writer.
//   2. Drive the user-facing menu workflow: stash a request, open an
//      ImGuiFileDialog, decode lazily on confirm, write the file.
//
// Menu helpers live here too — tex_export_menu_rgba/blob/named() each
// render the four "PNG / JPG / TIFF / DDS" items inside an existing
// context-menu popup. They write through to tex_export_begin_*().
//
// Only one export request is alive at a time. A second begin_*() call
// while a dialog is already open replaces the request — by design, so
// the user can re-pick a different format from a different menu without
// having to close the dialog first.

#include "TextureExport.h"

#include "../TexParser.h"
#include "../../UI/ModelPreview.h"   // decode_tex_to_rgba
#include "../../BNKCore.cpp"          // build_any_tex_buffer_for_name

#include "imgui.h"
#include "ImGuiFileDialog.h"

#include <filesystem>
#include <fstream>
#include <utility>

// ---------------------------------------------------------------------------

const char* tex_export_extension(TexExportFormat fmt) {
    switch (fmt) {
        case TexExportFormat::PNG:  return ".png";
        case TexExportFormat::JPG:  return ".jpg";
        case TexExportFormat::TIFF: return ".tif";
        case TexExportFormat::DDS:  return ".dds";
        case TexExportFormat::TEX:  return ".tex";
    }
    return ".bin";
}

// ImGuiFileDialog filter strings — single-extension save dialogs.
static const char* tex_export_filter(TexExportFormat fmt) {
    switch (fmt) {
        case TexExportFormat::PNG:  return "PNG (*.png){.png}";
        case TexExportFormat::JPG:  return "JPEG (*.jpg){.jpg,.jpeg}";
        case TexExportFormat::TIFF: return "TIFF (*.tif){.tif,.tiff}";
        case TexExportFormat::DDS:  return "DDS (*.dds){.dds}";
        case TexExportFormat::TEX:  return "Raw .tex (*.tex){.tex}";
    }
    return "All Files (*.*){.*}";
}

// Write a raw blob verbatim — no decoding, no header munging. Used by
// the TEX format. Lives here (not in its own file) because it's a one-
// liner and doesn't share any infrastructure with the image writers.
static bool write_raw_blob(const std::string& path,
                           const unsigned char* data, size_t size) {
    if (!data || size == 0) return false;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data), (std::streamsize)size);
    return f.good();
}

bool tex_export_rgba(const std::string& path, TexExportFormat fmt,
                     const uint8_t* rgba, int w, int h) {
    switch (fmt) {
        case TexExportFormat::PNG:  return tex_export_png (path, rgba, w, h);
        case TexExportFormat::JPG:  return tex_export_jpg (path, rgba, w, h);
        case TexExportFormat::TIFF: return tex_export_tiff(path, rgba, w, h);
        case TexExportFormat::DDS:  return tex_export_dds (path, rgba, w, h);
        case TexExportFormat::TEX:
            // RGBA can't reconstruct the original .tex bytes — only the
            // blob/named code paths can satisfy a TEX export.
            return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Pending-request state. Three "modes" describe how to source the
// bytes when the user confirms the dialog:
//   RGBA   — bytes are already in `rgba`, just write
//   BLOB   — bytes need decoding from `blob` at `mip_index`
//   NAMED  — bytes need fetching by `tex_name` (BNK pipeline) THEN
//            decoding at `mip_index`
// ---------------------------------------------------------------------------

namespace {

enum class SourceMode { None, Rgba, Blob, Named };

struct PendingExport {
    SourceMode      mode = SourceMode::None;
    TexExportFormat fmt  = TexExportFormat::PNG;
    std::string     base_name;          // suggested filename (no extension)
    // Decoded bytes (mode = Rgba)
    std::vector<uint8_t>      rgba;
    int                       w = 0, h = 0;
    // Raw blob (mode = Blob / Named)
    std::vector<unsigned char> blob;
    std::string                tex_name;
    std::string                preferred_bnk;
    int                        mip_index = 0;
};

PendingExport g_pending;

// ImGuiFileDialog needs a key to identify the dialog instance — we use
// one shared key, since only one export can be in flight at a time.
constexpr const char* kDialogKey = "##tex_export_dialog";

// Small helper — set up the dialog from the current g_pending state.
void open_dialog_from_pending() {
    if (g_pending.mode == SourceMode::None) return;
    const std::string default_name =
        g_pending.base_name + tex_export_extension(g_pending.fmt);

    IGFD::FileDialogConfig cfg;
    cfg.path     = ".";
    cfg.fileName = default_name;
    cfg.flags    = ImGuiFileDialogFlags_ConfirmOverwrite;

    ImGuiFileDialog::Instance()->OpenDialog(
        kDialogKey,
        std::string("Export texture (") + tex_export_extension(g_pending.fmt) + ")",
        tex_export_filter(g_pending.fmt),
        cfg);
}

// Resolve the pending request to an RGBA buffer. Returns true on success.
bool resolve_pending_to_rgba(std::vector<uint8_t>& out_rgba,
                             int& out_w, int& out_h) {
    switch (g_pending.mode) {
        case SourceMode::Rgba:
            out_rgba = g_pending.rgba;
            out_w = g_pending.w;
            out_h = g_pending.h;
            return !out_rgba.empty() && out_w > 0 && out_h > 0;
        case SourceMode::Blob: {
            int w = 0, h = 0;
            bool has_alpha = false;
            if (!decode_tex_to_rgba(g_pending.blob, out_rgba, w, h,
                                    &has_alpha, g_pending.mip_index))
                return false;
            out_w = w;
            out_h = h;
            return true;
        }
        case SourceMode::Named: {
            std::vector<unsigned char> buf;
            if (!build_any_tex_buffer_for_name(
                    g_pending.tex_name, buf, g_pending.preferred_bnk))
                return false;
            int w = 0, h = 0;
            bool has_alpha = false;
            if (!decode_tex_to_rgba(buf, out_rgba, w, h, &has_alpha,
                                    g_pending.mip_index))
                return false;
            out_w = w;
            out_h = h;
            return true;
        }
        case SourceMode::None: return false;
    }
    return false;
}

// Pull a "base name" out of a path / asset name — strip any directory
// prefix and the extension. Used as the default filename in the dialog.
std::string make_base_name(const std::string& any_name) {
    std::filesystem::path p(any_name);
    std::string stem = p.stem().string();
    return stem.empty() ? any_name : stem;
}

} // anonymous

// ---------------------------------------------------------------------------

void tex_export_begin_rgba(TexExportFormat fmt,
                           const std::string& base_name,
                           std::vector<uint8_t> rgba, int w, int h) {
    g_pending = {};
    g_pending.mode      = SourceMode::Rgba;
    g_pending.fmt       = fmt;
    g_pending.base_name = make_base_name(base_name);
    g_pending.rgba      = std::move(rgba);
    g_pending.w         = w;
    g_pending.h         = h;
    open_dialog_from_pending();
}

void tex_export_begin_blob(TexExportFormat fmt,
                           const std::string& base_name,
                           std::vector<unsigned char> blob,
                           int mip_index) {
    g_pending = {};
    g_pending.mode      = SourceMode::Blob;
    g_pending.fmt       = fmt;
    g_pending.base_name = make_base_name(base_name);
    g_pending.blob      = std::move(blob);
    g_pending.mip_index = mip_index;
    open_dialog_from_pending();
}

void tex_export_begin_named(TexExportFormat fmt,
                            const std::string& tex_name,
                            const std::string& preferred_bnk,
                            int mip_index) {
    g_pending = {};
    g_pending.mode          = SourceMode::Named;
    g_pending.fmt           = fmt;
    g_pending.base_name     = make_base_name(tex_name);
    g_pending.tex_name      = tex_name;
    g_pending.preferred_bnk = preferred_bnk;
    g_pending.mip_index     = mip_index;
    open_dialog_from_pending();
}

void tex_export_drive() {
    if (g_pending.mode == SourceMode::None) return;

    // Wide minimum size so the file picker is usable. Same scaling the
    // folder picker uses elsewhere.
    ImVec2 min_size(720.0f, 420.0f);
    ImVec2 max_size(FLT_MAX, FLT_MAX);

    if (ImGuiFileDialog::Instance()->Display(kDialogKey,
                                             ImGuiWindowFlags_NoCollapse,
                                             min_size, max_size)) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string path = ImGuiFileDialog::Instance()->GetFilePathName();
            // The TEX format short-circuits the RGBA decode pipeline —
            // it just dumps the source blob bytes. Pull them straight
            // out of the request based on its source mode.
            if (g_pending.fmt == TexExportFormat::TEX) {
                std::vector<unsigned char> blob;
                if (g_pending.mode == SourceMode::Blob) {
                    blob = std::move(g_pending.blob);
                } else if (g_pending.mode == SourceMode::Named) {
                    build_any_tex_buffer_for_name(
                        g_pending.tex_name, blob, g_pending.preferred_bnk);
                }
                if (!blob.empty()) {
                    write_raw_blob(path, blob.data(), blob.size());
                }
            } else {
                std::vector<uint8_t> rgba;
                int w = 0, h = 0;
                if (resolve_pending_to_rgba(rgba, w, h)) {
                    tex_export_rgba(path, g_pending.fmt, rgba.data(), w, h);
                }
            }
        }
        ImGuiFileDialog::Instance()->Close();
        g_pending = {};
    }
}

// ---------------------------------------------------------------------------
// "Export to" submenu helpers — caller is inside an open popup.
// Each item dispatches to the matching tex_export_begin_*() flavour.
// ---------------------------------------------------------------------------

namespace {

// Render the image-format items (PNG / JPG / TIFF / DDS). The TEX
// (raw) item is rendered separately by callers that have access to the
// source blob — see render_raw_tex_item().
template <typename Begin>
void render_export_items(const Begin& begin) {
    if (ImGui::MenuItem("PNG"))  begin(TexExportFormat::PNG);
    if (ImGui::MenuItem("JPG"))  begin(TexExportFormat::JPG);
    if (ImGui::MenuItem("TIFF")) begin(TexExportFormat::TIFF);
    if (ImGui::MenuItem("DDS"))  begin(TexExportFormat::DDS);
}

// Renders a single ".tex (raw)" entry — for use only when the source
// has the original blob bytes available (blob/named flavours). Adds a
// separator above so it visually splits "decoded image formats" from
// "raw extract".
template <typename BeginRaw>
void render_raw_tex_item(const BeginRaw& begin_raw) {
    ImGui::Separator();
    if (ImGui::MenuItem(".tex (raw)")) begin_raw();
}

} // anonymous

void tex_export_menu_rgba(const std::string& base_name,
                          const std::vector<uint8_t>& rgba, int w, int h) {
    if (rgba.empty() || w <= 0 || h <= 0) return;
    if (ImGui::BeginMenu("Export to")) {
        render_export_items([&](TexExportFormat fmt) {
            tex_export_begin_rgba(fmt, base_name,
                                  std::vector<uint8_t>(rgba), w, h);
        });
        // No raw .tex option — the rgba flavour has no original bytes.
        ImGui::EndMenu();
    }
}

void tex_export_menu_blob(const std::string& base_name,
                          const std::vector<unsigned char>& blob,
                          int mip_index) {
    if (blob.empty()) return;
    if (ImGui::BeginMenu("Export to")) {
        render_export_items([&](TexExportFormat fmt) {
            tex_export_begin_blob(fmt, base_name,
                                  std::vector<unsigned char>(blob),
                                  mip_index);
        });
        render_raw_tex_item([&]() {
            tex_export_begin_blob(TexExportFormat::TEX, base_name,
                                  std::vector<unsigned char>(blob),
                                  mip_index);
        });
        ImGui::EndMenu();
    }
}

void tex_export_menu_named(const std::string& base_name,
                           const std::string& tex_name,
                           const std::string& preferred_bnk,
                           int mip_index) {
    if (tex_name.empty()) return;
    if (ImGui::BeginMenu("Export to")) {
        render_export_items([&](TexExportFormat fmt) {
            tex_export_begin_named(fmt, tex_name, preferred_bnk, mip_index);
        });
        render_raw_tex_item([&]() {
            tex_export_begin_named(TexExportFormat::TEX, tex_name,
                                   preferred_bnk, mip_index);
        });
        ImGui::EndMenu();
    }
}
