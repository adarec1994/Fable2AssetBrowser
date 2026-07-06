#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <cstdint>
#include "imgui_hex.h"
#include "../MDL/ModelParser.h"
#include "../animations/AnimBank.h"
#include "../Level/GdbParser.h"

#ifdef _WIN32
#include <d3d11.h>
#endif

struct BNKItemUI {
    int index;
    std::string name;
    uint32_t size;
};

struct TexInfo {
    uint32_t Sign;
    uint32_t RawDataSize;
    uint32_t Unknown_0;
    uint32_t Unknown_1;
    uint32_t TextureWidth;
    uint32_t TextureHeight;
    uint32_t PixelFormat;
    uint32_t MipMap;
    std::vector<uint32_t> MipMapOffset;

    struct MipDef {
        size_t DefOffset;
        uint32_t CompFlag;
        uint32_t DataOffset;
        uint32_t DataSize;
        uint32_t Unknown_3;
        uint32_t Unknown_4;
        uint32_t Unknown_5;
        uint32_t Unknown_6;
        uint32_t Unknown_7;
        uint32_t Unknown_8;
        uint32_t Unknown_9;
        uint32_t Unknown_10;
        uint32_t Unknown_11;
        bool HasWH;
        uint16_t MipWidth;
        uint16_t MipHeight;
        size_t MipDataOffset;
        size_t MipDataSizeParsed;
    };

    std::vector<MipDef> Mips;
};

struct LuaFileUI {
    int index;
    std::string path;
    std::string filename;
    uint32_t size;
};

struct FlatAssetEntry {
    std::string name;
    std::string full_path;
    std::string bnk_path;
    int file_index;
    uint32_t size;
    bool from_nested;
};

struct GdbViewerRow {
    std::string name;
    std::string hash_name;
    std::string parent_name;
    std::string model_path_name;
    std::string skeleton_file_name;
    std::string retarget_skeleton_file_name;
    std::vector<std::string> model_path_names;
    std::vector<uint32_t> model_path_hashes;
    uint32_t marker = 0;
    uint32_t record_index = 0;
    uint32_t hash = 0;
    uint32_t parent_hash = 0;
    uint32_t model_path_hash = 0;
    uint32_t skeleton_file_hash = 0;
    uint32_t retarget_skeleton_file_hash = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    float rot_x = 0.0f;
    float rot_y = 0.0f;
    float rot_z = 0.0f;
    float scale = 1.0f;
    bool has_rotation = false;
    bool indexed_record = false;
    bool transform_from_indexed_record = false;
};

struct State {
    std::string root_dir;
    std::vector<std::string> bnk_paths;

    std::vector<std::string> nested_bnk_paths;

    std::map<std::string, std::string> nested_bnk_parents;
    std::map<std::string, std::string> nested_bnk_virtual_paths;
    std::vector<std::string> adb_paths;
    std::vector<LuaFileUI> lua_files;
    std::string bnk_filter;
    std::string selected_bnk;
    std::string selected_nested_bnk;
    std::set<std::string> expanded_bnks;
    int selected_nested_index = -1;
    std::string selected_nested_temp_path;
    bool viewing_adb = false;
    bool viewing_lua = false;
    std::vector<BNKItemUI> files;
    int selected_file_index = -1;
    bool hide_tooltips = false;

    std::vector<FlatAssetEntry> all_mdl_files;
    std::vector<FlatAssetEntry> all_tex_files;
    std::vector<FlatAssetEntry> all_wav_files;

    std::vector<FlatAssetEntry> all_anim_files;
    std::vector<FlatAssetEntry> all_level_files;
    std::vector<FlatAssetEntry> all_heightfield_files;
    bool terrain_mode = false;
    bool show_gdb_placements = false;
    std::string mdl_filter;
    std::string tex_filter;
    std::string wav_filter;
    std::string anim_filter_files;
    std::string level_filter;
    std::string lua_filter;

    bool show_paths = true;

    bool dev_mode = false;

    // Dev-only: render terrain with the engine-reconciled LANDSCAPEMATERIAL blend
    // (per-material tiling 16/dim, weight-normalized) instead of the current
    // shared-scale path. A/B toggle; only takes effect in dev mode.
    bool terrain_landscape_blend = false;

    // Show the neighbouring levels' heightfields (the "adjacent terrain"
    // meshes loaded with a level) in the level preview.
    bool show_adjacent_terrain = true;

    float font_size           = 17.0f;
    float pending_font_size   = 17.0f;
    bool  font_size_dirty     = false;
    bool  show_settings       = false;

    bool  cam_invert_x = false;
    bool  cam_invert_y = false;

    ImGuiKey key_forward       = ImGuiKey_W;
    ImGuiKey key_back          = ImGuiKey_S;
    ImGuiKey key_left          = ImGuiKey_D;
    ImGuiKey key_right         = ImGuiKey_A;
    ImGuiKey key_up            = ImGuiKey_E;
    ImGuiKey key_down          = ImGuiKey_Q;
    ImGuiKey key_rotate_mode   = ImGuiKey_R;
    int      capturing_key_id  = -1;
    bool     show_keybinds_window = false;

    std::string export_dir;

    std::string mdl_texture_export_format = "DDS";
    std::atomic<bool> cancel_requested{false};
    std::atomic<bool> exiting{false};
    std::mutex progress_mutex;
    int progress_total = 0;
    int progress_current = 0;
    std::string progress_label;
    std::atomic<bool> show_progress{false};
    bool show_error = false;
    std::string error_text;
    bool show_completion = false;
    std::string completion_text;
    std::string file_filter;
    std::string ext_filter;
    std::string global_search;
    std::string last_dir;
    std::string selected_folder_path;
    std::atomic<bool> hex_loading{false};
    bool hex_open = false;
    std::string hex_title;
    std::vector<unsigned char> hex_data;
    ImGuiHexEditorState hex_state;
    bool tex_info_ok = false;
    TexInfo tex_info;
    size_t pending_goto = (size_t) -1;
    bool show_preview_popup = false;
    int preview_mip_index = -1;
#ifdef _WIN32
    ID3D11ShaderResourceView *preview_srv = nullptr;
#else
    unsigned int preview_tex = 0;
#endif
    std::string hex_file_path;

    bool show_texture_window = false;
    std::string texture_window_name;
    int texture_window_width = 0;
    int texture_window_height = 0;
#ifdef _WIN32
    ID3D11ShaderResourceView *texture_window_srv = nullptr;
#else
    unsigned int texture_window_gl = 0;
#endif
    std::atomic<bool> pending_texture_load{false};
    std::vector<unsigned char> pending_texture_rgba;
    int pending_texture_w = 0;
    int pending_texture_h = 0;

    std::vector<unsigned char> texture_blob;
    int  texture_mip_index = 0;
    std::atomic<bool> pending_texture_mip_change{false};

    bool tex_show_r = true;
    bool tex_show_g = true;
    bool tex_show_b = true;
    bool tex_show_a = true;

    bool mdl_info_ok = false;
    MDLInfo mdl_info;

    bool show_model_preview = false;
    bool model_preview_open = false;
    bool model_materials_open = false;
    std::atomic<bool> pending_preview_build{false};
    std::vector<MDLMeshGeom> mdl_meshes;

    std::vector<float> bone_rot_deltas;
    std::vector<float> bone_anim_rot_absolute;
    std::vector<uint8_t> bone_anim_rot_present;
    std::vector<float> bone_anim_trans_delta;
    std::vector<uint8_t> bone_anim_trans_present;
    bool bone_anim_pose_active = false;
    int  selected_bone     = -1;
    bool bone_rotate_mode  = false;
    float cam_yaw = 0.0f;
    float cam_pitch = 0.2f;
    float cam_dist = 3.0f;
#ifdef _WIN32
    ID3D11ShaderResourceView* model_diffuse_srv = nullptr;
#else
    unsigned int model_diffuse_tex = 0;
#endif

    std::string lua_preview_content;
    std::string lua_preview_title;
    int lua_preview_selected = -1;
    std::atomic<bool> lua_preview_loading{false};

    bool show_lua_render = false;

    std::string gdb_view_title;
    std::string gdb_view_filter;
    std::vector<GdbViewerRow> gdb_view_rows;
    bool show_gdb_render = false;

    std::vector<Anim::AnimClip> anim_clips;
    std::string                 anim_filter;
    bool                        anim_compatible_only = true;
    bool                        anim_authored_only = true;
    int                         anim_selected_clip = -1;
    std::string                 current_mdl_path;
    uint32_t                    current_mdl_path_hash = 0;
    uint64_t                    anim_authored_signature = 0;
    std::vector<uint8_t>         anim_authored_cache;
    uint64_t                    anim_compat_signature = 0;
    std::vector<uint8_t>         anim_compat_cache;
    std::vector<uint16_t>        anim_compat_matches;
    std::vector<uint16_t>        anim_compat_named_tracks;
};

extern State S;
