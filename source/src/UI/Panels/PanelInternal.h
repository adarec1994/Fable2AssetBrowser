#pragma once
// Internal header shared by the files split out of UI_Panels.cpp. The
// public surface is still in UI_Panels.h; this just lets the .cpp files
// in src/UI/Panels/ see one another's globals and helpers without
// promoting them to the public interface.

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <cstdint>

#include "../../Utilities/State.h"

#ifdef _WIN32
#include <d3d11.h>
#endif

// ---------------------------------------------------------------------------
// File tree node — populated by build_unified_file_tree, walked by
// draw_tree_node. Lives in TreeBuilder.cpp.
// ---------------------------------------------------------------------------
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

extern TreeNode g_tree_root;

// Tree-build worker state — defined in TreeBuilder.cpp.
extern std::atomic<bool> g_tree_built;
extern std::atomic<bool> g_tree_building;
extern std::atomic<bool> g_tree_build_complete;
extern float             g_tree_build_start_time;
extern std::string       g_tree_last_root_dir;
extern std::atomic<int>  g_tree_done_units;
extern std::atomic<int>  g_tree_total_units;
extern std::mutex        g_tree_label_mutex;
extern std::string       g_tree_current_label;

void set_tree_label(std::string s);
void start_tree_build_for_root(const std::string& root_dir,
                               std::vector<std::string> bnk_paths);
bool find_mdl_files_in_folder(TreeNode& root, const std::string& folder_name,
                              std::vector<std::pair<std::string, std::string>>& out_mdl_paths);

// ---------------------------------------------------------------------------
// Pending preview / texture loads — set from various click sites
// (draw_tree_node, load_flat_asset_entry, draw_file_table,
// draw_global_results_table, draw_right_panel) and consumed by
// process_pending_loads. Defined in PendingLoads.cpp.
// ---------------------------------------------------------------------------
extern std::atomic<bool> g_pending_mdl_load;
extern int               g_pending_mdl_index;
extern std::string       g_pending_mdl_full_path;
extern std::atomic<bool> g_pending_tex_load;
extern int               g_pending_tex_index;

// ---------------------------------------------------------------------------
// Global-search state — defined in FileTable.cpp.
// ---------------------------------------------------------------------------
struct GlobalHit;  // forward — full type from operations.h

extern std::vector<GlobalHit> g_global_hits;
extern std::atomic<bool>      g_global_busy;
extern std::atomic<bool>      g_cancel_search;
extern std::string            g_last_global_search;
extern int                    g_selected_global;

// ---------------------------------------------------------------------------
// Internal helpers shared across the split files.
// ---------------------------------------------------------------------------
bool open_audio_player_for_selected(int file_index);  // defined in Selection.cpp
bool reconstruct_nested_mdl(const std::string& nested_bnk_path, int file_index,
                            std::vector<unsigned char>& out);  // Selection.cpp
bool is_in_audio_folder(const std::string& path);  // Selection.cpp
void load_flat_asset_entry(const FlatAssetEntry& e, int kind);  // LeftPanel.cpp

// draw_tree_node lives in TreeRender.cpp; signature varies with platform.
#ifdef _WIN32
void draw_tree_node(TreeNode& node, ID3D11Device* device);
#else
void draw_tree_node(TreeNode& node);
#endif
