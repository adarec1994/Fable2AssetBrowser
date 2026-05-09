#include "UI_Panels.h"
#include "../Utilities/State.h"
#include "../Utilities/Utils.h"
#include "../Utilities/operations.h"
#include "../ISO/IsoMount.h"
#include "../UI/HexView.h"
#include "UI_Main.h"
#include "../TexParser.h"
#include "../LhTexCodec.h"
#include "../MDL/ModelParser.h"
#include "ModelPreview.h"
#include "../MDL/mdl_converter.h"
#include "../BNKCore.cpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "ImGuiFileDialog.h"
#include <filesystem>
#include <algorithm>
#include <thread>
#include <atomic>
#include <cstring>
#include "../Utilities/Progress.h"
#include "../Utilities/Files.h"
#include "../Lua.h"
#include "AudioPlayerWindow.h"

#ifdef _WIN32
#include <d3d11.h>
#include <shellapi.h>
#else
#include <GL/glew.h>
#endif

void refresh_file_table() { S.selected_file_index = -1; }

// Extract the selected file's raw bytes (going through the user's currently-
// selected BNK or nested-BNK temp copy) and open the in-app audio player on
// them. Returns true if the player accepted the data.
static bool open_audio_player_for_selected(int file_index) {
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

// Forward decl — defined further down (after the TreeNode declaration).
// Kicks the file-tree build in a worker thread so it's already in
// progress (or done) by the time the user navigates to the File Tree tab.
extern void start_tree_build_for_root(const std::string& root_dir,
                                      std::vector<std::string> bnk_paths);

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

static bool reconstruct_nested_mdl(const std::string& nested_bnk_path, int file_index, std::vector<unsigned char>& out) {
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

static bool is_in_audio_folder(const std::string& path) {
    std::string lower_path = path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);
    return lower_path.find("/audio/") != std::string::npos ||
           lower_path.find("\\audio\\") != std::string::npos;
}

bool can_tex = false, can_mdl = false;

static std::vector<GlobalHit> g_global_hits;
static std::atomic<bool> g_global_busy(false);
static std::atomic<bool> g_cancel_search(false);
static std::string g_last_global_search;
static int g_selected_global = -1;

static std::atomic<bool> g_pending_mdl_load{false};
static int g_pending_mdl_index = -1;
static std::string g_pending_mdl_full_path;

static std::atomic<bool> g_pending_tex_load{false};
static int g_pending_tex_index = -1;

struct TreeNode {
    std::string name;
    bool is_file;
    bool is_nested_source = false;
    std::string full_path;
    std::string bnk_source;
    int bnk_index;
    uint32_t file_size;
    std::map<std::string, TreeNode> children;
};

static TreeNode g_tree_root;

// Defined later in this file — forward-decl so the eager-start helper
// below can reference it.
static void build_unified_file_tree(TreeNode& root, std::vector<std::string> bnk_paths);

// File-tree build state. Used to be function-static inside draw_left_panel
// (= the build only kicked off when the user opened the File Tree tab).
// Now hoisted so we can start the build IMMEDIATELY after open_folder_logic
// / open_iso_logic — by the time the user clicks into the tab the work is
// usually already done.
static std::atomic<bool> g_tree_built{false};
static std::atomic<bool> g_tree_building{false};
static std::atomic<bool> g_tree_build_complete{false};
static float             g_tree_build_start_time = 0.0f;
static std::string       g_tree_last_root_dir;

// Progress reporting from inside the build thread, used by the loading
// screen to draw a real percentage instead of an indeterminate stripe.
// `total` and `done` are both incremented as work is discovered (phase 2
// nested BNKs aren't known until phase 1 finishes), so progress can
// briefly stay below 100% near the end as new work is added.
static std::atomic<int>  g_tree_done_units{0};
static std::atomic<int>  g_tree_total_units{0};
static std::mutex        g_tree_label_mutex;
static std::string       g_tree_current_label;
static void set_tree_label(std::string s) {
    std::lock_guard<std::mutex> lk(g_tree_label_mutex);
    g_tree_current_label = std::move(s);
}
extern void  start_tree_build_for_root(const std::string& root_dir,
                                       std::vector<std::string> bnk_paths);
// Public accessors — exposed in UI_Panels.h so the LoadingScreen module
// can react to build state without taking a hard dependency on these
// file-private atomics.
bool tree_build_in_progress() { return g_tree_building.load(); }
bool tree_build_finished()    { return g_tree_built.load(); }
float tree_build_elapsed_seconds() {
    if (g_tree_build_start_time <= 0.0f) return 0.0f;
    return (float)ImGui::GetTime() - g_tree_build_start_time;
}
int  tree_build_done_units()  { return g_tree_done_units.load(); }
int  tree_build_total_units() { return g_tree_total_units.load(); }
float tree_build_progress() {
    int total = g_tree_total_units.load();
    if (total <= 0) return 0.0f;
    int done = g_tree_done_units.load();
    if (done > total) done = total;
    return (float)done / (float)total;
}
std::string tree_build_current_label() {
    std::lock_guard<std::mutex> lk(g_tree_label_mutex);
    return g_tree_current_label;
}

void start_tree_build_for_root(const std::string& root_dir,
                               std::vector<std::string> bnk_paths) {
    if (root_dir == g_tree_last_root_dir && (g_tree_built.load() || g_tree_building.load()))
        return;
    g_tree_last_root_dir   = root_dir;
    g_tree_built.store(false);
    g_tree_building.store(true);
    g_tree_build_complete.store(false);
    g_tree_build_start_time = (float)ImGui::GetTime();
    g_tree_root.children.clear();
    g_tree_done_units.store(0);
    // Phase 1 only — phase 2's nested-BNK count is added once phase 1
    // discovers them. Total grows during the run; the loading bar handles
    // that gracefully because progress is computed as done/total each frame.
    g_tree_total_units.store((int)bnk_paths.size());
    set_tree_label("");
    // Drop any nested-BNK temp-paths from the previous root so we don't
    // search stale paths (the temp files may have been deleted, and even
    // if not they belong to a different game install).
    S.nested_bnk_paths.clear();
    S.nested_bnk_parents.clear();

    std::thread([bnk_snapshot = std::move(bnk_paths)]() mutable {
        try {
            build_unified_file_tree(g_tree_root, std::move(bnk_snapshot));
        } catch (...) { /* swallow — UI shows empty tree on failure */ }
        // Flip the visible-state flags directly from the worker. This used
        // to be a UI-thread promotion buried inside the File Tree tab body,
        // which meant the loading screen (drawn instead of the tab while
        // the build was in flight) would stall at 100% forever — the tab
        // never ran, so the flags never flipped.
        g_tree_build_complete.store(true);
        g_tree_built.store(true);
        g_tree_building.store(false);
    }).detach();
}

static bool find_mdl_files_in_folder(TreeNode& root, const std::string& folder_name, std::vector<std::pair<std::string, std::string>>& out_mdl_paths) {
    out_mdl_paths.clear();

    std::function<TreeNode*(TreeNode&, const std::string&)> find_folder = [&](TreeNode& node, const std::string& name) -> TreeNode* {
        if (!node.is_file && node.name == name) {
            return &node;
        }
        for (auto& pair : node.children) {
            if (!pair.second.is_file) {
                TreeNode* result = find_folder(pair.second, name);
                if (result) return result;
            }
        }
        return nullptr;
    };

    TreeNode* folder = find_folder(root, folder_name);
    if (!folder) return false;

    for (auto& pair : folder->children) {
        if (pair.second.is_file) {
            std::string fname = pair.first;
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
            if (fname == "interior.mdl" || fname == "exterior.mdl") {
                out_mdl_paths.push_back({pair.second.full_path, pair.second.bnk_source});
            }
        }
    }

    return !out_mdl_paths.empty();
}

static void build_unified_file_tree(TreeNode& root, std::vector<std::string> bnk_paths) {
    root.children.clear();
    // Reset flat caches consumed by the Models / Textures / Audio tabs.
    // They get appended to inside add_to_tree below as we walk every BNK.
    S.all_mdl_files.clear();
    S.all_tex_files.clear();
    S.all_wav_files.clear();

    auto is_header_bnk = [](const std::string& bnk_path) -> bool {
        std::string lower_path = bnk_path;
        std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);
        std::string filename = std::filesystem::path(lower_path).filename().string();
        return filename.find("header") != std::string::npos;
    };

    auto is_nested_bnk = [](const std::string& filename) -> bool {
        std::string lower = filename;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        return lower.size() >= 4 && lower.substr(lower.size() - 4) == ".bnk";
    };

    // Match by filename suffix — case insensitive. Cheaper than building
    // a temporary lowered string just to compare 4 chars.
    auto ends_with_ci = [](const std::string& s, const char* suffix) -> bool {
        size_t n = std::strlen(suffix);
        if (s.size() < n) return false;
        for (size_t i = 0; i < n; ++i) {
            char a = s[s.size() - n + i];
            char b = suffix[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) return false;
        }
        return true;
    };

    auto add_to_tree = [&root, &ends_with_ci](const std::string& path, const std::string& bnk_source,
                               int bnk_index, uint32_t file_size, bool is_nested = false) {
        // Bookkeeping for the Models / Textures tabs. Use the leaf
        // filename for display so the user isn't seeing internal asset
        // paths cluttering the list.
        std::string leaf;
        size_t slash = path.find_last_of("/\\");
        leaf = (slash == std::string::npos) ? path : path.substr(slash + 1);
        if (ends_with_ci(leaf, ".mdl")) {
            FlatAssetEntry e;
            e.name = leaf;
            e.full_path = path;
            e.bnk_path = bnk_source;
            e.file_index = bnk_index;
            e.size = file_size;
            e.from_nested = is_nested;
            S.all_mdl_files.push_back(std::move(e));
        } else if (ends_with_ci(leaf, ".tex")) {
            FlatAssetEntry e;
            e.name = leaf;
            e.full_path = path;
            e.bnk_path = bnk_source;
            e.file_index = bnk_index;
            e.size = file_size;
            e.from_nested = is_nested;
            S.all_tex_files.push_back(std::move(e));
        } else if (ends_with_ci(leaf, ".wav")) {
            FlatAssetEntry e;
            e.name = leaf;
            e.full_path = path;
            e.bnk_path = bnk_source;
            e.file_index = bnk_index;
            e.size = file_size;
            e.from_nested = is_nested;
            S.all_wav_files.push_back(std::move(e));
        }

        std::string normalized_path = path;
        std::replace(normalized_path.begin(), normalized_path.end(), '\\', '/');

        std::vector<std::string> parts;
        size_t start = 0;
        size_t end = normalized_path.find('/');

        while (end != std::string::npos) {
            parts.push_back(normalized_path.substr(start, end - start));
            start = end + 1;
            end = normalized_path.find('/', start);
        }
        parts.push_back(normalized_path.substr(start));

        TreeNode* current = &root;
        for (size_t j = 0; j < parts.size(); ++j) {
            const std::string& part = parts[j];
            if (part.empty()) continue;

            bool is_last = (j == parts.size() - 1);

            TreeNode& child = current->children[part];
            child.name = part;
            child.is_file = is_last;

            if (is_last) {
                child.full_path = path;
                child.bnk_source = bnk_source;
                child.bnk_index = bnk_index;
                child.file_size = file_size;
                child.is_nested_source = is_nested;
            }

            current = &child;
        }
    };

    std::vector<std::pair<std::string, int>> nested_bnks;

    // Phase 1: walk the top-level BNKs and add their entries to the tree.
    for (const auto& bnk_path : bnk_paths) {
        set_tree_label(std::filesystem::path(bnk_path).filename().string());
        if (is_header_bnk(bnk_path)) {
            g_tree_done_units.fetch_add(1);
            continue;
        }

        try {
            BNKReader reader(bnk_path);
            const auto& files = reader.list_files();

            for (size_t i = 0; i < files.size(); ++i) {
                const auto& file = files[i];

                add_to_tree(file.name, bnk_path, (int)i, file.uncompressed_size);

                if (is_nested_bnk(file.name)) {
                    nested_bnks.push_back({bnk_path, (int)i});
                }
            }
        } catch (...) {
            // fall through — still increment so progress bar advances
        }
        g_tree_done_units.fetch_add(1);
    }

    // Phase 2 work is now known — fold its count into the total so the
    // progress bar reflects the full job, not just phase 1.
    g_tree_total_units.fetch_add((int)nested_bnks.size());

    auto tmpdir = std::filesystem::temp_directory_path() / "f2_nested_bnk_tree";
    {
        std::error_code ec;
        std::filesystem::create_directories(tmpdir, ec);
    }
    // Reset the lazy-extract registry from any previous run before we
    // populate it for this build.
    LazyNested::clear_all();

    // Group nested entries by their parent BNK so we can open each parent
    // exactly once instead of once per nested entry. Previous code did
    // ~hundreds of redundant BNKReader(parent_path) opens — on ISO that
    // re-read the parent's header off the disc image every time.
    std::unordered_map<std::string, std::vector<int>> nested_by_parent;
    nested_by_parent.reserve(nested_bnks.size());
    for (const auto& [parent_bnk_path, nested_index] : nested_bnks) {
        nested_by_parent[parent_bnk_path].push_back(nested_index);
    }

    for (auto& [parent_bnk_path, indices] : nested_by_parent) {
        // Open the parent once; reuse it for every nested entry below.
        std::optional<BNKReader> parent_reader;
        try {
            parent_reader.emplace(parent_bnk_path);
        } catch (...) {
            // Couldn't open parent — just count its nested entries as done
            // so the progress bar still advances.
            g_tree_done_units.fetch_add((int)indices.size());
            continue;
        }
        const auto& parent_files = parent_reader->list_files();

        for (int nested_index : indices) {
            if (nested_index < 0 || nested_index >= (int)parent_files.size()) {
                g_tree_done_units.fetch_add(1);
                continue;
            }
            const auto& nested_file = parent_files[nested_index];
            std::string nested_path = nested_file.name;
            set_tree_label(std::filesystem::path(parent_bnk_path).filename().string()
                           + " : " + std::filesystem::path(nested_path).filename().string());

            // Stable temp path — same as before so existing call sites that
            // open these paths still work. The actual disk write is now
            // deferred via LazyNested below.
            std::string nested_base = std::filesystem::path(nested_path).filename().string();
            std::string temp_name = std::to_string(std::hash<std::string>{}(parent_bnk_path + nested_path))
                                  + "_" + nested_base;
            auto temp_bnk_path = (tmpdir / temp_name).string();

            try {
                // Pull the nested BNK's bytes straight out of the parent
                // (in-memory). For ISO mode this is one read from the disc
                // image; for disk mode it's one read from the parent file.
                // Either way: zero temp-file I/O during enumeration.
                std::vector<uint8_t> nested_bytes =
                    parent_reader->extract_index_bytes(nested_index);

                BNKReader nested_reader(std::move(nested_bytes));
                const auto& nested_files = nested_reader.list_files();

                // Register the temp path for lazy materialization. If/when
                // anything else tries to OPEN this path (BNKReader ctor or
                // extract_one), it'll get extracted to disk on demand.
                LazyNested::register_pending(temp_bnk_path, parent_bnk_path, nested_index);

                {
                    if (std::find(S.nested_bnk_paths.begin(), S.nested_bnk_paths.end(), temp_bnk_path)
                        == S.nested_bnk_paths.end()) {
                        S.nested_bnk_paths.push_back(temp_bnk_path);
                    }
                    S.nested_bnk_parents[temp_bnk_path] = parent_bnk_path;
                }

                size_t last_slash = nested_path.find_last_of('/');
                std::string prefix = (last_slash == std::string::npos)
                    ? std::string()
                    : nested_path.substr(0, last_slash + 1);

                for (size_t i = 0; i < nested_files.size(); ++i) {
                    std::string full_nested_path = prefix + nested_files[i].name;
                    add_to_tree(full_nested_path, temp_bnk_path,
                                (int)i, nested_files[i].uncompressed_size, true);
                }
            } catch (...) {
                // fall through — still increment so progress bar advances
            }
            g_tree_done_units.fetch_add(1);
        }
    }

    // Alphabetize each flat cache once at the end of the build —
    // case-insensitive so MyAsset.tex and myasset.tex end up adjacent.
    // Doing it here (rather than every frame inside the tab) keeps the
    // per-frame cost of the Models / Textures / Audio tabs down to the
    // filter walk.
    auto cmp_ci = [](const FlatAssetEntry& a, const FlatAssetEntry& b) {
        const std::string& sa = a.name;
        const std::string& sb = b.name;
        size_t n = std::min(sa.size(), sb.size());
        for (size_t i = 0; i < n; ++i) {
            char ca = sa[i], cb = sb[i];
            if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
            if (ca != cb) return ca < cb;
        }
        return sa.size() < sb.size();
    };
    std::sort(S.all_mdl_files.begin(), S.all_mdl_files.end(), cmp_ci);
    std::sort(S.all_tex_files.begin(), S.all_tex_files.end(), cmp_ci);
    std::sort(S.all_wav_files.begin(), S.all_wav_files.end(), cmp_ci);

    set_tree_label("");
}

#ifdef _WIN32
static void draw_tree_node(TreeNode& node, ID3D11Device* device) {
#else
static void draw_tree_node(TreeNode& node) {
#endif
    auto is_mdl = [](const std::string& name) -> bool {
        std::string l = name;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        return l.size() >= 4 && l.rfind(".mdl") == l.size() - 4;
    };
    auto is_tex = [](const std::string& name) -> bool {
        std::string l = name;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        return l.size() >= 4 && l.rfind(".tex") == l.size() - 4;
    };

    if (node.is_file) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        bool selected = false;
        if (!S.viewing_adb && S.selected_bnk == node.bnk_source) {
            for (size_t i = 0; i < S.files.size(); ++i) {
                if (S.files[i].index == node.bnk_index) {
                    selected = (S.selected_file_index == (int)i);
                    break;
                }
            }
        }

        if (selected) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        std::string label = node.name;
        ImGui::TreeNodeEx(label.c_str(), flags);

        if (ImGui::IsItemClicked()) {
            S.selected_folder_path.clear();

            if (S.selected_bnk != node.bnk_source) {
                S.viewing_adb = false;
                S.global_search.clear();
                S.selected_nested_bnk.clear();
                S.selected_nested_index = -1;
                pick_bnk(node.bnk_source);
            }

            // pick_bnk clears selected_nested_temp_path — restore it for nested files
            if (node.is_nested_source) {
                S.selected_nested_temp_path = node.bnk_source;
                S.selected_nested_index = 0;
            }

            for (size_t i = 0; i < S.files.size(); ++i) {
                if (S.files[i].index == node.bnk_index) {
                    S.selected_file_index = (int)i;
                    // Single-click loads .mdl and .tex straight into the
                    // central render panel — no double-click needed.
                    if (is_mdl(node.name)) {
                        g_pending_mdl_full_path = node.full_path;
                        g_pending_mdl_load = true;
                        g_pending_mdl_index = (int)i;
                    }
                    if (is_tex(node.name)) {
                        g_pending_tex_load = true;
                        g_pending_tex_index = (int)i;
                    }
                    // .wav stays double-click → audio player (a separate
                    // floating window, not the central panel).
                    if (is_audio_file(node.name) && ImGui::IsMouseDoubleClicked(0)) {
                        open_audio_player_for_selected((int)i);
                    }
                    break;
                }
            }
        }

        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            // Basic mode: just the filename (last path component) so the
            // tooltip stays unobtrusive. Developer mode adds the extras.
            if (S.dev_mode) {
                ImGui::Text("%s", node.full_path.c_str());
                ImGui::Text("Size: %u bytes", node.file_size);
                ImGui::Text("BNK: %s",
                    std::filesystem::path(node.bnk_source).filename().string().c_str());
            } else {
                ImGui::TextUnformatted(node.name.c_str());
            }
            ImGui::EndTooltip();
        }
    } else {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;

        if (node.children.empty()) {
            flags |= ImGuiTreeNodeFlags_Leaf;
        }

        bool node_open = ImGui::TreeNodeEx(node.name.c_str(), flags);

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            S.selected_file_index = -1;
            S.selected_folder_path = node.name;
        }

        if (node_open) {
            std::vector<std::pair<std::string, TreeNode*>> sorted_children;
            for (auto& pair : node.children) {
                sorted_children.push_back({pair.first, &pair.second});
            }

            std::sort(sorted_children.begin(), sorted_children.end(),
                [](const auto& a, const auto& b) {
                    if (a.second->is_file != b.second->is_file) {
                        return !a.second->is_file;
                    }
                    return a.first < b.first;
                });

            for (auto& pair : sorted_children) {
#ifdef _WIN32
                draw_tree_node(*pair.second, device);
#else
                draw_tree_node(*pair.second);
#endif
            }

            ImGui::TreePop();
        }
    }
}

// Click-to-load handler shared by the Models / Textures tabs. Mirrors the
// equivalent block inside draw_tree_node so a click in either tab gets the
// same end result: switch the active BNK if needed, restore nested-source
// state, locate the matching entry inside S.files, then arm the pending-load
// flag the central render panel consumes.
//
// kind: 0 = .mdl, 1 = .tex, 2 = .wav (audio player)
static void load_flat_asset_entry(const FlatAssetEntry& e, int kind) {
    if (S.selected_bnk != e.bnk_path) {
        S.viewing_adb = false;
        S.global_search.clear();
        S.selected_nested_bnk.clear();
        S.selected_nested_index = -1;
        pick_bnk(e.bnk_path);
    }
    // pick_bnk wipes selected_nested_temp_path — re-establish it for nested
    // sources so subsequent extract paths know which nested BNK to pull from.
    if (e.from_nested) {
        S.selected_nested_temp_path = e.bnk_path;
        S.selected_nested_index = 0;
    }
    for (size_t i = 0; i < S.files.size(); ++i) {
        if (S.files[i].index == e.file_index) {
            S.selected_file_index = (int)i;
            if (kind == 0) {
                g_pending_mdl_full_path = e.full_path;
                g_pending_mdl_load = true;
                g_pending_mdl_index = (int)i;
            } else if (kind == 1) {
                g_pending_tex_load = true;
                g_pending_tex_index = (int)i;
            } else if (kind == 2) {
                // Audio: open the in-app audio player on the selected
                // file. Same code path the file-table double-click uses.
                open_audio_player_for_selected((int)i);
            }
            break;
        }
    }
}

#ifdef _WIN32
void draw_left_panel(ID3D11Device* device) {
#else
void draw_left_panel() {
#endif
    // Fill the parent container — the column width is now decided by the
    // outer MainLayout, not hardcoded here.
    ImGui::BeginChild("left_panel", ImVec2(0, 0), true);

    // ImGui's TabBar can't render across two rows, so we roll our own
    // tab strip out of regular Buttons styled with the Tab/TabActive
    // palette. Selection is exclusive across both rows — clicking a row
    // 2 button visually deselects row 1's active tab and vice versa.
    // s_active_tab values: 0 BNK List, 1 File Tree, 2 Models, 3 Textures,
    // 4 Audio. Default 1 puts the user on File Tree at startup.
    static int s_active_tab = 1;

    auto tab_button = [](const char* label, bool active, ImU32 text_col = 0) -> bool {
        const ImGuiStyle& st = ImGui::GetStyle();
        const ImVec4 bg     = st.Colors[active ? ImGuiCol_TabActive : ImGuiCol_Tab];
        const ImVec4 hov    = st.Colors[ImGuiCol_TabHovered];
        const ImVec4 act    = st.Colors[ImGuiCol_TabActive];
        ImGui::PushStyleColor(ImGuiCol_Button,        bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  act);
        if (text_col) ImGui::PushStyleColor(ImGuiCol_Text, text_col);
        bool clicked = ImGui::Button(label);
        if (text_col) ImGui::PopStyleColor();
        ImGui::PopStyleColor(3);
        return clicked;
    };

    // Row 1 — standard tabs.
    if (tab_button("BNK List", s_active_tab == 0))   s_active_tab = 0;
    ImGui::SameLine(0, 2);
    if (tab_button("File Tree", s_active_tab == 1))  s_active_tab = 1;

    // Row 2 — flat asset views, purple labels to set them apart.
    const ImU32 kPurpleLabel = IM_COL32(200, 130, 255, 255);
    if (tab_button("Models",   s_active_tab == 2, kPurpleLabel)) s_active_tab = 2;
    ImGui::SameLine(0, 2);
    if (tab_button("Textures", s_active_tab == 3, kPurpleLabel)) s_active_tab = 3;
    ImGui::SameLine(0, 2);
    if (tab_button("Audio",    s_active_tab == 4, kPurpleLabel)) s_active_tab = 4;

    ImGui::Separator();

    // Lambda used by the Models / Textures / Audio bodies further down.
    // Defined up here so all three branches can share it without the
    // pre-refactor double-tab-bar plumbing.
    auto draw_flat_asset_tab = [](const char* /*label*/,
                                  std::vector<FlatAssetEntry>& entries,
                                  std::string& filter,
                                  const char* child_id,
                                  int kind) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint(("##" + std::string(child_id) + "_filter").c_str(),
                                 "Filter", &filter);

        std::vector<int> vis;
        vis.reserve(entries.size());
        std::string flow = filter;
        std::transform(flow.begin(), flow.end(), flow.begin(), ::tolower);
        for (size_t i = 0; i < entries.size(); ++i) {
            if (flow.empty()) {
                vis.push_back((int)i);
            } else {
                std::string nlow = entries[i].name;
                std::transform(nlow.begin(), nlow.end(), nlow.begin(), ::tolower);
                if (nlow.find(flow) != std::string::npos) vis.push_back((int)i);
            }
        }

        if (S.dev_mode) {
            ImGui::TextDisabled("%d / %zu", (int)vis.size(), entries.size());
            ImGui::Separator();
        }

        ImGui::BeginChild(child_id, ImVec2(0, 0), false);
        ImGuiListClipper clipper;
        clipper.Begin((int)vis.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const FlatAssetEntry& e = entries[(size_t)vis[(size_t)row]];
                ImGui::PushID(row);
                bool selected = (S.selected_bnk == e.bnk_path &&
                                 S.selected_file_index >= 0 &&
                                 S.selected_file_index < (int)S.files.size() &&
                                 S.files[(size_t)S.selected_file_index].index == e.file_index);
                if (ImGui::Selectable(e.name.c_str(), selected,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    load_flat_asset_entry(e, kind);
                }
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    if (S.dev_mode) {
                        ImGui::TextUnformatted(e.full_path.c_str());
                        ImGui::Text("Size: %u bytes", e.size);
                        ImGui::Text("BNK: %s",
                            std::filesystem::path(e.bnk_path).filename().string().c_str());
                        if (e.from_nested) ImGui::TextDisabled("(nested)");
                    } else {
                        ImGui::TextUnformatted(e.name.c_str());
                    }
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
        }
        clipper.End();
        ImGui::EndChild();
    };

    if (s_active_tab == 0) {
            ImGui::SetNextItemWidth(-1);
            if (!S.bnk_paths.empty()) {
                ImGui::InputTextWithHint("##bnk_filter", "Filter", &S.bnk_filter);
            }
            ImGui::BeginChild("bnk_list", ImVec2(0, 0), false);

            auto paths = filtered_bnk_paths();

            if (!S.adb_paths.empty()) {
                ImGui::PushID("adb_entry");
                bool selected = S.viewing_adb;
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                if (ImGui::Selectable("Audio Database", selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    S.viewing_adb = true;
                    S.viewing_lua = false;
                    S.selected_bnk.clear();
                    S.global_search.clear();
                    S.files.clear();
                    S.selected_file_index = -1;

                    for (size_t i = 0; i < S.adb_paths.size(); ++i) {
                        std::string fname = S.adb_paths[i];
                        std::error_code ec;
                        auto fsize = std::filesystem::file_size(fname, ec);
                        uint32_t size = ec ? 0 : (uint32_t)fsize;
                        S.files.push_back({(int)i, fname, size});
                    }
                }
                ImGui::PopStyleColor();
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Audio Database Files (%d)", (int)S.adb_paths.size());
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }

            if (!S.lua_files.empty()) {
                ImGui::PushID("lua_entry");
                bool selected = S.viewing_lua;
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
                if (ImGui::Selectable("Lua Scripts", selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    S.viewing_lua = true;
                    S.viewing_adb = false;
                    S.selected_bnk.clear();
                    S.global_search.clear();
                    S.files.clear();
                    S.selected_file_index = -1;
                    S.lua_preview_content.clear();
                    S.lua_preview_title.clear();
                    S.lua_preview_selected = -1;

                    for (size_t i = 0; i < S.lua_files.size(); ++i) {
                        S.files.push_back({(int)i, S.lua_files[i].filename, S.lua_files[i].size});
                    }
                }
                ImGui::PopStyleColor();
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Lua Script Files (%d)", (int)S.lua_files.size());
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }

            // Cache of nested-BNK file-table reads, keyed by parent BNK
            // path. Without this we'd open the parent and read its file
            // table every frame an expanded BNK is on screen — fine on
            // disk, but very expensive on ISO (each list_files() goes
            // through the disc reader).
            struct NestedChild { int index; std::string name; };
            static std::unordered_map<std::string, std::vector<NestedChild>> s_nested_cache;
            // Drop the cache whenever the root changed (different folder/
            // ISO selected); g_tree_last_root_dir is the canonical signal.
            static std::string s_nested_cache_root;
            if (s_nested_cache_root != S.root_dir) {
                s_nested_cache.clear();
                s_nested_cache_root = S.root_dir;
            }

            // Build a flat row list so we can virtualize via ImGuiListClipper.
            // Each row is either a top-level BNK or a nested child shown
            // because its parent is expanded. Building this is O(N) over
            // visible BNKs only — no per-frame disc reads thanks to the
            // cache.
            struct Row {
                int kind;            // 0 = top-level, 1 = nested child
                int top_idx;         // into `paths`
                int nested_idx;      // BNKReader index inside parent (kind 1)
                std::string nested_name; // nested file name (kind 1)
            };
            std::vector<Row> rows;
            rows.reserve(paths.size() + 64);
            for (size_t idx = 0; idx < paths.size(); ++idx) {
                rows.push_back({0, (int)idx, -1, {}});

                const auto& p = paths[idx];
                std::string label = std::filesystem::path(p).filename().string();
                std::string label_lower = label;
                std::transform(label_lower.begin(), label_lower.end(), label_lower.begin(), ::tolower);
                bool is_nested_bnk = (label_lower == "levels.bnk" || label_lower == "streaming.bnk");
                bool is_expanded = S.expanded_bnks.count(p) > 0;

                if (is_nested_bnk && is_expanded) {
                    auto it_cache = s_nested_cache.find(p);
                    if (it_cache == s_nested_cache.end()) {
                        std::vector<NestedChild> children;
                        try {
                            BNKReader reader(p);
                            const auto& files = reader.list_files();
                            for (size_t i = 0; i < files.size(); ++i) {
                                std::string fname_lower = files[i].name;
                                std::transform(fname_lower.begin(), fname_lower.end(), fname_lower.begin(), ::tolower);
                                if (fname_lower.size() >= 4 && fname_lower.substr(fname_lower.size() - 4) == ".bnk") {
                                    children.push_back({(int)i, files[i].name});
                                }
                            }
                        } catch (...) {}
                        it_cache = s_nested_cache.emplace(p, std::move(children)).first;
                    }
                    for (const auto& c : it_cache->second) {
                        rows.push_back({1, (int)idx, c.index, c.name});
                    }
                }
            }

            ImGuiListClipper clipper;
            clipper.Begin((int)rows.size());
            while (clipper.Step()) {
                for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                    const Row& row = rows[(size_t)r];
                    if (row.kind == 0) {
                        const auto& p = paths[(size_t)row.top_idx];
                        ImGui::PushID(r);

                        std::string label = std::filesystem::path(p).filename().string();
                        std::string label_lower = label;
                        std::transform(label_lower.begin(), label_lower.end(), label_lower.begin(), ::tolower);
                        bool is_nested_bnk = (label_lower == "levels.bnk" || label_lower == "streaming.bnk");
                        bool is_expanded = S.expanded_bnks.count(p) > 0;
                        if (is_nested_bnk) {
                            label = (is_expanded ? "- " : "+ ") + label;
                        }

                        bool selected = (p == S.selected_bnk && !S.viewing_adb && S.selected_nested_index == -1);
                        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                            if (is_nested_bnk) {
                                if (is_expanded) S.expanded_bnks.erase(p);
                                else             S.expanded_bnks.insert(p);
                            }
                            S.viewing_adb = false;
                            S.viewing_lua = false;
                            S.global_search.clear();
                            S.selected_nested_bnk.clear();
                            S.selected_nested_index = -1;
                            pick_bnk(p);
                        }
                        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(p.c_str());
                            ImGui::EndTooltip();
                        }
                        ImGui::PopID();
                    } else {
                        const auto& p = paths[(size_t)row.top_idx];
                        const std::string& nested_name = row.nested_name;
                        int nested_idx = row.nested_idx;

                        ImGui::PushID(r);
                        std::string nested_label = "    " + std::filesystem::path(nested_name).filename().string();
                        bool nested_selected = (S.selected_nested_bnk == p && S.selected_nested_index == nested_idx);
                        if (ImGui::Selectable(nested_label.c_str(), nested_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                            S.viewing_adb = false;
                            S.viewing_lua = false;
                            S.selected_bnk = p;
                            S.selected_nested_bnk = p;
                            S.selected_nested_index = nested_idx;
                            S.global_search.clear();
                            S.files.clear();
                            S.selected_file_index = -1;

                            auto tmpdir = std::filesystem::temp_directory_path() / "f2_nested_bnk";
                            std::error_code ec;
                            std::filesystem::create_directories(tmpdir, ec);
                            auto tmp_nested = tmpdir / (std::to_string(std::hash<std::string>{}(nested_name)) + ".bnk");

                            extract_one(p, nested_idx, tmp_nested.string());
                            S.selected_nested_temp_path = tmp_nested.string();

                            BNKReader nested_reader(tmp_nested.string());
                            const auto& nested_files = nested_reader.list_files();
                            S.files.reserve(nested_files.size());
                            for (size_t j = 0; j < nested_files.size(); ++j) {
                                S.files.push_back({(int)j, nested_files[j].name, nested_files[j].uncompressed_size});
                            }
                            std::sort(S.files.begin(), S.files.end(), [](const BNKItemUI& a, const BNKItemUI& b) {
                                std::string x = std::filesystem::path(a.name).filename().string();
                                std::string y = std::filesystem::path(b.name).filename().string();
                                std::transform(x.begin(), x.end(), x.begin(), ::tolower);
                                std::transform(y.begin(), y.end(), y.begin(), ::tolower);
                                return x < y;
                            });
                        }
                        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(nested_name.c_str());
                            ImGui::EndTooltip();
                        }
                        ImGui::PopID();
                    }
                }
            }
            clipper.End();
            ImGui::EndChild();
        }

        if (s_active_tab == 1) {
            ImGui::BeginChild("file_tree", ImVec2(0, 0), false);

            // Tree build state was hoisted to file scope so we could
            // start the work right after open_folder_logic. Here we just
            // observe and render from those globals.
            //
            // (The "promote build_complete -> built/!building" step that
            // used to live here moved into the worker thread — see
            // start_tree_build_for_root. Doing it here meant the loading
            // screen could stall at 100% because this tab body wasn't
            // being drawn while the loading screen was up.)

            // If the user re-opened a folder/iso and a build hasn't been
            // kicked yet for some reason, kick one now (lazy fallback).
            if (g_tree_last_root_dir != S.root_dir && !S.bnk_paths.empty()
                && !g_tree_building.load() && !g_tree_built.load())
            {
                start_tree_build_for_root(S.root_dir, S.bnk_paths);
            }

            // Use the global tree root that the build thread populated.
            TreeNode& tree_render_root = g_tree_root;

            if (g_tree_building.load()) {
                {
                    ImVec2 avail = ImGui::GetContentRegionAvail();
                    float elapsed = (float)ImGui::GetTime() - g_tree_build_start_time;

                    float dot_cycle = fmodf(elapsed * 2.0f, 4.0f);
                    int dot_count = (int)dot_cycle;
                    std::string dots(dot_count, '.');
                    std::string loading_text = "Loading file tree" + dots;

                    ImVec2 text_size = ImGui::CalcTextSize(loading_text.c_str());
                    ImVec2 pos((avail.x - text_size.x) * 0.5f, (avail.y - text_size.y) * 0.5f);
                    if (pos.x < 0) pos.x = 0;
                    if (pos.y < 0) pos.y = 0;
                    ImGui::SetCursorPos(pos);
                    ImGui::TextUnformatted(loading_text.c_str());

                    if (elapsed > 10.0f) {
                        ImVec2 warning_size = ImGui::CalcTextSize("(this may take some time)");
                        ImVec2 warning_pos((avail.x - warning_size.x) * 0.5f, pos.y + text_size.y + 10.0f);
                        if (warning_pos.x < 0) warning_pos.x = 0;
                        ImGui::SetCursorPos(warning_pos);
                        ImGui::TextUnformatted("(this may take some time)");
                    }
                }
            } else if (g_tree_built.load()) {
                for (auto& pair : tree_render_root.children) {
#ifdef _WIN32
                    draw_tree_node(pair.second, device);
#else
                    draw_tree_node(pair.second);
#endif
                }
            }

            ImGui::EndChild();
        }

        if (s_active_tab == 2) {
            draw_flat_asset_tab("Models", S.all_mdl_files, S.mdl_filter,
                                "models_list", /*kind=*/0);
        }
        if (s_active_tab == 3) {
            draw_flat_asset_tab("Textures", S.all_tex_files, S.tex_filter,
                                "textures_list", /*kind=*/1);
        }
        if (s_active_tab == 4) {
            draw_flat_asset_tab("Audio", S.all_wav_files, S.wav_filter,
                                "audio_list", /*kind=*/2);
        }

    ImGui::EndChild(); // left_panel
}

void draw_file_table() {
    std::vector<int> vis;
    vis.reserve(S.files.size());
    for (size_t i = 0; i < S.files.size(); ++i)
        if (name_matches_filter(S.files[i].name, S.file_filter) &&
            name_matches_ext(S.files[i].name, S.ext_filter)) vis.push_back((int) i);

    auto get_filename = [](const std::string& path) -> std::string {
        size_t pos = path.find_last_of("/\\");
        if (pos != std::string::npos) return path.substr(pos + 1);
        return path;
    };

    auto is_mdl = [](const std::string& name) -> bool {
        std::string l = name;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        return l.size() >= 4 && l.rfind(".mdl") == l.size() - 4;
    };

    auto is_tex = [](const std::string& name) -> bool {
        std::string l = name;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        return l.size() >= 4 && l.rfind(".tex") == l.size() - 4;
    };

    ImGuiTable *tbl_ptr = nullptr;
    if (ImGui::BeginTable("files_table", 2,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter |
                          ImGuiTableFlags_SizingStretchProp)) {
        tbl_ptr = ImGui::GetCurrentTable();
        ImGui::TableSetupColumn("File");
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((int) vis.size());
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                int i = vis[r];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                bool selected = (i == S.selected_file_index);
                std::string base = get_filename(S.files[i].name);
                if (ImGui::Selectable(base.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    S.selected_file_index = i;
                    if (is_mdl(S.files[i].name) && !S.viewing_adb && !S.viewing_lua) {
                        g_pending_mdl_load = true;
                        g_pending_mdl_index = i;
                    }
                    if (ImGui::IsMouseDoubleClicked(0) && is_tex(S.files[i].name)) {
                        g_pending_tex_load = true;
                        g_pending_tex_index = i;
                    }
                    // Double-click a .wav -> open the in-app audio player.
                    if (ImGui::IsMouseDoubleClicked(0) && is_audio_file(S.files[i].name)
                        && !S.viewing_adb && !S.viewing_lua) {
                        open_audio_player_for_selected(i);
                    }
                }
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(S.files[i].name.c_str());
                    ImGui::EndTooltip();
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%u", S.files[i].size);
                ImGui::PopID();
            }
        }
        clipper.End();
        ImGui::EndTable();
    }
    if (tbl_ptr) {
        ImRect r = tbl_ptr->OuterRect;
        ImU32 col = ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Border));
        ImGui::GetWindowDrawList()->AddRect(r.Min, r.Max, col, 8.0f, 0, 1.0f);
    }
}

void draw_global_results_table() {
    if (g_global_busy) {
        ImGui::TextUnformatted("Searching all BNKs...");
        return;
    }

    auto get_filename = [](const std::string& path) -> std::string {
        size_t pos = path.find_last_of("/\\");
        if (pos != std::string::npos) return path.substr(pos + 1);
        return path;
    };

    auto is_mdl = [](const std::string& name) -> bool {
        std::string l = name;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        return l.size() >= 4 && l.rfind(".mdl") == l.size() - 4;
    };

    std::vector<int> vis;
    vis.reserve(g_global_hits.size());
    for (size_t i = 0; i < g_global_hits.size(); ++i) {
        if (name_matches_filter(g_global_hits[i].file_name, S.file_filter) &&
            name_matches_ext(g_global_hits[i].file_name, S.ext_filter)) {
            vis.push_back((int)i);
        }
    }

    ImGuiTable *tbl_ptr = nullptr;
    if (ImGui::BeginTable("global_results_table", 3,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter |
                          ImGuiTableFlags_SizingStretchProp)) {
        tbl_ptr = ImGui::GetCurrentTable();
        ImGui::TableSetupColumn("File");
        ImGui::TableSetupColumn("BNK", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((int)vis.size());
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                int i = vis[r];
                const auto& hit = g_global_hits[i];

                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);

                bool selected = (i == g_selected_global);
                std::string base = get_filename(hit.file_name);

                if (ImGui::Selectable(base.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    g_selected_global = i;
                    S.viewing_adb = false;
                    S.viewing_lua = false;
                    pick_bnk(hit.bnk_path);
                    for (size_t j = 0; j < S.files.size(); ++j) {
                        if (S.files[j].index == hit.index) {
                            S.selected_file_index = (int)j;
                            if (is_mdl(hit.file_name)) {
                                g_pending_mdl_load = true;
                                g_pending_mdl_index = (int)j;
                            }
                            break;
                        }
                    }
                }

                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(hit.file_name.c_str());
                    ImGui::EndTooltip();
                }

                ImGui::TableSetColumnIndex(1);
                std::string bnk_name = get_filename(hit.bnk_path);
                ImGui::TextUnformatted(bnk_name.c_str());

                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(hit.bnk_path.c_str());
                    ImGui::EndTooltip();
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u", hit.size);
                ImGui::PopID();
            }
        }
        clipper.End();
        ImGui::EndTable();
    }
    if (tbl_ptr) {
        ImRect r = tbl_ptr->OuterRect;
        ImU32 col = ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Border));
        ImGui::GetWindowDrawList()->AddRect(r.Min, r.Max, col, 8.0f, 0, 1.0f);
    }
}

void draw_folder_dialog() {
    ImVec2 vp = ImGui::GetMainViewport()->WorkSize;
    ImVec2 minSize(680, 440);
    ImVec2 maxSize(vp.x * 0.9f, vp.y * 0.9f);
    if (ImGuiFileDialog::Instance()->Display("PickDir", ImGuiWindowFlags_NoCollapse, minSize, maxSize)) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string sel = ImGuiFileDialog::Instance()->GetCurrentPath();
            open_folder_logic(sel);
        }
        ImGuiFileDialog::Instance()->Close();
    }
}

#ifdef _WIN32
void draw_right_panel(ID3D11Device* device) {
#else
void draw_right_panel() {
#endif
    ImGui::BeginChild("right_panel", ImVec2(0, 0), false);

    ImGui::BeginChild("extract_box", ImVec2(0, 100), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginGroup();
    ImGui::PushItemWidth(-1);

    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 0));

    ImGui::BeginGroup();

    if (!S.viewing_adb && !S.viewing_lua) {
        if (ImGui::Button("Dump All Files")) {
            ImGui::OpenPopup("progress_win");
            if (!S.global_search.empty()) {
                on_dump_all_global(g_global_hits);
            } else {
                on_dump_all_raw();
            }
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            if (!S.global_search.empty()) {
                ImGui::TextUnformatted("DUMPS ALL FILTERED GLOBAL RESULTS");
            } else {
                ImGui::TextUnformatted("DUMPS ALL FILES IN THE CURRENT BANK");
            }
            ImGui::EndTooltip();
        }

        ImGui::SameLine();
        bool has_selection = (S.selected_file_index >= 0 && S.selected_file_index < (int)S.files.size());
        if (!has_selection) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Dump File")) {
            ImGui::OpenPopup("progress_win");
            on_extract_selected_raw();
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Dump the selected file raw");
            ImGui::EndTooltip();
        }
        if (!has_selection) {
            ImGui::EndDisabled();
        }

        bool has_wav_files = false;
        if (!S.global_search.empty()) {
            for (const auto& h : g_global_hits) {
                if (is_audio_file(h.file_name)) {
                    has_wav_files = true;
                    break;
                }
            }
        } else {
            has_wav_files = any_wav_in_bnk();
        }

        ImGui::SameLine();
        if (has_wav_files) {
            if (ImGui::Button("Export WAV's")) {
                ImGui::OpenPopup("progress_win");
                if (!S.global_search.empty()) {
                    on_export_wavs_global(g_global_hits);
                } else {
                    on_export_wavs();
                }
            }
        }
        if (has_wav_files && !S.hide_tooltips && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Convert and export only the .wav files");
            ImGui::EndTooltip();
        }

        bool has_tex_files = false;
        if (!S.global_search.empty()) {
            for (const auto& h : g_global_hits) {
                if (is_tex_file(h.file_name)) {
                    has_tex_files = true;
                    break;
                }
            }
        } else {
            has_tex_files = is_texture_bnk_selected() && any_tex_in_bnk();
        }

        if (has_tex_files) {
            ImGui::SameLine();
            if (ImGui::Button("Rebuild and Extract All (.tex)")) {
                ImGui::OpenPopup("progress_win");
                if (!S.global_search.empty()) {
                    on_rebuild_and_extract_global_tex(g_global_hits);
                } else {
                    on_rebuild_and_extract();
                }
            }
            if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("Rebuilds every .tex file bitstream");
                ImGui::EndTooltip();
            }
        }

        bool has_mdl_files = false;
        if (!S.global_search.empty()) {
            for (const auto& h : g_global_hits) {
                if (is_mdl_file(h.file_name)) {
                    has_mdl_files = true;
                    break;
                }
            }
        } else {
            has_mdl_files = is_model_bnk_selected() && any_mdl_in_bnk();
        }

        if (has_mdl_files) {
            ImGui::SameLine();
            if (ImGui::Button("Rebuild and Extract All (.mdl)")) {
                ImGui::OpenPopup("progress_win");
                if (!S.global_search.empty()) {
                    on_rebuild_and_extract_global_mdl(g_global_hits);
                } else {
                    on_rebuild_and_extract_models();
                }
            }
            if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("Rebuilds every .mdl file bitstream");
                ImGui::EndTooltip();
            }
        }

        if (S.selected_file_index >= 0) {
            bool can_wav = false;
            if (S.selected_file_index >= 0 && S.selected_file_index < (int) S.files.size()) {
                std::string n = S.files[(size_t) S.selected_file_index].name;
                std::string l = n;
                std::transform(l.begin(), l.end(), l.begin(), ::tolower);
                can_wav = l.size() >= 4 && l.rfind(".wav") == l.size() - 4;
            }
            if (can_wav) {
                ImGui::SameLine();
                if (ImGui::Button("Play")) {
                    open_audio_player_for_selected(S.selected_file_index);
                }
                ImGui::SameLine();
                if (ImGui::Button("Extract WAV")) {
                    ImGui::OpenPopup("progress_win");
                    on_extract_selected_wav();
                }
            }
            bool can_tex = false, can_mdl = false;
            if (S.selected_file_index >= 0 && S.selected_file_index < (int) S.files.size()) {
                std::string n = S.files[(size_t) S.selected_file_index].name;
                std::string l = n;
                std::transform(l.begin(), l.end(), l.begin(), ::tolower);
                can_tex = l.size() >= 4 && l.rfind(".tex") == l.size() - 4;
                can_mdl = l.size() >= 4 && l.rfind(".mdl") == l.size() - 4;
            }

            if (can_tex && is_texture_bnk_selected()) {
                ImGui::SameLine();
                if (ImGui::Button("Rebuild and Extract (.tex)")) {
                    auto name = S.files[(size_t) S.selected_file_index].name;
                    ImGui::OpenPopup("progress_win");
                    on_rebuild_and_extract_one(name);
                }
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted("Rebuilds the .tex file bitstreams");
                    ImGui::EndTooltip();
                }
            }

            if (can_mdl && is_model_bnk_selected()) {
                ImGui::SameLine();
                if (ImGui::Button("Rebuild and Extract (.mdl)")) {
                    auto name = S.files[(size_t) S.selected_file_index].name;
                    ImGui::OpenPopup("progress_win");
                    on_rebuild_and_extract_one_mdl(name);
                }
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted("Rebuilds the .mdl file bitstreams");
                    ImGui::EndTooltip();
                }
            }
        }
    } else if (S.viewing_adb) {
        if (ImGui::Button("Extract All Uncompressed")) {
            ImGui::OpenPopup("progress_win");
            on_extract_all_adb();
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Extract all ADB files uncompressed to /extracted/audio_database/");
            ImGui::EndTooltip();
        }

        ImGui::SameLine();
        bool has_selection = (S.selected_file_index >= 0 && S.selected_file_index < (int)S.files.size());
        if (!has_selection) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Extract Uncompressed")) {
            ImGui::OpenPopup("progress_win");
            on_extract_adb_selected();
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Extract selected ADB file uncompressed");
            ImGui::EndTooltip();
        }
        if (!has_selection) {
            ImGui::EndDisabled();
        }
    } else if (S.viewing_lua) {
        bool has_selection = (S.selected_file_index >= 0 && S.selected_file_index < (int)S.files.size());

        if (!has_selection) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Dump Lua")) {
            if (has_selection && S.selected_file_index < (int)S.lua_files.size()) {
                std::string path = S.lua_files[S.selected_file_index].path;
                dump_single_lua_file(path, S.root_dir);
            }
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Decompile and dump selected Lua file");
            ImGui::EndTooltip();
        }
        if (!has_selection) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Dump All Lua")) {
            dump_all_lua_files(S.root_dir);
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Decompile and dump all Lua files");
            ImGui::EndTooltip();
        }
    }

    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0, 8));

    bool has_selection = (S.selected_file_index >= 0 && S.selected_file_index < (int)S.files.size());

    if (!has_selection) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Hex View")) {
        ImGui::OpenPopup("progress_win");
        if (S.viewing_adb) {
            auto item = S.files[(size_t)S.selected_file_index];
            progress_open(0, "Decompressing ADB...");
            std::thread([item]() {
                auto entries = decompress_adb(item.name);
                if (!entries.empty() && !entries[0].data.empty()) {
                    S.hex_data = entries[0].data;
                    S.hex_title = "Hex Editor - " + std::filesystem::path(item.name).filename().string() + " (decompressed)";
                    S.hex_open = true;
                    memset(&S.hex_state, 0, sizeof(S.hex_state));
                    S.hex_state.Bytes = (void*)S.hex_data.data();
                    S.hex_state.MaxBytes = (int)S.hex_data.size();
                    S.hex_state.ReadOnly = true;
                    S.hex_state.ShowAscii = true;
                    S.hex_state.ShowAddress = true;
                    S.hex_state.BytesPerLine = 16;
                }
                progress_done();
                if (entries.empty() || entries[0].data.empty()) show_error_box("Failed to decompress ADB file.");
            }).detach();
        } else {
            open_hex_for_selected();
        }
    }
    if (!has_selection) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    bool can_preview = false;
    bool can_tex = false, can_mdl = false;
    bool can_folder_preview = false;

    if (has_selection && !S.viewing_adb && !S.viewing_lua) {
        std::string n = S.files[(size_t)S.selected_file_index].name;
        std::string l = n;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        can_tex = l.size() >= 4 && l.rfind(".tex") == l.size() - 4;
        can_mdl = l.size() >= 4 && l.rfind(".mdl") == l.size() - 4;
        can_preview = can_tex || can_mdl;
    } else if (!S.selected_folder_path.empty()) {
        std::vector<std::pair<std::string, std::string>> mdl_paths;
        can_folder_preview = find_mdl_files_in_folder(g_tree_root, S.selected_folder_path, mdl_paths);
        can_preview = can_folder_preview;
    }

    // Always-available toggle for the Materials & Textures window. Useful
    // when the auto-open didn't fire or you closed the window and want it
    // back without reloading the model.
    {
        bool m_open = S.model_materials_open;
        if (ImGui::Checkbox("Materials Window", &m_open)) {
            S.model_materials_open = m_open;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Open Now")) {
            S.model_materials_open = true;
            S.model_preview_open   = true;
        }
    }

if (!can_preview) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Preview")) {
#ifdef _WIN32
        if (can_folder_preview && !S.selected_folder_path.empty()) {
            std::vector<std::pair<std::string, std::string>> mdl_paths;
            if (find_mdl_files_in_folder(g_tree_root, S.selected_folder_path, mdl_paths)) {
                progress_open(0, "Loading preview...");

                ID3D11Device* device_ptr = device;

                std::thread([device_ptr, mdl_paths]() {
                    std::vector<MDLMeshGeom> all_meshes;
                    MDLInfo combined_info;
                    bool any_success = false;

                    for (const auto& [mdl_path, bnk_source] : mdl_paths) {
                        std::vector<unsigned char> buf;
                        bool ok = false;

                        try {
                            ok = build_mdl_buffer_for_name(mdl_path, buf);
                        } catch (...) {}

                        if (ok && !buf.empty()) {
                            MDLInfo mdl_info;
                            if (parse_mdl_info(buf, mdl_info, mdl_path)) {
                                std::vector<MDLMeshGeom> meshes;
                                if (parse_mdl_geometry(buf, mdl_info, meshes)) {
                                    all_meshes.insert(all_meshes.end(), meshes.begin(), meshes.end());
                                    if (!any_success) {
                                        combined_info = mdl_info;
                                        any_success = true;
                                    }
                                }
                            }
                        }
                    }

                    if (any_success && !all_meshes.empty()) {
                        S.hex_data.clear();
                        S.mdl_info_ok = true;
                        S.mdl_info = combined_info;
                        S.mdl_meshes = all_meshes;

                        extern ModelPreview g_mp;
                        MP_Release(g_mp);
                        MP_Init(device_ptr, g_mp, 800, 520);
                        MP_Build(device_ptr, all_meshes, combined_info, g_mp);
                        S.cam_yaw = 0.0f;
                        S.cam_pitch = 0.2f;
                        S.cam_dist = 3.0f;
                        S.show_model_preview = true;
                    }

                    progress_done();
                    if (!any_success) {
                        show_error_box("Failed to load preview.");
                    }
                }).detach();
            }
        } else {
        auto item = S.files[(size_t)S.selected_file_index];
        auto name = item.name;


        {
            std::string filename = std::filesystem::path(name).filename().string();
            std::string filename_lower = filename;
            std::transform(filename_lower.begin(), filename_lower.end(), filename_lower.begin(), ::tolower);
            bool is_interior_or_exterior = (filename_lower == "interior.mdl" || filename_lower == "exterior.mdl");


            if (is_interior_or_exterior && can_mdl) {

                std::filesystem::path full_path(name);
                std::string folder_path = full_path.parent_path().string();


                std::vector<std::pair<std::string, int>> mdl_files;

                for (size_t i = 0; i < S.files.size(); ++i) {
                    const auto& file = S.files[i];
                    std::filesystem::path file_path(file.name);
                    std::string file_folder = file_path.parent_path().string();
                    std::string file_name_lower = file_path.filename().string();
                    std::transform(file_name_lower.begin(), file_name_lower.end(), file_name_lower.begin(), ::tolower);


                    if (file_folder == folder_path &&
                        (file_name_lower == "interior.mdl" || file_name_lower == "exterior.mdl")) {
                        mdl_files.push_back({file.name, file.index});
                    }
                }

                if (mdl_files.size() > 0) {

                    progress_open(0, "Loading preview...");

                    ID3D11Device* device_ptr = device;

                    std::thread([device_ptr, mdl_files]() {
                        std::vector<MDLMeshGeom> all_meshes;
                        MDLInfo combined_info;
                        bool any_success = false;

                        for (const auto& [mdl_name, mdl_index] : mdl_files) {
                            std::vector<unsigned char> buf;
                            bool ok = false;

                            try {
                                ok = build_mdl_buffer_for_name(mdl_name, buf);
                            } catch (...) {}

                            if (ok && !buf.empty()) {
                                MDLInfo mdl_info;
                                if (parse_mdl_info(buf, mdl_info, mdl_name)) {
                                    std::vector<MDLMeshGeom> meshes;
                                    if (parse_mdl_geometry(buf, mdl_info, meshes)) {
                                        all_meshes.insert(all_meshes.end(), meshes.begin(), meshes.end());
                                        if (!any_success) {
                                            combined_info = mdl_info;
                                            any_success = true;
                                        }
                                    }
                                }
                            }
                        }

                        if (any_success && !all_meshes.empty()) {
                            S.hex_data.clear();
                            S.mdl_info_ok = true;
                            S.mdl_info = combined_info;
                            S.mdl_meshes = all_meshes;

                            extern ModelPreview g_mp;
                            MP_Release(g_mp);
                            MP_Init(device_ptr, g_mp, 800, 520);
                            MP_Build(device_ptr, all_meshes, combined_info, g_mp);
                            S.cam_yaw = 0.0f;
                            S.cam_pitch = 0.2f;
                            S.cam_dist = 3.0f;
                            S.show_model_preview = true;
                        }

                        progress_done();
                        if (!any_success) {
                            show_error_box("Failed to load preview.");
                        }
                    }).detach();

                    goto skip_preview;
                }

            }
        }


        {
        std::string bnk_to_use;
        std::string nested_temp_copy;
        bool is_nested = false;

        if (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty()) {
            is_nested = true;
            auto tmpdir = std::filesystem::temp_directory_path() / "f2_preview";
            std::error_code ec;
            std::filesystem::create_directories(tmpdir, ec);

            auto unique_temp = tmpdir / ("nested_" + std::to_string(std::hash<std::string>{}(S.selected_nested_temp_path + std::to_string(std::time(nullptr)))) + ".bnk");

            try {
                if (!std::filesystem::exists(S.selected_nested_temp_path)) {
                    show_error_box("Nested BNK source file does not exist");
                    goto skip_preview;
                }

                std::filesystem::copy_file(S.selected_nested_temp_path, unique_temp,
                                          std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) {
                    nested_temp_copy = unique_temp.string();
                    bnk_to_use = nested_temp_copy;
                } else {
                    show_error_box("Failed to copy nested BNK: " + ec.message());
                    goto skip_preview;
                }
            } catch (const std::exception& e) {
                show_error_box(std::string("Exception copying nested BNK: ") + e.what());
                goto skip_preview;
            }
        } else {
            bnk_to_use = S.selected_bnk;
        }

        progress_open(0, "Loading preview...");

        // The original (non-copy) nested BNK path is what's keyed in
        // S.nested_bnk_parents — pass it to build_any_tex_buffer_for_name
        // so sibling BNKs from the same parent take priority over globals.
        std::string preferred_for_tex = is_nested
            ? S.selected_nested_temp_path
            : S.selected_bnk;

        std::thread([device, item, name, can_tex, can_mdl, bnk_to_use, nested_temp_copy, is_nested, preferred_for_tex]() {
            std::vector<unsigned char> buf;
            bool ok = false;
            try {
                if (can_tex) {
                    ok = build_any_tex_buffer_for_name(name, buf, preferred_for_tex);
                } else if (can_mdl) {
                    if (is_nested) {
                        ok = reconstruct_nested_mdl(bnk_to_use, item.index, buf);
                    } else {
                        ok = build_mdl_buffer_for_name(name, buf);
                    }
                }

                if (!ok) {
                    auto tmpdir = std::filesystem::temp_directory_path() / "f2_preview";
                    std::error_code ec;
                    std::filesystem::create_directories(tmpdir, ec);
                    auto tmp_file = tmpdir / ("preview_" + std::to_string(std::hash<std::string>{}(name + std::to_string(std::time(nullptr)))) + ".bin");

                    try {
                        extract_one(bnk_to_use, item.index, tmp_file.string());
                        buf = read_all_bytes(tmp_file);
                        ok = !buf.empty();
                        std::filesystem::remove(tmp_file, ec);
                    } catch (...) {
                        std::filesystem::remove(tmp_file, ec);
                        throw;
                    }
                }
            } catch (...) {
                ok = false;
            }

            if (!nested_temp_copy.empty()) {
                std::error_code ec;
                std::filesystem::remove(nested_temp_copy, ec);
            }

            if (ok) {
                S.hex_data = buf;

                if (can_tex) {
                    S.tex_info_ok = parse_tex_info(S.hex_data, S.tex_info);
                    if (S.tex_info_ok && !S.tex_info.Mips.empty()) {
                        // Pick the LARGEST mip overall — compressed mips are
                        // now decodable via the Lionhead codec port.
                        int best_mip = -1;
                        size_t best_area = 0;
                        for (int i = 0; i < (int)S.tex_info.Mips.size(); ++i) {
                            int w = S.tex_info.Mips[i].HasWH ? (int)S.tex_info.Mips[i].MipWidth : std::max(1, (int)S.tex_info.TextureWidth >> i);
                            int h = S.tex_info.Mips[i].HasWH ? (int)S.tex_info.Mips[i].MipHeight : std::max(1, (int)S.tex_info.TextureHeight >> i);
                            size_t area = (size_t)w * (size_t)h;
                            if (area > best_area) {
                                best_area = area;
                                best_mip = i;
                            }
                        }
                        if (best_mip >= 0) {
                            S.preview_mip_index = best_mip;
                            S.show_preview_popup = true;
                        } else {
                            log_tagged("preview", "no mip found for " + name);
                        }
                    } else if (!S.tex_info_ok) {
                        log_tagged("preview", "parse_tex_info failed for " + name);
                    }
                } else if (can_mdl) {
                    S.mdl_info_ok = parse_mdl_info(S.hex_data, S.mdl_info, name);
                    if (S.mdl_info_ok) {
                        S.mdl_meshes.clear();
                        parse_mdl_geometry(S.hex_data, S.mdl_info, S.mdl_meshes);
                        extern ModelPreview g_mp;
                        MP_Release(g_mp);
                        MP_Init(device, g_mp, 800, 520);
                        MP_Build(device, S.mdl_meshes, S.mdl_info, g_mp);
                        S.cam_yaw = 0.0f; S.cam_pitch = 0.2f; S.cam_dist = 3.0f;
                        S.show_model_preview = true;
                    }
                }
            }

            progress_done();
            if (!ok) show_error_box("Failed to load preview.");
        }).detach();
        }

        skip_preview:;
    }
#else



        if (can_folder_preview && !S.selected_folder_path.empty()) {
            std::vector<std::pair<std::string, std::string>> mdl_paths;
            if (find_mdl_files_in_folder(g_tree_root, S.selected_folder_path, mdl_paths)) {
                progress_open(0, "Loading preview...");

                std::thread([mdl_paths]() {
                    std::vector<MDLMeshGeom> all_meshes;
                    MDLInfo combined_info;
                    bool any_success = false;

                    for (const auto& [mdl_path, bnk_source] : mdl_paths) {
                        std::vector<unsigned char> buf;
                        bool ok = false;

                        try {
                            ok = build_mdl_buffer_for_name(mdl_path, buf);
                        } catch (...) {}

                        if (ok && !buf.empty()) {
                            MDLInfo mdl_info;
                            if (parse_mdl_info(buf, mdl_info, mdl_path)) {
                                std::vector<MDLMeshGeom> meshes;
                                if (parse_mdl_geometry(buf, mdl_info, meshes)) {
                                    all_meshes.insert(all_meshes.end(), meshes.begin(), meshes.end());
                                    if (!any_success) {
                                        combined_info = mdl_info;
                                        any_success = true;
                                    }
                                }
                            }
                        }
                    }

                    if (any_success && !all_meshes.empty()) {
                        S.hex_data.clear();
                        S.mdl_info_ok = true;
                        S.mdl_info = combined_info;
                        S.mdl_meshes = all_meshes;
                        S.cam_yaw = 0.0f;
                        S.cam_pitch = 0.2f;
                        S.cam_dist = 3.0f;
                        S.pending_preview_build = true;
                    }

                    progress_done();
                    if (!any_success) {
                        show_error_box("Failed to load preview.");
                    }
                }).detach();
            }
        } else if (can_mdl) {
            auto item = S.files[(size_t)S.selected_file_index];
            auto name = item.name;
            progress_open(0, "Loading preview...");

            std::thread([name]() {
                bool ok = false;
                std::vector<unsigned char> buf;

                try {
                    ok = build_mdl_buffer_for_name(name, buf);
                } catch (...) {}

                if (ok && !buf.empty()) {
                    MDLInfo mdl_info;
                    if (parse_mdl_info(buf, mdl_info, name)) {
                        std::vector<MDLMeshGeom> meshes;
                        if (parse_mdl_geometry(buf, mdl_info, meshes)) {
                            S.hex_data.clear();
                            S.mdl_info_ok = true;
                            S.mdl_info = mdl_info;
                            S.mdl_meshes = meshes;
                            S.cam_yaw = 0.0f;
                            S.cam_pitch = 0.2f;
                            S.cam_dist = 3.0f;
                            S.pending_preview_build = true;
                            ok = true;
                        }
                    }
                }

                progress_done();
                if (!ok) show_error_box("Failed to load preview.");
            }).detach();
        }
#endif
    }
    if (!can_preview) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    bool has_mdl_files = false;
    if (!S.global_search.empty()) {
        for (const auto& h : g_global_hits) {
            if (is_mdl_file(h.file_name)) {
                has_mdl_files = true;
                break;
            }
        }
    } else {
        has_mdl_files = is_model_bnk_selected() && any_mdl_in_bnk();
    }

    if (!has_mdl_files || S.viewing_adb) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Export All to GLB")) {
        ImGui::OpenPopup("progress_win");
        if (!S.global_search.empty()) {
            on_export_global_mdl_to_glb(g_global_hits);
        } else {
            on_export_all_mdl_to_glb();
        }
    }
    if (!S.hide_tooltips && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Export all .mdl files to GLB format");
        ImGui::EndTooltip();
    }
    if (!has_mdl_files || S.viewing_adb) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    bool can_export_mdl = false;
    if (has_selection && !S.viewing_adb) {
        std::string n = S.files[(size_t)S.selected_file_index].name;
        std::string l = n;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        can_export_mdl = l.size() >= 4 && l.rfind(".mdl") == l.size() - 4;
    }

    if (!can_export_mdl) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Export to GLB")) {
        ImGui::OpenPopup("progress_win");
        on_export_mdl_to_glb();
    }
    if (!S.hide_tooltips && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Export selected .mdl file to GLB format");
        ImGui::EndTooltip();
    }
    if (!can_export_mdl) {
        ImGui::EndDisabled();
    }

    ImGui::PopStyleVar();
    ImGui::EndGroup();

    static bool hide_tt = false;
    if (ImGui::Checkbox("Hide Paths Tooltip", &hide_tt)) { S.hide_tooltips = hide_tt; }

    int visible = count_visible_files();
    ImGui::Text("Files found: %d/%d", visible, (int) S.files.size());

    ImGui::PopItemWidth();
    ImGui::EndGroup();
    ImGui::EndChild();

    {
        std::vector<std::string> exts = unique_file_extensions();
        ImGui::SetNextItemWidth(160.0f);
        const char* current_label = S.ext_filter.empty() ? "(all extensions)" : S.ext_filter.c_str();
        if (ImGui::BeginCombo("##ext_filter", current_label)) {
            if (ImGui::Selectable("(all extensions)", S.ext_filter.empty())) {
                S.ext_filter.clear();
            }
            for (const auto& e : exts) {
                bool is_selected = (e == S.ext_filter);
                if (ImGui::Selectable(e.c_str(), is_selected)) {
                    S.ext_filter = e;
                }
                if (is_selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Show only files with this extension");
            ImGui::EndTooltip();
        }
    }

    float available_width = ImGui::GetContentRegionAvail().x;
    float field_width = (available_width - 8.0f) * 0.5f;

    ImGui::SetNextItemWidth(field_width);
    const char* filter_hint = "Filter Current BNK";
    if (S.viewing_adb) filter_hint = "Filter ADB Files";
    else if (S.viewing_lua) filter_hint = "Filter Lua Scripts";
    ImGui::InputTextWithHint("##file_filter", filter_hint, &S.file_filter);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(field_width);
    bool search_changed = ImGui::InputTextWithHint("##global_search", "Search All BNKs", &S.global_search);

    if (S.global_search != g_last_global_search) {
        g_last_global_search = S.global_search;
        g_global_hits.clear();
        g_selected_global = -1;

        if (!S.global_search.empty()) {
            S.viewing_adb = false;
            if (!g_global_busy) {
                g_global_busy = true;
                std::string search_term = S.global_search;

                std::thread([search_term]() {
                    std::vector<GlobalHit> local_hits;
                    std::string needle = search_term;
                    std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);

                    auto is_header_bnk = [](const std::string& bnk_path) -> bool {
                        std::string lower_path = bnk_path;
                        std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);
                        std::string filename = std::filesystem::path(lower_path).filename().string();
                        return filename.find("header") != std::string::npos;
                    };

                    auto is_nested_bnk = [](const std::string& filename) -> bool {
                        std::string lower = filename;
                        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                        return lower.size() >= 4 && lower.substr(lower.size() - 4) == ".bnk";
                    };

                    try {
                        for (const auto& bnk_path : S.bnk_paths) {
                            if (is_header_bnk(bnk_path)) {
                                continue;
                            }

                            BNKReader reader(bnk_path);
                            const auto& files = reader.list_files();

                            for (size_t i = 0; i < files.size(); ++i) {
                                std::string fname = files[i].name;
                                std::string fname_lower = fname;
                                std::transform(fname_lower.begin(), fname_lower.end(), fname_lower.begin(), ::tolower);

                                if (fname_lower.find(needle) != std::string::npos) {
                                    local_hits.push_back({
                                        bnk_path,
                                        fname,
                                        (int)i,
                                        files[i].uncompressed_size
                                    });
                                }

                                if (is_nested_bnk(fname)) {
                                    try {
                                        auto tmpdir = std::filesystem::temp_directory_path() / "f2_global_search_nested";
                                        std::error_code ec;
                                        std::filesystem::create_directories(tmpdir, ec);

                                        std::string temp_name = "search_nested_" + std::to_string(std::hash<std::string>{}(bnk_path + fname)) + ".bnk";
                                        auto temp_bnk_path = tmpdir / temp_name;

                                        extract_one(bnk_path, (int)i, temp_bnk_path.string());

                                        BNKReader nested_reader(temp_bnk_path.string());
                                        const auto& nested_files = nested_reader.list_files();

                                        // Use '/' as the only separator to keep behavior consistent
                                        // with POSIX; std::filesystem::path on Windows would also
                                        // recognize '\', causing the prefix to change shape per platform.
                                        size_t fname_last_slash = fname.find_last_of('/');
                                        std::string prefix = (fname_last_slash == std::string::npos)
                                            ? std::string()
                                            : fname.substr(0, fname_last_slash + 1);

                                        for (size_t j = 0; j < nested_files.size(); ++j) {
                                            const auto& nested_file = nested_files[j];
                                            std::string nested_fname = prefix + nested_file.name;
                                            std::string nested_fname_lower = nested_fname;
                                            std::transform(nested_fname_lower.begin(), nested_fname_lower.end(), nested_fname_lower.begin(), ::tolower);

                                            if (nested_fname_lower.find(needle) != std::string::npos) {
                                                local_hits.push_back({
                                                    temp_bnk_path.string(),
                                                    nested_fname,
                                                    (int)j,
                                                    nested_files[j].uncompressed_size
                                                });
                                            }
                                        }
                                    } catch (...) {}
                                }
                            }
                        }
                    } catch (...) {}

                    g_global_hits = std::move(local_hits);
                    g_global_busy = false;
                }).detach();
            }
        }
    }

    if (!S.hide_tooltips && ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Type to search across all BNK files");
        ImGui::EndTooltip();
    }

    if (S.viewing_lua) {
        float available_height = ImGui::GetContentRegionAvail().y;
        float table_height = available_height * 0.35f;
        float preview_height = available_height - table_height - 8.0f;

        ImGui::BeginChild("lua_table_container", ImVec2(0, table_height), false);
        draw_file_table();
        ImGui::EndChild();

        if (S.selected_file_index >= 0 && S.selected_file_index < (int)S.lua_files.size()) {
            if (S.lua_preview_selected != S.selected_file_index && !S.lua_preview_loading) {
                S.lua_preview_selected = S.selected_file_index;
                S.lua_preview_title = S.lua_files[S.selected_file_index].filename;
                S.lua_preview_content.clear();
                S.lua_preview_loading = true;

                std::string path = S.lua_files[S.selected_file_index].path;
                std::string title = S.lua_preview_title;

                progress_open(0, "Decompiling " + title + "...");

                std::thread([path, title]() {
                    std::string content = read_lua_file_content(path);
                    S.lua_preview_content = content;
                    S.lua_preview_loading = false;
                    progress_done();
                }).detach();
            }
        }

        ImGui::Dummy(ImVec2(0, 4));

        if (S.lua_preview_loading) {
            ImGui::BeginChild("lua_preview_loading", ImVec2(0, preview_height), true);
            ImGui::TextDisabled("Decompiling...");
            ImGui::EndChild();
        } else if (!S.lua_preview_content.empty()) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
            ImGui::BeginChild("lua_preview", ImVec2(0, preview_height), true, ImGuiWindowFlags_HorizontalScrollbar);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.7f, 1.0f));
            ImGui::TextUnformatted(S.lua_preview_title.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.9f, 0.8f, 1.0f));
            ImGui::TextUnformatted(S.lua_preview_content.c_str());
            ImGui::PopStyleColor();

            ImGui::EndChild();
            ImGui::PopStyleColor();
        } else {
            ImGui::BeginChild("lua_preview_empty", ImVec2(0, preview_height), true);
            ImGui::TextDisabled("Select a Lua file to preview");
            ImGui::EndChild();
        }
    } else {
        ImGui::BeginChild("right_table_container", ImVec2(0, 0), false);
        if (!S.global_search.empty()) {
            draw_global_results_table();
        } else {
            draw_file_table();
        }
        ImGui::EndChild();
    };

    if (g_pending_mdl_load && g_pending_mdl_index >= 0 && g_pending_mdl_index < (int)S.files.size()) {
        g_pending_mdl_load = false;
        auto item = S.files[(size_t)g_pending_mdl_index];
        auto name = item.name;
        std::string parse_path = g_pending_mdl_full_path.empty() ? name : g_pending_mdl_full_path;
        g_pending_mdl_full_path.clear();

        std::string bnk_to_use;
        std::string nested_temp_copy;
        bool is_nested = false;

        if (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty()) {
            is_nested = true;
            auto tmpdir = std::filesystem::temp_directory_path() / "f2_preview";
            std::error_code ec;
            std::filesystem::create_directories(tmpdir, ec);
            auto unique_temp = tmpdir / ("nested_" + std::to_string(std::hash<std::string>{}(S.selected_nested_temp_path + std::to_string(std::time(nullptr)))) + ".bnk");
            try {
                if (std::filesystem::exists(S.selected_nested_temp_path)) {
                    std::filesystem::copy_file(S.selected_nested_temp_path, unique_temp,
                                              std::filesystem::copy_options::overwrite_existing, ec);
                    if (!ec) {
                        nested_temp_copy = unique_temp.string();
                        bnk_to_use = nested_temp_copy;
                    }
                }
            } catch (...) {}
        } else {
            bnk_to_use = S.selected_bnk;
        }

        if (!bnk_to_use.empty()) {
#ifdef _WIN32
            ID3D11Device* device_ptr = device;
            std::thread([device_ptr, item, name, parse_path, bnk_to_use, nested_temp_copy, is_nested]() {
#else
            std::thread([item, name, parse_path, bnk_to_use, nested_temp_copy, is_nested]() {
#endif
                std::vector<unsigned char> buf;
                bool ok = false;

                try {
                    if (is_nested) {
                        ok = reconstruct_nested_mdl(bnk_to_use, item.index, buf);
                    } else {
                        ok = build_mdl_buffer_for_name(name, buf);
                    }

                    if (!ok) {
                        auto tmpdir = std::filesystem::temp_directory_path() / "f2_preview";
                        std::error_code ec;
                        std::filesystem::create_directories(tmpdir, ec);
                        auto tmp_file = tmpdir / ("preview_" + std::to_string(std::hash<std::string>{}(name + std::to_string(std::time(nullptr)))) + ".bin");
                        try {
                            extract_one(bnk_to_use, item.index, tmp_file.string());
                            buf = read_all_bytes(tmp_file);
                            ok = !buf.empty();
                            std::filesystem::remove(tmp_file, ec);
                        } catch (...) {
                            std::filesystem::remove(tmp_file, ec);
                        }
                    }
                } catch (...) {
                    ok = false;
                }

                if (!nested_temp_copy.empty()) {
                    std::error_code ec;
                    std::filesystem::remove(nested_temp_copy, ec);
                }

                if (ok && !buf.empty()) {
                    S.mdl_info_ok = parse_mdl_info(buf, S.mdl_info, parse_path);
                    if (S.mdl_info_ok) {
                        S.mdl_meshes.clear();
                        parse_mdl_geometry(buf, S.mdl_info, S.mdl_meshes);
#ifdef _WIN32
                        extern ModelPreview g_mp;
                        MP_Release(g_mp);
                        MP_Init(device_ptr, g_mp, 800, 600);
                        MP_Build(device_ptr, S.mdl_meshes, S.mdl_info, g_mp);
#else
                        S.pending_preview_build = true;
#endif
                    }
                }
            }).detach();
        }
        g_pending_mdl_index = -1;
    }

    if (g_pending_tex_load && g_pending_tex_index >= 0 && g_pending_tex_index < (int)S.files.size()) {
        g_pending_tex_load = false;
        auto item = S.files[(size_t)g_pending_tex_index];
        auto name = item.name;
        S.texture_window_name = name;

        // Pass the user's clicked BNK so nested-region sibling BNKs are
        // searched before generic globals.
        std::string preferred_for_tex = (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty())
            ? S.selected_nested_temp_path
            : S.selected_bnk;

        // Show the loading popup while the texture decode runs in the
        // background. Matches the MDL preview path.
        progress_open(0, "Loading texture...");

        std::thread([name, preferred_for_tex]() {
            std::vector<unsigned char> tex_buf;
            if (!build_any_tex_buffer_for_name(name, tex_buf, preferred_for_tex)) {
                log_tagged("texture window",
                           "build_any_tex_buffer_for_name failed for " + name);
                S.texture_window_name = "ERROR: Could not load texture file (see tex_errors.log)";
                S.pending_texture_load = true;
                S.pending_texture_w = 0;
                S.pending_texture_h = 0;
                progress_done();
                return;
            }

            // decode_tex_to_rgba picks the largest mip available — comp=7 raw
            // for BC1/BC3, or Lionhead-compressed (comp != 7) BC1 via the
            // codec we ported from default.xex.
            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            bool has_alpha = false;
            if (!decode_tex_to_rgba(tex_buf, rgba, w, h, &has_alpha)) {
                log_tagged("texture window",
                           "decode_tex_to_rgba failed for " + name);
                S.texture_window_name = "ERROR: Could not decode texture (see tex_errors.log)";
                S.pending_texture_load = true;
                S.pending_texture_w = 0;
                S.pending_texture_h = 0;
                progress_done();
                return;
            }

            S.pending_texture_rgba = std::move(rgba);
            S.pending_texture_w = w;
            S.pending_texture_h = h;
            S.pending_texture_load = true;
            progress_done();
        }).detach();

        g_pending_tex_index = -1;
    }

#ifndef _WIN32
    if (S.pending_preview_build) {
        S.pending_preview_build = false;
        extern ModelPreview g_mp;
        extern FlyCam g_flycam;
        MP_Release(g_mp);
        MP_Init(g_mp, 800, 600);
        MP_Build(S.mdl_meshes, S.mdl_info, g_mp);
    }
#endif

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// process_pending_loads — same handler block as the tail of draw_right_panel
// above, lifted into its own function so the new layout (which doesn't
// draw the right panel any more) can still drive MDL/TEX preview loads.
//
// Called once per frame from draw_main(). The handlers self-clear their
// pending flags, so calling this in addition to draw_right_panel (if both
// were active) would be a no-op past the first invocation.
// ---------------------------------------------------------------------------
#ifdef _WIN32
void process_pending_loads(ID3D11Device* device) {
#else
void process_pending_loads() {
#endif
    if (g_pending_mdl_load && g_pending_mdl_index >= 0 && g_pending_mdl_index < (int)S.files.size()) {
        g_pending_mdl_load = false;
        auto item = S.files[(size_t)g_pending_mdl_index];
        auto name = item.name;
        std::string parse_path = g_pending_mdl_full_path.empty() ? name : g_pending_mdl_full_path;
        g_pending_mdl_full_path.clear();

        std::string bnk_to_use;
        std::string nested_temp_copy;
        bool is_nested = false;

        if (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty()) {
            is_nested = true;
            auto tmpdir = std::filesystem::temp_directory_path() / "f2_preview";
            std::error_code ec;
            std::filesystem::create_directories(tmpdir, ec);
            auto unique_temp = tmpdir / ("nested_" + std::to_string(std::hash<std::string>{}(S.selected_nested_temp_path + std::to_string(std::time(nullptr)))) + ".bnk");
            try {
                if (std::filesystem::exists(S.selected_nested_temp_path)) {
                    std::filesystem::copy_file(S.selected_nested_temp_path, unique_temp,
                                              std::filesystem::copy_options::overwrite_existing, ec);
                    if (!ec) {
                        nested_temp_copy = unique_temp.string();
                        bnk_to_use = nested_temp_copy;
                    }
                }
            } catch (...) {}
        } else {
            bnk_to_use = S.selected_bnk;
        }

        if (!bnk_to_use.empty()) {
            // Show the loading popup while the MDL parse + MP_Build run in
            // the background.
            progress_open(0, "Loading model...");
#ifdef _WIN32
            ID3D11Device* device_ptr = device;
            std::thread([device_ptr, item, name, parse_path, bnk_to_use, nested_temp_copy, is_nested]() {
#else
            std::thread([item, name, parse_path, bnk_to_use, nested_temp_copy, is_nested]() {
#endif
                std::vector<unsigned char> buf;
                bool ok = false;
                try {
                    if (is_nested) {
                        ok = reconstruct_nested_mdl(bnk_to_use, item.index, buf);
                    } else {
                        ok = build_mdl_buffer_for_name(name, buf);
                    }
                    if (!ok) {
                        auto tmpdir = std::filesystem::temp_directory_path() / "f2_preview";
                        std::error_code ec;
                        std::filesystem::create_directories(tmpdir, ec);
                        auto tmp_file = tmpdir / ("preview_" + std::to_string(std::hash<std::string>{}(name + std::to_string(std::time(nullptr)))) + ".bin");
                        try {
                            extract_one(bnk_to_use, item.index, tmp_file.string());
                            buf = read_all_bytes(tmp_file);
                            ok = !buf.empty();
                            std::filesystem::remove(tmp_file, ec);
                        } catch (...) {
                            std::filesystem::remove(tmp_file, ec);
                        }
                    }
                } catch (...) { ok = false; }

                if (!nested_temp_copy.empty()) {
                    std::error_code ec;
                    std::filesystem::remove(nested_temp_copy, ec);
                }

                if (ok && !buf.empty()) {
                    S.mdl_info_ok = parse_mdl_info(buf, S.mdl_info, parse_path);
                    if (S.mdl_info_ok) {
                        S.mdl_meshes.clear();
                        parse_mdl_geometry(buf, S.mdl_info, S.mdl_meshes);
#ifdef _WIN32
                        extern ModelPreview g_mp;
                        MP_Release(g_mp);
                        MP_Init(device_ptr, g_mp, 800, 600);
                        MP_Build(device_ptr, S.mdl_meshes, S.mdl_info, g_mp);
                        // Reset orbit camera to fit the new model.
                        S.cam_yaw = 0.0f;
                        S.cam_pitch = 0.2f;
                        S.cam_dist = 3.0f;
                        // Drop the texture preview so the render panel
                        // doesn't briefly show a stale tex behind the model.
                        if (S.texture_window_srv) {
                            S.texture_window_srv->Release();
                            S.texture_window_srv = nullptr;
                        }
#else
                        S.pending_preview_build = true;
#endif
                    }
                }
                progress_done();
            }).detach();
        }
        g_pending_mdl_index = -1;
    }

    if (g_pending_tex_load && g_pending_tex_index >= 0 && g_pending_tex_index < (int)S.files.size()) {
        g_pending_tex_load = false;
        auto item = S.files[(size_t)g_pending_tex_index];
        auto name = item.name;
        S.texture_window_name = name;

        std::string preferred_for_tex = (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty())
            ? S.selected_nested_temp_path
            : S.selected_bnk;

        progress_open(0, "Loading texture...");

        std::thread([name, preferred_for_tex]() {
            std::vector<unsigned char> tex_buf;
            if (!build_any_tex_buffer_for_name(name, tex_buf, preferred_for_tex)) {
                S.texture_window_name = "ERROR: Could not load texture file";
                S.pending_texture_load = true;
                S.pending_texture_w = 0;
                S.pending_texture_h = 0;
                progress_done();
                return;
            }
            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            bool has_alpha = false;
            if (!decode_tex_to_rgba(tex_buf, rgba, w, h, &has_alpha)) {
                S.texture_window_name = "ERROR: Could not decode texture";
                S.pending_texture_load = true;
                S.pending_texture_w = 0;
                S.pending_texture_h = 0;
                progress_done();
                return;
            }
            // Switching to a texture closes any active model preview so
            // the render panel paints just the texture, not both.
#ifdef _WIN32
            extern ModelPreview g_mp;
            extern bool g_mp_initialized;
            if (g_mp.has_model) {
                MP_Release(g_mp);
                g_mp.has_model = false;
                g_mp_initialized = false;
            }
#endif
            S.pending_texture_rgba = std::move(rgba);
            S.pending_texture_w = w;
            S.pending_texture_h = h;
            S.pending_texture_load = true;
            progress_done();
        }).detach();
        g_pending_tex_index = -1;
    }

#ifndef _WIN32
    if (S.pending_preview_build) {
        S.pending_preview_build = false;
        extern ModelPreview g_mp;
        extern FlyCam g_flycam;
        MP_Release(g_mp);
        MP_Init(g_mp, 800, 600);
        MP_Build(S.mdl_meshes, S.mdl_info, g_mp);
    }
#endif
}