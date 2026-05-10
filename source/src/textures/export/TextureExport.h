#pragma once
// Texture export — write a decoded RGBA8 buffer to disk in one of the
// four supported formats. Each format lives in its own .cpp file:
//
//   PngExport.cpp  — stb_image_write (lossless 8-bit RGBA)
//   JpgExport.cpp  — stb_image_write (lossy, alpha dropped)
//   TiffExport.cpp — hand-rolled baseline uncompressed RGBA8 TIFF
//   DdsExport.cpp  — hand-rolled uncompressed 32-bpp ARGB DDS
//
// The high-level "trigger an export from a UI menu" workflow is also
// here — it stashes the source bytes (either an already-decoded RGBA
// buffer, or a raw .tex blob with mip index for deferred decode), then
// runs an ImGuiFileDialog to let the user pick a save path. Call
// tex_export_drive() once per frame from the main loop to drive the
// dialog. tex_export_menu_*() renders the "Export to" submenu when
// called inside a popup.

#include <string>
#include <vector>
#include <cstdint>

enum class TexExportFormat {
    PNG,
    JPG,
    TIFF,
    DDS,
    // Raw .tex — dumps the BNK-extracted bytes verbatim, no decoding.
    // Useful for round-tripping or feeding into external Fable 2
    // tooling. Only available when the source is a blob/named flavour
    // (the rgba flavour has no original bytes to write back).
    TEX,
};

// Suggested file extension (with leading dot) for `fmt`.
const char* tex_export_extension(TexExportFormat fmt);

// Format-specific writers. Take an RGBA8 buffer (4 bytes/pixel,
// top-down rows) and a destination path. Returns true on success.
bool tex_export_png (const std::string& path, const uint8_t* rgba, int w, int h);
bool tex_export_jpg (const std::string& path, const uint8_t* rgba, int w, int h);
bool tex_export_tiff(const std::string& path, const uint8_t* rgba, int w, int h);
bool tex_export_dds (const std::string& path, const uint8_t* rgba, int w, int h);

// Format dispatcher.
bool tex_export_rgba(const std::string& path, TexExportFormat fmt,
                     const uint8_t* rgba, int w, int h);

// ---- High-level menu workflow ---------------------------------------------
// Both flavours stash the request and open an ImGuiFileDialog on the
// next call to tex_export_drive(). The dialog's default filename is
// `<base_name><.ext>`. Only one export can be in flight at a time —
// calling either while a dialog is already open replaces the request.

// "Already decoded" path — the caller has the RGBA buffer in hand
// (texture preview, popout window).
void tex_export_begin_rgba(TexExportFormat fmt,
                           const std::string& base_name,
                           std::vector<uint8_t> rgba, int w, int h);

// "Decode-on-confirm" path — the caller has the raw .tex blob and a
// mip index. The export decodes that mip only after the user picks a
// path, so we don't pay for a decode the user never confirms.
void tex_export_begin_blob(TexExportFormat fmt,
                           const std::string& base_name,
                           std::vector<unsigned char> blob,
                           int mip_index);

// "Lookup-on-confirm" path — the caller knows the texture's asset name
// (e.g. material thumbnails) but not the blob. After the user picks a
// path, build_any_tex_buffer_for_name() is called to fetch the blob
// and the chosen mip is decoded. Same idea as begin_blob, just resolves
// the bytes lazily through the BNK pipeline.
void tex_export_begin_named(TexExportFormat fmt,
                            const std::string& tex_name,
                            const std::string& preferred_bnk,
                            int mip_index);

// Drive the file dialog once per frame. Writes the file on user
// confirmation, no-ops when no export is pending.
void tex_export_drive();

// "Export to" submenu — call inside an existing context-menu popup.
// Renders BeginMenu("Export to") with PNG/JPG/TIFF/DDS items and wires
// each item to the corresponding tex_export_begin_*. The `_rgba`
// flavour expects pre-decoded bytes; `_blob` defers decode; `_named`
// defers both fetch and decode.
void tex_export_menu_rgba (const std::string& base_name,
                           const std::vector<uint8_t>& rgba, int w, int h);
void tex_export_menu_blob (const std::string& base_name,
                           const std::vector<unsigned char>& blob,
                           int mip_index);
void tex_export_menu_named(const std::string& base_name,
                           const std::string& tex_name,
                           const std::string& preferred_bnk,
                           int mip_index);
