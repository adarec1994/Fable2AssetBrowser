#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <d3d11.h>
#include "imgui_hex.h"
#include "ModelParser.h"

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

struct State {
    std::string root_dir;
    std::vector<std::string> bnk_paths;
    std::vector<std::string> adb_paths;
    std::string bnk_filter;
    std::string selected_bnk;
    bool viewing_adb = false;
    std::vector<BNKItemUI> files;
    int selected_file_index = -1;
    bool hide_tooltips = false;
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
    std::string global_search;
    std::string last_dir;
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
    ID3D11ShaderResourceView *preview_srv = nullptr;

    bool mdl_info_ok = false;
    MDLInfo mdl_info;

    bool show_model_preview = false;
    std::vector<MDLMeshGeom> mdl_meshes;
    float cam_yaw = 0.0f;
    float cam_pitch = 0.2f;
    float cam_dist = 3.0f;
    ID3D11ShaderResourceView* model_diffuse_srv = nullptr;
};

extern State S;