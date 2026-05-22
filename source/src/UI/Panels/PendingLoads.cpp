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
#include <exception>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <mutex>
#include <thread>
#include <ctime>
#include <cstring>
#include <cctype>
#include <sstream>
#include <unordered_map>

#ifndef _WIN32
#include <GL/glew.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

std::atomic<bool> g_pending_mdl_load{false};
int g_pending_mdl_index = -1;
std::string g_pending_mdl_full_path;

std::atomic<bool> g_pending_tex_load{false};
int g_pending_tex_index = -1;

extern ModelPreview g_mp;
extern bool         g_mp_initialized;
extern FlyCam       g_flycam;

    namespace {

struct CachedPropModel {
    bool loaded = false;
    MDLInfo info;
    std::vector<MDLMeshGeom> geoms;
};

static std::string normalized_asset_path(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

static std::string asset_leaf(std::string s)
{
    const size_t sl = s.find_last_of("/\\");
    if (sl != std::string::npos) s = s.substr(sl + 1);
    return s;
}

static bool is_shell_pair_model_path(const std::string& model_path)
{
    std::string leaf = normalized_asset_path(asset_leaf(model_path));
    return leaf == "exterior.mdl" || leaf == "interior.mdl";
}

static std::string asset_parent_key(const std::string& bnk_path)
{
    auto it = S.nested_bnk_parents.find(bnk_path);
    if (it != S.nested_bnk_parents.end()) {
        return normalized_asset_path(it->second);
    }
    std::filesystem::path p(bnk_path);
    return normalized_asset_path(p.parent_path().string());
}

static bool parse_prop_model_buffer(const std::vector<unsigned char>& buf,
                                    const std::string& model_path,
                                    CachedPropModel& out,
                                    std::string* reason = nullptr)
{
    CachedPropModel tmp;
    const bool main_ok = parse_mdl_info(buf, tmp.info, model_path);

    auto missing_count = [&]() -> size_t {
        size_t empty = 0;
        if (tmp.info.MeshBuffers.size() < tmp.info.MeshCount) {
            empty += tmp.info.MeshCount - tmp.info.MeshBuffers.size();
        }
        for (const auto& mb : tmp.info.MeshBuffers) {
            if (mb.VertexCount == 0) ++empty;
        }
        return empty;
    };

    if (main_ok) {
        bool all_empty = !tmp.info.MeshBuffers.empty();
        for (const auto& mb : tmp.info.MeshBuffers) {
            if (mb.VertexCount > 0) {
                all_empty = false;
                break;
            }
        }
        if (all_empty) {
            reparse_mdl_buffers_via_polymsh_scan(buf, tmp.info);
        }
    }

    if (missing_count() > 0) {
        reparse_mdl_missing_buffers_optstr(buf, tmp.info);
    }
    if (missing_count() > 0) {
        reparse_mdl_as_foliage_48b(buf, tmp.info);
    }
    {
        std::string lp = model_path;
        std::transform(lp.begin(), lp.end(), lp.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });
        std::replace(lp.begin(), lp.end(), '\\', '/');
        const bool multi_instance_target =
            lp.find("bs_townhouse_basic_snow_v2") != std::string::npos &&
            (lp.find("/exterior.mdl") != std::string::npos ||
             lp.find("/interior.mdl") != std::string::npos);
        if (multi_instance_target) {
            reparse_mdl_multi_instance_buffers(buf, tmp.info);
        }
    }

    if (!main_ok && missing_count() >= tmp.info.MeshCount) {
        if (reason) {
            *reason = "parse_mdl_info failed, bytes=" +
                      std::to_string(buf.size());
        }
        return false;
    }

    parse_mdl_geometry(buf, tmp.info, tmp.geoms);
    if (tmp.geoms.empty()) {
        if (reason) {
            *reason = "parse_mdl_geometry produced 0 geoms"
                      ", bytes=" + std::to_string(buf.size()) +
                      ", meshes=" + std::to_string(tmp.info.Meshes.size()) +
                      ", buffers=" +
                      std::to_string(tmp.info.MeshBuffers.size());
        }
        return false;
    }

    out.info = std::move(tmp.info);
    out.geoms = std::move(tmp.geoms);
    if (reason) {
        *reason = "ok, geoms=" + std::to_string(out.geoms.size());
    }
    return true;
}

static std::vector<const FlatAssetEntry*>
collect_prop_model_candidates(const std::string& model_path,
                              const std::string& preferred_body_bnk)
{
    const std::string want_full = normalized_asset_path(model_path);
    const std::string want_leaf = normalized_asset_path(asset_leaf(model_path));
    const std::string preferred_bnk = normalized_asset_path(preferred_body_bnk);
    const std::string preferred_parent = asset_parent_key(preferred_body_bnk);

    struct Scored {
        const FlatAssetEntry* entry = nullptr;
        int score = 0;
    };

    std::vector<Scored> exact;
    std::vector<Scored> leaf;
    exact.reserve(16);
    leaf.reserve(16);

    for (const auto& e : S.all_mdl_files) {
        const std::string e_full = normalized_asset_path(e.full_path);
        const std::string e_leaf = normalized_asset_path(e.name);
        const bool full_match = (e_full == want_full);
        const bool leaf_match = (!full_match && !want_leaf.empty() &&
                                 e_leaf == want_leaf);
        if (!full_match && !leaf_match) continue;

        int score = full_match ? 100000 : 10000;
        const std::string e_bnk = normalized_asset_path(e.bnk_path);
        if (!preferred_bnk.empty() && e_bnk == preferred_bnk) {
            score += 50000;
        }
        if (!preferred_parent.empty() &&
            asset_parent_key(e.bnk_path) == preferred_parent) {
            score += 10000;
        }
        if (!e.from_nested) score += 1000;
        score += std::min<uint32_t>(e.size, 1000000u) / 10000;

        (full_match ? exact : leaf).push_back({&e, score});
    }

    auto by_score = [](const Scored& a, const Scored& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.entry->bnk_path < b.entry->bnk_path;
    };

    auto& picked = exact.empty() ? leaf : exact;
    std::sort(picked.begin(), picked.end(), by_score);

    std::vector<const FlatAssetEntry*> out;
    out.reserve(picked.size());
    for (const auto& s : picked) out.push_back(s.entry);
    return out;
}

static bool try_prop_model_candidate(const FlatAssetEntry& entry,
                                     const std::string& model_path,
                                     CachedPropModel& cached,
                                     std::string& method,
                                     std::string* fail_reason = nullptr)
{
    try {
        std::vector<unsigned char> body =
            BnkCache::extract_bytes(entry.bnk_path, entry.file_index);
        if (!body.empty()) {
            std::string parse_reason;
            if (parse_prop_model_buffer(body, model_path, cached,
                                        &parse_reason)) {
                method = "body";
                return true;
            }
            if (fail_reason) {
                *fail_reason = "body parse rejected: " + parse_reason;
            }
        } else if (fail_reason) {
            *fail_reason = "body extract returned 0 bytes";
        }
    } catch (...) {
        if (fail_reason) {
            *fail_reason = "body extract threw";
        }
    }

    std::vector<unsigned char> buf;
    if (build_mdl_buffer_for_name_with_body(model_path, entry.bnk_path, buf)) {
        std::string parse_reason;
        if (parse_prop_model_buffer(buf, model_path, cached, &parse_reason)) {
            method = "body+header";
            return true;
        }
        if (fail_reason) {
            if (!fail_reason->empty()) *fail_reason += "; ";
            *fail_reason += "body+header parse rejected: " + parse_reason;
        }
    } else if (fail_reason) {
        if (!fail_reason->empty()) *fail_reason += "; ";
        *fail_reason += "body+header build failed";
    }

    return false;
}

static bool load_cached_prop_model(const std::string& model_path,
                                   const std::string& preferred_body_bnk,
                                   CachedPropModel& cached)
{
    const bool shell_pair_model = is_shell_pair_model_path(model_path);
    const std::string want_full = normalized_asset_path(model_path);

    if (shell_pair_model) {
        std::vector<const FlatAssetEntry*> candidates =
            collect_prop_model_candidates(model_path, preferred_body_bnk);
        for (const FlatAssetEntry* candidate : candidates) {
            if (!candidate ||
                normalized_asset_path(candidate->full_path) != want_full) {
                continue;
            }
            const FlatAssetEntry& entry = *candidate;

            std::string method;
            std::string fail_reason;
            if (try_prop_model_candidate(entry, model_path, cached, method,
                                         &fail_reason)) {
                return true;
            }
        }
    }

    std::vector<unsigned char> buf;
    if (build_mdl_buffer_for_name_with_body(model_path,
                                            preferred_body_bnk,
                                            buf)) {
        std::string parse_reason;
        if (parse_prop_model_buffer(buf, model_path, cached, &parse_reason)) {
            return true;
        }
    }

    const auto candidates =
        collect_prop_model_candidates(model_path, preferred_body_bnk);
    for (const FlatAssetEntry* entry : candidates) {
        if (!entry) continue;
        std::string method;
        std::string fail_reason;
        if (try_prop_model_candidate(*entry, model_path, cached, method,
                                     &fail_reason)) {
            return true;
        }
    }

    return false;
}

struct GeneratedTerrainTexture {
    size_t               mesh_index = 0;
    std::string          label;
    std::vector<uint8_t> rgba;
    int                  width = 0;
    int                  height = 0;
};

static void append_transformed_prop_geom(std::vector<MDLMeshGeom>& out,
                                         const MDLMeshGeom& src,
                                         const Level::PropInstance& inst,
                                         float terrain_cx,
                                         float terrain_cz);

static void transform_instance_point(const Level::PropInstance& inst,
                                     float lx,
                                     float ly,
                                     float lz,
                                     float& x,
                                     float& y,
                                     float& z)
{
    const float px = inst.values[0];
    const float py = inst.values[2];
    const float pz = inst.values[1];
    if (inst.has_full_transform) {
        float scale = inst.values[12];
        if (!std::isfinite(scale) || scale == 0.0f) scale = 1.0f;
        lx *= scale;
        ly *= scale;
        lz *= scale;
        const float* m = &inst.values[3];
        x = px + m[0] * lx + m[1] * ly + m[2] * lz;
        y = py + m[3] * lx + m[4] * ly + m[5] * lz;
        z = pz + m[6] * lx + m[7] * ly + m[8] * lz;
        return;
    }

    const float s  = inst.values[6];
    const float c  = inst.values[7];
    const float sx = inst.values[9]  == 0.0f ? 1.0f : inst.values[9];
    const float sy = inst.values[10] == 0.0f ? sx   : inst.values[10];
    const float sz = inst.values[11] == 0.0f ? sx   : inst.values[11];
    lx *= sx;
    ly *= sz;
    lz *= sy;
    x = px + lx * c + lz * s;
    y = py + ly;
    z = pz - lx * s + lz * c;
}

static void transform_instance_normal(const Level::PropInstance& inst,
                                      float lx,
                                      float ly,
                                      float lz,
                                      float& x,
                                      float& y,
                                      float& z)
{
    if (inst.has_full_transform) {
        const float* m = &inst.values[3];
        x = m[0] * lx + m[1] * ly + m[2] * lz;
        y = m[3] * lx + m[4] * ly + m[5] * lz;
        z = m[6] * lx + m[7] * ly + m[8] * lz;
        const float len = std::sqrt(x * x + y * y + z * z);
        if (std::isfinite(len) && len > 1e-6f) {
            x /= len;
            y /= len;
            z /= len;
        }
        return;
    }

    const float s = inst.values[6];
    const float c = inst.values[7];
    x = lx * c + lz * s;
    y = ly;
    z = -lx * s + lz * c;
}

static void merge_transformed_instance_into(MDLMeshGeom& dst,
                                            const MDLMeshGeom& src,
                                            const Level::PropInstance& inst)
{
    if (src.positions.empty() || src.indices.empty()) return;

    const size_t src_vcount = src.positions.size() / 3;
    const size_t dst_vcount = dst.positions.size() / 3;
    if (src_vcount == 0 ||
        dst_vcount > size_t(std::numeric_limits<uint32_t>::max())) {
        return;
    }
    for (uint32_t idx : src.indices) {
        if (idx >= src_vcount) return;
    }

    const uint32_t base_vertex = uint32_t(dst_vcount);

    const size_t pos_off = dst.positions.size();
    dst.positions.resize(pos_off + src.positions.size());
    for (size_t i = 0; i < src_vcount; ++i) {
        const float lx = src.positions[i*3+0];
        const float ly = src.positions[i*3+2];
        const float lz = src.positions[i*3+1];
        transform_instance_point(inst, lx, ly, lz,
                                 dst.positions[pos_off + i*3 + 0],
                                 dst.positions[pos_off + i*3 + 1],
                                 dst.positions[pos_off + i*3 + 2]);
    }

    if (!src.normals.empty()) {
        const size_t n_off = dst.normals.size();
        dst.normals.resize(n_off + src.normals.size());
        const size_t src_ncount = src.normals.size() / 3;
        for (size_t i = 0; i < src_ncount; ++i) {
            const float lx = src.normals[i*3+0];
            const float ly = src.normals[i*3+2];
            const float lz = src.normals[i*3+1];
            transform_instance_normal(inst, lx, ly, lz,
                                      dst.normals[n_off + i*3 + 0],
                                      dst.normals[n_off + i*3 + 1],
                                      dst.normals[n_off + i*3 + 2]);
        }
    }

    if (!src.uvs.empty()) {
        dst.uvs.insert(dst.uvs.end(), src.uvs.begin(), src.uvs.end());
    }
    if (!src.bone_ids.empty()) {
        dst.bone_ids.insert(dst.bone_ids.end(),
                            src.bone_ids.begin(), src.bone_ids.end());
    }
    if (!src.bone_weights.empty()) {
        dst.bone_weights.insert(dst.bone_weights.end(),
                                src.bone_weights.begin(),
                                src.bone_weights.end());
    }

    const size_t i_off = dst.indices.size();
    dst.indices.resize(i_off + src.indices.size());
    for (size_t k = 0; k < src.indices.size(); ++k) {
        dst.indices[i_off + k] = src.indices[k] + base_vertex;
    }
}

static std::string make_combined_prop_name(const std::string& model_path,
                                           const std::string& src_name,
                                           size_t              instance_count,
                                           uint32_t            block_type)
{
    std::string base = model_path;
    const size_t sl = base.find_last_of("/\\");
    if (sl != std::string::npos) base = base.substr(sl + 1);

    const bool is_engine_level = (block_type == 2u) || (block_type == 21u);
    std::string name =
        is_engine_level ? std::string("engine_level: ") + base
                        : std::string("prop: ") + base;
    if (!src_name.empty()) name += "#" + src_name;
    name += " (" + std::to_string(instance_count) + " inst)";
    return name;
}

constexpr size_t kMaxCombinedPropVertices = 250000;
constexpr size_t kMaxCombinedPropIndices  = 750000;

static void init_combined_prop_geom(MDLMeshGeom& dst,
                                    const MDLMeshGeom& src,
                                    const std::string& model_path,
                                    size_t instance_count,
                                    uint32_t block_type,
                                    size_t part_index)
{
    dst = MDLMeshGeom{};
    dst.diffuse_tex_name  = src.diffuse_tex_name;
    dst.normal_tex_name   = src.normal_tex_name;
    dst.specular_tex_name = src.specular_tex_name;
    dst.metallic_tex_name = src.metallic_tex_name;
    dst.extra_tex_name    = src.extra_tex_name;
    dst.MeshIndex         = src.MeshIndex;
    dst.SubMeshIndex      = src.SubMeshIndex;
    dst.name = make_combined_prop_name(
        model_path, src.name, instance_count, block_type);
    if (part_index > 0) {
        dst.name += " part " + std::to_string(part_index + 1);
    }
}

static bool would_exceed_combined_prop_limits(const MDLMeshGeom& dst,
                                              const MDLMeshGeom& src)
{
    if (dst.positions.empty()) return false;
    const size_t dst_vertices = dst.positions.size() / 3;
    const size_t src_vertices = src.positions.size() / 3;
    if (dst_vertices + src_vertices > kMaxCombinedPropVertices) return true;
    if (dst.indices.size() + src.indices.size() > kMaxCombinedPropIndices) {
        return true;
    }
    return false;
}

static void flush_combined_prop_geom(std::vector<MDLMeshGeom>& out,
                                     MDLMeshGeom& geom,
                                     const MDLMeshGeom& src,
                                     const std::string& model_path,
                                     size_t instance_count,
                                     uint32_t block_type,
                                     size_t& part_index)
{
    if (!geom.positions.empty() && !geom.indices.empty()) {
        out.push_back(std::move(geom));
        ++part_index;
    }
    init_combined_prop_geom(geom, src, model_path, instance_count,
                            block_type, part_index);
}

static void normalize_grid_uvs(MDLMeshGeom& geom, uint32_t width, uint32_t height)
{
    if (width < 2 || height < 2) return;
    const size_t vertex_count = size_t(width) * size_t(height);
    if (geom.uvs.size() < vertex_count * 2) return;

    for (uint32_t y = 0; y < height; ++y) {
        const float v = float(y) / float(height - 1);
        for (uint32_t x = 0; x < width; ++x) {
            const size_t i = size_t(y) * width + x;
            geom.uvs[i * 2 + 0] = float(x) / float(width - 1);
            geom.uvs[i * 2 + 1] = v;
        }
    }
}

static uint32_t env_texture_hash(std::string s, bool lowercase)
{
        if (lowercase) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) {
                               return (char)std::tolower(c);
                           });
        }
        std::replace(s.begin(), s.end(), '/', '\\');
        uint32_t h = 0x811C9DC5u;
        for (unsigned char c : s) {
            h *= 0x01000193u;
            h ^= uint32_t(c);
        }
        return h;
}

static std::string resolve_env_texture_hash(uint32_t hash)
{
    if (hash == 0 || hash == 0x811C9DC5u) return {};
    for (const FlatAssetEntry& tex : S.all_tex_files) {
        if (env_texture_hash(tex.full_path, true) == hash ||
            env_texture_hash(tex.name, true) == hash ||
            env_texture_hash(tex.full_path, false) == hash ||
            env_texture_hash(tex.name, false) == hash) {
            return tex.full_path;
        }
    }
    return {};
}

static void apply_sky_texture_hashes_to_preview(ModelPreview& mp,
                                                const Gdb::SkyTheme& sky)
{
#ifdef _WIN32
    auto reset_overlay = [&]() {
        if (mp.sky_overlay_srv) {
            mp.sky_overlay_srv->Release();
            mp.sky_overlay_srv = nullptr;
        }
        mp.sky_overlay_tried = false;
        mp.sky_overlay_tex_name.clear();
    };
    auto reset_moon = [&]() {
        if (mp.sky_moon_srv) {
            mp.sky_moon_srv->Release();
            mp.sky_moon_srv = nullptr;
        }
        mp.sky_moon_tried = false;
        mp.sky_moon_tex_name.clear();
    };
    auto reset_glare = [&]() {
        if (mp.sky_moon_glare_srv) {
            mp.sky_moon_glare_srv->Release();
            mp.sky_moon_glare_srv = nullptr;
        }
        mp.sky_moon_glare_tried = false;
        mp.sky_moon_glare_tex_name.clear();
    };

    reset_overlay();
    reset_moon();
    reset_glare();
    if (sky.has_sky_overlay_texture) {
        mp.sky_overlay_tex_name =
            resolve_env_texture_hash(sky.sky_overlay_texture_hash);
    }
    if (sky.has_moon_texture) {
        mp.sky_moon_tex_name =
            resolve_env_texture_hash(sky.moon_texture_hash);
    }
    if (sky.has_moon_glare_texture) {
        mp.sky_moon_glare_tex_name =
            resolve_env_texture_hash(sky.moon_glare_texture_hash);
    }
#else
    (void)mp;
    (void)sky;
#endif
}

static void apply_sky_theme_to_preview(ModelPreview& mp,
                                       const Gdb::SkyTheme& sky)
{
    mp.has_sky_theme = sky.has_any;
    if (!mp.has_sky_theme) return;

    std::copy(std::begin(sky.sky_colour),
              std::end(sky.sky_colour),
              std::begin(mp.sky_top_colour));
    if (sky.has_fog_colour) {
        std::copy(std::begin(sky.fog_colour),
                  std::end(sky.fog_colour),
                  std::begin(mp.sky_bottom_colour));
    } else {
        std::copy(std::begin(sky.complementary_colour),
                  std::end(sky.complementary_colour),
                  std::begin(mp.sky_bottom_colour));
    }
    std::copy(std::begin(sky.sunset_colour),
              std::end(sky.sunset_colour),
              std::begin(mp.sky_sunset_colour));
    mp.sky_params[0] = sky.sun_intensity;
    mp.sky_params[1] = sky.complementary_bias;
    mp.sky_params[2] = sky.rayleigh;
    mp.sky_params[3] = sky.mie;
    mp.sky_time_of_day = sky.source_time_of_day;
    apply_sky_texture_hashes_to_preview(mp, sky);
}

static void apply_cloud_theme_to_preview(ModelPreview& mp,
                                         const Gdb::CloudTheme& clouds)
{
    mp.has_cloud_theme = clouds.has_any;
    mp.cloud_layer_count = clouds.layer_count;
    int density_resolved = 0;
    int density_missing = 0;
    for (int i = 0; i < 4; ++i) {
        const Gdb::CloudLayerTheme& layer = clouds.layers[i];
        const float opacity = 1.0f - std::clamp(layer.transparency, 0.0f, 1.0f);
        mp.cloud_layer[i][0] = layer.enabled ? opacity : 0.0f;
        mp.cloud_layer[i][1] = layer.height;
        mp.cloud_layer[i][2] = layer.texture_scale_x;
        mp.cloud_layer[i][3] = layer.texture_scale_y;

        mp.cloud_motion[i][0] = layer.position_x;
        mp.cloud_motion[i][1] = layer.position_y;
        mp.cloud_motion[i][2] = layer.velocity_x;
        mp.cloud_motion[i][3] = layer.velocity_y;

        mp.cloud_light[i][0] = layer.brightness;
        mp.cloud_light[i][1] = layer.ambient_light;
        mp.cloud_light[i][2] = layer.normal_strength;
        mp.cloud_light[i][3] = layer.translucency_strength;

#ifdef _WIN32
        if (mp.cloud_density_srv[i]) {
            mp.cloud_density_srv[i]->Release();
            mp.cloud_density_srv[i] = nullptr;
        }
        mp.cloud_density_tried[i] = false;
#endif
        mp.cloud_density_tex_name[i].clear();
        if (layer.enabled && layer.has_density_map) {
            mp.cloud_density_tex_name[i] =
                resolve_env_texture_hash(layer.density_map_hash);
            if (!mp.cloud_density_tex_name[i].empty()) {
                ++density_resolved;
            } else {
                ++density_missing;
            }
        }
    }
    if (clouds.has_any) {
        std::ostringstream ss;
        ss << "cloud theme: density maps resolved=" << density_resolved
           << " missing=" << density_missing;
        OutputLog::info(ss.str());
    }
}

static void copy_sky_theme_to_keyframe(MPSkyCloudKeyframe& key,
                                       const Gdb::SkyTheme& sky)
{
    if (!sky.has_any) return;
    std::copy(std::begin(sky.sky_colour),
              std::end(sky.sky_colour),
              std::begin(key.sky_top_colour));
    if (sky.has_fog_colour) {
        std::copy(std::begin(sky.fog_colour),
                  std::end(sky.fog_colour),
                  std::begin(key.sky_bottom_colour));
    } else {
        std::copy(std::begin(sky.complementary_colour),
                  std::end(sky.complementary_colour),
                  std::begin(key.sky_bottom_colour));
    }
    std::copy(std::begin(sky.sunset_colour),
              std::end(sky.sunset_colour),
              std::begin(key.sky_sunset_colour));
    key.sky_params[0] = sky.sun_intensity;
    key.sky_params[1] = sky.complementary_bias;
    key.sky_params[2] = sky.rayleigh;
    key.sky_params[3] = sky.mie;
}

static void copy_cloud_theme_to_keyframe(MPSkyCloudKeyframe& key,
                                         const Gdb::CloudTheme& clouds)
{
    key.has_cloud_theme = clouds.has_any;
    key.cloud_layer_count = clouds.layer_count;
    for (int i = 0; i < 4; ++i) {
        const Gdb::CloudLayerTheme& layer = clouds.layers[i];
        const float opacity =
            1.0f - std::clamp(layer.transparency, 0.0f, 1.0f);
        key.cloud_layer[i][0] = layer.enabled ? opacity : 0.0f;
        key.cloud_layer[i][1] = layer.height;
        key.cloud_layer[i][2] = layer.texture_scale_x;
        key.cloud_layer[i][3] = layer.texture_scale_y;

        key.cloud_motion[i][0] = layer.position_x;
        key.cloud_motion[i][1] = layer.position_y;
        key.cloud_motion[i][2] = layer.velocity_x;
        key.cloud_motion[i][3] = layer.velocity_y;

        key.cloud_light[i][0] = layer.brightness;
        key.cloud_light[i][1] = layer.ambient_light;
        key.cloud_light[i][2] = layer.normal_strength;
        key.cloud_light[i][3] = layer.translucency_strength;
    }
}

static void apply_environment_timeline_to_preview(
    ModelPreview& mp,
    const Gdb::EnvironmentThemeTimeline& timeline)
{
    mp.has_day_night_cycle =
        timeline.has_any && timeline.keyframes.size() >= 2;
    mp.day_night_keyframes.clear();
    if (!mp.has_day_night_cycle) return;

    for (const Gdb::EnvironmentThemeKeyframe& src : timeline.keyframes) {
        MPSkyCloudKeyframe key;
        key.time_of_day = std::isfinite(src.time_of_day)
            ? src.time_of_day - std::floor(src.time_of_day)
            : 0.0f;
        if (key.time_of_day < 0.0f) key.time_of_day += 1.0f;
        std::copy(std::begin(mp.sky_top_colour),
                  std::end(mp.sky_top_colour),
                  std::begin(key.sky_top_colour));
        std::copy(std::begin(mp.sky_bottom_colour),
                  std::end(mp.sky_bottom_colour),
                  std::begin(key.sky_bottom_colour));
        std::copy(std::begin(mp.sky_sunset_colour),
                  std::end(mp.sky_sunset_colour),
                  std::begin(key.sky_sunset_colour));
        std::copy(std::begin(mp.sky_params),
                  std::end(mp.sky_params),
                  std::begin(key.sky_params));
        copy_sky_theme_to_keyframe(key, src.sky);
        copy_cloud_theme_to_keyframe(key, src.clouds);
#ifdef _WIN32
        if (mp.sky_overlay_tex_name.empty() &&
            src.sky.has_sky_overlay_texture) {
            mp.sky_overlay_tex_name =
                resolve_env_texture_hash(src.sky.sky_overlay_texture_hash);
            mp.sky_overlay_tried = false;
        }
        if (mp.sky_moon_tex_name.empty() && src.sky.has_moon_texture) {
            mp.sky_moon_tex_name =
                resolve_env_texture_hash(src.sky.moon_texture_hash);
            mp.sky_moon_tried = false;
        }
        if (mp.sky_moon_glare_tex_name.empty() &&
            src.sky.has_moon_glare_texture) {
            mp.sky_moon_glare_tex_name =
                resolve_env_texture_hash(src.sky.moon_glare_texture_hash);
            mp.sky_moon_glare_tried = false;
        }
#endif
        mp.day_night_keyframes.push_back(key);
    }

    mp.has_sky_theme = true;
    std::ostringstream ss;
    ss << "day/night cycle: preview timeline active keyframes="
       << mp.day_night_keyframes.size()
       << " cycle=" << std::fixed << std::setprecision(0)
       << mp.day_night_cycle_seconds << "s";
    OutputLog::info(ss.str());
}

#ifdef _WIN32
struct LevelPropStreamState {
    std::atomic<int>            phase{0};
    std::atomic<size_t>         instances_loaded{0};
    size_t                      total_instances = 0;
    size_t                      model_misses    = 0;
    std::vector<MDLMeshGeom>    geoms;
    MDLInfo                     info;
    std::thread                 worker;
    std::vector<GeneratedTerrainTexture> terrain_textures;

    std::vector<Level::PropBlock>  blocks;
    std::string                    model_body_bnk;
    Gdb::SkyTheme                  sky_theme;
    Gdb::CloudTheme                cloud_theme;
    Gdb::EnvironmentThemeTimeline  environment_timeline;
    float                          terrain_tile_size = 1.0f;
    int                            terrain_width  = 0;
    int                            terrain_height = 0;
};

static LevelPropStreamState g_level_prop_stream;

static void prop_worker_run(LevelPropStreamState* s)
{
    std::string current_model;
    try {
        const float terrain_cx =
            (float(s->terrain_width) - 1.0f) * 0.5f * s->terrain_tile_size;
        const float terrain_cz =
            (float(s->terrain_height) - 1.0f) * 0.5f * s->terrain_tile_size;

        std::unordered_map<std::string, CachedPropModel> cache;

        for (const auto& block : s->blocks) {
            current_model = block.model_path;
            if (S.cancel_requested.load()) {
                OutputLog::warn("prop bake worker aborted: cancel requested");
                break;
            }
            if (block.model_path.empty()) {
                s->instances_loaded.fetch_add(block.instances.size(),
                                              std::memory_order_relaxed);
                continue;
            }

            auto& cached = cache[block.model_path];
            if (!cached.loaded) {
                load_cached_prop_model(block.model_path,
                                       s->model_body_bnk,
                                       cached);
                cached.loaded = true;
                if (cached.geoms.empty()) {
                    ++s->model_misses;
                    OutputLog::warn("level props: model load miss " +
                                    block.model_path);

                    std::string want = block.model_path;
                    size_t sl = want.find_last_of("/\\");
                    std::string want_base = (sl == std::string::npos)
                                               ? want : want.substr(sl + 1);
                    std::string stem = want_base;
                    size_t dot = stem.find_last_of('.');
                    if (dot != std::string::npos) stem.resize(dot);
                    std::transform(stem.begin(), stem.end(), stem.begin(),
                                   ::tolower);

                    int shown = 0;
                    for (const auto& e : S.all_mdl_files) {
                        if (shown >= 5) break;
                        std::string lname = e.name;
                        std::transform(lname.begin(), lname.end(),
                                       lname.begin(), ::tolower);
                        if (lname.find(stem) == std::string::npos) continue;
                        std::string bnk_leaf =
                            std::filesystem::path(e.bnk_path).filename().string();
                        OutputLog::info("  near-match: " + e.full_path +
                                        "  (in " + bnk_leaf + ")");
                        ++shown;
                    }
                    if (shown == 0) {
                        OutputLog::info("  no near-matches in " +
                                        std::to_string(S.all_mdl_files.size()) +
                                        "-entry global .mdl index");
                    }
                }
            }

            if (cached.geoms.empty()) {
                s->instances_loaded.fetch_add(block.instances.size(),
                                              std::memory_order_relaxed);
                continue;
            }

            std::vector<MDLMeshGeom> combined(cached.geoms.size());
            std::vector<size_t> chunk_index(cached.geoms.size(), 0);
            for (size_t gi = 0; gi < cached.geoms.size(); ++gi) {
                const auto& src = cached.geoms[gi];
                init_combined_prop_geom(combined[gi], src, block.model_path,
                                        block.instances.size(), block.type, 0);
            }

            for (const auto& inst : block.instances) {
                for (size_t gi = 0; gi < cached.geoms.size(); ++gi) {
                    const auto& src = cached.geoms[gi];
                    if (!src.positions.empty() && !src.indices.empty()) {
                        if (would_exceed_combined_prop_limits(combined[gi],
                                                              src)) {
                            flush_combined_prop_geom(
                                s->geoms, combined[gi], src, block.model_path,
                                block.instances.size(), block.type,
                                chunk_index[gi]);
                        }
                        merge_transformed_instance_into(combined[gi], src, inst);
                    }
                }
                s->instances_loaded.fetch_add(1, std::memory_order_relaxed);
            }

            for (auto& cg : combined) {
                if (!cg.positions.empty() && !cg.indices.empty()) {
                    s->geoms.push_back(std::move(cg));
                }
            }
            (void)terrain_cx; (void)terrain_cz;
        }

        s->phase.store(2, std::memory_order_release);
    } catch (const std::exception& e) {
        OutputLog::error("level props: prop bake worker aborted on " +
                         current_model + " (" + e.what() + ")");
        s->phase.store(2, std::memory_order_release);
    } catch (...) {
        OutputLog::error("level props: prop bake worker aborted on " +
                         current_model + " (unknown exception)");
        s->phase.store(2, std::memory_order_release);
    }
}

static void start_level_prop_stream(std::vector<MDLMeshGeom> geoms,
                                    MDLInfo info)
{
    if (g_level_prop_stream.worker.joinable()) {
        g_level_prop_stream.worker.join();
    }
    g_level_prop_stream.phase.store(0);
    g_level_prop_stream.instances_loaded.store(0);
    g_level_prop_stream.total_instances = 0;
    g_level_prop_stream.model_misses    = 0;
    g_level_prop_stream.terrain_textures.clear();
    g_level_prop_stream.geoms           = std::move(geoms);
    g_level_prop_stream.info            = std::move(info);
    g_level_prop_stream.blocks          = g_pending_level_prop_blocks;
    g_level_prop_stream.model_body_bnk  = g_pending_level_model_body_bnk;
    g_level_prop_stream.sky_theme       = g_pending_level_sky_theme;
    g_level_prop_stream.cloud_theme     = g_pending_level_cloud_theme;
    g_level_prop_stream.environment_timeline =
        g_pending_level_environment_timeline;
    g_level_prop_stream.terrain_tile_size = g_pending_terrain_ghf_tile_size;
    g_level_prop_stream.terrain_width   = g_pending_terrain_ghf_width;
    g_level_prop_stream.terrain_height  = g_pending_terrain_ghf_height;

    for (const auto& b : g_level_prop_stream.blocks) {
        g_level_prop_stream.total_instances += b.instances.size();
    }
    if (g_level_prop_stream.total_instances == 0) return;

    progress_open((int)g_level_prop_stream.total_instances,
                  "Loading props...");

    g_level_prop_stream.phase.store(1);
    g_level_prop_stream.worker = std::thread(prop_worker_run,
                                             &g_level_prop_stream);
}

static void bind_generated_terrain_textures(
    ID3D11Device* device,
    const std::vector<GeneratedTerrainTexture>& textures,
    const char* log_prefix)
{
    if (!device || textures.empty() || g_mp.meshes.empty()) return;

    for (const GeneratedTerrainTexture& t : textures) {
        if (t.rgba.empty() || t.width <= 0 || t.height <= 0) continue;
        if (t.mesh_index >= g_mp.meshes.size()) continue;

        ID3D11ShaderResourceView* srv =
            create_srv_from_rgba(device, t.width, t.height, t.rgba);
        if (!srv) continue;

        MPPerMesh& m = g_mp.meshes[t.mesh_index];
        if (m.srv_diffuse) m.srv_diffuse->Release();
        m.srv_diffuse      = srv;
        m.diffuse_visible  = true;
        m.diffuse_tex_name = t.label;
        const bool splat_active =
            (t.mesh_index == 0 && TerrainSplat::Get().ok);
        if (t.mesh_index == 0) {
            m.is_terrain = true;
            if (splat_active) {
                m.diffuse_tex_name = "ehf_splat_terrain";
            }
        }

        TerrainTextureRegistry::Register(t.label, t.rgba, t.width, t.height);
        if (splat_active) {
            OutputLog::success(
                "terrain SPLAT shader retained after prop upload "
                "(composite kept only as fallback)");
        } else {
            OutputLog::success(std::string(log_prefix) + ": " + t.label +
                               " (" + std::to_string(t.width) + "x" +
                               std::to_string(t.height) + ")");
        }
    }
}

static bool stream_level_prop_batch(ID3D11Device* device)
{
    const int phase = g_level_prop_stream.phase.load(std::memory_order_acquire);
    if (phase == 0 || phase == 3) return false;

    const size_t loaded =
        g_level_prop_stream.instances_loaded.load(std::memory_order_relaxed);
    const size_t total = g_level_prop_stream.total_instances;
    progress_update((int)loaded, (int)std::max<size_t>(total, 1),
                    "Loading props " + std::to_string(loaded) + "/" +
                    std::to_string(total));

    if (phase != 2) return true;

    if (g_level_prop_stream.worker.joinable()) {
        g_level_prop_stream.worker.join();
    }

    if (S.cancel_requested.load()) {
        OutputLog::warn("prop upload aborted: cancel requested");
        g_level_prop_stream.geoms.clear();
        g_level_prop_stream.geoms.shrink_to_fit();
        g_level_prop_stream.blocks.clear();
        g_level_prop_stream.terrain_textures.clear();
        g_level_prop_stream.phase.store(3);
        progress_done();
        S.cancel_requested.store(false);
        return true;
    }

    if (!g_level_prop_stream.geoms.empty()) {
        FlyCam saved_cam = g_flycam;
        if (!g_mp_initialized) {
            MP_Init(device, g_mp, 800, 600);
            g_mp_initialized = true;
        }
        try {
            MP_Build(device, g_level_prop_stream.geoms,
                     g_level_prop_stream.info, g_mp);
            apply_sky_theme_to_preview(g_mp, g_level_prop_stream.sky_theme);
            apply_cloud_theme_to_preview(g_mp,
                                         g_level_prop_stream.cloud_theme);
            apply_environment_timeline_to_preview(
                g_mp, g_level_prop_stream.environment_timeline);
        } catch (const std::exception& e) {
            OutputLog::error(std::string("level props: MP_Build failed (") +
                             e.what() + ")");
        } catch (...) {
            OutputLog::error("level props: MP_Build failed (unknown exception)");
        }
        bind_generated_terrain_textures(
            device,
            g_level_prop_stream.terrain_textures,
            "terrain texture rebound after prop upload");
        size_t water_meshes = 0;
        for (const auto& m : g_mp.meshes) {
            if (m.is_water) ++water_meshes;
        }
        if (water_meshes > 0) {
            OutputLog::success("water: " + std::to_string(water_meshes) +
                               " mesh(es) active after prop upload");
        }
        g_mp.no_tilt = true;
        S.terrain_mode = true;
        g_flycam = saved_cam;
    }

    OutputLog::info("level props: streamed " + std::to_string(loaded) +
                    " prop instances" +
                    (g_level_prop_stream.model_misses
                        ? " (" + std::to_string(g_level_prop_stream.model_misses) +
                          " model load misses)"
                        : std::string()));
    progress_done();

    g_level_prop_stream.geoms.clear();
    g_level_prop_stream.geoms.shrink_to_fit();
    g_level_prop_stream.blocks.clear();
    g_level_prop_stream.blocks.shrink_to_fit();
    g_level_prop_stream.terrain_textures.clear();
    g_level_prop_stream.terrain_textures.shrink_to_fit();
    g_level_prop_stream.phase.store(3);
    return true;
}
#endif

static void append_transformed_prop_geom(std::vector<MDLMeshGeom>& out,
                                         const MDLMeshGeom& src,
                                         const Level::PropInstance& inst,
                                         float ,
                                         float )
{
    MDLMeshGeom dst = src;

    for (size_t i = 0; i + 2 < dst.positions.size(); i += 3) {
        const float lx = src.positions[i + 0];
        const float ly = src.positions[i + 2];
        const float lz = src.positions[i + 1];
        transform_instance_point(inst, lx, ly, lz,
                                 dst.positions[i + 0],
                                 dst.positions[i + 1],
                                 dst.positions[i + 2]);
    }

    if (dst.normals.size() == src.normals.size()) {
        for (size_t i = 0; i + 2 < dst.normals.size(); i += 3) {
            const float lx = src.normals[i + 0];
            const float ly = src.normals[i + 2];
            const float lz = src.normals[i + 1];
            transform_instance_normal(inst, lx, ly, lz,
                                      dst.normals[i + 0],
                                      dst.normals[i + 1],
                                      dst.normals[i + 2]);
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
            cached.loaded =
                load_cached_prop_model(block.model_path,
                                       g_pending_level_model_body_bnk,
                                       cached);
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

        std::vector<MDLMeshGeom> combined(cached.geoms.size());
        std::vector<size_t> chunk_index(cached.geoms.size(), 0);
        for (size_t gi = 0; gi < cached.geoms.size(); ++gi) {
            const auto& src = cached.geoms[gi];
            init_combined_prop_geom(combined[gi], src, block.model_path,
                                    block.instances.size(), block.type, 0);
        }

        for (const auto& inst : block.instances) {
            ++instances_seen;
            for (size_t gi = 0; gi < cached.geoms.size(); ++gi) {
                const auto& src = cached.geoms[gi];
                if (!src.positions.empty() && !src.indices.empty()) {
                    if (would_exceed_combined_prop_limits(combined[gi], src)) {
                        flush_combined_prop_geom(
                            geoms, combined[gi], src, block.model_path,
                            block.instances.size(), block.type,
                            chunk_index[gi]);
                    }
                    merge_transformed_instance_into(combined[gi], src, inst);
                }
            }
            ++instances_loaded;
        }

        for (auto& cg : combined) {
            if (!cg.positions.empty() && !cg.indices.empty()) {
                geoms.push_back(std::move(cg));
            }
        }
        (void)terrain_cx; (void)terrain_cz;
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
                    " prop instances" +
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

            std::vector<unsigned char> buf;
            bool ok = false;
            try {
                if (is_nested) {
                    ok = reconstruct_nested_mdl(bnk_to_use, item.index, buf);
                } else {
                    ok = build_mdl_buffer_for_name(name, buf);
                }
                if (!ok) {
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
                    if (S.mdl_info.MeshCount > 0) {
                        if (reparse_mdl_missing_buffers_optstr(buf, S.mdl_info)) {
                            OutputLog::info("MDL: optstr-scan fallback recovered after parse failure");
                            S.mdl_info_ok = true;
                        }
                    }
                    if (!S.mdl_info_ok) {
                        if (reparse_mdl_as_foliage_48b(buf, S.mdl_info)) {
                            OutputLog::info("MDL: foliage-48b fallback recovered after parse failure");
                            S.mdl_info_ok = true;
                        }
                    }
                }
                if (S.mdl_info_ok) {
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
                    {
                        auto missing = [&]() -> size_t {
                            size_t n = 0;
                            if (S.mdl_info.MeshBuffers.size() < S.mdl_info.MeshCount) {
                                n += S.mdl_info.MeshCount - S.mdl_info.MeshBuffers.size();
                            }
                            for (const auto& mb : S.mdl_info.MeshBuffers) {
                                if (mb.VertexCount == 0) ++n;
                            }
                            return n;
                        };
                        if (missing() > 0) {
                            if (reparse_mdl_missing_buffers_optstr(buf, S.mdl_info)) {
                                OutputLog::info("MDL: optstr-scan fallback filled missing buffer(s)");
                            }
                        }
                        if (missing() > 0) {
                            if (reparse_mdl_as_foliage_48b(buf, S.mdl_info)) {
                                OutputLog::info("MDL: foliage-48b fallback filled the buffer");
                            }
                        }
                        {
                            std::string lp = parse_path;
                            std::transform(lp.begin(), lp.end(), lp.begin(),
                                           [](unsigned char c){ return (char)std::tolower(c); });
                            std::replace(lp.begin(), lp.end(), '\\', '/');
                            const bool multi_instance_target =
                                lp.find("bs_townhouse_basic_snow_v2") != std::string::npos &&
                                (lp.find("/exterior.mdl") != std::string::npos ||
                                 lp.find("/interior.mdl") != std::string::npos);
                            if (multi_instance_target) {
                                const size_t buffers_before_multi =
                                    S.mdl_info.MeshBuffers.size();
                                if (reparse_mdl_multi_instance_buffers(buf, S.mdl_info)) {
                                    OutputLog::info(
                                        "MDL: multi-instance fallback expanded " +
                                        std::to_string(buffers_before_multi) +
                                        " buffer(s) to " +
                                        std::to_string(S.mdl_info.MeshBuffers.size()));
                                }
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
                    if (S.texture_window_srv) {
                        S.texture_window_srv->Release();
                        S.texture_window_srv = nullptr;
                    }
#endif
                    S.pending_preview_build = true;
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
            S.texture_mip_index = -1;
            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            bool has_alpha = false;
            if (!decode_tex_to_rgba(tex_buf, rgba, w, h, &has_alpha,
                                    -1)) {
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

    if (S.pending_preview_build) {
        S.pending_preview_build = false;
#ifdef _WIN32
        try {
            if (!g_mp_initialized) {
                g_mp_initialized = MP_Init(device, g_mp, 800, 600);
            }
            if (g_mp_initialized) {
                MP_Build(device, S.mdl_meshes, S.mdl_info, g_mp);
                S.show_model_preview = false;
                S.model_preview_open = false;
                S.model_materials_open = false;
                S.terrain_mode = false;
                g_mp.no_tilt = false;
                S.cam_yaw = 3.14159265f;
                S.cam_pitch = 0.2f;
                S.cam_dist = 3.0f;
            }
        } catch (const std::exception& e) {
            OutputLog::error(std::string("MDL preview build failed: ") +
                             e.what());
        } catch (...) {
            OutputLog::error("MDL preview build failed: unknown exception");
        }
#else
        MP_Release(g_mp);
        g_mp_initialized = MP_Init(g_mp, 800, 600);
        if (g_mp_initialized) {
            MP_Build(S.mdl_meshes, S.mdl_info, g_mp);
            S.show_model_preview = false;
            S.model_preview_open = false;
            S.model_materials_open = false;
            S.terrain_mode = false;
            g_mp.no_tilt = false;
            S.cam_yaw = 3.14159265f;
            S.cam_pitch = 0.2f;
            S.cam_dist = 3.0f;
        }
#endif
    }

#ifdef _WIN32
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

    if (g_pending_terrain_load.exchange(false)) {
        if (S.cancel_requested.load()) {
            OutputLog::warn("terrain stage skipped: cancel requested");
            g_pending_terrain_mesh = Level::TerrainMesh{};
            g_pending_terrain_label.clear();
            g_pending_terrain_ehf_bytes.clear();
            g_pending_adjacent_terrain_meshes.clear();
            g_pending_terrain_ghf_payload.clear();
            g_pending_terrain_ghf_heights.clear();
            g_pending_terrain_ghf_entry = FlatAssetEntry{};
            g_pending_level_prop_blocks.clear();
            g_pending_level_model_body_bnk.clear();
            g_pending_level_water_present = false;
            g_pending_level_water_scene = Level::WaterScene{};
            g_pending_level_water_theme = Gdb::WaterTheme{};
            g_pending_level_sky_theme = Gdb::SkyTheme{};
            g_pending_level_cloud_theme = Gdb::CloudTheme{};
            g_pending_level_environment_timeline =
                Gdb::EnvironmentThemeTimeline{};
            progress_done();
            S.cancel_requested.store(false);
            return;
        }
        progress_update(72, 100, "Uploading terrain...");
        const Level::TerrainMesh& tm = g_pending_terrain_mesh;
        if (!tm.ok || tm.indices.empty()) {
            OutputLog::error("pending terrain mesh is empty - skipped");
        } else {
            TerrainTextureRegistry::Clear();
            EhfLodThumbnails::Clear();
            TerrainSplat::Clear();
            TerrainEdit::Clear();
            if (!g_mp_initialized) {
                MP_Init(device, g_mp, 800, 600);
                g_mp_initialized = true;
            }

            MDLMeshGeom g;
            g.positions    = tm.positions;
            g.normals      = tm.normals;
            g.uvs          = tm.uvs;
            g.indices      = tm.indices;
            g.bone_ids.assign(tm.positions.size() / 3 * 4, 0);
            g.bone_weights.assign(tm.positions.size() / 3 * 4, 0.f);
            for (size_t v = 0; v < tm.positions.size() / 3; ++v) {
                g.bone_weights[v * 4 + 0] = 1.0f;
            }
            g.name = "terrain";
            g.is_terrain = true;

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

            std::vector<uint8_t> picked_rgba;
            int picked_w = 0, picked_h = 0;
            std::string picked_label;
            float uv_scale = 1.0f;
            std::vector<GeneratedTerrainTexture> generated_terrain_textures;

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
                picked_label = (composite_name == "embedded_tile_albedo")
                    ? std::string("ehf_embedded_tile_albedo")
                    : "ehf_composite[" + composite_name + " * lightmap]";
                uv_scale     = 1.0f;
            } else if (Level::DecodeEhfTerrainAlbedoFromBytes(
                    g_pending_terrain_ehf_bytes,
                    g_pending_terrain_mesh.width,
                    g_pending_terrain_mesh.height,
                    picked_rgba, picked_w, picked_h))
            {
                picked_label = "ehf_baked_albedo";
                uv_scale     = 1.0f;
            } else {
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
                    uv_scale = 16.0f;
                } else {
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

            if (!picked_rgba.empty() && picked_w > 0 && picked_h > 0) {
                GeneratedTerrainTexture gt;
                gt.mesh_index = 0;
                gt.label      = picked_label;
                gt.rgba       = picked_rgba;
                gt.width      = picked_w;
                gt.height     = picked_h;
                generated_terrain_textures.push_back(std::move(gt));
            }

            const bool terrain_space_texture =
                picked_label.rfind("ehf_composite[", 0) == 0 ||
                picked_label == "ehf_embedded_tile_albedo" ||
                picked_label == "ehf_baked_albedo";
            if (terrain_space_texture) {
                normalize_grid_uvs(g, tm.width, tm.height);
            } else if (uv_scale != 1.0f) {
                for (float& uv : g.uvs) uv *= uv_scale;
            }

            std::vector<MDLMeshGeom> geoms;
            geoms.push_back(std::move(g));
            for (const auto& adj : g_pending_adjacent_terrain_meshes) {
                const Level::TerrainMesh& am = adj.mesh;
                if (!am.ok || am.indices.empty()) continue;
                MDLMeshGeom ag;
                ag.positions = am.positions;
                ag.normals   = am.normals;
                ag.uvs       = am.uvs;
                ag.indices   = am.indices;
                normalize_grid_uvs(ag, am.width, am.height);
                ag.bone_ids.assign(am.positions.size() / 3 * 4, 0);
                ag.bone_weights.assign(am.positions.size() / 3 * 4, 0.f);
                for (size_t v = 0; v < am.positions.size() / 3; ++v) {
                    ag.bone_weights[v * 4 + 0] = 1.0f;
                }
                ag.name = adj.label.empty()
                    ? std::string("adjacent terrain")
                    : std::string("adjacent terrain: ") + adj.label;
                geoms.push_back(std::move(ag));
            }

            size_t water_geom_first = geoms.size();
            size_t water_geom_count = 0;
            size_t themed_water_geom_count = 0;
            float water_theme_opacity = 1.0f;
            if (g_pending_level_water_present) {
                for (size_t bi = 0; bi < g_pending_level_water_scene.bodies.size(); ++bi) {
                    const auto& body = g_pending_level_water_scene.bodies[bi];
                    for (size_t ti = 0; ti < body.tiles.size(); ++ti) {
                        const auto& t = body.tiles[ti];

                        float y = body.base_height;
                        if (!std::isfinite(y) || y == 0.0f) y = t.h_min;
                        if (!std::isfinite(y) || y == 0.0f) y = t.h_max;

                        const float half_x = std::abs(t.ex) * 0.5f;
                        const float half_z = std::abs(t.ez) * 0.5f;
                        const float x0 = t.cx - half_x;
                        const float x1 = t.cx + half_x;
                        const float z0 = t.cz - half_z;
                        const float z1 = t.cz + half_z;

                        const size_t mask_count = t.mask.size();
                        int mask_w = 0;
                        int mask_h = 0;
                        auto accept_mask_dims = [&](uint32_t w,
                                                    uint32_t h) -> bool {
                            if (w == 0 || h == 0) return false;
                            const uint64_t cells = uint64_t(w) * uint64_t(h);
                            if (cells != mask_count ||
                                cells > uint64_t(std::numeric_limits<int>::max())) {
                                return false;
                            }
                            mask_w = int(w);
                            mask_h = int(h);
                            return true;
                        };
                        auto accept_vertex_dims = [&](uint32_t w,
                                                      uint32_t h) -> bool {
                            if (w <= 1 || h <= 1) return false;
                            return accept_mask_dims(w - 1, h - 1);
                        };
                        if (t.dims.size() >= 2) {
                            const uint32_t a = t.dims[0];
                            const uint32_t b = t.dims[1];
                            (void)(accept_mask_dims(a, b) ||
                                   accept_mask_dims(b, a) ||
                                   accept_vertex_dims(a, b) ||
                                   accept_vertex_dims(b, a));
                        }
                        if (mask_w <= 0 || mask_h <= 0) {
                            const int sq = int(std::lround(
                                std::sqrt(double(mask_count))));
                            if (sq > 0 && size_t(sq) * size_t(sq) == mask_count) {
                                mask_w = sq;
                                mask_h = sq;
                            } else if (mask_count > 0) {
                                mask_w = std::max(1, int(mask_count));
                                mask_h = 1;
                            } else {
                                mask_w = 16;
                                mask_h = 16;
                            }
                        }
                        mask_w = std::max(mask_w, 1);
                        mask_h = std::max(mask_h, 1);

                        auto choose_subdiv = [](float extent,
                                                int cells) -> int {
                            if (cells <= 0 || !std::isfinite(extent))
                                return 1;
                            const float cell = std::abs(extent) / float(cells);
                            if (cell <= 0.75f) return 1;
                            return std::clamp(int(std::ceil(cell / 0.75f)),
                                              1, 16);
                        };
                        int sub_x = choose_subdiv(x1 - x0, mask_w);
                        int sub_z = choose_subdiv(z1 - z0, mask_h);
                        int mesh_w = mask_w * sub_x;
                        int mesh_h = mask_h * sub_z;
                        while (uint64_t(mesh_w + 1) * uint64_t(mesh_h + 1) >
                               120000ull && (sub_x > 1 || sub_z > 1)) {
                            if (sub_x >= sub_z && sub_x > 1) --sub_x;
                            else if (sub_z > 1) --sub_z;
                            mesh_w = mask_w * sub_x;
                            mesh_h = mask_h * sub_z;
                        }

                        MDLMeshGeom wg;
                        const int vert_w = mesh_w + 1;
                        const int vert_h = mesh_h + 1;
                        const size_t vert_count =
                            size_t(vert_w) * size_t(vert_h);
                        wg.positions.reserve(vert_count * 3);
                        wg.normals.reserve(vert_count * 3);
                        wg.uvs.reserve(vert_count * 2);
                        for (int mz = 0; mz <= mesh_h; ++mz) {
                            const float vz = float(mz) / float(mesh_h);
                            const float pz = z0 + (z1 - z0) * vz;
                            for (int mx = 0; mx <= mesh_w; ++mx) {
                                const float vx = float(mx) / float(mesh_w);
                                const float px = x0 + (x1 - x0) * vx;
                                wg.positions.insert(wg.positions.end(),
                                                    { px, y, pz });
                                wg.normals.insert(wg.normals.end(),
                                                  { 0.0f, 1.0f, 0.0f });
                                wg.uvs.insert(wg.uvs.end(), { px, pz });
                            }
                        }

                        auto active_cell = [&](int mx, int mz) -> bool {
                            if (t.mask.empty()) return true;
                            const int mask_x =
                                std::min(mask_w - 1, mx / sub_x);
                            const int mask_z =
                                std::min(mask_h - 1, mz / sub_z);
                            const size_t mi = size_t(mask_z) * size_t(mask_w)
                                            + size_t(mask_x);
                            return mi < t.mask.size() && t.mask[mi] != 0;
                        };
                        for (int mz = 0; mz < mesh_h; ++mz) {
                            for (int mx = 0; mx < mesh_w; ++mx) {
                                if (!active_cell(mx, mz)) continue;
                                const uint32_t v00 =
                                    uint32_t(mz * vert_w + mx);
                                const uint32_t v10 = v00 + 1;
                                const uint32_t v01 =
                                    uint32_t((mz + 1) * vert_w + mx);
                                const uint32_t v11 = v01 + 1;
                                wg.indices.insert(wg.indices.end(),
                                                  { v00, v11, v10,
                                                    v00, v01, v11 });
                            }
                        }
                        if (wg.indices.empty()) {
                            continue;
                        }
                        wg.bone_ids.assign(vert_count * 4, 0);
                        wg.bone_weights.assign(vert_count * 4, 0.0f);
                        for (size_t v = 0; v < vert_count; ++v) {
                            wg.bone_weights[v * 4 + 0] = 1.0f;
                        }
                        wg.name = "water: body" + std::to_string(bi) +
                                  ":tile" + std::to_string(ti);
                        wg.diffuse_tex_name = body.normal_map_path;
                        wg.is_water = true;
                        for (size_t pi = 0; pi < body.wave_params.size() &&
                                            pi < 38; ++pi) {
                            wg.water_params[pi] = body.wave_params[pi];
                        }
                        wg.water_params[0] = y;
                        if (g_pending_level_water_theme.has_any) {
                            const Gdb::WaterTheme& wt =
                                g_pending_level_water_theme;
                            wg.has_water_theme = true;
                            wg.water_opacity = wt.opacity;
                            water_theme_opacity = wt.opacity;
                            std::copy(std::begin(wt.shallow_colour),
                                      std::end(wt.shallow_colour),
                                      std::begin(wg.water_shallow_colour));
                            std::copy(std::begin(wt.deep_colour),
                                      std::end(wt.deep_colour),
                                      std::begin(wg.water_deep_colour));
                            wg.water_theme_params[0] = wt.edge_blend_min;
                            wg.water_theme_params[1] = wt.edge_blend_max;
                            wg.water_theme_params[2] = wt.edge_blend_bias;
                            wg.water_theme_params[3] =
                                wt.max_refraction_distance;
                            wg.water_theme_params[4] = wt.fresnel_bias;
                            wg.water_theme_params[5] =
                                wt.reflection_strength;
                            wg.water_theme_params[6] = wt.refraction_scale;
                            wg.water_theme_params[7] = wt.reflection_scale;
                            wg.water_theme_params[8] = wt.reflection_bias;
                            wg.water_theme_params[9] = wt.normal_scale;
                            ++themed_water_geom_count;
                        }
                        geoms.push_back(std::move(wg));
                        ++water_geom_count;
                    }
                }
                if (water_geom_count > 0) {
                    OutputLog::success("water: " +
                        std::to_string(water_geom_count) +
                        " tessellated masked surface tile(s) emitted from .water");
                    if (themed_water_geom_count > 0) {
                        std::ostringstream ss;
                        ss << "water: GDB theme applied to "
                           << themed_water_geom_count
                           << " tile(s), opacity="
                           << std::fixed << std::setprecision(2)
                           << water_theme_opacity;
                        OutputLog::info(ss.str());
                    } else {
                        OutputLog::info(
                            "water: no GDB theme on emitted tiles; opaque fallback");
                    }
                }
            }

            start_level_prop_stream(geoms, info);

            if (g_mp.has_model) {
                MP_Release(g_mp);
                g_mp.has_model = false;
                g_mp_initialized = false;
                MP_Init(device, g_mp, 800, 600);
                g_mp_initialized = true;
            }
            MP_Build(device, geoms, info, g_mp);
            g_mp.no_tilt = true;
            apply_sky_theme_to_preview(g_mp, g_pending_level_sky_theme);
            apply_cloud_theme_to_preview(g_mp,
                                         g_pending_level_cloud_theme);
            apply_environment_timeline_to_preview(
                g_mp, g_pending_level_environment_timeline);

            (void)water_geom_first;
            (void)water_geom_count;

            S.terrain_mode = true;

            float minx = 1e30f, miny = 1e30f, minz = 1e30f;
            float maxx = -1e30f, maxy = -1e30f, maxz = -1e30f;
            for (size_t v = 0; v + 2 < geoms[0].positions.size(); v += 3) {
                const float x = geoms[0].positions[v];
                const float y = geoms[0].positions[v + 1];
                const float z = geoms[0].positions[v + 2];
                if (x < minx) minx = x;  if (x > maxx) maxx = x;
                if (y < miny) miny = y;  if (y > maxy) maxy = y;
                if (z < minz) minz = z;  if (z > maxz) maxz = z;
            }
            const float cx_mesh = 0.5f * (minx + maxx);
            const float cy_mesh = 0.5f * (miny + maxy);
            const float cz_mesh = 0.5f * (minz + maxz);
            const float ext_x   = (maxx - minx);
            const float ext_z   = (maxz - minz);
            const float diag    = std::sqrt(ext_x * ext_x + ext_z * ext_z) * 0.5f;

            g_flycam.pos[0] = cx_mesh;
            g_flycam.pos[1] = maxy + diag * 0.7f;
            g_flycam.pos[2] = cz_mesh - diag * 1.0f;
            g_flycam.yaw    = 0.0f;
            g_flycam.pitch  = -0.6f;
            g_flycam.is_looking = false;
            g_flycam.move_speed = std::max(diag * 0.2f, 50.0f);

            g_mp.radius   = std::max(g_mp.radius, diag * 2.0f);
            g_mp.center[0] = cx_mesh;
            g_mp.center[1] = cy_mesh;
            g_mp.center[2] = cz_mesh;

            if (!g_pending_terrain_ghf_heights.empty() &&
                g_pending_terrain_ghf_width > 0 &&
                g_pending_terrain_ghf_height > 0)
            {
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
                    }
                }

                TerrainEdit::Init(
                    g_pending_terrain_ghf_width,
                    g_pending_terrain_ghf_height,
                    g_pending_terrain_ghf_tile_size,
                     0.0f,
                     0.0f,
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

            ID3D11ShaderResourceView* terrain_srv = nullptr;
            if (!picked_rgba.empty() && picked_w > 0 && picked_h > 0) {
                terrain_srv = create_srv_from_rgba(device, picked_w,
                                                   picked_h, picked_rgba);
            }
            if (terrain_srv && !g_mp.meshes.empty()) {
                MPPerMesh& m = g_mp.meshes[0];
                if (m.srv_diffuse) m.srv_diffuse->Release();
                m.srv_diffuse      = terrain_srv;
                m.diffuse_visible  = true;
                m.diffuse_tex_name = picked_label;

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

            const std::vector<TerrainTextureRegistry::LodPaletteEntry>
                main_lod_palette = TerrainTextureRegistry::GetLodPalette();

            for (size_t ai = 0; ai < g_pending_adjacent_terrain_meshes.size(); ++ai) {
                const size_t mesh_idx = ai + 1;
                if (mesh_idx >= g_mp.meshes.size()) break;
                const auto& adj = g_pending_adjacent_terrain_meshes[ai];
                std::vector<uint8_t> adj_rgba;
                int adj_w = 0, adj_h = 0;
                std::string adj_name;
                if (!Level::BakeEhfTerrainCompositeWithBnk(
                        adj.ehf_bytes, adj.preferred_bnk,
                        adj_rgba, adj_w, adj_h, adj_name,
                        false)) {
                    continue;
                }
                ID3D11ShaderResourceView* adj_srv =
                    create_srv_from_rgba(device, adj_w, adj_h, adj_rgba);
                if (!adj_srv) continue;
                MPPerMesh& m = g_mp.meshes[mesh_idx];
                if (m.srv_diffuse) m.srv_diffuse->Release();
                m.srv_diffuse = adj_srv;
                m.diffuse_visible = true;
                m.diffuse_tex_name = "ehf_composite[" + adj_name + "]";

                GeneratedTerrainTexture gt;
                gt.mesh_index = mesh_idx;
                gt.label      = m.diffuse_tex_name;
                gt.rgba       = adj_rgba;
                gt.width      = adj_w;
                gt.height     = adj_h;
                generated_terrain_textures.push_back(std::move(gt));

                OutputLog::success("adjacent terrain texture bound: " +
                    m.diffuse_tex_name + " (" + std::to_string(adj_w) +
                    "x" + std::to_string(adj_h) + ")");
            }
            TerrainTextureRegistry::SetLodPalette(main_lod_palette);

            if (!generated_terrain_textures.empty() &&
                g_level_prop_stream.phase.load(std::memory_order_acquire) != 0)
            {
                g_level_prop_stream.terrain_textures =
                    std::move(generated_terrain_textures);
            }

            if (!g_mp.meshes.empty() && !g_pending_terrain_ehf_bytes.empty()) {
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

            (void)splat_dbg_rgba; (void)splat_dbg_w; (void)splat_dbg_h;

            {
                const std::string& preferred_bnk =
                    g_pending_terrain_level_entry.bnk_path;
                const auto& palette =
                    TerrainTextureRegistry::GetLodPalette();
                std::vector<EhfLodThumbnails::Entry> thumbs;
                thumbs.reserve(palette.size());

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
                    if (S.cancel_requested.load()) {
                        OutputLog::warn("LOD palette decode aborted: cancel requested");
                        break;
                    }
                    EhfLodThumbnails::Entry e;
                    e.base_diffuse_path   = pe.base_diffuse;
                    e.base_normal_path    = pe.base_normal;
                    e.detail_diffuse_path = pe.detail_diffuse;
                    e.detail_normal_path  = pe.detail_normal;
                    e.base_tile_scale     = pe.base_tile_scale;
                    e.detail_tile_scale   = pe.detail_tile_scale;
                    e.base_intensity      = pe.base_intensity;
                    e.detail_intensity    = pe.detail_intensity;
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

                Level::EhfParsedBody splat_parsed;
                if (Level::ParseEhfBody(g_pending_terrain_ehf_bytes,
                                        splat_parsed)) {
                    std::vector<uint8_t> lm_rgba;
                    int lm_w = 0, lm_h = 0;
                    const auto* lm_entry =
                        TerrainTextureRegistry::Find("ehf_lightmap");
                    if (lm_entry) {
                        lm_rgba = lm_entry->rgba;
                        lm_w    = lm_entry->width;
                        lm_h    = lm_entry->height;
                    }
                    const auto& fresh_thumbs = EhfLodThumbnails::Get();
                    if (TerrainSplat::Build(
                            device,
                            splat_parsed,
                            fresh_thumbs,
                            lm_rgba,
                            lm_w,
                            lm_h,
                            0.0f,
                            0.0f))
                    {
                        if (!g_mp.meshes.empty()) {
                            g_mp.meshes[0].is_terrain = true;
                            g_mp.meshes[0].diffuse_tex_name =
                                "ehf_splat_terrain";
                        }
                        OutputLog::success(
                            "terrain SPLAT shader bound: global EHF material weights");
                    } else {
                        OutputLog::warn(
                            "terrain SPLAT shader unavailable; using EHF composite texture");
                    }
                } else {
                    OutputLog::warn(
                        "terrain SPLAT parse failed; using EHF composite texture");
                }
            }

            OutputLog::success("terrain '" + g_pending_terrain_label +
                               "' built (" +
                               std::to_string(geoms[0].positions.size()/3) +
                               " verts)");
            if (g_level_prop_stream.phase.load() == 0) {
                progress_done();
            }
        }

        g_pending_terrain_mesh = Level::TerrainMesh{};
        g_pending_terrain_label.clear();
        g_pending_terrain_ehf_bytes.clear();
        g_pending_terrain_ehf_bytes.shrink_to_fit();
        g_pending_adjacent_terrain_meshes.clear();
        g_pending_adjacent_terrain_meshes.shrink_to_fit();

        g_pending_terrain_ghf_payload.clear();
        g_pending_terrain_ghf_payload.shrink_to_fit();
        g_pending_terrain_ghf_heights.clear();
        g_pending_terrain_ghf_heights.shrink_to_fit();
        g_pending_terrain_ghf_entry = FlatAssetEntry{};
        g_pending_level_prop_blocks.clear();
        g_pending_level_prop_blocks.shrink_to_fit();
        g_pending_level_model_body_bnk.clear();

        g_pending_level_water_present = false;
        g_pending_level_water_scene = Level::WaterScene{};
        g_pending_level_water_theme = Gdb::WaterTheme{};
        g_pending_level_sky_theme = Gdb::SkyTheme{};
        g_pending_level_cloud_theme = Gdb::CloudTheme{};
        g_pending_level_environment_timeline =
            Gdb::EnvironmentThemeTimeline{};
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
            g.is_terrain = true;

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
                picked_label = (composite_name == "embedded_tile_albedo")
                    ? std::string("ehf_embedded_tile_albedo")
                    : "ehf_composite[" + composite_name + " * lightmap]";
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

            const bool terrain_space_texture =
                picked_label.rfind("ehf_composite[", 0) == 0 ||
                picked_label == "ehf_embedded_tile_albedo" ||
                picked_label == "ehf_baked_albedo";
            if (terrain_space_texture) {
                normalize_grid_uvs(g, tm.width, tm.height);
            } else if (uv_scale != 1.0f) {
                for (float& uv : g.uvs) uv *= uv_scale;
            }

            std::vector<MDLMeshGeom> geoms;
            geoms.push_back(std::move(g));
            for (const auto& adj : g_pending_adjacent_terrain_meshes) {
                const Level::TerrainMesh& am = adj.mesh;
                if (!am.ok || am.indices.empty()) continue;
                MDLMeshGeom ag;
                ag.positions = am.positions;
                ag.normals = am.normals;
                ag.uvs = am.uvs;
                ag.indices = am.indices;
                normalize_grid_uvs(ag, am.width, am.height);
                ag.bone_ids.assign(am.positions.size() / 3 * 4, 0);
                ag.bone_weights.assign(am.positions.size() / 3 * 4, 0.f);
                for (size_t v = 0; v < am.positions.size() / 3; ++v) {
                    ag.bone_weights[v * 4 + 0] = 1.0f;
                }
                ag.name = adj.label.empty()
                    ? std::string("adjacent terrain")
                    : std::string("adjacent terrain: ") + adj.label;
                geoms.push_back(std::move(ag));
            }
            append_level_props_to_geoms(geoms);

            MP_Release(g_mp);
            g_mp_initialized = false;
            g_mp_initialized = MP_Init(g_mp, 800, 600);
            if (g_mp_initialized) {
                MP_Build(geoms, info, g_mp);
                g_mp.no_tilt = true;
                apply_sky_theme_to_preview(g_mp, g_pending_level_sky_theme);
                apply_cloud_theme_to_preview(g_mp,
                                             g_pending_level_cloud_theme);
                apply_environment_timeline_to_preview(
                    g_mp, g_pending_level_environment_timeline);
                S.terrain_mode = true;
                S.show_model_preview = false;
                S.model_preview_open = false;
                S.model_materials_open = false;

                float minx = 1e30f, miny = 1e30f, minz = 1e30f;
                float maxx = -1e30f, maxy = -1e30f, maxz = -1e30f;
                for (size_t v = 0; v + 2 < geoms[0].positions.size(); v += 3) {
                    const float x = geoms[0].positions[v];
                    const float y = geoms[0].positions[v + 1];
                    const float z = geoms[0].positions[v + 2];
                    if (x < minx) minx = x;  if (x > maxx) maxx = x;
                    if (y < miny) miny = y;  if (y > maxy) maxy = y;
                    if (z < minz) minz = z;  if (z > maxz) maxz = z;
                }
                const float cx_mesh = 0.5f * (minx + maxx);
                const float cy_mesh = 0.5f * (miny + maxy);
                const float cz_mesh = 0.5f * (minz + maxz);
                const float ext_x   = (maxx - minx);
                const float ext_z   = (maxz - minz);
                const float diag    = std::sqrt(ext_x * ext_x + ext_z * ext_z) * 0.5f;
                g_flycam.pos[0] = cx_mesh;
                g_flycam.pos[1] = maxy + diag * 0.7f;
                g_flycam.pos[2] = cz_mesh - diag * 1.0f;
                g_flycam.yaw = 0.0f;
                g_flycam.pitch = -0.6f;
                g_flycam.is_looking = false;
                g_flycam.move_speed = std::max(diag * 0.2f, 50.0f);
                g_mp.radius = std::max(g_mp.radius, diag * 2.0f);
                g_mp.center[0] = cx_mesh;
                g_mp.center[1] = cy_mesh;
                g_mp.center[2] = cz_mesh;

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
        g_pending_adjacent_terrain_meshes.clear();
        g_pending_adjacent_terrain_meshes.shrink_to_fit();
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
