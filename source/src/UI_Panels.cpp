#include "UI_Panels.h"
#include "State.h"
#include "Utils.h"
#include "Operations.h"
#include "HexView.h"
#include "UI_Main.h"
#include "TexParser.h"
#include "ModelParser.h"
#include "ModelPreview.h"
#include "mdl_converter.h"
#include "BNKCore.cpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "ImGuiFileDialog.h"
#include <filesystem>
#include <algorithm>
#include <thread>
#include <atomic>
#include "Progress.h"
#include "files.h"



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

struct TreeNode {
    std::string name;
    bool is_file;
    std::string full_path;
    std::string bnk_source;
    int bnk_index;
    uint32_t file_size;
    std::map<std::string, TreeNode> children;
};

static TreeNode g_tree_root;

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

static void build_unified_file_tree(TreeNode& root) {
    root.children.clear();

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

    auto add_to_tree = [&root](const std::string& path, const std::string& bnk_source,
                               int bnk_index, uint32_t file_size, bool is_nested = false) {
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
            }

            current = &child;
        }
    };

    std::vector<std::pair<std::string, int>> nested_bnks;

    for (const auto& bnk_path : S.bnk_paths) {
        if (is_header_bnk(bnk_path)) {
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
            continue;
        }
    }

    for (const auto& [parent_bnk_path, nested_index] : nested_bnks) {
        try {
            BNKReader parent_reader(parent_bnk_path);
            const auto& parent_files = parent_reader.list_files();

            if (nested_index < 0 || nested_index >= (int)parent_files.size()) {
                continue;
            }

            const auto& nested_file = parent_files[nested_index];
            std::string nested_path = nested_file.name;

            auto tmpdir = std::filesystem::temp_directory_path() / "f2_nested_bnk_tree";
            std::error_code ec;
            std::filesystem::create_directories(tmpdir, ec);

            std::string temp_name = "nested_" + std::to_string(std::hash<std::string>{}(parent_bnk_path + nested_path)) + ".bnk";
            auto temp_bnk_path = tmpdir / temp_name;

            extract_one(parent_bnk_path, nested_index, temp_bnk_path.string());

            BNKReader nested_reader(temp_bnk_path.string());
            const auto& nested_files = nested_reader.list_files();

            std::filesystem::path nested_parent = std::filesystem::path(nested_path).parent_path();
            std::string prefix = nested_parent.empty() ? "" : nested_parent.string() + "/";

            for (size_t i = 0; i < nested_files.size(); ++i) {
                const auto& file = nested_files[i];
                std::string full_nested_path = prefix + file.name;
                add_to_tree(full_nested_path, temp_bnk_path.string(), (int)i, file.uncompressed_size, true);
            }

        } catch (...) {
            continue;
        }
    }
}

static void draw_tree_node(TreeNode& node, ID3D11Device* device) {
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

            for (size_t i = 0; i < S.files.size(); ++i) {
                if (S.files[i].index == node.bnk_index) {
                    S.selected_file_index = (int)i;
                    break;
                }
            }
        }

        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", node.full_path.c_str());
            ImGui::Text("Size: %u bytes", node.file_size);
            ImGui::Text("BNK: %s", std::filesystem::path(node.bnk_source).filename().string().c_str());
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
                draw_tree_node(*pair.second, device);
            }

            ImGui::TreePop();
        }
    }
}

void draw_left_panel(ID3D11Device* device) {
    ImGui::BeginChild("left_panel", ImVec2(360, 0), true);

    if (ImGui::BeginTabBar("LeftPanelTabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("BNK List")) {
            ImGui::SetNextItemWidth(-1);
            if (!S.bnk_paths.empty()) {
                ImGui::InputTextWithHint("##bnk_filter", "Filter", &S.bnk_filter);
            }
            ImGui::BeginChild("bnk_list", ImVec2(0, 0), false);

            auto paths = filtered_bnk_paths();

            std::vector<std::string> audio_bnks;
            std::vector<std::string> other_bnks;

            for (const auto& p : paths) {
                if (is_in_audio_folder(p)) {
                    audio_bnks.push_back(p);
                } else {
                    other_bnks.push_back(p);
                }
            }

            if (!S.adb_paths.empty()) {
                ImGui::PushID("adb_entry");
                bool selected = S.viewing_adb;
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                if (ImGui::Selectable("Audio Database", selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    S.viewing_adb = true;
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

            for (size_t idx = 0; idx < paths.size(); ++idx) {
                auto &p = paths[idx];
                ImGui::PushID((int)idx);

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
                        if (is_expanded) {
                            S.expanded_bnks.erase(p);
                        } else {
                            S.expanded_bnks.insert(p);
                        }
                    }
                    S.viewing_adb = false;
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

                if (is_nested_bnk && is_expanded) {
                    try {
                        BNKReader reader(p);
                        const auto& files = reader.list_files();
                        for (size_t i = 0; i < files.size(); ++i) {
                            const auto& file = files[i];
                            std::string fname_lower = file.name;
                            std::transform(fname_lower.begin(), fname_lower.end(), fname_lower.begin(), ::tolower);
                            if (fname_lower.size() >= 4 && fname_lower.substr(fname_lower.size() - 4) == ".bnk") {
                                ImGui::PushID((int)i + 100000);
                                std::string nested_label = "    " + std::filesystem::path(file.name).filename().string();
                                bool nested_selected = (S.selected_nested_bnk == p && S.selected_nested_index == (int)i);
                                if (ImGui::Selectable(nested_label.c_str(), nested_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                                    S.viewing_adb = false;
                                    S.selected_bnk = p;
                                    S.selected_nested_bnk = p;
                                    S.selected_nested_index = (int)i;
                                    S.global_search.clear();
                                    S.files.clear();
                                    S.selected_file_index = -1;

                                    auto tmpdir = std::filesystem::temp_directory_path() / "f2_nested_bnk";
                                    std::error_code ec;
                                    std::filesystem::create_directories(tmpdir, ec);
                                    auto tmp_nested = tmpdir / (std::to_string(std::hash<std::string>{}(file.name)) + ".bnk");

                                    extract_one(p, (int)i, tmp_nested.string());

                                    S.selected_nested_temp_path = tmp_nested.string();

                                    BNKReader nested_reader(tmp_nested.string());
                                    const auto& nested_files = nested_reader.list_files();
                                    S.files.reserve(nested_files.size());
                                    for (size_t j = 0; j < nested_files.size(); ++j) {
                                        S.files.push_back({(int)j, nested_files[j].name, nested_files[j].uncompressed_size});
                                    }

                                    std::sort(S.files.begin(), S.files.end(), [](const BNKItemUI &a, const BNKItemUI &b) {
                                        std::string x = std::filesystem::path(a.name).filename().string();
                                        std::string y = std::filesystem::path(b.name).filename().string();
                                        std::transform(x.begin(), x.end(), x.begin(), ::tolower);
                                        std::transform(y.begin(), y.end(), y.begin(), ::tolower);
                                        return x < y;
                                    });
                                }
                                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                                    ImGui::BeginTooltip();
                                    ImGui::TextUnformatted(file.name.c_str());
                                    ImGui::EndTooltip();
                                }
                                ImGui::PopID();
                            }
                        }
                    } catch (...) {}
                }

                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();

        if (ImGui::BeginTabItem("File Tree")) {
            ImGui::BeginChild("file_tree", ImVec2(0, 0), false);

            static bool tree_built = false;
            static bool tree_building = false;
            static std::atomic<bool> build_complete(false);
            static float build_start_time = 0.0f;

            if (!tree_built && !tree_building && !S.bnk_paths.empty()) {
                tree_building = true;
                build_complete = false;
                build_start_time = ImGui::GetTime();

                TreeNode* root_ptr = &g_tree_root;
                std::atomic<bool>* complete_ptr = &build_complete;

                std::thread([root_ptr, complete_ptr]() {
                    build_unified_file_tree(*root_ptr);
                    complete_ptr->store(true);
                }).detach();
            }

            if (tree_building) {
                if (build_complete) {
                    tree_building = false;
                    tree_built = true;
                } else {
                    ImVec2 avail = ImGui::GetContentRegionAvail();
                    float elapsed = ImGui::GetTime() - build_start_time;

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
            } else if (tree_built) {
                for (auto& pair : g_tree_root.children) {
                    draw_tree_node(pair.second, device);
                }
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        }

        if (ImGui::BeginTabItem("File Tree")) {
            ImGui::BeginChild("file_tree", ImVec2(0, 0), false);

            static TreeNode root;
            static bool tree_built = false;
            static bool tree_building = false;
            static std::atomic<bool> build_complete(false);
            static float build_start_time = 0.0f;

            if (!tree_built && !tree_building && !S.bnk_paths.empty()) {
                tree_building = true;
                build_complete = false;
                build_start_time = ImGui::GetTime();

                TreeNode* root_ptr = &root;
                std::atomic<bool>* complete_ptr = &build_complete;

                std::thread([root_ptr, complete_ptr]() {
                    build_unified_file_tree(*root_ptr);
                    complete_ptr->store(true);
                }).detach();
            }

            if (tree_building) {
                if (build_complete) {
                    tree_building = false;
                    tree_built = true;
                } else {
                    ImVec2 avail = ImGui::GetContentRegionAvail();
                    float elapsed = ImGui::GetTime() - build_start_time;

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
            } else if (tree_built) {
                for (auto& pair : root.children) {
                    draw_tree_node(pair.second, device);
                }
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::EndChild();
}

void draw_file_table() {
    std::vector<int> vis;
    vis.reserve(S.files.size());
    for (size_t i = 0; i < S.files.size(); ++i)
        if (name_matches_filter(S.files[i].name, S.file_filter)) vis.push_back((int) i);

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
                std::string base = std::filesystem::path(S.files[i].name).filename().string();
                if (ImGui::Selectable(base.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
                    S.selected_file_index = i;
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

    std::vector<int> vis;
    vis.reserve(g_global_hits.size());
    for (size_t i = 0; i < g_global_hits.size(); ++i) {
        if (name_matches_filter(g_global_hits[i].file_name, S.file_filter)) {
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
                std::string base = std::filesystem::path(hit.file_name).filename().string();

                if (ImGui::Selectable(base.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    g_selected_global = i;
                    S.viewing_adb = false;
                    pick_bnk(hit.bnk_path);
                    for (size_t j = 0; j < S.files.size(); ++j) {
                        if (S.files[j].index == hit.index) {
                            S.selected_file_index = (int)j;
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
                std::string bnk_name = std::filesystem::path(hit.bnk_path).filename().string();
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

void draw_right_panel(ID3D11Device* device) {
    ImGui::BeginChild("right_panel", ImVec2(0, 0), false);

    ImGui::BeginChild("extract_box", ImVec2(0, 100), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginGroup();
    ImGui::PushItemWidth(-1);

    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 0));

    ImGui::BeginGroup();

    if (!S.viewing_adb) {
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
    } else {
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

    if (has_selection && !S.viewing_adb) {
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

if (!can_preview) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Preview")) {
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

        std::thread([device, item, name, can_tex, can_mdl, bnk_to_use, nested_temp_copy, is_nested]() {
            std::vector<unsigned char> buf;
            bool ok = false;
            try {
                if (can_tex) {
                    ok = build_any_tex_buffer_for_name(name, buf);
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
                        int best_mip = -1;
                        size_t best_area = 0;
                        for (int i = 0; i < (int)S.tex_info.Mips.size(); ++i) {
                            if (S.tex_info.Mips[i].CompFlag == 7) {
                                int w = S.tex_info.Mips[i].HasWH ? (int)S.tex_info.Mips[i].MipWidth : std::max(1, (int)S.tex_info.TextureWidth >> i);
                                int h = S.tex_info.Mips[i].HasWH ? (int)S.tex_info.Mips[i].MipHeight : std::max(1, (int)S.tex_info.TextureHeight >> i);
                                size_t area = (size_t)w * (size_t)h;
                                if (area > best_area) {
                                    best_area = area;
                                    best_mip = i;
                                }
                            }
                        }
                        if (best_mip >= 0) {
                            S.preview_mip_index = best_mip;
                            S.show_preview_popup = true;
                        }
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

        skip_preview:;
    }
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

    float available_width = ImGui::GetContentRegionAvail().x;
    float field_width = (available_width - 8.0f) * 0.5f;

    ImGui::SetNextItemWidth(field_width);
    ImGui::InputTextWithHint("##file_filter", S.viewing_adb ? "Filter ADB Files" : "Filter Current BNK", &S.file_filter);

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

                                        std::filesystem::path nested_parent = std::filesystem::path(fname).parent_path();
                                        std::string prefix = nested_parent.empty() ? "" : nested_parent.string() + "/";

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

    ImGui::BeginChild("right_table_container", ImVec2(0, 0), false);
    if (!S.global_search.empty()) {
        draw_global_results_table();
    } else {
        draw_file_table();
    }
    ImGui::EndChild();

    if(S.show_preview_popup){
        ImGui::OpenPopup("Mip Preview");
        S.show_preview_popup = false;
    }

    if(ImGui::BeginPopupModal("Mip Preview", nullptr, ImGuiWindowFlags_None)){
        if(S.preview_mip_index >= 0 && S.preview_mip_index < (int)S.tex_info.Mips.size()){
            const auto& m = S.tex_info.Mips[S.preview_mip_index];
            if(!S.preview_srv){
                uint32_t base_w = S.tex_info.TextureWidth;
                uint32_t base_h = S.tex_info.TextureHeight;
                uint32_t w = m.HasWH ? (uint32_t)std::max(1,(int)m.MipWidth)  : std::max(1u, base_w >> S.preview_mip_index);
                uint32_t h = m.HasWH ? (uint32_t)std::max(1,(int)m.MipHeight) : std::max(1u, base_h >> S.preview_mip_index);
                if(m.MipDataOffset < S.hex_data.size() && m.MipDataOffset + m.MipDataSizeParsed <= S.hex_data.size()){
                    const uint8_t* src = S.hex_data.data() + m.MipDataOffset;
                    size_t src_sz = m.MipDataSizeParsed;

                    DXGI_FORMAT fmt = DXGI_FORMAT_BC1_UNORM;
                    if(S.tex_info.PixelFormat == 39) fmt = DXGI_FORMAT_BC3_UNORM;
                    else if(S.tex_info.PixelFormat == 40) fmt = DXGI_FORMAT_BC5_UNORM;

                    size_t blocks_x = (w + 3) / 4;
                    std::vector<uint8_t> payload(src, src + src_sz);

                    for(size_t i = 0; i + 8 <= payload.size(); i += 8) {
                        uint16_t c0 = (payload[i+0] << 8) | payload[i+1];
                        uint16_t c1 = (payload[i+2] << 8) | payload[i+3];
                        uint32_t idx = (payload[i+4] << 24) | (payload[i+5] << 16) | (payload[i+6] << 8) | payload[i+7];

                        payload[i+0] = c0 & 0xFF;
                        payload[i+1] = (c0 >> 8) & 0xFF;
                        payload[i+2] = c1 & 0xFF;
                        payload[i+3] = (c1 >> 8) & 0xFF;
                        payload[i+4] = idx & 0xFF;
                        payload[i+5] = (idx >> 8) & 0xFF;
                        payload[i+6] = (idx >> 16) & 0xFF;
                        payload[i+7] = (idx >> 24) & 0xFF;
                    }

                    D3D11_TEXTURE2D_DESC td{};
                    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = fmt;
                    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                    D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = payload.data(); sd.SysMemPitch = (UINT)(blocks_x * 8);
                    ID3D11Texture2D* tex = nullptr;
                    if(device->CreateTexture2D(&td, &sd, &tex) == S_OK){
                        D3D11_SHADER_RESOURCE_VIEW_DESC svd{};
                        svd.Format = td.Format; svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; svd.Texture2D.MipLevels = 1;
                        device->CreateShaderResourceView(tex, &svd, &S.preview_srv); tex->Release();
                    }
                }
            }
            if(S.preview_srv) ImGui::Image((ImTextureID)S.preview_srv, ImVec2(512, 512));
            else ImGui::TextUnformatted("Preview unsupported or failed.");
        }else{
            ImGui::TextUnformatted("No mip selected");
        }
        if(ImGui::Button("Close", ImVec2(-1,0))) {
            if(S.preview_srv) { S.preview_srv->Release(); S.preview_srv = nullptr; }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    {
        if(S.show_model_preview){ ImGui::OpenPopup("Model Preview"); S.show_model_preview = false; }

        const ImVec2 canvas(960, 640);
        const ImVec2 win_size(canvas.x + 32.0f, canvas.y + 110.0f);

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Always);
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

        if(ImGui::BeginPopupModal("Model Preview", nullptr, ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings))
        {
            extern ModelPreview g_mp;
            MP_Render(device, g_mp, S.cam_yaw, S.cam_pitch, S.cam_dist);

            ImVec2 pos = ImGui::GetCursorScreenPos();
            if(g_mp.srv) ImGui::GetWindowDrawList()->AddImage((ImTextureID)g_mp.srv, pos, ImVec2(pos.x + canvas.x, pos.y + canvas.y));
            ImGui::InvisibleButton("model_canvas", canvas);

            float dt = ImGui::GetIO().DeltaTime;
            S.cam_yaw += dt * 0.6f;
            if(S.cam_yaw > 6.2831853f) S.cam_yaw -= 6.2831853f;

            if(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)){
                float wheel = ImGui::GetIO().MouseWheel;
                if(fabsf(wheel) > 0.0001f) S.cam_dist *= (wheel > 0.f ? 0.9f : 1.1f);
            }

            if(ImGui::Button("Zoom -", ImVec2(90,0))) S.cam_dist *= 1.1f;
            ImGui::SameLine();
            if(ImGui::Button("Zoom +", ImVec2(90,0))) S.cam_dist *= 0.9f;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220);
            ImGui::SliderFloat("##zoom", &S.cam_dist, 0.3f, 50.0f, "Dist %.2f");
            if(S.cam_dist < 0.3f)  S.cam_dist = 0.3f;
            if(S.cam_dist > 50.0f) S.cam_dist = 50.0f;

            ImGui::Dummy(ImVec2(0,6));
            if(ImGui::Button("Close", ImVec2(-1,0))) {
                MP_Release(g_mp);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    ImGui::EndChild();
}