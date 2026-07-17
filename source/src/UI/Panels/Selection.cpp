#include "../UI_Panels.h"
#include "PanelInternal.h"
#include "../../Utilities/Utils.h"
#include "../../Utilities/Files.h"
#include "../../Utilities/Progress.h"
#include "../../Utilities/operations.h"
#include "../../ISO/IsoMount.h"
#include "../../BNKCore.cpp"
#include "../../GDB/GdbParser.h"
#include "../UI_Main.h"
#include "../AudioPlayerWindow.h"
#include "../OutputLog.h"
#include "../HexView.h"
#include "ImportDialog.h"
#include "../../textures/export/TextureExport.h"
#include "../../Utilities/GameBackup.h"
#include "../../Audio/XmaDecoder.h"

#include "../../Audio/MfAudioEncoder.h"
#include "../../ISO/IsoDump.h"

#include "../../animations/AnimBank.h"
#include "../../animations/AnimDataFile.h"
#include "../../animations/Locomotion.h"
#include "imgui.h"
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstring>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>

void refresh_file_table() { S.selected_file_index = -1; }

bool open_audio_player_for_selected(int file_index) {
    if (file_index < 0 || file_index >= (int)S.files.size()) return false;

    const auto& item = S.files[(size_t)file_index];

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

namespace {

std::string gdb_lower_slash(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

uint32_t gdb_fnv1_model_path_hash(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    std::replace(s.begin(), s.end(), '/', '\\');

    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : s) {
        h *= 0x01000193u;
        h ^= uint32_t(c);
    }
    return h;
}

std::vector<std::pair<uint32_t, std::string>>
parse_save_hash_to_name(const std::vector<uint8_t>& save_bytes)
{
    std::vector<std::pair<uint32_t, std::string>> out;
    if (save_bytes.empty()) return out;
    std::string xml(reinterpret_cast<const char*>(save_bytes.data()),
                    save_bytes.size());
    const std::string tag_open = "<Entity name=\"";
    const std::string tag_close = "</Entity>";
    size_t pos = 0;
    while (true) {
        size_t a = xml.find(tag_open, pos);
        if (a == std::string::npos) break;
        a += tag_open.size();
        size_t name_end = xml.find('"', a);
        if (name_end == std::string::npos) break;
        std::string name = xml.substr(a, name_end - a);
        size_t hash_start = xml.find("0x", name_end);
        if (hash_start == std::string::npos) break;
        size_t hash_end = xml.find('<', hash_start);
        if (hash_end == std::string::npos) break;
        std::string hex =
            xml.substr(hash_start + 2, hash_end - hash_start - 2);
        size_t entity_close = xml.find(tag_close, hash_end);
        if (entity_close == std::string::npos) break;

        uint32_t h = 0;
        bool ok = true;
        for (char c : hex) {
            h <<= 4;
            if (c >= '0' && c <= '9') h |= uint32_t(c - '0');
            else if (c >= 'A' && c <= 'F') h |= uint32_t(c - 'A' + 10);
            else if (c >= 'a' && c <= 'f') h |= uint32_t(c - 'a' + 10);
            else {
                ok = false;
                break;
            }
        }
        if (ok) {
            out.emplace_back(h, std::move(name));
        }
        pos = entity_close + tag_close.size();
    }
    return out;
}

bool find_save_sibling_bytes(const std::string& bnk_path,
                             const std::string& gdb_file_name,
                             std::vector<uint8_t>& out)
{
    std::string save_key = gdb_lower_slash(gdb_file_name);
    const size_t dot = save_key.find_last_of('.');
    if (dot != std::string::npos) {
        save_key.resize(dot);
    }
    save_key += ".save";
    const std::string save_leaf =
        std::filesystem::path(save_key).filename().string();

    auto try_bnk = [&](const std::string& candidate_bnk) -> bool {
        if (candidate_bnk.empty()) return false;
        int idx = BnkCache::find_index(candidate_bnk, save_key);
        if (idx < 0) idx = BnkCache::find_index(candidate_bnk, save_leaf);
        if (idx < 0) return false;
        try {
            out = BnkCache::extract_bytes(candidate_bnk, idx);
        } catch (...) {
            out.clear();
            return false;
        }
        return !out.empty();
    };

    if (try_bnk(bnk_path)) return true;

    if (auto other_bnk = find_bnk_by_virtual_path(save_key)) {
        if (try_bnk(*other_bnk)) return true;
    }

    return false;
}

}

bool open_gdb_viewer_for_bnk_entry(const std::string& bnk_path,
                                   int file_index,
                                   const std::string& file_name)
{
    if (bnk_path.empty() || file_index < 0) return false;

    std::vector<uint8_t> gdb_bytes;
    try {
        gdb_bytes = BnkCache::extract_bytes(bnk_path, file_index);
    } catch (const std::exception& ex) {
        OutputLog::error("GDB viewer: failed to extract " + file_name +
                         " (" + ex.what() + ")");
        return false;
    } catch (...) {
        OutputLog::error("GDB viewer: failed to extract " + file_name);
        return false;
    }
    if (gdb_bytes.empty()) {
        OutputLog::warn("GDB viewer: empty GDB " + file_name);
        return false;
    }

    std::vector<uint8_t> save_bytes;
    std::vector<std::pair<uint32_t, std::string>> save_hash_to_name;
    if (find_save_sibling_bytes(bnk_path, file_name, save_bytes)) {
        save_hash_to_name = parse_save_hash_to_name(save_bytes);
    }

    std::vector<Gdb::RecordRow> records =
        Gdb::Build010RecordRows(gdb_bytes, save_hash_to_name);

    std::unordered_map<uint32_t, std::string> save_name_by_hash;
    save_name_by_hash.reserve(save_hash_to_name.size() * 2);
    for (const auto& kv : save_hash_to_name) {
        save_name_by_hash.emplace(kv.first, kv.second);
    }

    std::unordered_map<uint32_t, std::string> model_name_by_hash;
    model_name_by_hash.reserve(S.all_mdl_files.size() * 2);
    for (const FlatAssetEntry& e : S.all_mdl_files) {
        if (e.full_path.empty()) continue;
        model_name_by_hash.emplace(gdb_fnv1_model_path_hash(e.full_path),
                                   e.full_path);
    }

    S.gdb_view_rows.clear();
    S.gdb_view_rows.reserve(records.size());
    size_t named = 0;
    size_t parent_named = 0;
    size_t model_named = 0;
    size_t skeleton_refs = 0;
    for (const Gdb::RecordRow& rec : records) {
        GdbViewerRow row;
        row.record_index = rec.index;
        row.name = rec.name;
        auto hash_it = save_name_by_hash.find(rec.hash);
        if (hash_it != save_name_by_hash.end()) {
            row.hash_name = hash_it->second;
            if (row.name.empty()) row.name = row.hash_name;
        }
        if (!row.name.empty() || !row.hash_name.empty()) ++named;
        if (rec.parent_hash != 0) {
            auto parent_it = save_name_by_hash.find(rec.parent_hash);
            if (parent_it != save_name_by_hash.end()) {
                row.parent_name = parent_it->second;
                ++parent_named;
            }
        }
        row.hash = rec.hash;
        row.parent_hash = rec.parent_hash;
        row.model_path_hash = rec.model_path_hash;
        row.skeleton_file_hash = rec.skeleton_file_hash;
        row.retarget_skeleton_file_hash = rec.retarget_skeleton_file_hash;
        row.skeleton_file_name = rec.skeleton_file_name;
        row.retarget_skeleton_file_name = rec.retarget_skeleton_file_name;
        if (row.skeleton_file_hash != 0 ||
            row.retarget_skeleton_file_hash != 0) {
            ++skeleton_refs;
        }
        row.model_path_hashes = rec.model_path_hashes;
        for (uint32_t model_hash : row.model_path_hashes) {
            auto model_it = model_name_by_hash.find(model_hash);
            if (model_it != model_name_by_hash.end()) {
                if (row.model_path_name.empty()) {
                    row.model_path_name = model_it->second;
                }
                row.model_path_names.push_back(model_it->second);
                ++model_named;
            } else {
                row.model_path_names.emplace_back();
            }
        }
        row.indexed_record = true;
        S.gdb_view_rows.push_back(std::move(row));
    }

    std::ostringstream title;
    title << std::filesystem::path(file_name).filename().string()
          << "  rows=" << S.gdb_view_rows.size()
          << "  row-names=" << named
          << "  parent-names=" << parent_named
          << "  model-names=" << model_named
          << "  skeleton-refs=" << skeleton_refs
          << "  save-map=" << save_hash_to_name.size();
    S.gdb_view_title = title.str();
    S.gdb_view_filter.clear();
    S.show_gdb_render = true;
    S.show_lua_render = false;

    OutputLog::success("GDB viewer opened: " + file_name + " (" +
                       std::to_string(S.gdb_view_rows.size()) + " rows)");
    if (save_hash_to_name.empty()) {
        OutputLog::warn("GDB viewer: no .save sibling names resolved for " +
                        file_name);
    }
    return true;
}

void pick_bnk(const std::string &path) {
    S.selected_bnk = path;
    S.viewing_lua = false;
    S.lua_preview_content.clear();
    S.lua_preview_title.clear();
    S.lua_preview_selected = -1;
    ++S.lua_preview_request;
    S.lua_preview_loading = false;
    S.lua_preview_is_quest = false;
    S.quest_preview_select_nodes = false;
    S.show_gdb_render = false;
    S.gdb_view_rows.clear();
    S.gdb_view_title.clear();
    S.gdb_view_filter.clear();
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

void open_iso_logic(const std::string& iso_path) {
    if (iso_path.empty()) { show_error_box("No ISO selected"); return; }
    try {

        S.bnk_paths = scan_bnks_recursive(iso_path);
        if (S.bnk_paths.empty()) S.bnk_paths = find_bnks(iso_path);
        S.adb_paths = scan_adbs_recursive(iso_path);

        auto lua_paths = scan_luas_recursive(iso_path);
        S.lua_files.clear();
        S.lua_files.reserve(lua_paths.size());
        for (size_t i = 0; i < lua_paths.size(); ++i) {
            std::filesystem::path p(lua_paths[i]);

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
        S.bnk_paths.clear();
        S.adb_paths.clear();
        S.lua_files.clear();
        ISO::IsoMount::instance().unmount();
        show_error_box("Error indexing BNK files in the ISO");
        return;
    }
    if (S.bnk_paths.empty()) {
        S.adb_paths.clear();
        S.lua_files.clear();
        ISO::IsoMount::instance().unmount();
        show_error_box("No .bnk files found in the ISO.");
        return;
    }

    S.root_dir = iso_path;
    S.last_dir = std::filesystem::path(iso_path).parent_path().string();
    save_last_dir(S.last_dir);

    if (Anim::load_toc_for_root(iso_path, S.anim_clips)) {

        Anim::global_data_file().open_for_root(iso_path);

        Anim::resolve_clip_names_from_luas(S.anim_clips);
    }
    Anim::load_locomotion_for_root(iso_path);
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
        S.bnk_paths.clear();
        S.adb_paths.clear();
        S.lua_files.clear();
        show_error_box("Error searching for BNK files");
        return;
    }
    if (S.bnk_paths.empty()) {
        S.adb_paths.clear();
        S.lua_files.clear();
        show_error_box(
            std::string("No .bnk files found in:\n") + sel + std::string(
                "\n\nPlease select a folder containing Fable 2 BNK files."));
        return;
    }

    S.root_dir = sel;
    S.last_dir = sel;
    save_last_dir(sel);

    if (Anim::load_toc_for_root(sel, S.anim_clips)) {
        Anim::global_data_file().open_for_root(sel);

        Anim::resolve_clip_names_from_luas(S.anim_clips);
    }
    Anim::load_locomotion_for_root(sel);

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

    start_tree_build_for_root(sel, S.bnk_paths);
}

bool reconstruct_nested_mdl(const std::string& nested_bnk_path, int file_index, std::vector<unsigned char>& out, const std::string& mdl_full_path) {
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

        auto norm = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            std::replace(s.begin(), s.end(), '\\', '/');
            return s;
        };
        std::vector<std::string> full_keys;
        if (!mdl_full_path.empty()) full_keys.push_back(norm(mdl_full_path));
        if (mdl_name.find('/') != std::string::npos ||
            mdl_name.find('\\') != std::string::npos)
            full_keys.push_back(norm(mdl_name));
        const std::string base_key =
            norm(std::filesystem::path(mdl_name).filename().string());

        int header_idx = -1;
        for (const auto& want : full_keys) {
            for (size_t i = 0; i < header_files.size(); ++i) {
                if (norm(header_files[i].name) == want) { header_idx = (int)i; break; }
            }
            if (header_idx != -1) break;
        }
        if (header_idx == -1) {
            for (size_t i = 0; i < header_files.size(); ++i) {
                std::string hbase =
                    norm(std::filesystem::path(header_files[i].name).filename().string());
                if (hbase == base_key) { header_idx = (int)i; break; }
            }
        }

        if (header_idx == -1) {
            OutputLog::warn("nested MDL: no header match for body '" + mdl_name +
                            "' (full='" + mdl_full_path + "') -> body-only (will not parse)");
            out = body_data;
            return true;
        }
        OutputLog::info("nested MDL: body '" + mdl_name + "' (full='" + mdl_full_path +
                        "') -> header[" + std::to_string(header_idx) + "] '" +
                        header_files[header_idx].name + "'");

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

void extract_single_bnk_contents(const std::string& bnk_path) {
    if (bnk_path.empty()) {
        OutputLog::warn("Extract BNK: no archive selected.");
        return;
    }

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
                         bnk_path + " - " + e.what());
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
                    " -> " + export_root);

    progress_open(total,
                  std::string("Extracting ") +
                  std::filesystem::path(bnk_path).filename().string() +
                  " -> " + export_root);
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

                    extract_file_one(bnk_path, it, export_root,
                                     true);
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
                               std::to_string(total) + " file(s) -> " +
                               export_root);
        }
    }).detach();
}

namespace {

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

std::filesystem::path build_export_target(const std::string& asset_path) {
    std::string rel = asset_path;
    while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\'))
        rel.erase(rel.begin());
    std::filesystem::path root =
        S.export_dir.empty() ? std::filesystem::path("extracted")
                             : std::filesystem::path(S.export_dir);
    return root / rel;
}

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

}

static void asset_export_audio_raw_xma(const std::string& bnk_path,
                                       int file_index,
                                       const std::string& file_name)
{
    auto out = build_export_target(file_name);
    out.replace_extension(".xma");
    std::error_code ec;
    if (auto parent = out.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            OutputLog::error(std::string(".xma export: cannot create ") +
                             parent.string() + " - " + ec.message());
            return;
        }
    }
    bool ok = false;
    try {
        extract_one(bnk_path, file_index, out.string());
        ok = std::filesystem::exists(out, ec) && !ec;
    } catch (const std::exception& ex) {
        OutputLog::error(std::string(".xma export exception on ") +
                         file_name + ": " + ex.what());
    } catch (...) {
        OutputLog::error(std::string(".xma export exception on ") +
                         file_name);
    }
    if (ok) {
        OutputLog::success(std::string("Exported ") +
                           std::filesystem::path(file_name).filename().string()
                           + " as raw .xma -> " + out.string());
    } else {
        OutputLog::error(std::string(".xma export failed: ") + file_name);
    }
}

static void asset_export_audio_encoded(const std::string& bnk_path,
                                       int file_index,
                                       const std::string& file_name,
                                       bool aac )
{
    const char* fmt_label = aac ? "AAC"  : "MP3";
    const char* fmt_ext   = aac ? ".m4a" : ".mp3";

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
                             parent.string() + " - " + ec.message());
            return;
        }
    }

    bool ok = false;
    try {

        extract_one(bnk_path, file_index, scratch.string());
        if (!std::filesystem::exists(scratch, ec) || ec) {
            throw std::runtime_error("extract_one produced no file");
        }
        auto raw = read_all_bytes(scratch);
        std::filesystem::remove(scratch, ec);
        if (raw.empty()) {
            throw std::runtime_error("extracted .wav is empty");
        }

        std::vector<uint8_t> src(raw.begin(), raw.end());
        std::vector<int16_t> pcm;
        int sr = 0, ch = 0;
        std::string err;
        if (!XmaDecoder::decode_xma_to_pcm(src, pcm, sr, ch, &err) ||
            pcm.empty()) {
            throw std::runtime_error(
                std::string("XMA->PCM decode failed: ") + err);
        }

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
                           + " as " + fmt_label + " -> " + out.string());
    } else {
        OutputLog::error(std::string(fmt_label) + " export failed: " +
                         file_name);
    }
}

static void asset_export_to_export_dir(const std::string& bnk_path,
                                       int file_index, bool ,
                                       const std::string& file_name,
                                       bool convert_audio = true)
{
    auto out = build_export_target(file_name);
    std::error_code ec;
    if (auto parent = out.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            OutputLog::error(std::string("Export: cannot create ") +
                             parent.string() + " - " + ec.message());
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

            BNKItemUI item{};
            item.index = file_index;
            item.name  = file_name;
            item.size  = 0;
            extract_file_one(bnk_path, item,
                             out.parent_path().string(),
                             convert_audio);

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
                           " -> " + out.string());
    } else {
        OutputLog::error(std::string("Export failed: ") + file_name +
                         " (target: " + out.string() + ")");
    }
}

void file_hex_context_menu(const std::string& bnk_path,
                           int file_index, bool is_nested,
                           const std::string& file_name) {
    const bool show_hex = S.dev_mode;
    const bool is_tex   = is_tex_file(file_name);

    if (ImGui::BeginPopupContextItem()) {
        if (show_hex) {
            if (ImGui::MenuItem("Hex View")) {

                if (S.selected_bnk != bnk_path) {
                    S.viewing_adb = false;
                    S.viewing_lua = false;
                    S.global_search.clear();
                    S.selected_nested_bnk.clear();
                    S.selected_nested_index = -1;
                    pick_bnk(bnk_path);
                }

                if (is_nested) {
                    S.selected_nested_temp_path = bnk_path;
                    S.selected_nested_index = 0;
                }

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

            std::string backup_error;
            const bool backup_ready =
                GameBackup::RequireBackup(backup_error);
            if (!backup_ready) {
                ImGui::MenuItem(
                    "Replace (UNAVAILABLE WITHOUT BACKUP)", nullptr,
                    false, false);
            } else {
                const bool replace_busy = ImportDialog::Busy();
                if (replace_busy) ImGui::BeginDisabled();
                if (ImGui::MenuItem("Replace...")) {
                    ImportDialog::OpenTextureReplacement(
                        bnk_path, file_index, file_name);
                }
                if (replace_busy) ImGui::EndDisabled();
            }
            ImGui::Separator();

            tex_export_menu_named(file_name, file_name, bnk_path,
                                  0);
        } else if (is_mdl_file(file_name)) {

            if (ImGui::BeginMenu("Export to")) {
                if (ImGui::MenuItem("GLB")) {
                    ISO::mdl_export_begin_named(
                        ISO::MdlExportFormat::GLB,
                        bnk_path, file_index, file_name, is_nested);
                }
                if (ImGui::MenuItem("FBX")) {
                    ISO::mdl_export_begin_named(
                        ISO::MdlExportFormat::FBX,
                        bnk_path, file_index, file_name, is_nested);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(".mdl (raw)")) {
                    ISO::mdl_export_begin_named(
                        ISO::MdlExportFormat::RAW,
                        bnk_path, file_index, file_name, is_nested);
                }
                ImGui::EndMenu();
            }
        } else if (is_audio_file(file_name)) {

            if (ImGui::BeginMenu("Export to")) {
                if (ImGui::MenuItem("MP3")) {
                    asset_export_audio_encoded(bnk_path, file_index,
                                               file_name, false);
                }
                if (ImGui::MenuItem("M4A")) {
                    asset_export_audio_encoded(bnk_path, file_index,
                                               file_name, true);
                }
                if (ImGui::MenuItem("WAV")) {
                    asset_export_to_export_dir(bnk_path, file_index,
                                               is_nested, file_name,
                                               true);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(".xma (raw)")) {
                    asset_export_audio_raw_xma(bnk_path, file_index,
                                               file_name);
                }
                ImGui::EndMenu();
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
