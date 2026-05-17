#include "../UI_Panels.h"
#include "../OutputLog.h"
#include "PanelInternal.h"
#include "../../Utilities/Utils.h"
#include "../../BNKCore.cpp"
#include "../../ISO/IsoMount.h"
#include "imgui.h"
#include <filesystem>
#include <algorithm>
#include <map>
#include <sstream>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <optional>
#include <functional>
#include <cstring>

TreeNode g_tree_root;

static void build_unified_file_tree(TreeNode& root, std::vector<std::string> bnk_paths);

std::atomic<bool> g_tree_built{false};
std::atomic<bool> g_tree_building{false};
std::atomic<bool> g_tree_build_complete{false};
float             g_tree_build_start_time = 0.0f;
std::string       g_tree_last_root_dir;

std::atomic<int>  g_tree_done_units{0};
std::atomic<int>  g_tree_total_units{0};
std::mutex        g_tree_label_mutex;
std::string       g_tree_current_label;
void set_tree_label(std::string s) {
    std::lock_guard<std::mutex> lk(g_tree_label_mutex);
    g_tree_current_label = std::move(s);
}
extern void  start_tree_build_for_root(const std::string& root_dir,
                                       std::vector<std::string> bnk_paths);

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

    g_tree_total_units.store((int)bnk_paths.size());
    set_tree_label("");

    S.nested_bnk_paths.clear();
    S.nested_bnk_parents.clear();
    S.nested_bnk_virtual_paths.clear();

    std::thread([bnk_snapshot = std::move(bnk_paths)]() mutable {
        try {
            build_unified_file_tree(g_tree_root, std::move(bnk_snapshot));
        } catch (...) {  }

        g_tree_build_complete.store(true);
        g_tree_built.store(true);
        g_tree_building.store(false);
    }).detach();
}

bool find_mdl_files_in_folder(TreeNode& root, const std::string& folder_name, std::vector<std::pair<std::string, std::string>>& out_mdl_paths) {
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

    S.all_mdl_files.clear();
    S.all_tex_files.clear();
    S.all_wav_files.clear();
    S.all_anim_files.clear();
    S.all_level_files.clear();
    S.all_heightfield_files.clear();

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
        } else if (ends_with_ci(leaf, ".anim")) {
            FlatAssetEntry e;
            e.name = leaf;
            e.full_path = path;
            e.bnk_path = bnk_source;
            e.file_index = bnk_index;
            e.size = file_size;
            e.from_nested = is_nested;
            S.all_anim_files.push_back(std::move(e));
        } else if (ends_with_ci(leaf, ".engine_level")) {
            FlatAssetEntry e;
            e.name = leaf;
            e.full_path = path;
            e.bnk_path = bnk_source;
            e.file_index = bnk_index;
            e.size = file_size;
            e.from_nested = is_nested;
            S.all_level_files.push_back(std::move(e));
        } else if (ends_with_ci(leaf, ".ehf") ||
                   ends_with_ci(leaf, ".ghf") ||
                   ends_with_ci(leaf, ".hdb") ||
                   ends_with_ci(leaf, ".genv") ||
                   ends_with_ci(leaf, ".ama") ||
                   ends_with_ci(leaf, ".amm") ||
                   ends_with_ci(leaf, ".amr") ||
                   ends_with_ci(leaf, ".texture_atlas")) {
            FlatAssetEntry e;
            e.name = leaf;
            e.full_path = path;
            e.bnk_path = bnk_source;
            e.file_index = bnk_index;
            e.size = file_size;
            e.from_nested = is_nested;
            S.all_heightfield_files.push_back(std::move(e));
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

        }
        g_tree_done_units.fetch_add(1);
    }

    g_tree_total_units.fetch_add((int)nested_bnks.size());

    auto tmpdir = std::filesystem::temp_directory_path() / "f2_nested_bnk_tree";
    {
        std::error_code ec;
        std::filesystem::create_directories(tmpdir, ec);
    }

    LazyNested::clear_all();

    std::unordered_map<std::string, std::vector<int>> nested_by_parent;
    nested_by_parent.reserve(nested_bnks.size());
    for (const auto& [parent_bnk_path, nested_index] : nested_bnks) {
        nested_by_parent[parent_bnk_path].push_back(nested_index);
    }

    for (auto& [parent_bnk_path, indices] : nested_by_parent) {

        std::optional<BNKReader> parent_reader;
        try {
            parent_reader.emplace(parent_bnk_path);
        } catch (...) {

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

            std::string nested_base = std::filesystem::path(nested_path).filename().string();
            std::string temp_name = std::to_string(std::hash<std::string>{}(parent_bnk_path + nested_path))
                                  + "_" + nested_base;
            auto temp_bnk_path = (tmpdir / temp_name).string();

            try {

                std::vector<uint8_t> nested_bytes =
                    parent_reader->extract_index_bytes(nested_index);

                BNKReader nested_reader(std::move(nested_bytes));
                const auto& nested_files = nested_reader.list_files();

                LazyNested::register_pending(temp_bnk_path, parent_bnk_path, nested_index);

                {
                    if (std::find(S.nested_bnk_paths.begin(), S.nested_bnk_paths.end(), temp_bnk_path)
                        == S.nested_bnk_paths.end()) {
                        S.nested_bnk_paths.push_back(temp_bnk_path);
                    }
                    S.nested_bnk_parents[temp_bnk_path] = parent_bnk_path;
                    S.nested_bnk_virtual_paths[temp_bnk_path] = nested_path;
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

            }
            g_tree_done_units.fetch_add(1);
        }
    }

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
    std::sort(S.all_anim_files.begin(), S.all_anim_files.end(), cmp_ci);

    {
        std::ostringstream os;
        os << "tree built: " << bnk_paths.size() << " parent BNKs + "
           << S.nested_bnk_paths.size() << " nested BNKs indexed  →  "
           << S.all_mdl_files.size() << " mdl, "
           << S.all_tex_files.size() << " tex, "
           << S.all_wav_files.size() << " wav, "
           << S.all_anim_files.size() << " anim";
        OutputLog::info(os.str());
    }

    set_tree_label("");
}
