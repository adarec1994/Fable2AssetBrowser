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

struct State {
    std::string root_dir;
    std::vector<std::string> bnk_paths;

    std::vector<std::string> nested_bnk_paths;

    std::map<std::string, std::string> nested_bnk_parents;
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
    /* One entry per `.engine_level` we find in any loaded BNK.
       Each represents a discoverable level (world / region / scenario
       combo).  Same FlatAssetEntry shape as the other asset lists so
       it slots into the same UI helpers. */
    std::vector<FlatAssetEntry> all_level_files;
    /* Loose-format heightfield siblings of a `.engine_level` — the
       terrain mesh (`.ehf`), gzipped raw heights (`.ghf`), heightfield
       database (`.hdb`), environment table (`.genv`) and the three
       `ADMP` variants (`.ama` / `.amm` / `.amr`).  Indexed for the
       Levels tab + the terrain pipeline to find them by BNK lookup. */
    std::vector<FlatAssetEntry> all_heightfield_files;
    /* True while the render panel is showing a terrain (heightfield
       mesh) instead of a regular MDL.  When set, the render panel
       routes input through the flycam (WASD + Q/E + right-drag look)
       and skips the orbit-on-left-drag logic. */
    bool terrain_mode = false;
    std::string mdl_filter;
    std::string tex_filter;
    std::string wav_filter;
    std::string anim_filter_files;
    std::string level_filter;

    bool show_paths = true;

    bool dev_mode = false;

    float font_size           = 17.0f;
    float pending_font_size   = 17.0f;
    bool  font_size_dirty     = false;
    bool  show_settings       = false;

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

    std::vector<Anim::AnimClip> anim_clips;
    std::string                 anim_filter;
    int                         anim_selected_clip = -1;
};

extern State S;
