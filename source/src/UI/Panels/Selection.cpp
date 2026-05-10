#include "../UI_Panels.h"
#include "PanelInternal.h"
#include "../../Utilities/Utils.h"
#include "../../Utilities/Files.h"
#include "../../Utilities/Progress.h"
#include "../../Utilities/operations.h"
#include "../../ISO/IsoMount.h"
#include "../../BNKCore.cpp"
#include "../UI_Main.h"
#include "../AudioPlayerWindow.h"
#include "../OutputLog.h"
#include "../HexView.h"
#include "../../textures/export/TextureExport.h"
#include "../../Audio/XmaDecoder.h"      // XMA→PCM for the per-file
                                          // MP3 / AAC right-click items.
#include "../../Audio/MfAudioEncoder.h"  // PCM→MP3 / PCM→AAC.
#include "../../animations/AnimBank.h"
#include "../../animations/AnimDataFile.h"
#include "imgui.h"
#include <filesystem>
#include <algorithm>
#include <ctime>
#include <cstring>
#include <optional>

void refresh_file_table() { S.selected_file_index = -1; }

// Extract the selected file's raw bytes (going through the user's currently-
// selected BNK or nested-BNK temp copy) and open the in-app audio player on
// them. Returns true if the player accepted the data.
bool open_audio_player_for_selected(int file_index) {
    if (file_index < 0 || file_index >= (int)S.files.size()) return false;

    const auto& item = S.files[(size_t)file_index];

    // Pick the BNK we'll extract from. Same logic as the preview/hex paths.
    std::string bnk_to_use;
    if (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty()) {
        bnk_to_use = S.selected_nested_temp_path;
    } else {
        bnk_to_use = S.selected_bnk;
    }
    if (bnk_to_use.empty()) return false;

    auto tmpdir = std::filesystem::temp_directory_path() / "f2_audio_play";
    std::error_code ec;
    std::filesystem::create_directories(tmpdir, ec);
    auto tmp_file = tmpdir / ("audio_" + std::to_string(std::hash<std::string>{}(item.name + std::to_string(std::time(nullptr)))) + ".bin");

    std::vector<unsigned char> bytes;
    try {
        extract_one(bnk_to_use, item.index, tmp_file.string());
        bytes = read_all_bytes(tmp_file);
        std::filesystem::remove(tmp_file, ec);
    } catch (...) {
        std::filesystem::remove(tmp_file, ec);
        return false;
    }
    if (bytes.empty()) return false;

    return UI::open_audio_player_for(item.name, bytes);
}

void pick_bnk(const std::string &path) {
    S.selected_bnk = path;
    S.viewing_lua = false;
    S.lua_preview_content.clear();
    S.lua_preview_title.clear();
    S.lua_preview_selected = -1;
    S.selected_nested_temp_path.clear();
    S.files.clear();
    S.file_filter.clear();
    S.ext_filter.clear();
    BNKReader reader(path);
    const auto &fe = reader.list_files();
    S.files.reserve(fe.size());
    for (size_t i = 0; i < fe.size(); ++i) S.files.push_back({(int) i, fe[i].name, fe[i].uncompressed_size});

    auto get_filename = [](const std::string& p) -> std::string {
        size_t pos = p.find_last_of("/\\");
        if (pos != std::string::npos) return p.substr(pos + 1);
        return p;
    };

    std::sort(S.files.begin(), S.files.end(), [&get_filename](const BNKItemUI &a, const BNKItemUI &b) {
        std::string x = get_filename(a.name);
        std::string y = get_filename(b.name);
        std::transform(x.begin(), x.end(), x.begin(), ::tolower);
        std::transform(y.begin(), y.end(), y.begin(), ::tolower);
        return x < y;
    });

    refresh_file_table();
}

// Companion to open_folder_logic for the case where the user picked an
// ISO file. Skips the is_directory check (the path is a regular file)
// and relies on ISO::IsoMount::is_mounted() being true so the BNK-scan
// helpers route through the in-memory tree instead of the OS filesystem.
void open_iso_logic(const std::string& iso_path) {
    if (iso_path.empty()) { show_error_box("No ISO selected"); return; }
    S.root_dir = iso_path;
    S.last_dir = std::filesystem::path(iso_path).parent_path().string();
    save_last_dir(S.last_dir);
    try {
        // Same set of scans as folder mode — they all short-circuit to
        // IsoMount::list_recursive() when a disc is mounted.
        S.bnk_paths = scan_bnks_recursive(iso_path);
        if (S.bnk_paths.empty()) S.bnk_paths = find_bnks(iso_path);
        S.adb_paths = scan_adbs_recursive(iso_path);

        auto lua_paths = scan_luas_recursive(iso_path);
        S.lua_files.clear();
        S.lua_files.reserve(lua_paths.size());
        for (size_t i = 0; i < lua_paths.size(); ++i) {
            std::filesystem::path p(lua_paths[i]);
            // For ISO paths, file_size on the std::filesystem::path will
            // fail — fall back to the IsoMount entry's recorded size.
            uint32_t size = 0;
            if (ISO::IsoMount::is_iso_path(lua_paths[i])) {
                if (auto* mf = ISO::IsoMount::instance().find(
                        ISO::IsoMount::strip_iso_prefix(lua_paths[i]))) {
                    size = mf->size;
                }
            } else {
                std::error_code ec;
                auto fsize = std::filesystem::file_size(p, ec);
                size = ec ? 0 : (uint32_t)fsize;
            }
            S.lua_files.push_back({(int)i, lua_paths[i], p.filename().string(), size});
        }

        std::sort(S.lua_files.begin(), S.lua_files.end(), [](const LuaFileUI& a, const LuaFileUI& b) {
            std::string x = a.filename, y = b.filename;
            std::transform(x.begin(), x.end(), x.begin(), ::tolower);
            std::transform(y.begin(), y.end(), y.begin(), ::tolower);
            return x < y;
        });
    } catch (...) {
        show_error_box("Error indexing BNK files in the ISO");
        return;
    }
    if (S.bnk_paths.empty()) {
        show_error_box("No .bnk files found in the ISO.");
        return;
    }
    // Kick the file-tree build in the background so the tab is ready by
    // the time the user navigates to it.
    start_tree_build_for_root(iso_path, S.bnk_paths);

    // Pull the global animation TOC out of the ISO. Optional — log-only
    // failure leaves S.anim_clips empty and the Animations tab will
    // simply show "no clips loaded".
    if (Anim::load_toc_for_root(iso_path, S.anim_clips)) {
        // Pair the TOC up with the data file. The TOC's clip offsets
        // are useless without the bytes they point into. Failure
        // here is non-fatal — the clip list still browses, just
        // without any path to playback.
        Anim::global_data_file().open_for_root(iso_path);
        // Recover friendly clip names by hashing every quoted
        // string in every .lua and matching against clip key0.
        // Replaces the synthesised id_HHHHHHHH placeholders in
        // place. Cheap (~1-2s on retail data); skipping in non-
        // dev mode is unnecessary since the result is always
        // useful.
        Anim::resolve_clip_names_from_luas(S.anim_clips);
    }
}

void open_folder_logic(const std::string &sel) {
    if (sel.empty()) {
        show_error_box("No folder selected");
        return;
    }
    if (!std::filesystem::exists(sel)) {
        show_error_box(std::string("Folder does not exist: ") + sel);
        return;
    }
    if (!std::filesystem::is_directory(sel)) {
        show_error_box(std::string("Selected path is not a directory: ") + sel);
        return;
    }
    S.root_dir = sel;
    S.last_dir = sel;
    save_last_dir(sel);
    try {
        S.bnk_paths = scan_bnks_recursive(sel);
        if (S.bnk_paths.empty()) S.bnk_paths = find_bnks(sel);

        S.adb_paths = scan_adbs_recursive(sel);

        auto lua_paths = scan_luas_recursive(sel);
        S.lua_files.clear();
        S.lua_files.reserve(lua_paths.size());
        for (size_t i = 0; i < lua_paths.size(); ++i) {
            std::filesystem::path p(lua_paths[i]);
            std::error_code ec;
            auto fsize = std::filesystem::file_size(p, ec);
            uint32_t size = ec ? 0 : (uint32_t)fsize;
            S.lua_files.push_back({(int)i, lua_paths[i], p.filename().string(), size});
        }

        std::sort(S.lua_files.begin(), S.lua_files.end(), [](const LuaFileUI& a, const LuaFileUI& b) {
            std::string x = a.filename, y = b.filename;
            std::transform(x.begin(), x.end(), x.begin(), ::tolower);
            std::transform(y.begin(), y.end(), y.begin(), ::tolower);
            return x < y;
        });
    } catch (...) {
        show_error_box("Error searching for BNK files");
        return;
    }
    if (S.bnk_paths.empty()) {
        show_error_box(
            std::string("No .bnk files found in:\n") + sel + std::string(
                "\n\nPlease select a folder containing Fable 2 BNK files."));
        return;
    }

    // Kick the file-tree build in the background — see equivalent call in
    // open_iso_logic. Same idea: do the slow work right at root-selection
    // time so the tab loads instantly.
    start_tree_build_for_root(sel, S.bnk_paths);

    // Pull the global animation TOC out of the data folder. Optional —
    // log-only failure leaves S.anim_clips empty.
    if (Anim::load_toc_for_root(sel, S.anim_clips)) {
        Anim::global_data_file().open_for_root(sel);
        // Recover friendly clip names from .lua scripts (see ISO
        // path for rationale).
        Anim::resolve_clip_names_from_luas(S.anim_clips);
    }

    auto get_filename = [](const std::string& p) -> std::string {
        size_t pos = p.find_last_of("/\\");
        if (pos != std::string::npos) return p.substr(pos + 1);
        return p;
    };

    std::sort(S.bnk_paths.begin(), S.bnk_paths.end(), [&get_filename](const std::string &a, const std::string &b) {
        std::string A = get_filename(a);
        std::string B = get_filename(b);
        std::transform(A.begin(), A.end(), A.begin(), ::tolower);
        std::transform(B.begin(), B.end(), B.begin(), ::tolower);
        return A < B;
    });

    std::sort(S.adb_paths.begin(), S.adb_paths.end(), [&get_filename](const std::string &a, const std::string &b) {
        std::string A = get_filename(a);
        std::string B = get_filename(b);
        std::transform(A.begin(), A.end(), A.begin(), ::tolower);
        std::transform(B.begin(), B.end(), B.begin(), ::tolower);
        return A < B;
    });

    S.selected_bnk.clear();
    S.files.clear();
    refresh_file_table();
}

bool reconstruct_nested_mdl(const std::string& nested_bnk_path, int file_index, std::vector<unsigned char>& out) {
    try {
        BNKReader nested_reader(nested_bnk_path);
        const auto& files = nested_reader.list_files();
        if (file_index < 0 || file_index >= (int)files.size()) return false;

        std::string mdl_name = files[file_index].name;

        auto tmpdir = std::filesystem::temp_directory_path() / "f2_nested_mdl_reconstruct";
        std::error_code ec;
        std::filesystem::create_directories(tmpdir, ec);

        auto tmp_body = tmpdir / "body.bin";
        extract_one(nested_bnk_path, file_index, tmp_body.string());
        auto body_data = read_all_bytes(tmp_body);
        std::filesystem::remove(tmp_body, ec);

        if (body_data.empty()) return false;

        auto p_headers = find_bnk_by_filename("globals_model_headers.bnk");
        if (!p_headers) {
            out = body_data;
            return true;
        }

        BNKReader r_headers(*p_headers);
        const auto& header_files = r_headers.list_files();

        std::string mdl_filename = std::filesystem::path(mdl_name).filename().string();
        std::string mdl_lower = mdl_filename;
        std::transform(mdl_lower.begin(), mdl_lower.end(), mdl_lower.begin(), ::tolower);

        int header_idx = -1;
        for (size_t i = 0; i < header_files.size(); ++i) {
            std::string hname = std::filesystem::path(header_files[i].name).filename().string();
            std::string hname_lower = hname;
            std::transform(hname_lower.begin(), hname_lower.end(), hname_lower.begin(), ::tolower);
            if (hname_lower == mdl_lower) {
                header_idx = (int)i;
                break;
            }
        }

        if (header_idx == -1) {
            out = body_data;
            return true;
        }

        auto tmp_header = tmpdir / "header.bin";
        extract_one(*p_headers, header_idx, tmp_header.string());
        auto header_data = read_all_bytes(tmp_header);
        std::filesystem::remove(tmp_header, ec);

        if (header_data.empty()) {
            out = body_data;
            return true;
        }

        out.clear();
        out.reserve(header_data.size() + body_data.size());
        out.insert(out.end(), header_data.begin(), header_data.end());
        out.insert(out.end(), body_data.begin(), body_data.end());

        return true;

    } catch (...) {
        return false;
    }
}

bool is_in_audio_folder(const std::string& path) {
    std::string lower_path = path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);
    return lower_path.find("/audio/") != std::string::npos ||
           lower_path.find("\\audio\\") != std::string::npos;
}

// Bulk-extract every file in a single BNK. Mirrors the per-asset
// "Export" path used by the right-click context menu — same call to
// `extract_file_one` per file, same XMA→PCM conversion for .wav. The
// difference is just the loop. Worker thread + progress modal so the
// UI stays responsive on archives with thousands of files.
//
// Output layout: `${S.export_dir}/<asset_path>` for each entry. We
// don't add a per-BNK subdirectory the way `dump_bnk_contents` does
// because the user is opting into a single archive — they expect the
// asset paths to land directly under their export root, same as
// individual right-click Exports.
void extract_single_bnk_contents(const std::string& bnk_path) {
    if (bnk_path.empty()) {
        OutputLog::warn("Extract BNK: no archive selected.");
        return;
    }

    // Build the file list on the calling thread (fast — just opens the
    // BNK and reads its TOC). The actual extraction loop runs async.
    std::vector<BNKItemUI> items;
    try {
        BNKReader reader(bnk_path);
        const auto& files = reader.list_files();
        items.reserve(files.size());
        for (size_t i = 0; i < files.size(); ++i) {
            BNKItemUI it;
            it.index = (int)i;
            it.name  = files[i].name;
            it.size  = files[i].uncompressed_size;
            items.push_back(std::move(it));
        }
    } catch (const std::exception& e) {
        OutputLog::error(std::string("Extract BNK: failed to open ") +
                         bnk_path + " — " + e.what());
        return;
    } catch (...) {
        OutputLog::error(std::string("Extract BNK: failed to open ") +
                         bnk_path);
        return;
    }
    if (items.empty()) {
        OutputLog::warn(std::string("Extract BNK: ") + bnk_path +
                        " contains no entries.");
        return;
    }

    const std::string export_root =
        S.export_dir.empty() ? std::filesystem::absolute("extracted").string()
                             : S.export_dir;
    const int total = (int)items.size();
    OutputLog::info(std::string("Extracting ") + std::to_string(total) +
                    " file(s) from " +
                    std::filesystem::path(bnk_path).filename().string() +
                    " → " + export_root);

    progress_open(total,
                  std::string("Extracting ") +
                  std::filesystem::path(bnk_path).filename().string() +
                  " → " + export_root);
    progress_update(0, total, "Starting...");

    std::thread([items = std::move(items),
                 bnk_path,
                 export_root, total]() {
        struct PG {
            ~PG() { progress_done(); }
        } pg;

        std::atomic<int> done{0};
        std::vector<std::string> failed;
        std::mutex fail_m;

        try {
            for (const auto& it : items) {
                if (S.cancel_requested.load() || S.exiting.load()) break;
                bool ok = false;
                try {
                    // convert_audio=true so .wav entries come out as
                    // playable PCM rather than the original XMA blob —
                    // matches the per-file right-click Export.
                    extract_file_one(bnk_path, it, export_root,
                                     /*convert_audio=*/true);
                    ok = true;
                } catch (const std::exception& e) {
                    OutputLog::error(std::string("Extract failed (") +
                                     it.name + "): " + e.what());
                } catch (...) {
                    OutputLog::error(std::string("Extract failed (") +
                                     it.name + ")");
                }
                if (!ok) {
                    std::lock_guard<std::mutex> lk(fail_m);
                    failed.push_back(it.name);
                }
                int cur = ++done;
                progress_update(cur, total,
                                std::filesystem::path(it.name)
                                    .filename().string());
            }
        } catch (const std::exception& e) {
            OutputLog::error(std::string("Extract worker aborted: ") +
                             e.what());
            return;
        } catch (...) {
            OutputLog::error("Extract worker aborted (unknown exception).");
            return;
        }

        if (S.cancel_requested.load()) {
            OutputLog::warn(std::string("BNK extract cancelled (") +
                            std::to_string(done.load()) + "/" +
                            std::to_string(total) + ").");
            S.cancel_requested = false;
            return;
        }
        const int n_failed = (int)failed.size();
        if (n_failed > 0) {
            OutputLog::warn(std::string("BNK extract finished: ") +
                            std::to_string(done.load() - n_failed) + "/" +
                            std::to_string(total) + " written, " +
                            std::to_string(n_failed) + " failed.");
        } else {
            OutputLog::success(std::string("BNK extract complete: ") +
                               std::to_string(total) + " file(s) → " +
                               export_root);
        }
    }).detach();
}

// ---------------------------------------------------------------------------
// Per-asset right-click export
// ---------------------------------------------------------------------------
// The .tex case has had a multi-format submenu (PNG / JPG / TIFF /
// DDS / .tex raw) since the texture exporter shipped — that's
// `tex_export_menu_named`, called below. For every other type we
// want a single "Export" entry that lands the asset in the user's
// configured export directory at its original asset path. .mdl needs
// reconstruction (header + body, the same shape `dump_mdl_files`
// produces); .wav needs an XMA→PCM pass through `extract_file_one`'s
// audio path so the output plays in any external tool; everything
// else is a straight raw extract.
//
// Output layout matches the bulk dumpers and the .tex export so the
// user has a single, predictable destination tree:
//   `${S.export_dir}/<asset_path>`

namespace {

// Reconstruct an MDL by pulling the body out of `bnk_path` at
// `file_index` and prefixing the matching header from the paired
// `*_model_headers.bnk` (or `globals_model_headers.bnk` as a
// fallback). Mirrors the BNK-pair derivation `reconstruct_one_mdl` in
// IsoDump.cpp uses for the bulk MDL dump — duplicating it here keeps
// the right-click path independent of that worker's BNK cache, which
// only lives for the duration of a `dump_mdl_files` run.
//
// Returns true and fills `out` on success. On any failure (source
// can't open, body extract throws, etc.) returns false and leaves
// `out` empty. A body-with-no-paired-header is treated as success and
// emits the body raw — matches `reconstruct_nested_mdl`'s long-
// standing fallback so unpaired entries still get dumped.
bool reconstruct_mdl_paired(const std::string& bnk_path,
                            int file_index,
                            std::vector<unsigned char>& out) {
    out.clear();
    try {
        BNKReader src(bnk_path);
        const auto& src_files = src.list_files();
        if (file_index < 0 || (size_t)file_index >= src_files.size())
            return false;
        std::string mdl_name = src_files[file_index].name;

        auto tmpdir = std::filesystem::temp_directory_path() / "f2_mdl_export";
        std::error_code ec;
        std::filesystem::create_directories(tmpdir, ec);

        auto tmp_body = tmpdir / "body.bin";
        extract_one(bnk_path, file_index, tmp_body.string());
        auto body = read_all_bytes(tmp_body);
        std::filesystem::remove(tmp_body, ec);
        if (body.empty()) return false;

        // Substitution rule: <root>_models.bnk → <root>_model_headers.bnk.
        // Lower-case the basename so the comparison + replacement
        // doesn't care about disc-side capitalisation differences.
        std::string base = std::filesystem::path(bnk_path)
                               .filename().string();
        std::transform(base.begin(), base.end(), base.begin(), ::tolower);
        std::optional<std::string> p_headers;
        const std::string suffix = "_models.bnk";
        if (base.size() >= suffix.size() &&
            base.compare(base.size() - suffix.size(),
                         suffix.size(), suffix) == 0) {
            std::string paired = base.substr(0, base.size() - suffix.size())
                               + "_model_headers.bnk";
            p_headers = find_bnk_by_filename(paired);
        }
        if (!p_headers) {
            p_headers = find_bnk_by_filename("globals_model_headers.bnk");
        }
        if (!p_headers) {
            // Body alone — unpaired nested archive. Same fallback
            // reconstruct_nested_mdl uses; better than failing the
            // export when at least the body is available.
            out = std::move(body);
            return true;
        }

        BNKReader hr(*p_headers);
        const auto& h_files = hr.list_files();
        std::string mdl_leaf = std::filesystem::path(mdl_name)
                                   .filename().string();
        std::transform(mdl_leaf.begin(), mdl_leaf.end(),
                       mdl_leaf.begin(), ::tolower);

        int h_idx = -1;
        for (size_t i = 0; i < h_files.size(); ++i) {
            std::string hn = std::filesystem::path(h_files[i].name)
                                 .filename().string();
            std::transform(hn.begin(), hn.end(), hn.begin(), ::tolower);
            if (hn == mdl_leaf) { h_idx = (int)i; break; }
        }
        if (h_idx < 0) {
            // Header BNK exists but doesn't contain this asset's
            // header — emit body anyway, same fallback shape.
            out = std::move(body);
            return true;
        }

        auto tmp_h = tmpdir / "header.bin";
        extract_one(*p_headers, h_idx, tmp_h.string());
        auto hbuf = read_all_bytes(tmp_h);
        std::filesystem::remove(tmp_h, ec);
        if (hbuf.empty()) { out = std::move(body); return true; }

        out.reserve(hbuf.size() + body.size());
        out.insert(out.end(), hbuf.begin(), hbuf.end());
        out.insert(out.end(), body.begin(), body.end());
        return true;
    } catch (...) {
        out.clear();
        return false;
    }
}

// Compute the on-disk export destination for an asset. Same path
// normalisation rule the .tex exporter uses (drop leading separators
// so the concat doesn't accidentally anchor to a Windows root).
std::filesystem::path build_export_target(const std::string& asset_path) {
    std::string rel = asset_path;
    while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\'))
        rel.erase(rel.begin());
    std::filesystem::path root =
        S.export_dir.empty() ? std::filesystem::path("extracted")
                             : std::filesystem::path(S.export_dir);
    return root / rel;
}

// Lower-case extension test — "endsWith(".mdl")" with the tolower
// happening only on the trailing slice, not the whole asset path.
bool ext_is(const std::string& name, const char* ext) {
    size_t n = std::strlen(ext);
    if (name.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        char a = name[name.size() - n + i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != ext[i]) return false;
    }
    return true;
}

} // anonymous

// Generic right-click export handler. Kicks off a synchronous
// extract-and-write to `${S.export_dir}/<asset_path>`. .mdl gets
// reconstructed via `reconstruct_mdl_paired`; .wav goes through
// `extract_file_one`'s convert_audio path so the output is a
// playable PCM .wav (the source bytes are XMA-encoded and won't
// open in any non-game tool); everything else is a raw extract.
//
// Synchronous on the UI thread: a single-file extract is fast and
// the user's right-click is a deliberate single-asset operation, so
// adding a worker thread + progress modal would just add latency.
// The existing `dump_*_files()` functions are the path for bulk.
// Per-file MP3 / AAC encoder for the right-click "Extract as ..."
// audio submenu. Mirrors the encoded-format branch in IsoDump.cpp's
// `dump_wav_files_as`, just for a single asset:
//   1. extract the XMA-encoded bytes from the BNK to a scratch file
//   2. read into a buffer + decode to PCM via XmaDecoder
//   3. encode to MP3 or AAC via MfAudio (Windows Media Foundation)
//   4. drop the scratch file regardless of outcome
//
// `out_ext` is the destination file extension WITH the leading dot —
// `.mp3` for MP3, `.m4a` for AAC (the MF SinkWriter picks its muxer
// from the URL extension, and `.aac` raw isn't recognised — `.m4a`
// or `.mp4` are). All output goes under `${S.export_dir}` at the
// asset's relative path with the extension swapped.
static void asset_export_audio_encoded(const std::string& bnk_path,
                                       int file_index,
                                       const std::string& file_name,
                                       bool aac /*false = mp3*/)
{
    const char* fmt_label = aac ? "AAC"  : "MP3";
    const char* fmt_ext   = aac ? ".m4a" : ".mp3";

    // Destination path: ${export_dir}/<asset_path with replaced ext>.
    // Same normalisation `build_export_target` does (strip leading
    // separators); replace_extension swaps the trailing piece so an
    // input `audio/foo.wav` lands as `audio/foo.mp3` instead of
    // `audio/foo.wav.mp3`.
    auto out = build_export_target(file_name);
    out.replace_extension(fmt_ext);
    auto scratch = out;
    scratch += ".xma.tmp";

    std::error_code ec;
    if (auto parent = out.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            OutputLog::error(std::string(fmt_label) +
                             " export: cannot create " +
                             parent.string() + " — " + ec.message());
            return;
        }
    }

    bool ok = false;
    try {
        // Step 1 — pull the raw XMA-encoded RIFF/WAVE from the BNK.
        extract_one(bnk_path, file_index, scratch.string());
        if (!std::filesystem::exists(scratch, ec) || ec) {
            throw std::runtime_error("extract_one produced no file");
        }
        auto raw = read_all_bytes(scratch);
        std::filesystem::remove(scratch, ec);
        if (raw.empty()) {
            throw std::runtime_error("extracted .wav is empty");
        }

        // Step 2 — XMA → PCM.
        std::vector<uint8_t> src(raw.begin(), raw.end());
        std::vector<int16_t> pcm;
        int sr = 0, ch = 0;
        std::string err;
        if (!XmaDecoder::decode_xma_to_pcm(src, pcm, sr, ch, &err) ||
            pcm.empty()) {
            throw std::runtime_error(
                std::string("XMA→PCM decode failed: ") + err);
        }

        // Step 3 — PCM → MP3 / AAC via Media Foundation.
        bool encoded = aac
            ? MfAudio::encode_pcm_to_aac(pcm, sr, ch, out.string(), &err)
            : MfAudio::encode_pcm_to_mp3(pcm, sr, ch, out.string(), &err);
        if (!encoded) {
            throw std::runtime_error(
                std::string(fmt_label) + " encode failed: " + err);
        }
        ok = true;
    } catch (const std::exception& ex) {
        std::filesystem::remove(scratch, ec);
        OutputLog::error(std::string(fmt_label) + " export exception on "
                         + file_name + ": " + ex.what());
    } catch (...) {
        std::filesystem::remove(scratch, ec);
        OutputLog::error(std::string(fmt_label) + " export exception on "
                         + file_name);
    }

    if (ok) {
        OutputLog::success(std::string("Exported ") +
                           std::filesystem::path(file_name).filename().string()
                           + " as " + fmt_label + " → " + out.string());
    } else {
        OutputLog::error(std::string(fmt_label) + " export failed: " +
                         file_name);
    }
}

// `convert_audio` only matters for .wav assets: when true, the
// extract_file_one call below decodes XMA → PCM in-place, leaving a
// playable .wav at the destination. When false the bytes land
// verbatim — the original XMA-encoded RIFF/WAVE the BNK stores.
// Non-audio assets ignore the flag (it just feeds straight through to
// extract_file_one which only acts on `is_audio_file` matches anyway).
static void asset_export_to_export_dir(const std::string& bnk_path,
                                       int file_index, bool /*is_nested*/,
                                       const std::string& file_name,
                                       bool convert_audio = true)
{
    auto out = build_export_target(file_name);
    std::error_code ec;
    if (auto parent = out.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            OutputLog::error(std::string("Export: cannot create ") +
                             parent.string() + " — " + ec.message());
            return;
        }
    }

    bool ok = false;
    try {
        if (ext_is(file_name, ".mdl")) {
            std::vector<unsigned char> buf;
            if (reconstruct_mdl_paired(bnk_path, file_index, buf) &&
                !buf.empty()) {
                std::ofstream f(out, std::ios::binary | std::ios::trunc);
                if (f) {
                    f.write(reinterpret_cast<const char*>(buf.data()),
                            (std::streamsize)buf.size());
                    ok = f.good();
                }
            }
        } else {
            // Raw extract path — extract_file_one streams to disk
            // and (for audio assets, only when convert_audio is true)
            // follows up with an XMA→PCM decode pass in-place.
            // Anything that isn't audio just gets written byte-for-
            // byte regardless of the flag.
            BNKItemUI item{};
            item.index = file_index;
            item.name  = file_name;
            item.size  = 0;
            extract_file_one(bnk_path, item,
                             out.parent_path().string(),
                             convert_audio);
            // extract_file_one writes to base_out_dir / item.name —
            // which equals our `out` so long as `file_name` doesn't
            // have leading separators. Our build_export_target
            // already normalised that.
            ok = std::filesystem::exists(out, ec) && !ec;
        }
    } catch (const std::exception& ex) {
        OutputLog::error(std::string("Export exception on ") + file_name +
                         ": " + ex.what());
        ok = false;
    } catch (...) {
        OutputLog::error(std::string("Export exception on ") + file_name);
        ok = false;
    }

    if (ok) {
        OutputLog::success(std::string("Exported ") +
                           std::filesystem::path(file_name).filename().string() +
                           " → " + out.string());
    } else {
        OutputLog::error(std::string("Export failed: ") + file_name +
                         " (target: " + out.string() + ")");
    }
}

// Right-click context menu attached to the most-recently-rendered file
// item. ImGui::BeginPopupContextItem ties the popup's lifecycle to the
// last item's ID, so the caller just needs to render its Selectable /
// TreeNodeEx then call this function.
//
// Selecting "Hex View" reroutes S.selected_bnk / S.selected_file_index
// to point at the right-clicked file (mirroring what a normal click
// does), then calls open_hex_for_selected() which kicks the bytes
// load on a worker thread.
//
// The "Export" entry is always available (every file we surface in
// the UI is exportable). For .tex it expands into a multi-format
// submenu (PNG/JPG/TIFF/DDS/.tex raw); for everything else it's a
// single MenuItem that drops the asset into `${S.export_dir}` —
// reconstructed for .mdl, PCM-converted for .wav, raw for the rest.
void file_hex_context_menu(const std::string& bnk_path,
                           int file_index, bool is_nested,
                           const std::string& file_name) {
    const bool show_hex = S.dev_mode;
    const bool is_tex   = is_tex_file(file_name);

    if (ImGui::BeginPopupContextItem()) {
        if (show_hex) {
            if (ImGui::MenuItem("Hex View")) {
                // Switch active BNK if needed — pick_bnk repopulates
                // S.files for the new BNK, which we need so
                // open_hex_for_selected can resolve the entry name.
                if (S.selected_bnk != bnk_path) {
                    S.viewing_adb = false;
                    S.viewing_lua = false;
                    S.global_search.clear();
                    S.selected_nested_bnk.clear();
                    S.selected_nested_index = -1;
                    pick_bnk(bnk_path);
                }
                // pick_bnk wipes nested state — re-establish it for nested
                // files so extract paths route through the right archive.
                if (is_nested) {
                    S.selected_nested_temp_path = bnk_path;
                    S.selected_nested_index = 0;
                }
                // Find the entry in S.files whose BNK index matches and
                // mark it selected. open_hex_for_selected reads
                // S.selected_file_index, not the BNK's intrinsic index,
                // so this mapping step is required.
                S.selected_file_index = -1;
                for (size_t i = 0; i < S.files.size(); ++i) {
                    if (S.files[i].index == file_index) {
                        S.selected_file_index = (int)i;
                        break;
                    }
                }
                if (S.selected_file_index >= 0) {
                    open_hex_for_selected();
                }
            }
        }
        if (is_tex) {
            // The export menu defers the actual decode + write until
            // after the user picks a path in the save dialog. mip 0 is
            // the highest-resolution mip per the .tex format convention.
            tex_export_menu_named(file_name, file_name, bnk_path,
                                  /*mip_index=*/0);
        } else if (is_audio_file(file_name)) {
            // Audio gets the same set of formats the Audio tab's
            // "Extract All as..." footer offers, just for one file.
            // PCM and Raw write `.wav`; MP3 writes `.mp3`; AAC writes
            // `.m4a` (MP4 container — required by Media Foundation's
            // SinkWriter, which selects the muxer from the URL
            // extension). Each lands at `${export_dir}/<asset_path>`
            // with the extension swapped, overwriting any previous
            // export of the same asset in that format.
            if (ImGui::MenuItem("Extract (.wav PCM)")) {
                asset_export_to_export_dir(bnk_path, file_index,
                                           is_nested, file_name,
                                           /*convert_audio=*/true);
            }
            if (ImGui::MenuItem("Extract Raw")) {
                asset_export_to_export_dir(bnk_path, file_index,
                                           is_nested, file_name,
                                           /*convert_audio=*/false);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Extract MP3")) {
                asset_export_audio_encoded(bnk_path, file_index,
                                           file_name, /*aac=*/false);
            }
            if (ImGui::MenuItem("Extract AAC (.m4a)")) {
                asset_export_audio_encoded(bnk_path, file_index,
                                           file_name, /*aac=*/true);
            }
        } else {
            if (ImGui::MenuItem("Export")) {
                asset_export_to_export_dir(bnk_path, file_index,
                                           is_nested, file_name);
            }
        }
        ImGui::EndPopup();
    }
}
