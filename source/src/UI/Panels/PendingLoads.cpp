#include "../UI_Panels.h"
#include "PanelInternal.h"
#include "../ModelPreview.h"
#include "../../textures/TexParser.h"
#include "../../textures/LhTexCodec.h"
#include "../../MDL/ModelParser.h"
#include "../../MDL/mdl_converter.h"
#include "../../Utilities/Files.h"
#include "../../Utilities/Utils.h"
#include "../../Utilities/Progress.h"
#include "../../BNKCore.cpp"
#include <filesystem>
#include <atomic>
#include <thread>
#include <ctime>
#include <cstring>

std::atomic<bool> g_pending_mdl_load{false};
int g_pending_mdl_index = -1;
std::string g_pending_mdl_full_path;

std::atomic<bool> g_pending_tex_load{false};
int g_pending_tex_index = -1;

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

        // Synchronous main-thread cleanup BEFORE kicking the parse
        // worker. Anything the render panel might dereference next
        // frame has to be torn down here, on the same thread that
        // does the drawing — otherwise the worker can free a D3D
        // resource (texture SRV, mesh array) right while ImGui is
        // walking it for AddImage / IsItemHovered. That race was the
        // source of the "switching from texture preview to model
        // crashes / shows weird things" bug.
        //
        // Order matters: drop the texture-preview SRV first so the
        // render panel falls through to the placeholder while the
        // new model parses, then mark the existing model preview as
        // "no model" so the Materials/Skeleton overlays short-circuit.
        // The worker still owns the heavy MP_Release / MP_Init /
        // MP_Build sequence below; flipping has_model = false here
        // just stops the UI from iterating g_mp.meshes mid-rebuild.
#ifdef _WIN32
        if (S.texture_window_srv) {
            S.texture_window_srv->Release();
            S.texture_window_srv = nullptr;
        }
        // Texture popout points into the materials' SRVs of whatever
        // model is going away — close it so it doesn't repaint with a
        // dangling pointer.
        extern ID3D11ShaderResourceView* g_tex_popout_srv;
        extern std::string                g_tex_popout_name;
        extern bool                       g_tex_popout_open;
        extern int                        g_tex_popout_mesh_idx;
        g_tex_popout_srv      = nullptr;
        g_tex_popout_name.clear();
        g_tex_popout_open     = false;
        g_tex_popout_mesh_idx = -1;
#else
        if (S.texture_window_gl) {
            glDeleteTextures(1, &S.texture_window_gl);
            S.texture_window_gl = 0;
        }
#endif
        S.texture_window_name.clear();
        S.texture_window_width  = 0;
        S.texture_window_height = 0;
        S.texture_blob.clear();
        S.tex_info_ok           = false;
        S.show_texture_window   = false;
        S.show_preview_popup    = false;
        S.preview_mip_index     = -1;

        // Materials/skeleton overlay state — same reasoning. We don't
        // reach into g_mp here (the worker will MP_Release it); we
        // just stop the overlays from iterating its data this frame.
        extern ModelPreview g_mp;
        if (g_mp.has_model) g_mp.has_model = false;
        S.mdl_info_ok        = false;
        S.show_model_preview = false;
        S.model_preview_open = false;
        S.model_materials_open = false;
        S.selected_bone      = -1;
        S.bone_rotate_mode   = false;

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
                        // Default yaw = PI: Fable 2 character/prop MDLs are
                        // authored facing -Z, so a yaw-0 orbit camera at +Z
                        // looks at the back. Spinning 180° drops us in
                        // front of the mesh on first load.
                        S.cam_yaw = 3.14159265f;
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
                S.texture_blob.clear();
                S.tex_info_ok = false;
                progress_done();
                return;
            }
            // Parse + cache the blob's mip layout so the preview's mip
            // selector knows what to show without round-tripping through
            // the BNK extract again. Reset to mip 0 (= largest in
            // well-formed files) for each fresh load.
            S.tex_info_ok = parse_tex_info(tex_buf, S.tex_info);
            S.texture_mip_index = 0;
            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            bool has_alpha = false;
            if (!decode_tex_to_rgba(tex_buf, rgba, w, h, &has_alpha,
                                    /*mip_index=*/0)) {
                S.texture_window_name = "ERROR: Could not decode texture";
                S.pending_texture_load = true;
                S.pending_texture_w = 0;
                S.pending_texture_h = 0;
                S.texture_blob.clear();
                S.tex_info_ok = false;
                progress_done();
                return;
            }
            S.texture_blob = std::move(tex_buf);
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
