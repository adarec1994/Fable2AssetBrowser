// Texture-export dispatcher + UI workflow.
//
// Two responsibilities:
//   1. Dispatch tex_export_rgba() to the right format-specific writer.
//   2. Provide the user-facing menu workflow: "Export to" submenu items
//      that immediately write the result to disk under S.export_dir,
//      preserving the asset's relative path. Status is reported through
//      OutputLog (success / error toasts in the bottom slide-up panel).
//
// File-dialog UX is intentionally gone — every export goes to
//   ${S.export_dir}/${asset_relative_path}.${ext}
// with parent directories created on demand. The user changes the
// export root from the Settings dropdown.

#include "TextureExport.h"

#include "../TexParser.h"
#include "../../UI/ModelPreview.h"   // decode_tex_to_rgba
#include "../../UI/OutputLog.h"
#include "../../Utilities/State.h"
#include "../../BNKCore.cpp"          // build_any_tex_buffer_for_name

#include "imgui.h"

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
// Path helpers
// ---------------------------------------------------------------------------

namespace {

// Build the on-disk export path for an asset.
//   asset_path       — relative asset path (e.g. "props/foo/bar.tex").
//                      May or may not include the original extension.
//   fmt              — output format; its extension replaces any
//                      existing one on `asset_path`.
// Returns ${S.export_dir} / sanitized(asset_path with replaced ext).
std::filesystem::path build_export_path(const std::string& asset_path,
                                        TexExportFormat fmt) {
    std::filesystem::path root = S.export_dir.empty() ? "." : S.export_dir;

    // Normalise: drop leading slashes, swap backslashes so we don't
    // accidentally root the path on Windows ("/foo" → "foo").
    std::string rel = asset_path;
    while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\'))
        rel.erase(rel.begin());

    std::filesystem::path p(rel);
    // If the asset already has any extension, drop it — we're writing
    // a different format. replace_extension("") on a directoryless stem
    // works the same way.
    p.replace_extension();

    std::filesystem::path out = root / p;
    out += tex_export_extension(fmt);
    return out;
}

// Make sure the parent directory of `p` exists. Creates the whole
// chain if necessary. Errors here become export errors at the call
// site (the subsequent ofstream open will fail).
bool ensure_parent_dir(const std::filesystem::path& p) {
    auto parent = p.parent_path();
    if (parent.empty()) return true;
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    return !ec;
}

// Fire the right OutputLog level + a single human-readable line. We
// keep the asset name in the message (rather than the full path) so the
// log stays scannable; the full path is appended in dev mode for
// debugging.
void log_export_result(bool ok, const std::string& asset_label,
                       const std::filesystem::path& out_path,
                       TexExportFormat fmt) {
    const char* fmt_name =
        fmt == TexExportFormat::PNG  ? "PNG"  :
        fmt == TexExportFormat::JPG  ? "JPG"  :
        fmt == TexExportFormat::TIFF ? "TIFF" :
        fmt == TexExportFormat::DDS  ? "DDS"  :
        fmt == TexExportFormat::TEX  ? "TEX (raw)" : "?";
    if (ok) {
        OutputLog::success(std::string("Exported ") + asset_label +
                           " as " + fmt_name + " → " + out_path.string());
    } else {
        OutputLog::error(std::string("Failed to export ") + asset_label +
                         " as " + fmt_name +
                         " (target: " + out_path.string() + ")");
    }
}

} // anonymous

// ---------------------------------------------------------------------------
// Begin-* entry points: synchronous now. They resolve bytes, build the
// path, mkdir, write, and emit a log line. No deferred state.
// ---------------------------------------------------------------------------

void tex_export_begin_rgba(TexExportFormat fmt,
                           const std::string& base_name,
                           std::vector<uint8_t> rgba, int w, int h) {
    std::filesystem::path out = build_export_path(base_name, fmt);
    bool ok = false;
    if (fmt == TexExportFormat::TEX) {
        // No original blob to dump — RGBA path can't satisfy a raw .tex
        // request. Caller shouldn't have offered it; bail loudly.
        ok = false;
    } else if (!rgba.empty() && w > 0 && h > 0 && ensure_parent_dir(out)) {
        ok = tex_export_rgba(out.string(), fmt, rgba.data(), w, h);
    }
    log_export_result(ok, std::filesystem::path(base_name).filename().string(),
                      out, fmt);
}

void tex_export_begin_blob(TexExportFormat fmt,
                           const std::string& base_name,
                           std::vector<unsigned char> blob,
                           int mip_index) {
    std::filesystem::path out = build_export_path(base_name, fmt);
    bool ok = false;
    if (!ensure_parent_dir(out)) {
        log_export_result(false,
                          std::filesystem::path(base_name).filename().string(),
                          out, fmt);
        return;
    }
    if (fmt == TexExportFormat::TEX) {
        ok = write_raw_blob(out.string(), blob.data(), blob.size());
    } else {
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        bool has_alpha = false;
        if (decode_tex_to_rgba(blob, rgba, w, h, &has_alpha, mip_index) &&
            !rgba.empty() && w > 0 && h > 0) {
            ok = tex_export_rgba(out.string(), fmt, rgba.data(), w, h);
        }
    }
    log_export_result(ok, std::filesystem::path(base_name).filename().string(),
                      out, fmt);
}

void tex_export_begin_named(TexExportFormat fmt,
                            const std::string& tex_name,
                            const std::string& preferred_bnk,
                            int mip_index) {
    std::filesystem::path out = build_export_path(tex_name, fmt);
    bool ok = false;
    if (!ensure_parent_dir(out)) {
        log_export_result(false,
                          std::filesystem::path(tex_name).filename().string(),
                          out, fmt);
        return;
    }
    std::vector<unsigned char> blob;
    if (build_any_tex_buffer_for_name(tex_name, blob, preferred_bnk) &&
        !blob.empty()) {
        if (fmt == TexExportFormat::TEX) {
            ok = write_raw_blob(out.string(), blob.data(), blob.size());
        } else {
            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            bool has_alpha = false;
            if (decode_tex_to_rgba(blob, rgba, w, h, &has_alpha, mip_index) &&
                !rgba.empty() && w > 0 && h > 0) {
                ok = tex_export_rgba(out.string(), fmt, rgba.data(), w, h);
            }
        }
    }
    log_export_result(ok, std::filesystem::path(tex_name).filename().string(),
                      out, fmt);
}

// Stub kept so the main loop's "drive once per frame" call still
// compiles. Exports are synchronous now — there's nothing to drive.
// Left in place because removing it would ripple into UI_Main.cpp; if
// we ever want to revive deferred / threaded export this is where it
// would resume from.
void tex_export_drive() {
    // intentionally empty
}

// ---------------------------------------------------------------------------
// "Export to" submenu helpers — caller is inside an open popup.
// Each item dispatches to the matching tex_export_begin_*() flavour.
// ---------------------------------------------------------------------------

namespace {

template <typename Begin>
void render_export_items(const Begin& begin) {
    if (ImGui::MenuItem("PNG"))  begin(TexExportFormat::PNG);
    if (ImGui::MenuItem("JPG"))  begin(TexExportFormat::JPG);
    if (ImGui::MenuItem("TIFF")) begin(TexExportFormat::TIFF);
    if (ImGui::MenuItem("DDS"))  begin(TexExportFormat::DDS);
}

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
