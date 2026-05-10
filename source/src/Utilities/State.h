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

// Flat-list entry used by the Models / Textures tabs in the left panel.
// Built once during build_unified_file_tree() — covers files in every
// top-level BNK plus nested BNKs that were extracted to disk. Click-load
// uses the same g_pending_*_load pipe the tree click does.
struct FlatAssetEntry {
    std::string name;       // logical filename, e.g. "props_barrel_01.tex"
    std::string full_path;  // full asset path inside the BNK (e.g. "props/foo/bar.mdl")
    std::string bnk_path;   // BNK (top-level or nested temp path) it lives in
    int file_index;         // index inside that BNK's directory listing
    uint32_t size;          // file size in bytes (0 if unknown)
    bool from_nested;       // true if bnk_path is a nested-BNK temp path
};

struct State {
    std::string root_dir;
    std::vector<std::string> bnk_paths;
    // Disk-extracted nested BNKs the file-tree builder pulled out of
    // parent BNKs. build_any_tex_buffer_for_name() and friends search
    // these in addition to bnk_paths so textures that live inside a
    // nested BNK (region archives, etc.) can be opened from the preview.
    std::vector<std::string> nested_bnk_paths;
    // Map from nested BNK temp path -> the parent BNK it was extracted from.
    // Used by texture lookup to prefer sibling nested BNKs (header+body+mip0
    // from the same parent) over generic top-level globals when the user
    // clicks a texture inside a nested BNK.
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

    // Flat caches for the Models / Textures tabs in the left panel.
    // Populated by build_unified_file_tree() — one pass over every BNK
    // (top-level + nested) collecting .mdl / .tex entries respectively.
    // Filter strings live next to them so the UI keeps the input
    // value across tab switches.
    std::vector<FlatAssetEntry> all_mdl_files;
    std::vector<FlatAssetEntry> all_tex_files;
    std::vector<FlatAssetEntry> all_wav_files;
    // Per-character animation config files (.anim). Each .anim
    // contains a hash → clip-name table that, once parsed, lets us
    // override the global TOC's id_HHHHHHHH names with friendly
    // strings. Indexed at file-tree-build time alongside the other
    // flat-list extensions.
    std::vector<FlatAssetEntry> all_anim_files;
    std::string mdl_filter;
    std::string tex_filter;
    std::string wav_filter;
    std::string anim_filter_files;

    // ---- User-facing settings (persisted to config.ini) ----
    // show_paths controls whether the tree/file-table tooltips show the
    // full asset path on hover. Mirrors the inverse of hide_tooltips —
    // we keep both because hide_tooltips is the legacy flag that older
    // code paths still check.
    bool show_paths = true;
    // Developer mode — when off (the default) the tree tooltips and
    // any in-panel info overlays show only the filename. When on, they
    // also include file size, BNK source, decoder/format details etc.
    // Toggled from the Settings dropdown.
    bool dev_mode = false;
    // Base font size in pixels. The actual ImGui atlas only rebuilds
    // when font_size_dirty flips true (handled before NewFrame in
    // main.cpp). pending_font_size lets the slider preview without
    // hammering the atlas every frame mid-drag.
    float font_size           = 17.0f;
    float pending_font_size   = 17.0f;
    bool  font_size_dirty     = false;
    bool  show_settings       = false;
    // Texture / asset export root. Default initialised to
    // <exe_dir>/extracted on first startup; user-changeable via the
    // Settings dropdown. Each export writes to
    //   `${export_dir}/${asset_relative_path}.${ext}`
    // creating any intermediate dirs as it goes. Persisted to
    // config.ini ("export_dir" key).
    std::string export_dir;
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

    // ---- Texture mip browsing ----
    // Cache of the .tex blob currently displayed in the texture preview,
    // plus the user-chosen mip index. The mip selector overlay re-decodes
    // out of `texture_blob` when the index changes, avoiding a re-extract
    // from BNK. `pending_texture_mip_change` signals UI_Main to do the
    // re-decode + SRV swap; the worker thread that originally loaded the
    // texture also primes these fields so the dropdown shows up at first
    // open. tex_info already lives above (`bool tex_info_ok` / `TexInfo
    // tex_info`) and gets populated alongside `texture_blob`.
    std::vector<unsigned char> texture_blob;
    int  texture_mip_index = 0;
    std::atomic<bool> pending_texture_mip_change{false};

    // Per-channel display toggles for the texture preview. When a flag
    // is false, that channel is masked out as the RGBA buffer is
    // uploaded to the GPU — R/G/B get zeroed, A is forced to 255 so the
    // image still shows opaque. See apply_tex_channel_mask in UI_Main.
    // Toggling a checkbox flips the flag AND raises
    // pending_texture_mip_change so the mask is re-applied.
    bool tex_show_r = true;
    bool tex_show_g = true;
    bool tex_show_b = true;
    bool tex_show_a = true;

    bool mdl_info_ok = false;
    MDLInfo mdl_info;

    bool show_model_preview = false;        // trigger: set true to open the model preview window
    bool model_preview_open = false;          // window visibility (kept in sync with the &open close button)
    bool model_materials_open = false;        // separate Materials window, opens automatically with model
    std::atomic<bool> pending_preview_build{false};
    std::vector<MDLMeshGeom> mdl_meshes;

    // ---- Bone editing (skeleton overlay + R-rotate keybind) ----
    // Per-bone rotation deltas applied on top of the rest pose.
    // Stored as quaternions xyzw (so length is BoneCount*4). Empty when
    // no model is loaded; resized + zeroed (identity quat = (0,0,0,1))
    // by MP_Build whenever a new model is built.
    //
    // Both ModelPreview's skinning shader and RenderPanel's skeleton
    // overlay consume these — so editing one bone updates skeleton + mesh
    // simultaneously.
    std::vector<float> bone_rot_deltas;
    int  selected_bone     = -1;   // -1 when nothing is picked
    bool bone_rotate_mode  = false; // true while the user is in R-rotate
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
    // Set to true when the user clicks a Lua script in the BNK List
    // drill-in view. The render panel checks this and renders the
    // decompiled Lua source full-bleed in place of the model / texture
    // / placeholder. Cleared automatically when the user clicks a
    // model or texture (by the relevant load path setting its own
    // state); the in-panel close button also clears it explicitly.
    bool show_lua_render = false;

    // ---- Animations -----------------------------------------------------
    // Single global TOC parsed at root-load time from
    //   <root>/data/animation/fable2_anims.animation_toc
    // (or the equivalent virtual path inside a mounted ISO). Each clip
    // points into the .animation_data blob via byte offset/length —
    // we don't read the data file itself yet (Phase C).
    //
    // anim_filter is the search box value used by the UI panels. Lives
    // here so it persists across tab switches like the other filters.
    std::vector<Anim::AnimClip> anim_clips;
    std::string                 anim_filter;
    int                         anim_selected_clip = -1;
};

extern State S;