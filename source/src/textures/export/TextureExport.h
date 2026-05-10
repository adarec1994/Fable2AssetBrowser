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
// Each begin_*() runs synchronously: it resolves the bytes (decode or
// BNK lookup as needed), composes the destination path under
// S.export_dir using the asset's relative path, mkdirs the parent
// chain, writes the file, and emits a single OutputLog line on the
// way out. No file dialog — the user changes the export root from the
// Settings dropdown.

// "Already decoded" path — the caller has the RGBA buffer in hand
// (texture preview, popout window). `base_name` should be the asset's
// relative path (e.g. "props/foo/bar.tex"); the original extension is
// stripped and replaced with the format extension.
void tex_export_begin_rgba(TexExportFormat fmt,
                           const std::string& base_name,
                           std::vector<uint8_t> rgba, int w, int h);

// Raw-blob path — the caller has the raw .tex blob and the mip to
// decode. PNG/JPG/TIFF/DDS run through decode_tex_to_rgba; TEX dumps
// the blob verbatim.
void tex_export_begin_blob(TexExportFormat fmt,
                           const std::string& base_name,
                           std::vector<unsigned char> blob,
                           int mip_index);

// Named-asset path — caller knows only the asset name (e.g. material
// thumbnails). build_any_tex_buffer_for_name() fetches the blob,
// then decode/raw-write proceeds as in begin_blob.
void tex_export_begin_named(TexExportFormat fmt,
                            const std::string& tex_name,
                            const std::string& preferred_bnk,
                            int mip_index);

// Per-frame export driver. Synchronous exports leave nothing to drive
// — this is a no-op stub kept for the main loop wiring and as the
// re-entry point for any future deferred / threaded export work.
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
