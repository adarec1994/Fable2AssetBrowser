#include "../UI_Panels.h"
#include "PanelInternal.h"
#include "../../Utilities/Utils.h"
#include "../../Utilities/Files.h"
#include "../../Utilities/Progress.h"
#include "../../ISO/IsoMount.h"
#include "../../BNKCore.cpp"
#include "../UI_Main.h"
#include "../AudioPlayerWindow.h"
#include <filesystem>
#include <algorithm>
#include <ctime>
#include <cstring>

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
