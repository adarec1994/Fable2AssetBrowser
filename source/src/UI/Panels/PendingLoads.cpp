#include "../UI_Panels.h"
#include "PanelInternal.h"
#include "../ModelPreview.h"
#include "../OutputLog.h"
#include "../../textures/TexParser.h"
#include "../../textures/LhTexCodec.h"
#include "../../MDL/ModelParser.h"
#include "../../MDL/mdl_converter.h"
#include "../../Level/LevelLoader.h"
#include "../../Level/TextureAtlasDecoder.h"
#include "../../Level/TerrainTextureRegistry.h"
#include "../../Level/EhfLodThumbnails.h"
#include "../../Level/EhfChunkParser.h"
#include "../../Level/TerrainSplat.h"
#include "../../Level/TerrainEdit.h"
#include "../../Utilities/Files.h"
#include "../../Utilities/Utils.h"
#include "../../Utilities/Progress.h"
#include "../../BNKCore.cpp"
#include <filesystem>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <thread>
#include <ctime>
#include <cstring>
#include <sstream>
#include <unordered_map>

#ifndef _WIN32
#include <GL/glew.h>
#endif

std::atomic<bool> g_pending_mdl_load{false};
int g_pending_mdl_index = -1;
std::string g_pending_mdl_full_path;

std::atomic<bool> g_pending_tex_load{false};
int g_pending_tex_index = -1;

namespace {

struct CachedPropModel {
    bool loaded = false;
    MDLInfo info;
    std::vector<MDLMeshGeom> geoms;
};

static void append_transformed_prop_geom(std::vector<MDLMeshGeom>& out,
                                         const MDLMeshGeom& src,
                                         const Level::PropInstance& inst,
                                         float terrain_cx,
                                         float terrain_cz);

#ifdef _WIN32
struct LevelPropStreamState {
    bool active = false;
    std::vector<Level::PropBlock> blocks;
    std::string model_body_bnk;
    std::vector<MDLMeshGeom> geoms;
    MDLInfo info;
    std::unordered_map<std::string, CachedPropModel> cache;
    size_t block_index = 0;
    size_t instance_index = 0;
    size_t instances_loaded = 0;
    size_t total_instances = 0;
    size_t model_misses = 0;
    float terrain_tile_size = 1.0f;
    int terrain_width = 0;
    int terrain_height = 0;
};

static LevelPropStreamState g_level_prop_stream;

static void start_level_prop_stream(std::vector<MDLMeshGeom> geoms,
                                    MDLInfo info)
{
    g_level_prop_stream = LevelPropStreamState{};
    g_level_prop_stream.blocks = g_pending_level_prop_blocks;
    g_level_prop_stream.model_body_bnk = g_pending_level_model_body_bnk;
    g_level_prop_stream.geoms = std::move(geoms);
    g_level_prop_stream.info = std::move(info);
    g_level_prop_stream.terrain_tile_size = g_pending_terrain_ghf_tile_size;
    g_level_prop_stream.terrain_width = g_pending_terrain_ghf_width;
    g_level_prop_stream.terrain_height = g_pending_terrain_ghf_height;
    for (const auto& b : g_level_prop_stream.blocks) {
        g_level_prop_stream.total_instances += b.instances.size();
    }
    g_level_prop_stream.total_instances =
        std::min<size_t>(g_level_prop_stream.total_instances, 256);
    g_level_prop_stream.active = g_level_prop_stream.total_instances > 0;
}

static bool stream_level_prop_batch(ID3D11Device* device)
{
    if (!g_level_prop_stream.active) return false;

    constexpr size_t kBatchInstances = 8;
    const float terrain_cx =
        (float(g_level_prop_stream.terrain_width) - 1.0f) * 0.5f *
        g_level_prop_stream.terrain_tile_size;
    const float terrain_cz =
        (float(g_level_prop_stream.terrain_height) - 1.0f) * 0.5f *
        g_level_prop_stream.terrain_tile_size;

    size_t added_this_frame = 0;
    while (added_this_frame < kBatchInstances &&
           g_level_prop_stream.instances_loaded <
               g_level_prop_stream.total_instances &&
           g_level_prop_stream.block_index < g_level_prop_stream.blocks.size())
    {
        auto& block = g_level_prop_stream.blocks[g_level_prop_stream.block_index];
        if (block.model_path.empty() ||
            g_level_prop_stream.instance_index >= block.instances.size())
        {
            ++g_level_prop_stream.block_index;
            g_level_prop_stream.instance_index = 0;
            continue;
        }

        auto& cached = g_level_prop_stream.cache[block.model_path];
        if (!cached.loaded) {
            std::vector<unsigned char> buf;
            if (build_mdl_buffer_for_name_with_body(block.model_path,
                                                    g_level_prop_stream.model_body_bnk,
                                                    buf) &&
                parse_mdl_info(buf, cached.info, block.model_path)) {
                bool all_empty = !cached.info.MeshBuffers.empty();
                for (const auto& mb : cached.info.MeshBuffers) {
                    if (mb.VertexCount > 0) {
                        all_empty = false;
                        break;
                    }
                }
                if (all_empty) {
                    reparse_mdl_buffers_via_polymsh_scan(buf, cached.info);
                }
                parse_mdl_geometry(buf, cached.info, cached.geoms);
            }
            cached.loaded = true;
            if (cached.geoms.empty()) {
                ++g_level_prop_stream.model_misses;
            }
        }

        if (!cached.geoms.empty()) {
            const auto& inst = block.instances[g_level_prop_stream.instance_index];
            for (const auto& src : cached.geoms) {
                if (!src.positions.empty() && !src.indices.empty()) {
                    append_transformed_prop_geom(g_level_prop_stream.geoms,
                                                 src, inst,
                                                 terrain_cx, terrain_cz);
                }
            }
            ++added_this_frame;
        }

        ++g_level_prop_stream.instance_index;
        ++g_level_prop_stream.instances_loaded;
    }

    if (added_this_frame > 0) {
        extern ModelPreview g_mp;
        extern bool         g_mp_initialized;
        extern FlyCam       g_flycam;
        FlyCam saved_cam = g_flycam;
        if (!g_mp_initialized) {
            MP_Init(device, g_mp, 800, 600);
            g_mp_initialized = true;
        }
        MP_Build(device, g_level_prop_stream.geoms,
                 g_level_prop_stream.info, g_mp);
        g_mp.no_tilt = true;
        S.terrain_mode = true;
        g_flycam = saved_cam;
    }

    const int pct = 75 + (int)(
        (20.0 * double(g_level_prop_stream.instances_loaded)) /
        double(std::max<size_t>(g_level_prop_stream.total_instances, 1)));
    progress_update(pct, 100,
                    "Loading props " +
                    std::to_string(g_level_prop_stream.instances_loaded) +
                    "/" +
                    std::to_string(g_level_prop_stream.total_instances));

    if (g_level_prop_stream.instances_loaded >=
            g_level_prop_stream.total_instances ||
        g_level_prop_stream.block_index >= g_level_prop_stream.blocks.size())
    {
        OutputLog::info("level props: streamed " +
                        std::to_string(g_level_prop_stream.instances_loaded) +
                        " type-2 instances" +
                        (g_level_prop_stream.model_misses
                            ? " (" + std::to_string(g_level_prop_stream.model_misses) +
                              " model load misses)"
                            : std::string()));
        g_level_prop_stream = LevelPropStreamState{};
        progress_done();
    }

    return true;
}
#endif

static void append_transformed_prop_geom(std::vector<MDLMeshGeom>& out,
                                         const MDLMeshGeom& src,
                                         const Level::PropInstance& inst,
                                         float terrain_cx,
                                         float terrain_cz)
{
    MDLMeshGeom dst = src;
    const float px = inst.values[0] - terrain_cx;
    const float py = inst.values[2];
    const float pz = inst.values[1] - terrain_cz;
    const float s = inst.values[6];
    const float c = inst.values[7];
    const float sx = inst.values[9] == 0.0f ? 1.0f : inst.values[9];
    const float sy = inst.values[10] == 0.0f ? sx : inst.values[10];
    const float sz = inst.values[11] == 0.0f ? sx : inst.values[11];

    for (size_t i = 0; i + 2 < dst.positions.size(); i += 3) {
        const float lx = src.positions[i + 0] * sx;
        const float ly = src.positions[i + 2] * sy;
        const float lz = -src.positions[i + 1] * sz;
        dst.positions[i + 0] = px + lx * c + lz * s;
        dst.positions[i + 1] = py + ly;
        dst.positions[i + 2] = pz - lx * s + lz * c;
    }

    if (dst.normals.size() == src.normals.size()) {
        for (size_t i = 0; i + 2 < dst.normals.size(); i += 3) {
            const float lx = src.normals[i + 0];
            const float ly = src.normals[i + 2];
            const float lz = -src.normals[i + 1];
            dst.normals[i + 0] = lx * c + lz * s;
            dst.normals[i + 1] = ly;
            dst.normals[i + 2] = -lx * s + lz * c;
        }
    }

    dst.name = src.name.empty()
        ? std::string("prop")
        : std::string("prop: ") + src.name;
    out.push_back(std::move(dst));
}

static void append_level_props_to_geoms(std::vector<MDLMeshGeom>& geoms)
{
    if (g_pending_level_prop_blocks.empty()) return;

    constexpr size_t kMaxInstances = 256;
    const float terrain_cx =
        (float(g_pending_terrain_ghf_width) - 1.0f) * 0.5f *
        g_pending_terrain_ghf_tile_size;
    const float terrain_cz =
        (float(g_pending_terrain_ghf_height) - 1.0f) * 0.5f *
        g_pending_terrain_ghf_tile_size;

    std::unordered_map<std::string, CachedPropModel> cache;
    size_t instances_seen = 0;
    size_t instances_loaded = 0;
    size_t models_failed = 0;
    size_t misses_logged = 0;

    if (g_pending_level_model_body_bnk.empty()) {
        OutputLog::warn("level props: no level model body BNK resolved");
    } else {
        OutputLog::info("level props: model body BNK " +
                        std::filesystem::path(g_pending_level_model_body_bnk)
                            .filename().string());
    }

    for (const auto& block : g_pending_level_prop_blocks) {
        if (block.model_path.empty()) continue;
        auto& cached = cache[block.model_path];
        if (!cached.loaded && cached.geoms.empty()) {
            std::vector<unsigned char> buf;
            if (build_mdl_buffer_for_name_with_body(block.model_path,
                                                    g_pending_level_model_body_bnk,
                                                    buf) &&
                parse_mdl_info(buf, cached.info, block.model_path)) {
                bool all_empty = !cached.info.MeshBuffers.empty();
                for (const auto& mb : cached.info.MeshBuffers) {
                    if (mb.VertexCount > 0) {
                        all_empty = false;
                        break;
                    }
                }
                if (all_empty) {
                    reparse_mdl_buffers_via_polymsh_scan(buf, cached.info);
                }
                parse_mdl_geometry(buf, cached.info, cached.geoms);
                cached.loaded = !cached.geoms.empty();
            }
            if (!cached.loaded) {
                ++models_failed;
                if (misses_logged < 5) {
                    ++misses_logged;
                    OutputLog::warn("level props: model load miss " +
                                    block.model_path);
                }
                cached.loaded = true;
            }
        }

        if (cached.geoms.empty()) continue;
        for (const auto& inst : block.instances) {
            if (instances_seen >= kMaxInstances) break;
            ++instances_seen;
            for (const auto& src : cached.geoms) {
                if (!src.positions.empty() && !src.indices.empty()) {
                    append_transformed_prop_geom(geoms, src, inst,
                                                 terrain_cx, terrain_cz);
                }
            }
            ++instances_loaded;
        }
        if (instances_seen >= kMaxInstances) break;
    }

    OutputLog::info("level props: appended " +
                    std::to_string(instances_loaded) +
                    " of " +
                    std::to_string([&] {
                        size_t n = 0;
                        for (const auto& b : g_pending_level_prop_blocks) {
                            n += b.instances.size();
                        }
                        return n;
                    }()) +
                    " type-2 instances" +
                    (models_failed ? " (" + std::to_string(models_failed) +
                                      " model load misses)" : std::string()));
}

}

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

#ifdef _WIN32
        if (S.texture_window_srv) {
            S.texture_window_srv->Release();
            S.texture_window_srv = nullptr;
        }

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
        extern unsigned int g_tex_popout_gl;
        extern std::string  g_tex_popout_name;
        extern bool         g_tex_popout_open;
        extern int          g_tex_popout_mesh_idx;
        g_tex_popout_gl       = 0;
        g_tex_popout_name.clear();
        g_tex_popout_open     = false;
        g_tex_popout_mesh_idx = -1;
#endif
        S.texture_window_name.clear();
        S.texture_window_width  = 0;
        S.texture_window_height = 0;
        S.texture_blob.clear();
        S.tex_info_ok           = false;
        S.show_texture_window   = false;
        S.show_preview_popup    = false;
        S.preview_mip_index     = -1;

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

            /* No progress dialog for model loads: with the BNK cache +
               in-memory extracts + SRV cache, loads are typically
               sub-frame.  A dialog that flashes for one frame feels
               worse than no dialog at all.  Run the load synchronously
               on the UI thread — one stall is cheaper than the cost of
               spawning a thread and double-buffering state.

               (The first cold load after app start can still take a
                moment because the BNKReader has to decompress its file
                table.  If that becomes the new bottleneck we'll prime
                the cache during boot.) */
            std::vector<unsigned char> buf;
            bool ok = false;
            try {
                if (is_nested) {
                    ok = reconstruct_nested_mdl(bnk_to_use, item.index, buf);
                } else {
                    ok = build_mdl_buffer_for_name(name, buf);
                }
                if (!ok) {
                    /* Last-resort fallback: raw extract straight from
                       the BNK.  Uses the cache so this is also
                       in-memory. */
                    try {
                        buf = BnkCache::extract_bytes(bnk_to_use, item.index);
                        ok  = !buf.empty();
                    } catch (...) {}
                }
            } catch (...) { ok = false; }

            if (!nested_temp_copy.empty()) {
                std::error_code ec;
                std::filesystem::remove(nested_temp_copy, ec);
            }

            if (ok && !buf.empty()) {
                S.mdl_info_ok = parse_mdl_info(buf, S.mdl_info, parse_path);
                if (!S.mdl_info_ok) {
                    OutputLog::error("MDL: parse_mdl_info FAILED for '" + name +
                                     "' (buf=" + std::to_string(buf.size()) + " bytes)");
                } else {
                    /* Echo the parsed structure so we can tell from the log
                       whether a blank preview is a parse-time failure
                       (e.g. RS_Golden_Acorn — metadata walker falls into
                       garbage) or a geometry-time one (mesh-buffer search
                       can't recover after a bad anchor). */
                    OutputLog::info("MDL: parsed '" + name + "' meshes=" +
                                    std::to_string(S.mdl_info.MeshCount) +
                                    " buffers=" + std::to_string(S.mdl_info.MeshBuffers.size()));
                    for (size_t mi = 0; mi < S.mdl_info.MeshBuffers.size(); ++mi) {
                        const auto& mb = S.mdl_info.MeshBuffers[mi];
                        std::string nm = (mi < S.mdl_info.Meshes.size())
                                       ? S.mdl_info.Meshes[mi].MeshName : std::string("?");
                        OutputLog::info("  mesh[" + std::to_string(mi) + "] '" + nm +
                                        "' verts=" + std::to_string(mb.VertexCount) +
                                        " faces=" + std::to_string(mb.FaceCount) +
                                        " subs=" + std::to_string(mb.SubMeshCount) +
                                        " (in-list=" + std::to_string(mb.SubMeshes.size()) +
                                        ") alt=" + std::to_string(mb.IsAltPath ? 1 : 0) +
                                        " foliage=" + std::to_string(mb.IsFoliagePath ? 1 : 0));
                    }
                    /* Empty-buffer recovery: if every parsed mesh buffer
                       came back with no vertices the standard walker
                       lost the anchor (e.g. RS_Golden_Acorn — its leaf
                       material has a trailing translucency texture that
                       desyncs the mesh-buffer scan).  Re-anchor by
                       scanning the file for "polymsh\0\x01" markers and
                       rebuild MeshBuffers from those, then continue with
                       the same geometry decoder. */
                    if (!S.mdl_info.MeshBuffers.empty()) {
                        bool all_empty = true;
                        for (const auto& mb : S.mdl_info.MeshBuffers) {
                            if (mb.VertexCount > 0) { all_empty = false; break; }
                        }
                        if (all_empty) {
                            if (reparse_mdl_buffers_via_polymsh_scan(buf, S.mdl_info)) {
                                OutputLog::info("MDL: polymsh-scan fallback recovered " +
                                                std::to_string(S.mdl_info.MeshBuffers.size()) +
                                                " mesh buffer(s)");
                            } else {
                                OutputLog::warn("MDL: all buffers empty AND polymsh-scan fallback found nothing");
                            }
                        }
                    }
                    S.mdl_meshes.clear();
                    parse_mdl_geometry(buf, S.mdl_info, S.mdl_meshes);
                    OutputLog::info("MDL: parse_mdl_geometry produced " +
                                    std::to_string(S.mdl_meshes.size()) + " geom blocks");
                    {
                        size_t nonempty = 0;
                        for (const auto& g : S.mdl_meshes) {
                            if (!g.positions.empty() && !g.indices.empty()) ++nonempty;
                        }
                        OutputLog::info("  -> " + std::to_string(nonempty) + " non-empty");
                    }
                }
                if (S.mdl_info_ok) {
#ifdef _WIN32
                    extern ModelPreview g_mp;
                    extern bool         g_mp_initialized;
                    if (!g_mp_initialized) {
                        MP_Init(device, g_mp, 800, 600);
                        g_mp_initialized = true;
                    }
                    MP_Build(device, S.mdl_meshes, S.mdl_info, g_mp);

                    /* MDL loaded — leave terrain mode if we were in it. */
                    S.terrain_mode = false;
                    g_mp.no_tilt   = false;   /* restore MDL X-axis tilt */

                    S.cam_yaw = 3.14159265f;
                    S.cam_pitch = 0.2f;
                    S.cam_dist = 3.0f;

                    if (S.texture_window_srv) {
                        S.texture_window_srv->Release();
                        S.texture_window_srv = nullptr;
                    }
#else
                    S.pending_preview_build = true;
#endif
                }
            }
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
                OutputLog::error("Texture preview: could not assemble buffer for '" + name + "' (no matching entry in any texture BNK)");
                S.texture_window_name = "ERROR: Could not load texture file";
                S.pending_texture_load = true;
                S.pending_texture_w = 0;
                S.pending_texture_h = 0;
                S.texture_blob.clear();
                S.tex_info_ok = false;
                progress_done();
                return;
            }

            S.tex_info_ok = parse_tex_info(tex_buf, S.tex_info);
            /* Pick the LARGEST mip by area for the initial display.
               Fable 2's assembled blob has the small mips (1..N) inline
               in the header BNK first, with the full-resolution mip 0
               appended afterwards from `1024mip0_textures.bnk`.  That
               means `Mips[0]` is the smallest, not the largest — using
               mip_index=0 here was effectively forcing the lowest LOD
               and producing the "pixelated" preview the user was seeing.
               mip_index=-1 lets decode_tex_to_rgba scan all mips and
               pick the one with the largest area. */
            S.texture_mip_index = -1;
            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            bool has_alpha = false;
            if (!decode_tex_to_rgba(tex_buf, rgba, w, h, &has_alpha,
                                    /*mip_index=*/-1)) {
                extern const std::string& mp_last_decode_fail_reason();
                extern const std::string& mp_last_decode_info();
                OutputLog::error("Texture preview: decode failed for '" + name +
                                 "' — reason=" + mp_last_decode_fail_reason() +
                                 "  info=[" + mp_last_decode_info() + "]");
                S.texture_window_name = "ERROR: Could not decode texture";
                S.pending_texture_load = true;
                S.pending_texture_w = 0;
                S.pending_texture_h = 0;
                S.texture_blob.clear();
                S.tex_info_ok = false;
                progress_done();
                return;
            }
            {
                extern const std::string& mp_last_decode_info();
                OutputLog::info("Texture preview: '" + name + "' decoded " +
                                std::to_string(w) + "x" + std::to_string(h) +
                                " (" + mp_last_decode_info() + ")");
            }
            S.texture_blob = std::move(tex_buf);

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
        extern bool g_mp_initialized;
        MP_Release(g_mp);
        g_mp_initialized = MP_Init(g_mp, 800, 600);
        if (g_mp_initialized) {
            MP_Build(S.mdl_meshes, S.mdl_info, g_mp);
            S.show_model_preview = true;
            S.model_preview_open = true;
            S.model_materials_open = true;
            S.terrain_mode = false;
            g_mp.no_tilt = false;
            S.cam_yaw = 3.14159265f;
            S.cam_pitch = 0.2f;
            S.cam_dist = 3.0f;
        }
    }
#endif

#ifdef _WIN32
    /* Pending "View Heightmap" hand-off — populated by the click in
       the Levels tab (no device), consumed here to build a D3D SRV
       from the grayscale RGBA buffer and surface the floating popout
       window from RenderPanel.cpp. */
    {
        extern std::atomic<bool>    g_pending_heightmap_view_load;
        extern std::vector<uint8_t> g_pending_heightmap_view_rgba;
        extern int                  g_pending_heightmap_view_w;
        extern int                  g_pending_heightmap_view_h;
        extern std::string          g_pending_heightmap_view_name;

        extern ID3D11ShaderResourceView* g_heightmap_popout_srv;
        extern std::string               g_heightmap_popout_name;
        extern int                       g_heightmap_popout_w;
        extern int                       g_heightmap_popout_h;
        extern bool                      g_heightmap_popout_open;
        extern std::vector<uint8_t>      g_heightmap_popout_rgba;

        if (g_pending_heightmap_view_load.exchange(false)) {
            const int w = g_pending_heightmap_view_w;
            const int h = g_pending_heightmap_view_h;
            if (w > 0 && h > 0 &&
                g_pending_heightmap_view_rgba.size() == size_t(w) * size_t(h) * 4) {

                if (g_heightmap_popout_srv) {
                    g_heightmap_popout_srv->Release();
                    g_heightmap_popout_srv = nullptr;
                }
                g_heightmap_popout_srv =
                    create_srv_from_rgba(device, w, h,
                                         g_pending_heightmap_view_rgba);
                if (g_heightmap_popout_srv) {
                    g_heightmap_popout_w    = w;
                    g_heightmap_popout_h    = h;
                    g_heightmap_popout_name = g_pending_heightmap_view_name;
                    g_heightmap_popout_rgba = std::move(g_pending_heightmap_view_rgba);
                    g_heightmap_popout_open = true;
                    OutputLog::success("heightmap opened: " +
                                       g_heightmap_popout_name + "  (" +
                                       std::to_string(w) + "x" +
                                       std::to_string(h) + ")");
                } else {
                    OutputLog::error("heightmap: failed to create SRV");
                }
            } else {
                OutputLog::error("heightmap: invalid RGBA payload (" +
                                 std::to_string(w) + "x" +
                                 std::to_string(h) + ")");
            }
            g_pending_heightmap_view_rgba.clear();
            g_pending_heightmap_view_name.clear();
            g_pending_heightmap_view_w = 0;
            g_pending_heightmap_view_h = 0;
        }
    }

    /* Pending terrain (heightfield-as-mesh) hand-off — populated by
       Level::Open on the UI thread, consumed here on the renderer
       thread so MP_Build sees a live ID3D11Device. */
    if (g_pending_terrain_load.exchange(false)) {
        progress_update(72, 100, "Uploading terrain...");
        const Level::TerrainMesh& tm = g_pending_terrain_mesh;
        if (!tm.ok || tm.indices.empty()) {
            OutputLog::error("pending terrain mesh is empty - skipped");
        } else {
            /* Drop any RGBA buffers we cached for the previous
               level's terrain.  They're keyed by name (e.g.
               "ehf_lightmap") which would collide with this load. */
            TerrainTextureRegistry::Clear();
            EhfLodThumbnails::Clear();
            TerrainSplat::Clear();
            TerrainEdit::Clear();
            extern ModelPreview g_mp;
            extern bool         g_mp_initialized;
            if (!g_mp_initialized) {
                MP_Init(device, g_mp, 800, 600);
                g_mp_initialized = true;
            }

            /* Adapt the TerrainMesh into the MDLMeshGeom + MDLInfo
               shape MP_Build wants.  Single submesh, no bones, one
               diffuse-texture slot we leave empty for now (the
               terrain will render with the default checker until we
               wire texture-atlas sampling next). */
            MDLMeshGeom g;
            g.positions    = tm.positions;
            g.normals      = tm.normals;
            g.uvs          = tm.uvs;
            g.indices      = tm.indices;
            g.bone_ids.assign(tm.positions.size() / 3 * 4, 0);
            g.bone_weights.assign(tm.positions.size() / 3 * 4, 0.f);
            /* Bind everything to bone 0 with full weight so the
               skinning vertex shader leaves positions alone. */
            for (size_t v = 0; v < tm.positions.size() / 3; ++v) {
                g.bone_weights[v * 4 + 0] = 1.0f;
            }
            g.name = "terrain";

            MDLInfo info;
            info.MeshCount = 1;
            MDLMeshInfo mi;
            mi.MeshName       = "terrain";
            mi.MaterialCount  = 0;
            info.Meshes.push_back(mi);
            MDLMeshBufferInfo mb;
            mb.VertexCount  = (uint32_t)(tm.positions.size() / 3);
            mb.FaceCount    = (uint32_t)tm.indices.size();
            mb.SubMeshCount = 1;
            info.MeshBuffers.push_back(mb);

            /* Decide the terrain texture BEFORE we upload UVs to the
               GPU — different texture types want different UV scales:

                 - `.ehf` baked albedo  : 1 texel per cell, UVs span [0, 1]
                                          across the whole terrain
                 - `.texture_atlas`     : source-material library (wood /
                                          grass / brick / …), per-cell tile
                                          selection unknown — tile the
                                          atlas N times across the terrain
                                          so something legible shows up. */
            std::vector<uint8_t> picked_rgba;
            int picked_w = 0, picked_h = 0;
            std::string picked_label;
            float uv_scale = 1.0f;

            /* Preferred path: bake a composite from the .ehf body's
               PF=24 lightmap × the first palette diffuse, sized to
               the lightmap (so terrain UVs = [0,1] across the
               heightfield with no further tiling).  This gives the
               terrain proper baked AO/shadows over a real ground
               material — dramatically better than a stretched plain
               material.  Falls through to the older paths if the
               composite bake fails for any reason.                 */
            std::string composite_name;
            std::vector<uint8_t> splat_dbg_rgba;
            int splat_dbg_w = 0, splat_dbg_h = 0;
            if (Level::BakeEhfTerrainCompositeAndSplatDebug(
                    g_pending_terrain_ehf_bytes,
                    g_pending_terrain_level_entry.bnk_path,
                    picked_rgba, picked_w, picked_h,
                    composite_name,
                    splat_dbg_rgba, splat_dbg_w, splat_dbg_h))
            {
                picked_label = "ehf_composite[" + composite_name
                             + " * lightmap]";
                uv_scale     = 1.0f;
                /* Stash the splat-map debug RGBA — it gets bound to
                   mesh.srv_specular below after the mesh is built. */
            } else if (Level::DecodeEhfTerrainAlbedoFromBytes(
                    g_pending_terrain_ehf_bytes,
                    g_pending_terrain_mesh.width,
                    g_pending_terrain_mesh.height,
                    picked_rgba, picked_w, picked_h))
            {
                picked_label = "ehf_baked_albedo";
                uv_scale     = 1.0f;
            } else {
                /* Try the ground-texture PALETTE next.  Each .ehf
                   carries a list of (diffuse, normal) ground-texture
                   .tex path pairs + per-entry metadata.  Bind the
                   first diffuse we can locate as the terrain texture,
                   tiled by the palette entry's tile_scale.  Visually
                   far more representative than texture_atlas — you
                   see actual grass / dirt / brightwood_earth etc.
                   instead of source-material source-page tiles. */
                std::vector<uint8_t> pal_rgba;
                int pal_w = 0, pal_h = 0;
                float pal_tile_scale = 0.125f;
                std::string pal_name;
                if (Level::DecodeEhfPaletteFirstDiffuse(
                        g_pending_terrain_ehf_bytes,
                        pal_rgba, pal_w, pal_h,
                        pal_tile_scale, pal_name))
                {
                    picked_rgba  = std::move(pal_rgba);
                    picked_w     = pal_w;
                    picked_h     = pal_h;
                    picked_label = "ehf_palette[" + pal_name + "]";
                    /* For visual purposes pick a moderate tile
                       factor — `dim * pal_tile_scale` gives ~100
                       repeats for a 769-cell terrain which makes the
                       texture look like pixel noise.  16 repeats is
                       enough to show the material clearly while
                       still feeling like ground texture.  Once we
                       have proper per-cell sampling this becomes
                       irrelevant — for now use a fixed sensible
                       repeat count.                                */
                    uv_scale = 16.0f;
                } else {
                    /* Last resort: the source-material .texture_atlas
                       tiled × 32.  Same visual as before any of the
                       per-cell work. */
                    std::vector<uint8_t> atlas_rgba;
                    int atlas_w = 0, atlas_h = 0;
                    if (Level::DecodeLevelTextureAtlas(
                            g_pending_terrain_level_entry,
                            atlas_rgba, atlas_w, atlas_h))
                    {
                        picked_rgba  = std::move(atlas_rgba);
                        picked_w     = atlas_w;
                        picked_h     = atlas_h;
                        picked_label = "texture_atlas_fallback";
                        uv_scale     = 32.0f;
                    }
                }
            }

            /* Apply the UV scale to the mesh geometry before upload. */
            if (uv_scale != 1.0f) {
                for (float& uv : g.uvs) uv *= uv_scale;
            }

            std::vector<MDLMeshGeom> geoms;
            geoms.push_back(std::move(g));
            start_level_prop_stream(geoms, info);

            if (g_mp.has_model) {
                MP_Release(g_mp);
                g_mp.has_model = false;
                g_mp_initialized = false;
                MP_Init(device, g_mp, 800, 600);
                g_mp_initialized = true;
            }
            MP_Build(device, geoms, info, g_mp);
            /* Terrain is Y-up natively — skip the engine's MDL tilt. */
            g_mp.no_tilt = true;

            /* Switch the render panel into terrain / flycam mode. */
            S.terrain_mode = true;

            /* Measure the terrain's world-space extent so we can
               place the flycam at a useful distance + give its
               movement speed sensible units.  Skipping the global
               tilt also means we can think in straight world axes:
               X across columns, Y up, Z across rows. */
            float ax = 0.f, ay_max = 0.f, az = 0.f;
            for (size_t v = 0; v + 2 < geoms[0].positions.size(); v += 3) {
                ax     = std::max(ax,     std::abs(geoms[0].positions[v]));
                ay_max = std::max(ay_max, std::abs(geoms[0].positions[v + 1]));
                az     = std::max(az,     std::abs(geoms[0].positions[v + 2]));
            }
            const float diag = std::sqrt(ax * ax + az * az);

            extern FlyCam g_flycam;
            g_flycam.pos[0] = 0.0f;
            g_flycam.pos[1] = ay_max + diag * 0.7f;
            g_flycam.pos[2] = -diag * 1.0f;
            g_flycam.yaw    = 0.0f;          /* look toward +Z */
            g_flycam.pitch  = -0.6f;         /* angled down ~34° */
            g_flycam.is_looking = false;
            /* Movement speed: enough to traverse the terrain in
               ~10 s of sustained forward.  Bloodstone's terrain is
               ~41 000 world units across so the default move_speed
               (set from MDL radius * 2) would feel glacial. */
            g_flycam.move_speed = std::max(diag * 0.2f, 50.0f);

            /* Push the far clip out so the whole landscape fits in
               the view frustum — MP_Render uses g_mp.radius * 100
               for the far plane.  Stash a generous radius here. */
            g_mp.radius   = std::max(g_mp.radius, diag);
            g_mp.center[0] = 0.0f;
            g_mp.center[1] = 0.0f;
            g_mp.center[2] = 0.0f;

            /* Hand the .ghf heights + positions to TerrainEdit so the
               user can mutate them through the Edit Terrain overlay.
               We pass a COPY of positions[] because BuildTerrainMesh
               owns them and we want TerrainEdit to keep a snapshot
               that survives the upcoming buffer release.            */
            if (!g_pending_terrain_ghf_heights.empty() &&
                g_pending_terrain_ghf_width > 0 &&
                g_pending_terrain_ghf_height > 0)
            {
                /* Resolve the .ghf BNK entry's on-disk location +
                   size + compression flag so TerrainEdit::Save can
                   patch the source ISO in place.                  */
                uint64_t bnk_entry_offset = 0;
                uint32_t bnk_entry_size   = 0;
                bool     bnk_entry_compressed = false;
                if (!g_pending_terrain_ghf_entry.bnk_path.empty() &&
                    g_pending_terrain_ghf_entry.file_index >= 0)
                {
                    try {
                        auto& bc = BnkCache::get(
                            g_pending_terrain_ghf_entry.bnk_path);
                        const auto& files = bc.reader->list_files();
                        const int idx =
                            g_pending_terrain_ghf_entry.file_index;
                        if (idx >= 0 && idx < (int)files.size()) {
                            bnk_entry_offset =
                                bc.reader->entry_disk_offset(idx);
                            bnk_entry_size =
                                bc.reader->entry_on_disk_size(idx);
                            bnk_entry_compressed =
                                bc.reader->entry_is_compressed(idx);
                        }
                    } catch (...) {
                        /* leave defaults — save will fall back to
                           the sibling backup file. */
                    }
                }

                TerrainEdit::Init(
                    g_pending_terrain_ghf_width,
                    g_pending_terrain_ghf_height,
                    g_pending_terrain_ghf_tile_size,
                    /*center_x*/ ax,
                    /*center_z*/ az,
                    g_pending_terrain_ghf_heights,
                    geoms[0].positions,
                    g_pending_terrain_ghf_payload,
                    g_pending_terrain_ghf_entry.bnk_path,
                    g_pending_terrain_ghf_entry.file_index,
                    g_pending_terrain_ghf_entry.full_path,
                    bnk_entry_offset,
                    bnk_entry_size,
                    bnk_entry_compressed);

                std::ostringstream tos;
                tos << "  TerrainEdit ready: "
                    << g_pending_terrain_ghf_width << "x"
                    << g_pending_terrain_ghf_height
                    << " heights, BNK entry "
                    << (bnk_entry_compressed ? "chunked" : "raw")
                    << " offset=0x" << std::hex << bnk_entry_offset
                    << std::dec << " size=" << bnk_entry_size << "B";
                OutputLog::info(tos.str());
            }

            /* Create + bind the terrain SRV using the `picked_rgba`
               buffer chosen by the texture-pick block ABOVE (which
               picks: .ehf baked albedo → .ehf palette diffuse →
               .texture_atlas fallback, in that order). */
            /* Texturing temporarily disabled — render terrain plain so
               we can sort out the splat pipeline separately.  The LOD
               palette materials still get decoded and listed in the
               Materials overlay; we just don't bind them here. */
            ID3D11ShaderResourceView* terrain_srv = nullptr;
            (void)picked_rgba; (void)picked_w; (void)picked_h;
            if (terrain_srv && !g_mp.meshes.empty()) {
                MPPerMesh& m = g_mp.meshes[0];
                if (m.srv_diffuse) m.srv_diffuse->Release();
                m.srv_diffuse      = terrain_srv;
                m.diffuse_visible  = true;
                m.diffuse_tex_name = picked_label;

                /* Also register the composite RGBA in the terrain
                   texture registry so the material thumbnail's
                   right-click "Export to" menu can export this
                   generated texture (it isn't backed by any BNK
                   file).                                            */
                TerrainTextureRegistry::Register(picked_label,
                                                 picked_rgba,
                                                 picked_w, picked_h);

                OutputLog::success("terrain texture bound: " + picked_label
                                   + " (" + std::to_string(picked_w) + "x"
                                   + std::to_string(picked_h)
                                   + ", uv_scale=" + std::to_string(uv_scale)
                                   + ")");
            } else {
                OutputLog::warn("terrain: no albedo texture decoded "
                                "(.ehf and .texture_atlas both failed)");
            }

            /* Decode the .ehf body's lightmap (PF=24) and normal
               map (PF=40) and bind them as ADDITIONAL thumbnails on
               the terrain mesh, so the material panel shows clickable
               "lightmap" / "normal" entries the user can pop out or
               export from the right-click menu.

               The MPPerMesh has five fixed thumbnail slots: diffuse,
               normal, specular, metallic, extra.  We use:
                 - normal  = .ehf BC5 normal map (RGBA with B
                             reconstructed from XY by our decoder)
                 - extra   = .ehf lightmap (16-bpp; export as RGBA
                             where R = the actual lightmap channel,
                             G = the secondary detail channel)       */
            if (!g_mp.meshes.empty() && !g_pending_terrain_ehf_bytes.empty()) {
                /* Parse the 63-byte .ehf header to get body slice. */
                const auto& ehf = g_pending_terrain_ehf_bytes;
                static constexpr char   kMagic[]   = "HeightFieldGraphicsFile";
                static constexpr size_t kMagicLen  = sizeof(kMagic) - 1;
                static constexpr size_t kHeaderLen = 63;
                bool header_ok = (ehf.size() >= kHeaderLen) &&
                    (std::memcmp(ehf.data(), kMagic, kMagicLen) == 0);
                uint32_t body_off = 0, body_size = 0;
                if (header_ok) {
                    auto rd = [&](size_t off) -> uint32_t {
                        return (uint32_t(ehf[off]) << 24)
                             | (uint32_t(ehf[off+1]) << 16)
                             | (uint32_t(ehf[off+2]) << 8)
                             |  uint32_t(ehf[off+3]);
                    };
                    body_off  = rd(55);
                    body_size = rd(59);
                    header_ok = (uint64_t(body_off) + body_size <= ehf.size());
                }

                if (header_ok && body_size > 0) {
                    std::vector<uint8_t> body_slice(
                        ehf.data() + body_off,
                        ehf.data() + body_off + body_size);

                    /* (1) Lightmap (the body's FIRST .tex blob). */
                    auto lm = TextureAtlas::DecodeAtlas(body_slice);
                    if (lm.ok && lm.pixel_format == 24u && !lm.rgba.empty()) {
                        MPPerMesh& m = g_mp.meshes[0];
                        ID3D11ShaderResourceView* lm_srv =
                            create_srv_from_rgba(device, lm.width,
                                                 lm.height, lm.rgba);
                        if (lm_srv) {
                            if (m.srv_extra) m.srv_extra->Release();
                            m.srv_extra      = lm_srv;
                            m.extra_visible  = false;  
                            m.extra_tex_name = "ehf_lightmap";
                            TerrainTextureRegistry::Register(
                                "ehf_lightmap", lm.rgba, lm.width, lm.height);
                            OutputLog::info("  bound lightmap thumbnail: "
                                + std::to_string(lm.width) + "x"
                                + std::to_string(lm.height) + " (PF=24)");
                        }
                    }

                    /* (2) Normal map: scan body for first PF=40 .tex. */
                    const size_t bn = body_slice.size();
                    for (size_t i = 4; i + 0x60 < bn; ++i) {
                        if (body_slice[i]   != 0xFF ||
                            body_slice[i+1] != 0xFF ||
                            body_slice[i+2] != 0xFF ||
                            body_slice[i+3] != 0xFE) continue;
                        const uint32_t pf =
                            (uint32_t(body_slice[i+0x18]) << 24) |
                            (uint32_t(body_slice[i+0x19]) << 16) |
                            (uint32_t(body_slice[i+0x1A]) << 8) |
                             uint32_t(body_slice[i+0x1B]);
                        if (pf != 40u) continue;
                        std::vector<uint8_t> nm_slice(
                            body_slice.begin() + i, body_slice.end());
                        auto nm = TextureAtlas::DecodeAtlas(nm_slice);
                        if (nm.ok && !nm.rgba.empty()) {
                            MPPerMesh& m = g_mp.meshes[0];
                            ID3D11ShaderResourceView* nm_srv =
                                create_srv_from_rgba(device, nm.width,
                                                     nm.height, nm.rgba);
                            if (nm_srv) {
                                if (m.srv_normal) m.srv_normal->Release();
                                m.srv_normal      = nm_srv;
                                m.normal_visible  = false;  
                                m.normal_tex_name = "ehf_normal";
                                TerrainTextureRegistry::Register(
                                    "ehf_normal", nm.rgba, nm.width, nm.height);
                                OutputLog::info("  bound normal thumbnail: "
                                    + std::to_string(nm.width) + "x"
                                    + std::to_string(nm.height) + " (PF=40 BC5)");
                            }
                        }
                        break;
                    }
                }
            }

            /* Splat-map debug thumbnail intentionally skipped — the
               PF=99 blob isn't actually a per-cell splat map (see
               docs/level_format.md § 9b.12 for the corrected reading).
               When the per-chunk parser is wired up we'll bind the
               real per-chunk material map here instead.              */
            (void)splat_dbg_rgba; (void)splat_dbg_w; (void)splat_dbg_h;

            /* Build per-LOD-material thumbnails from the .ehf LOD
               palette.  Each entry references a BaseLayer + DetailLayer
               pair of (diffuse, normal) .tex files.  We decode each one
               from its BNK and make an SRV, so the Materials overlay
               can show one row of thumbnails per LOD index. */
            {
                const std::string& preferred_bnk =
                    g_pending_terrain_level_entry.bnk_path;
                const auto& palette =
                    TerrainTextureRegistry::GetLodPalette();
                std::vector<EhfLodThumbnails::Entry> thumbs;
                thumbs.reserve(palette.size());

                /* Decode + SRV-create one .tex path.  Returns true and
                   fills out_srv/w/h on success; on failure leaves them
                   null and the caller renders a placeholder. */
                auto decode_one = [&](const std::string& path,
                                      ID3D11ShaderResourceView*& out_srv,
                                      int& out_w, int& out_h)
                {
                    out_srv = nullptr; out_w = out_h = 0;
                    if (path.empty()) return;

                    std::string basename =
                        std::filesystem::path(path).filename().string();
                    if (basename.empty()) return;
                    std::transform(basename.begin(), basename.end(),
                                   basename.begin(),
                                   [](unsigned char c){ return std::tolower(c); });

                    std::vector<unsigned char> blob_uc;
                    bool stitched = false;
                    try {
                        stitched = build_any_tex_buffer_for_name(
                            basename, blob_uc, preferred_bnk);
                    } catch (...) { stitched = false; }
                    if (!stitched || blob_uc.empty()) return;

                    std::vector<uint8_t> rgba;
                    bool has_alpha = false;
                    int w = 0, h = 0;
                    if (!decode_tex_to_rgba(blob_uc, rgba, w, h,
                                            &has_alpha, -1)) return;
                    if (rgba.empty() || w <= 0 || h <= 0) return;

                    ID3D11ShaderResourceView* srv =
                        create_srv_from_rgba(device, w, h, rgba);
                    if (!srv) return;
                    out_srv = srv;
                    out_w   = w;
                    out_h   = h;
                };

                for (const auto& pe : palette) {
                    EhfLodThumbnails::Entry e;
                    e.base_diffuse_path   = pe.base_diffuse;
                    e.base_normal_path    = pe.base_normal;
                    e.detail_diffuse_path = pe.detail_diffuse;
                    e.detail_normal_path  = pe.detail_normal;
                    decode_one(pe.base_diffuse,   e.srv_base_diffuse,
                               e.base_diffuse_w,  e.base_diffuse_h);
                    decode_one(pe.base_normal,    e.srv_base_normal,
                               e.base_normal_w,   e.base_normal_h);
                    decode_one(pe.detail_diffuse, e.srv_detail_diffuse,
                               e.detail_diffuse_w, e.detail_diffuse_h);
                    decode_one(pe.detail_normal,  e.srv_detail_normal,
                               e.detail_normal_w,  e.detail_normal_h);
                    thumbs.push_back(std::move(e));
                }

                int decoded_count = 0;
                for (const auto& e : thumbs) {
                    if (e.srv_base_diffuse)   ++decoded_count;
                    if (e.srv_base_normal)    ++decoded_count;
                    if (e.srv_detail_diffuse) ++decoded_count;
                    if (e.srv_detail_normal)  ++decoded_count;
                }
                EhfLodThumbnails::Set(std::move(thumbs));
                OutputLog::info("  decoded LOD palette: "
                    + std::to_string(palette.size()) + " materials, "
                    + std::to_string(decoded_count) + "/"
                    + std::to_string(palette.size() * 4) + " maps OK");

                /* Splat shader currently disabled per user request —
                   we keep the LOD palette decoded so the Materials
                   overlay shows the per-LOD thumbnails, but we do NOT
                   build the splat resources or mark the mesh as
                   is_terrain, so the terrain renders untextured.   */
                Level::EhfParsedBody splat_parsed;
                if (false &&
                    Level::ParseEhfBody(g_pending_terrain_ehf_bytes,
                                        splat_parsed)) {
                    /* Lightmap RGBA — pull from the registry where the
                       lightmap binding step above registered it.    */
                    std::vector<uint8_t> lm_rgba;
                    int lm_w = 0, lm_h = 0;
                    const auto* lm_entry =
                        TerrainTextureRegistry::Find("ehf_lightmap");
                    if (lm_entry) {
                        lm_rgba = lm_entry->rgba;
                        lm_w    = lm_entry->width;
                        lm_h    = lm_entry->height;
                    }
                    const auto& fresh = EhfLodThumbnails::Get();
                    /* The .ghf-derived mesh is centered at world origin,
                       so the bounding-box half-extents (ax, az) computed
                       above are exactly the mesh→world offset needed by
                       the splat shader to look up chunks correctly. */
                    if (TerrainSplat::Build(device, splat_parsed, fresh,
                                            lm_rgba, lm_w, lm_h,
                                            ax, az))
                    {
                        /* Mark mesh[0] as terrain so MP_Render uses the
                           splat shader for it. */
                        if (!g_mp.meshes.empty()) {
                            g_mp.meshes[0].is_terrain = true;
                            g_mp.meshes[0].diffuse_tex_name =
                                "ehf_splat_terrain";
                        }
                        OutputLog::success("terrain SPLAT shader bound: "
                            + std::to_string(splat_parsed.chunk_w) + "x"
                            + std::to_string(splat_parsed.chunk_h)
                            + " chunks, " + std::to_string(fresh.size())
                            + " LODs");
                    } else {
                        OutputLog::warn("terrain splat resources failed; "
                                        "falling back to composite");
                    }
                }
            }

            OutputLog::success("terrain '" + g_pending_terrain_label +
                               "' built (" +
                               std::to_string(geoms[0].positions.size()/3) +
                               " verts)");
            if (!g_level_prop_stream.active) {
                progress_done();
            }
        }

        /* Release the heap memory the pending mesh + the .ehf
           bytes the level loader parked here for the texture
           decode step.  Both are large and we no longer need
           them once the terrain has been built and textured. */
        g_pending_terrain_mesh = Level::TerrainMesh{};
        g_pending_terrain_label.clear();
        g_pending_terrain_ehf_bytes.clear();
        g_pending_terrain_ehf_bytes.shrink_to_fit();

        /* TerrainEdit took its own copies — drop the pending .ghf
           buffers to free heap.  TerrainEdit::Clear() runs separately
           when the user navigates away from this level.            */
        g_pending_terrain_ghf_payload.clear();
        g_pending_terrain_ghf_payload.shrink_to_fit();
        g_pending_terrain_ghf_heights.clear();
        g_pending_terrain_ghf_heights.shrink_to_fit();
        g_pending_terrain_ghf_entry = FlatAssetEntry{};
        g_pending_level_prop_blocks.clear();
        g_pending_level_prop_blocks.shrink_to_fit();
        g_pending_level_model_body_bnk.clear();
    }

    stream_level_prop_batch(device);
#else
    if (g_pending_terrain_load.exchange(false)) {
        const Level::TerrainMesh& tm = g_pending_terrain_mesh;
        if (!tm.ok || tm.indices.empty()) {
            OutputLog::error("pending terrain mesh is empty — skipped");
        } else {
            TerrainTextureRegistry::Clear();
            EhfLodThumbnails::Clear();
            TerrainSplat::Clear();
            TerrainEdit::Clear();

            MDLMeshGeom g;
            g.positions = tm.positions;
            g.normals = tm.normals;
            g.uvs = tm.uvs;
            g.indices = tm.indices;
            g.bone_ids.assign(tm.positions.size() / 3 * 4, 0);
            g.bone_weights.assign(tm.positions.size() / 3 * 4, 0.f);
            for (size_t v = 0; v < tm.positions.size() / 3; ++v) {
                g.bone_weights[v * 4 + 0] = 1.0f;
            }
            g.name = "terrain";

            MDLInfo info;
            info.MeshCount = 1;
            MDLMeshInfo mi;
            mi.MeshName = "terrain";
            mi.MaterialCount = 0;
            info.Meshes.push_back(mi);
            MDLMeshBufferInfo mb;
            mb.VertexCount = (uint32_t)(tm.positions.size() / 3);
            mb.FaceCount = (uint32_t)tm.indices.size();
            mb.SubMeshCount = 1;
            info.MeshBuffers.push_back(mb);

            std::vector<uint8_t> picked_rgba;
            int picked_w = 0;
            int picked_h = 0;
            std::string picked_label;
            float uv_scale = 1.0f;
            std::string composite_name;
            std::vector<uint8_t> splat_dbg_rgba;
            int splat_dbg_w = 0;
            int splat_dbg_h = 0;

            if (Level::BakeEhfTerrainCompositeAndSplatDebug(
                    g_pending_terrain_ehf_bytes,
                    g_pending_terrain_level_entry.bnk_path,
                    picked_rgba, picked_w, picked_h,
                    composite_name,
                    splat_dbg_rgba, splat_dbg_w, splat_dbg_h)) {
                picked_label = "ehf_composite[" + composite_name + " * lightmap]";
                uv_scale = 1.0f;
            } else if (Level::DecodeEhfTerrainAlbedoFromBytes(
                    g_pending_terrain_ehf_bytes,
                    g_pending_terrain_mesh.width,
                    g_pending_terrain_mesh.height,
                    picked_rgba, picked_w, picked_h)) {
                picked_label = "ehf_baked_albedo";
                uv_scale = 1.0f;
            } else {
                std::vector<uint8_t> pal_rgba;
                int pal_w = 0;
                int pal_h = 0;
                float pal_tile_scale = 0.125f;
                std::string pal_name;
                if (Level::DecodeEhfPaletteFirstDiffuse(
                        g_pending_terrain_ehf_bytes,
                        pal_rgba, pal_w, pal_h,
                        pal_tile_scale, pal_name)) {
                    picked_rgba = std::move(pal_rgba);
                    picked_w = pal_w;
                    picked_h = pal_h;
                    picked_label = "ehf_palette[" + pal_name + "]";
                    uv_scale = 16.0f;
                } else {
                    std::vector<uint8_t> atlas_rgba;
                    int atlas_w = 0;
                    int atlas_h = 0;
                    if (Level::DecodeLevelTextureAtlas(
                            g_pending_terrain_level_entry,
                            atlas_rgba, atlas_w, atlas_h)) {
                        picked_rgba = std::move(atlas_rgba);
                        picked_w = atlas_w;
                        picked_h = atlas_h;
                        picked_label = "texture_atlas_fallback";
                        uv_scale = 32.0f;
                    }
                }
            }

            if (uv_scale != 1.0f) {
                for (float& uv : g.uvs) uv *= uv_scale;
            }

            std::vector<MDLMeshGeom> geoms;
            geoms.push_back(std::move(g));
            append_level_props_to_geoms(geoms);

            extern ModelPreview g_mp;
            extern bool g_mp_initialized;
            extern FlyCam g_flycam;
            MP_Release(g_mp);
            g_mp_initialized = false;
            g_mp_initialized = MP_Init(g_mp, 800, 600);
            if (g_mp_initialized) {
                MP_Build(geoms, info, g_mp);
                g_mp.no_tilt = true;
                S.terrain_mode = true;
                S.show_model_preview = true;
                S.model_preview_open = true;
                S.model_materials_open = true;

                float ax = 0.f;
                float ay_max = 0.f;
                float az = 0.f;
                for (size_t v = 0; v + 2 < geoms[0].positions.size(); v += 3) {
                    ax = std::max(ax, std::abs(geoms[0].positions[v]));
                    ay_max = std::max(ay_max, std::abs(geoms[0].positions[v + 1]));
                    az = std::max(az, std::abs(geoms[0].positions[v + 2]));
                }
                const float diag = std::sqrt(ax * ax + az * az);
                g_flycam.pos[0] = 0.0f;
                g_flycam.pos[1] = ay_max + diag * 0.7f;
                g_flycam.pos[2] = -diag * 1.0f;
                g_flycam.yaw = 0.0f;
                g_flycam.pitch = -0.6f;
                g_flycam.is_looking = false;
                g_flycam.move_speed = std::max(diag * 0.2f, 50.0f);
                g_mp.radius = std::max(g_mp.radius, diag);
                g_mp.center[0] = 0.0f;
                g_mp.center[1] = 0.0f;
                g_mp.center[2] = 0.0f;

                if (!picked_rgba.empty() && picked_w > 0 && picked_h > 0 &&
                    !g_mp.meshes.empty()) {
                    unsigned int terrain_tex =
                        create_gl_texture_from_rgba(picked_w, picked_h,
                                                    picked_rgba.data());
                    if (terrain_tex) {
                        MPPerMesh& m = g_mp.meshes[0];
                        if (m.tex_diffuse && m.tex_diffuse != g_mp.default_tex) {
                            glDeleteTextures(1, &m.tex_diffuse);
                        }
                        m.tex_diffuse = terrain_tex;
                        m.diffuse_visible = true;
                        m.diffuse_tex_name = picked_label;
                        TerrainTextureRegistry::Register(picked_label,
                                                         picked_rgba,
                                                         picked_w,
                                                         picked_h);
                        OutputLog::success("terrain texture bound: " + picked_label +
                                           " (" + std::to_string(picked_w) + "x" +
                                           std::to_string(picked_h) + ")");
                    }
                } else {
                    OutputLog::warn("terrain: no albedo texture decoded");
                }

                OutputLog::success("terrain '" + g_pending_terrain_label +
                                   "' built (" +
                                   std::to_string(geoms[0].positions.size() / 3) +
                                   " verts)");
            }
        }

        g_pending_terrain_mesh = Level::TerrainMesh{};
        g_pending_terrain_label.clear();
        g_pending_terrain_ehf_bytes.clear();
        g_pending_terrain_ehf_bytes.shrink_to_fit();
        g_pending_terrain_ghf_payload.clear();
        g_pending_terrain_ghf_payload.shrink_to_fit();
        g_pending_terrain_ghf_heights.clear();
        g_pending_terrain_ghf_heights.shrink_to_fit();
        g_pending_terrain_ghf_entry = FlatAssetEntry{};
        g_pending_level_prop_blocks.clear();
        g_pending_level_prop_blocks.shrink_to_fit();
        g_pending_level_model_body_bnk.clear();
    }
#endif
}
