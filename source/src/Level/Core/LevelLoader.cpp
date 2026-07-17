#include "LevelLoader.h"
#include "Level/Terrain/HeightfieldLoader.h"
#include "Level/Creation/LandscapeAuthoring.h"
#include "Level/Editing/LevelEdit.h"
#include "Level/Terrain/TextureAtlasDecoder.h"
#include "Level/Terrain/EhfPalette.h"
#include "Level/Terrain/EhfChunkParser.h"
#include "Level/Terrain/TerrainTextureRegistry.h"
#include "Level/Loading/LevelBinaryReader.h"
#include "Level/Loading/LevelCatalogLoaderInternal.h"
#include "Level/Loading/LevelTerrainLoaderInternal.h"
#include "Level/IO/VfsConfig.h"
#include "GDB/GdbModelHashlist.h"
#include "GDB/GdbParser.h"
#include "Level/Database/TextBank.h"
#include "Level/Effects/ParticleBank.h"
#include "Level/Effects/ParticleFX.h"
#include "MDL/ModelParser.h"
#include "Havok/HavokPackfileReader.h"
#include "ISO/IsoMount.h"

#include "Utilities/State.h"
#include "Utilities/Utils.h"
#include "Utilities/Progress.h"
#include "BNKCore.cpp"
#include "UI/OutputLog.h"
#include "textures/TexParser.h"
#include "textures/LhTexCodec.h"
#include "textures/export/TextureExport.h"
#include <zlib.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <vector>
#include <cstdint>
extern bool decode_tex_to_rgba(const std::vector<unsigned char>& blob,
                               std::vector<uint8_t>& rgba,
                               int& out_w, int& out_h,
                               bool* out_has_alpha,
                               int mip_index = -1);
extern const std::string& mp_last_decode_fail_reason();
extern const std::string& mp_last_decode_info();

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>
#include <iomanip>
#include <iterator>
#include <climits>
#include <limits>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

std::atomic<bool>   g_pending_terrain_load{false};
std::atomic<bool>   g_level_export_only_load{false};
Level::TerrainMesh  g_pending_terrain_mesh;
std::string         g_pending_terrain_label;
FlatAssetEntry      g_pending_terrain_level_entry;
std::vector<uint8_t> g_pending_terrain_ehf_bytes;
std::vector<Level::PendingAdjacentTerrain> g_pending_adjacent_terrain_meshes;

std::vector<uint8_t>  g_pending_terrain_ghf_payload;
std::vector<float>    g_pending_terrain_ghf_heights;
float                 g_pending_terrain_ghf_tile_size = 1.f;
int                   g_pending_terrain_ghf_width = 0;
int                   g_pending_terrain_ghf_height = 0;
FlatAssetEntry        g_pending_terrain_ghf_entry;
std::vector<Level::PropBlock> g_pending_level_prop_blocks;
std::string                   g_pending_level_model_body_bnk;
Level::WaterScene             g_pending_level_water_scene;
bool                          g_pending_level_water_present = false;
Gdb::WaterTheme               g_pending_level_water_theme;
Gdb::SkyTheme                 g_pending_level_sky_theme;
Gdb::CloudTheme               g_pending_level_cloud_theme;
Gdb::WeatherTheme             g_pending_level_weather_theme;
Gdb::EnvironmentThemeTimeline g_pending_level_environment_timeline;
std::vector<Fx::Placement>    g_pending_level_fx;
Fx::Bank                      g_particle_bank;
bool                          g_particle_bank_loaded = false;
std::vector<std::string>           g_level_vfs_texture_body_bnks;
std::vector<std::string>           g_level_vfs_model_bnks;
std::vector<std::string>           g_level_vfs_streaming_bnks;
std::vector<HavokCollisionMesh>    g_level_havok_collision;
std::vector<GdbWorldPlacement>     g_level_gdb_placements;
std::unordered_map<uint32_t, Gdb::EntityContents> g_level_entity_contents;
std::unordered_map<uint32_t, Gdb::EntityGameplayDetails>
    g_level_entity_gameplay;
std::unordered_map<uint32_t, Gdb::PropertyDetails>
    g_level_property_details;
std::vector<Gdb::ItemCatalogEntry> g_level_item_catalog;
std::vector<Gdb::ItemDetail> g_item_details;
std::unordered_map<uint32_t, Gdb::PropTemplateInfo>
    g_level_prop_entity_templates;
std::unordered_map<uint32_t, Gdb::EntityTextTags> g_level_entity_text;
std::vector<LevelSpawnMarker> g_level_spawn_markers;
Gdb::SpawnDonorInfo g_level_spawn_donor;
Gdb::NpcDonorInfo g_level_npc_donor;
std::vector<Gdb::CreatureCatalogEntry> g_level_creature_catalog;
std::vector<Gdb::CreatureCatalogEntry> g_global_entity_catalog;
std::unordered_map<uint32_t, std::vector<uint32_t>>
    g_global_entity_model_hashes;
uint64_t g_global_entity_catalog_revision = 0;

#ifndef _WIN32
static Level::GhfHeights make_linux_preview_heightfield(
    const Level::GhfHeights& source, size_t max_vertices)
{
    const size_t source_vertices =
        size_t(source.width) * size_t(source.height);
    if (source_vertices <= max_vertices || max_vertices < 4)
        return source;

    const double ratio =
        std::sqrt(double(source_vertices) / double(max_vertices));
    const uint32_t step =
        std::max<uint32_t>(2, uint32_t(std::ceil(ratio)));
    const uint32_t preview_w = (source.width - 1 + step - 1) / step + 1;
    const uint32_t preview_h = (source.height - 1 + step - 1) / step + 1;

    Level::GhfHeights preview;
    preview.ok = true;
    preview.width = preview_w;
    preview.height = preview_h;
    preview.tile_size = source.tile_size * float(step);
    preview.min_height = std::numeric_limits<float>::infinity();
    preview.max_height = -std::numeric_limits<float>::infinity();
    preview.heights.resize(size_t(preview_w) * size_t(preview_h));

    for (uint32_t y = 0; y < preview_h; ++y) {
        const uint32_t sy = std::min(y * step, source.height - 1);
        for (uint32_t x = 0; x < preview_w; ++x) {
            const uint32_t sx = std::min(x * step, source.width - 1);
            const float height =
                source.heights[size_t(sy) * source.width + sx];
            preview.heights[size_t(y) * preview_w + x] = height;
            preview.min_height = std::min(preview.min_height, height);
            preview.max_height = std::max(preview.max_height, height);
        }
    }

    OutputLog::info(
        "Linux low-memory terrain preview: " +
        std::to_string(source.width) + "x" + std::to_string(source.height) +
        " -> " + std::to_string(preview.width) + "x" +
        std::to_string(preview.height) + " (sample step " +
        std::to_string(step) + ")");
    return preview;
}
#endif

const FlatAssetEntry* FindGlobalModelAssetByPathHash(uint32_t path_hash)
{
    if (path_hash == 0 || S.all_mdl_files.empty()) return nullptr;

    static std::string cached_root;
    static const FlatAssetEntry* cached_data = nullptr;
    static size_t cached_size = size_t(-1);
    static std::unordered_map<uint32_t, size_t> model_index;
    const FlatAssetEntry* current_data = S.all_mdl_files.data();
    if (cached_root != S.root_dir || cached_data != current_data ||
        cached_size != S.all_mdl_files.size()) {
        model_index.clear();
        model_index.reserve(S.all_mdl_files.size() * 2 + 1);
        for (size_t i = 0; i < S.all_mdl_files.size(); ++i) {
            uint32_t hash = 0x811C9DC5u;
            for (unsigned char c : S.all_mdl_files[i].full_path) {
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<unsigned char>(c - 'A' + 'a');
                }
                if (c == '/') c = '\\';
                hash *= 0x01000193u;
                hash ^= uint32_t(c);
            }
            model_index.try_emplace(hash, i);
        }
        cached_root = S.root_dir;
        cached_data = current_data;
        cached_size = S.all_mdl_files.size();
    }

    const auto found = model_index.find(path_hash);
    if (found == model_index.end() ||
        found->second >= S.all_mdl_files.size()) {
        return nullptr;
    }
    return &S.all_mdl_files[found->second];
}
std::unordered_map<uint32_t, Gdb::EntityGameplayDetails>
    g_global_entity_gameplay;
Gdb::EntityGameplayOptions g_global_entity_gameplay_options;
static std::atomic<bool>      g_level_async_loading{false};

namespace Level {

bool IsAsyncLoadInProgress()
{
    return g_level_async_loading.load() ||
           g_pending_terrain_load.load() ||
           S.show_progress.load();
}

void OpenAsync(const FlatAssetEntry& entry)
{
    bool expected = false;
    if (!g_level_async_loading.compare_exchange_strong(expected, true)) {
        OutputLog::warn("level load already in progress");
        return;
    }

    S.cancel_requested.store(false);

    progress_open(100, "Loading level...");
    std::thread([entry]() {
        progress_update(5, 100, "Extracting level...");
        const bool ok = Open(entry);
        const bool cancelled = S.cancel_requested.load();
        if (cancelled) {
            g_pending_terrain_load.store(false);
            g_pending_level_prop_blocks.clear();
            g_pending_adjacent_terrain_meshes.clear();
            g_pending_terrain_mesh = Level::TerrainMesh{};
            g_pending_terrain_ehf_bytes.clear();
            g_pending_terrain_ghf_heights.clear();
            g_pending_terrain_ghf_payload.clear();
            g_pending_terrain_ghf_width = 0;
            g_pending_terrain_ghf_height = 0;
            g_pending_terrain_ghf_tile_size = 1.0f;
            g_pending_terrain_ghf_entry = FlatAssetEntry{};
            g_pending_terrain_level_entry = FlatAssetEntry{};
            g_pending_terrain_label.clear();
            g_pending_level_model_body_bnk.clear();
            g_pending_level_water_present = false;
            g_pending_level_water_scene = Level::WaterScene{};
            g_pending_level_water_theme = Gdb::WaterTheme{};
            g_pending_level_sky_theme = Gdb::SkyTheme{};
            g_pending_level_cloud_theme = Gdb::CloudTheme{};
            g_pending_level_weather_theme = Gdb::WeatherTheme{};
            g_pending_level_environment_timeline =
                Gdb::EnvironmentThemeTimeline{};
            g_level_havok_collision.clear();
            g_level_gdb_placements.clear();
            g_level_entity_contents.clear();
            g_level_entity_gameplay.clear();
            g_level_property_details.clear();
            g_level_entity_text.clear();
            g_level_spawn_markers.clear();
            g_level_creature_catalog.clear();
            g_level_vfs_texture_body_bnks.clear();
            g_level_vfs_model_bnks.clear();
            g_level_vfs_streaming_bnks.clear();
            progress_done();
            OutputLog::warn("Level load cancelled.");
            S.cancel_requested.store(false);
        } else if (!ok) {
            progress_done();
        } else {
            progress_update(70, 100, "Preparing render...");
            if (!g_pending_terrain_load.load()) {
                progress_done();
            }
        }
        g_level_async_loading.store(false);
    }).detach();
}


namespace {


#include "Level/Loading/Stages/LevelPropSupport.inl"

}


static void loader_progress_update(int current,
                                   int total,
                                   const std::string& text)
{
    if (!g_level_export_only_load.load()) {
        progress_update(current, total, text);
    }
}

bool Open(const FlatAssetEntry& entry)
{
    bridge_debug_write(
        "Fable 2 Asset Browser - bridge placement trace\nlevel_name=" +
            entry.name + "\nlevel_path=" + entry.full_path +
            "\nlevel_bnk=" + entry.bnk_path,
        true);
    OutputLog::info("bridge debug log: " +
                    bridge_debug_log_path().string());
    if (!g_level_export_only_load.load()) {
        OutputLog::info("loading level '" + entry.name + "' ...");
    }
    loader_progress_update(8, 100, "Extracting " + entry.name);

    auto bail_if_cancelled = [&](const char* where) -> bool {
        if (!S.cancel_requested.load()) return false;
        OutputLog::warn(std::string("level load cancelled at ") + where);
        return true;
    };

    g_pending_level_prop_blocks.clear();
    g_pending_adjacent_terrain_meshes.clear();
    g_pending_level_water_theme = Gdb::WaterTheme{};
    g_pending_level_sky_theme = Gdb::SkyTheme{};
    g_pending_level_cloud_theme = Gdb::CloudTheme{};
    g_pending_level_weather_theme = Gdb::WeatherTheme{};
    g_pending_level_environment_timeline =
        Gdb::EnvironmentThemeTimeline{};

    if (bail_if_cancelled("entry")) return false;

    std::vector<uint8_t> bytes;
    try {
        bytes = BnkCache::extract_bytes(entry.bnk_path, entry.file_index);
    } catch (const std::exception& ex) {
        OutputLog::error("level extract failed: " + std::string(ex.what()));
        return false;
    } catch (...) {
        OutputLog::error("level extract failed (unknown exception)");
        return false;
    }
    if (bytes.empty()) {
        OutputLog::error("level extract produced 0 bytes");
        return false;
    }
    if (bail_if_cancelled("after-extract")) return false;

    loader_progress_update(18, 100, "Parsing level entries...");
    EngineLevelInfo info;
    if (!ParseEngineLevel(bytes, info)) {
        OutputLog::error("parse failed for '" + entry.name + "': "
                         + info.error);
        return false;
    }
    bridge_debug_dump_blocks("RAW ENGINE_LEVEL BLOCKS", info.prop_blocks);
    if (bail_if_cancelled("after-parse")) return false;

    info.source_path = entry.full_path;
    LevelEdit::OnLevelLoaded(entry);
    int n_t2 = 0, n_t4 = 0, n_t5 = 0, n_t21 = 0, n_t32 = 0, n_other = 0;
    for (const auto& e : info.entries) {
        switch (e.type) {
            case 2:  ++n_t2;  break;
            case 4:  ++n_t4;  break;
            case 5:  ++n_t5;  break;
            case 21: ++n_t21; break;
            case 32: ++n_t32; break;
            default: ++n_other; break;
        }
    }

    std::ostringstream os;
    os << "level OK  ver=" << info.version
       << "  entries=" << info.entries.size()
       << "/" << info.entry_count
       << "  (t2=" << n_t2
       << " t4=" << n_t4
       << " t5=" << n_t5
       << " t21=" << n_t21
       << " t32=" << n_t32
       << " other=" << n_other << ")";
    OutputLog::success(os.str());

    size_t t2_prop_blocks = 0, t2_prop_instances = 0;
    size_t t21_prop_blocks = 0, t21_prop_instances = 0;
    size_t other_prop_blocks = 0, other_prop_instances = 0;
    for (const auto& b : info.prop_blocks) {
        if (b.type == 2) {
            ++t2_prop_blocks;
            t2_prop_instances += b.instances.size();
        } else if (b.type == 21) {
            ++t21_prop_blocks;
            t21_prop_instances += b.instances.size();
        } else {
            ++other_prop_blocks;
            other_prop_instances += b.instances.size();
        }
    }
    std::ostringstream ps;
    ps << "level prop placements: "
       << "t2=" << t2_prop_blocks << " blocks / " << t2_prop_instances << " instances, "
       << "t21=" << t21_prop_blocks << " blocks / " << t21_prop_instances << " instances";
    if (other_prop_blocks > 0) {
        ps << ", other=" << other_prop_blocks << " blocks / "
           << other_prop_instances << " instances";
    }
    OutputLog::info(ps.str());

    std::unordered_set<std::string> authored_level_model_paths;
    authored_level_model_paths.reserve(info.prop_blocks.size() * 2);
    for (const auto& block : info.prop_blocks) {
        if (!block.model_path.empty()) {
            authored_level_model_paths.insert(lower_slash(block.model_path));
        }
        if (!block.lod_model_path.empty()) {
            authored_level_model_paths.insert(
                lower_slash(block.lod_model_path));
        }
    }

    {
        const std::vector<std::string> wanted = {
            "bridge", "lamp", "lantern", "fence", "bench", "post",
            "archway", "gate", "stall", "shop", "wall"
        };
        std::map<std::string, std::pair<size_t, size_t>> match_counts;
        std::map<std::string, std::vector<std::string>> match_paths;
        for (const auto& pb : info.prop_blocks) {
            std::string p = pb.model_path;
            std::transform(p.begin(), p.end(), p.begin(), ::tolower);
            for (const auto& kw : wanted) {
                if (p.find(kw) != std::string::npos) {
                    match_counts[kw].first += 1;
                    match_counts[kw].second += pb.instances.size();
                    if (match_paths[kw].size() < 3) {
                        match_paths[kw].push_back(pb.model_path);
                    }
                    break;
                }
            }
        }
        OutputLog::info("engine_level keyword scan (bridge/lamp/fence/...):");
        for (const auto& kw : wanted) {
            auto it = match_counts.find(kw);
            if (it == match_counts.end()) {
                OutputLog::warn("  " + kw + ":  NONE in engine_level");
            } else {
                std::ostringstream os;
                os << "  " << kw << ":  " << it->second.first
                   << " blocks / " << it->second.second << " instances";
                OutputLog::success(os.str());
                for (const auto& path : match_paths[kw]) {
                    OutputLog::info("    " + path);
                }
            }
        }
    }

    auto ends_with_ci = [](const std::string& s, const char* suffix) {
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

    int n_heightfield_refs = 0;
    int n_logged           = 0;
    const int kMaxLog      = 16;

    std::vector<std::string> all_ehf_refs;
    std::vector<std::string> all_water_refs;

    for (const auto& e : info.entries) {
        if (e.str_a.empty()) continue;

        const bool is_heightfield_like =
            ends_with_ci(e.str_a, ".ehf") ||
            ends_with_ci(e.str_a, ".ghf") ||
            ends_with_ci(e.str_a, ".hdb") ||
            ends_with_ci(e.str_a, ".genv") ||
            ends_with_ci(e.str_a, ".ama")  ||
            ends_with_ci(e.str_a, ".amm")  ||
            ends_with_ci(e.str_a, ".amr")  ||
            ends_with_ci(e.str_a, ".water") ||
            (e.str_a.find("heightfield") != std::string::npos) ||
            (e.str_a.find("Heightfield") != std::string::npos);

        if (is_heightfield_like) {
            ++n_heightfield_refs;
            OutputLog::info("  heightfield ref: t" + std::to_string(e.type)
                            + "  " + e.str_a);
            if (ends_with_ci(e.str_a, ".ehf")) {
                all_ehf_refs.push_back(e.str_a);
            } else if (ends_with_ci(e.str_a, ".water")) {
                all_water_refs.push_back(e.str_a);
            }
        } else if (n_logged < kMaxLog) {
            ++n_logged;
            OutputLog::info("  ref: t" + std::to_string(e.type)
                            + "  " + e.str_a
                            + (e.str_b.empty() ? std::string()
                                               : "  | " + e.str_b));
        }
    }

    if (n_heightfield_refs == 0) {
        OutputLog::warn("level references no .ehf/.ghf/heightfield* strings - "
                        "checking sibling .list file for the heightfield "
                        "names instead.");
    }

    auto sibling_with_ext = [&](const std::string& new_ext) {
        std::filesystem::path p = entry.full_path;
        p.replace_extension(new_ext);
        return p.string();
    };

    auto load_text_sibling = [&](const std::string& sibling_full_path,
                                 std::vector<uint8_t>& out_bytes,
                                 std::string* out_src_bnk = nullptr,
                                 int* out_src_idx = nullptr,
                                 std::string* out_src_file = nullptr) -> bool
    {
        out_bytes.clear();
        if (out_src_bnk) out_src_bnk->clear();
        if (out_src_idx) *out_src_idx = -1;
        if (out_src_file) out_src_file->clear();
        auto normalize_asset_key = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            std::replace(s.begin(), s.end(), '\\', '/');
            return s;
        };
        auto filename_of_key = [](const std::string& s) {
            const size_t p = s.find_last_of("/\\");
            return (p == std::string::npos) ? s : s.substr(p + 1);
        };
        auto try_extract = [&](const std::string& bnk_path,
                               int idx) -> bool
        {
            if (bnk_path.empty() || idx < 0) return false;
            try {
                auto v = BnkCache::extract_bytes(bnk_path, idx);
                if (v.empty()) return false;
                out_bytes.assign(v.begin(), v.end());
                if (out_src_bnk) *out_src_bnk = bnk_path;
                if (out_src_idx) *out_src_idx = idx;
                return true;
            } catch (...) {
                return false;
            }
        };
        auto try_bnk_path = [&](const std::string& bnk_path,
                                const std::string& key,
                                const std::string& leaf) -> bool
        {
            int idx = BnkCache::find_index(bnk_path, key);
            if (idx < 0 && !leaf.empty()) {
                idx = BnkCache::find_index(bnk_path, leaf);
            }
            return try_extract(bnk_path, idx);
        };
        auto try_file = [&](const std::filesystem::path& p) -> bool
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(p, ec)) return false;
            std::ifstream f(p, std::ios::binary);
            if (!f) return false;
            f.seekg(0, std::ios::end);
            const std::streamoff size = f.tellg();
            if (size <= 0) return false;
            f.seekg(0, std::ios::beg);
            out_bytes.resize(static_cast<size_t>(size));
            f.read(reinterpret_cast<char*>(out_bytes.data()), size);
            if (!f) {
                out_bytes.clear();
                return false;
            }
            if (out_src_file) *out_src_file = p.string();
            return true;
        };

        const std::string key = normalize_asset_key(sibling_full_path);
        const std::string leaf = filename_of_key(key);

        if (try_bnk_path(entry.bnk_path, key, leaf)) {
            return true;
        }

        for (const auto& fe : S.all_heightfield_files) {
            const std::string fe_full =
                normalize_asset_key(fe.full_path.empty()
                    ? fe.name : fe.full_path);
            const std::string fe_name = normalize_asset_key(fe.name);
            const bool match =
                fe_full == key ||
                fe_name == key ||
                (!leaf.empty() &&
                 (filename_of_key(fe_full) == leaf ||
                  filename_of_key(fe_name) == leaf));
            if (!match) continue;
            if (try_extract(fe.bnk_path, fe.file_index)) {
                return true;
            }
        }

        for (const auto& bnk_path : S.bnk_paths) {
            if (bnk_path == entry.bnk_path) continue;
            if (try_bnk_path(bnk_path, key, leaf)) {
                return true;
            }
        }

        if (key.compare(0, 5, "data/") == 0) {
            auto try_iso_file = [&](const std::string& virtual_path) -> bool {
                if (!ISO::IsoMount::instance().is_mounted()) return false;
                std::string vp = virtual_path;
                std::replace(vp.begin(), vp.end(), '\\', '/');
                auto bytes = ISO::IsoMount::instance().read_file(vp);
                if (bytes.empty()) return false;
                out_bytes = std::move(bytes);
                return true;
            };
            if (try_iso_file(key)) {
                return true;
            }

            std::vector<std::filesystem::path> game_roots;
            auto add_game_root = [&](const std::filesystem::path& root) {
                if (root.empty()) return;
                if (ISO::IsoMount::is_iso_path(root.string())) return;
                std::error_code ec;
                const auto abs = std::filesystem::absolute(root, ec);
                const auto candidate = ec ? root : abs;
                for (const auto& existing : game_roots) {
                    if (existing == candidate) return;
                }
                game_roots.push_back(candidate);
            };
            auto add_root_from_path = [&](const std::string& p) {
                if (p.empty()) return;
                if (ISO::IsoMount::is_iso_path(p)) return;
                std::string norm = p;
                std::replace(norm.begin(), norm.end(), '\\', '/');
                std::string low = norm;
                std::transform(low.begin(), low.end(), low.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                const size_t data_pos = low.find("/data/");
                if (data_pos != std::string::npos) {
                    add_game_root(norm.substr(0, data_pos));
                }
            };
            add_game_root(S.root_dir);
            add_root_from_path(entry.bnk_path);
            for (const auto& bnk_path : S.bnk_paths) {
                add_root_from_path(bnk_path);
            }
            for (const auto& bnk_path : S.nested_bnk_paths) {
                add_root_from_path(bnk_path);
            }

            const std::string without_data = key.substr(5);
            for (const auto& root : game_roots) {
                if (try_file(root / std::filesystem::path(key))) {
                    return true;
                }
                if (try_file(root / "data" /
                             std::filesystem::path(without_data))) {
                    return true;
                }
                if (lower_slash(root.string()).size() >= 4 &&
                    lower_slash(root.string()).compare(
                        lower_slash(root.string()).size() - 4, 4,
                        "data") == 0 &&
                    try_file(root / std::filesystem::path(without_data))) {
                    return true;
                }
            }
        }

        if (!leaf.empty()) {
            std::error_code ec;
            const auto cwd = std::filesystem::current_path(ec);
            if (!ec) {
                if (try_file(cwd / "extracted" / leaf)) return true;
                if (try_file(cwd / "cmake-build-debug" / "extracted" / leaf))
                    return true;
                if (try_file(cwd.parent_path() / "cmake-build-debug" /
                             "extracted" / leaf))
                    return true;
            }
        }

        return false;
    };

    LevelResources res;
    {
        std::vector<uint8_t> list_bytes;
        const std::string list_path = sibling_with_ext(".list");
        if (load_text_sibling(list_path, list_bytes)) {
            std::string list_str(reinterpret_cast<const char*>(list_bytes.data()),
                                 list_bytes.size());
            std::ostringstream ls; ls << "list (" << list_bytes.size() << " bytes):";
            OutputLog::info(ls.str());

            size_t pos = 0;
            while (pos < list_str.size()) {
                size_t eol = list_str.find_first_of("\r\n", pos);
                std::string line = (eol == std::string::npos)
                                       ? list_str.substr(pos)
                                       : list_str.substr(pos, eol - pos);
                pos = (eol == std::string::npos)
                          ? list_str.size()
                          : list_str.find_first_not_of("\r\n", eol);
                if (pos == std::string::npos) pos = list_str.size();
                if (line.empty()) continue;

                std::string low = line;
                std::transform(low.begin(), low.end(), low.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                auto matches = [&](const char* ext) {
                    size_t n = std::strlen(ext);
                    return low.size() >= n &&
                           low.compare(low.size() - n, n, ext) == 0;
                };
                if      (matches(".ehf"))  res.ehf_path  = line;
                else if (matches(".ghf"))  res.ghf_path  = line;
                else if (matches(".hdb"))  res.hdb_path  = line;
                else if (matches(".genv")) res.genv_path = line;
                else if (matches(".ama"))  res.ama_path  = line;
                else if (matches(".amm"))  res.amm_path  = line;
                else if (matches(".amr"))  res.amr_path  = line;
                else if (matches("_models.bnk")) res.model_body_bnk = line;

                OutputLog::info("  " + line);
            }
        } else {
            OutputLog::warn("no companion .list (" + list_path + ") in BNK");
        }
    }

    auto basename_no_ext = [](const std::string& p) -> std::string {
        size_t slash = p.find_last_of("/\\");
        std::string s = (slash == std::string::npos)
            ? p
            : p.substr(slash + 1);
        auto dot = s.find_last_of('.');
        if (dot != std::string::npos) s.resize(dot);
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return s;
    };
    if (res.ehf_path.empty() && !all_ehf_refs.empty()) {
        const std::string ghf_base = basename_no_ext(res.ghf_path);
        for (const auto& candidate : all_ehf_refs) {
            if (!ghf_base.empty() &&
                basename_no_ext(candidate) == ghf_base) {
                res.ehf_path = candidate;
                break;
            }
        }
        if (res.ehf_path.empty()) res.ehf_path = all_ehf_refs.front();
    }

    auto report_slot = [](const char* label, const std::string& v) {
        if (v.empty()) {
            OutputLog::warn(std::string("  ") + label + ": (missing)");
        } else {
            OutputLog::success(std::string("  ") + label + ": " + v);
        }
    };
    OutputLog::info("heightfield resources for this level:");
    report_slot(".ehf  (graphics desc)", res.ehf_path);
    report_slot(".ghf  (raw heightmap)", res.ghf_path);
    report_slot(".hdb  (height database)", res.hdb_path);
    report_slot(".genv (env table)",     res.genv_path);
    report_slot(".ama  (ambient)",       res.ama_path);
    report_slot(".amm  (ambient meta)",  res.amm_path);
    report_slot(".amr  (ambient refs)",  res.amr_path);
    report_slot("models",                res.model_body_bnk);

    {
        struct SiblingSlot { const char* label; const std::string& path; };
        const SiblingSlot slots[] = {
            { ".hdb  (height database)", res.hdb_path  },
            { ".genv (env table)",       res.genv_path },
            { ".ama  (ambient)",         res.ama_path  },
            { ".amm  (ambient meta)",    res.amm_path  },
            { ".amr  (ambient refs)",    res.amr_path  },
        };
        OutputLog::info("loading .list terrain siblings:");
        for (const auto& s : slots) {
            if (s.path.empty()) continue;
            std::vector<uint8_t> bytes;
            if (load_text_sibling(s.path, bytes)) {
                std::ostringstream os;
                os << "  " << s.label << " loaded (" << bytes.size() << " bytes)";
                OutputLog::success(os.str());
            } else {
                OutputLog::warn(std::string("  ") + s.label + " load FAILED: " + s.path);
            }
        }
    }

    g_level_havok_collision.clear();
    OutputLog::info("havok_scenario loading disabled");
    if (bail_if_cancelled("after terrain siblings")) return false;

    g_level_vfs_texture_body_bnks.clear();
    g_level_vfs_model_bnks.clear();
    g_level_vfs_streaming_bnks.clear();
    {
        std::vector<uint8_t> vfs_bytes;
        std::filesystem::path vfs_path = entry.full_path;
        vfs_path.replace_filename("level.vfsconfig");
        if (load_text_sibling(vfs_path.string(), vfs_bytes)) {
            auto vfs = Level::ParseVfsConfig(vfs_bytes);
            g_level_vfs_texture_body_bnks = std::move(vfs.texture_body_bnks);
            g_level_vfs_model_bnks        = std::move(vfs.model_bnks);
            g_level_vfs_streaming_bnks    = std::move(vfs.streaming_bnks);
            std::ostringstream os;
            os << "vfsconfig: "
               << g_level_vfs_texture_body_bnks.size() << " texture body BNKs, "
               << g_level_vfs_model_bnks.size() << " model BNKs, "
               << g_level_vfs_streaming_bnks.size() << " streaming BNKs";
            OutputLog::info(os.str());
            for (const auto& p : g_level_vfs_texture_body_bnks) {
                OutputLog::info("  tex-body: " + p);
            }
            for (const auto& p : g_level_vfs_model_bnks) {
                OutputLog::info("  model:    " + p);
            }
            for (const auto& p : g_level_vfs_streaming_bnks) {
                OutputLog::info("  stream:   " + p);
            }
        } else {
            OutputLog::warn("no level.vfsconfig sibling in BNK");
        }
    }
    if (bail_if_cancelled("after vfsconfig")) return false;
    const std::vector<StreamingModelCandidate> streaming_model_candidates =
        collect_streaming_model_candidates(g_level_vfs_streaming_bnks);
    if (!streaming_model_candidates.empty()) {
        size_t indexed = 0;
        for (const auto& c : streaming_model_candidates) {
            if (c.entry) ++indexed;
        }
        OutputLog::info("streaming model candidates: " +
                        std::to_string(streaming_model_candidates.size()) +
                        " streaming hint path(s), " + std::to_string(indexed) +
                        " resolved through global .mdl index");
    }
    loader_progress_update(22, 100, "Reading level GDB + save...");
    g_level_gdb_placements.clear();
    g_level_entity_contents.clear();
    g_level_entity_gameplay.clear();
    g_level_property_details.clear();
    g_level_entity_text.clear();
    g_level_spawn_markers.clear();
    g_level_creature_catalog.clear();
    {
#include "Level/Loading/Stages/LoadEntities.inl"
    loader_progress_update(26, 100, "Scanning level effects...");
#include "Level/Loading/Stages/LoadEffects.inl"
    loader_progress_update(28, 100, "Matching prop models...");
#include "Level/Loading/Stages/LoadProps.inl"
    }

    
    
    
    if (Creation::IsCustomLooseLevel(entry)) {
        const size_t cleared = g_level_gdb_placements.size();
        const size_t cleared_fx = g_pending_level_fx.size();
        g_level_gdb_placements.clear();
        g_pending_level_fx.clear();
        g_level_entity_contents.clear();
        g_level_entity_gameplay.clear();
        g_level_property_details.clear();
        if (cleared || cleared_fx) {
            OutputLog::info(
                "custom level: cleared " + std::to_string(cleared) +
                " donor entity placement(s) and " +
                std::to_string(cleared_fx) + " fx spawn(s)");
        }
    }

    for (const auto& vfs_stream_path : g_level_vfs_streaming_bnks) {
        std::string wanted_leaf =
            std::filesystem::path(vfs_stream_path).filename().string();
        std::transform(wanted_leaf.begin(), wanted_leaf.end(),
                       wanted_leaf.begin(), ::tolower);

        auto leaf_matches = [&](const std::string& mounted_leaf_lower) {
            if (mounted_leaf_lower == wanted_leaf) return true;
            if (mounted_leaf_lower.size() <= wanted_leaf.size() + 1) return false;
            const size_t off = mounted_leaf_lower.size() - wanted_leaf.size();
            if (mounted_leaf_lower.compare(off, wanted_leaf.size(),
                                           wanted_leaf) != 0) return false;
            return mounted_leaf_lower[off - 1] == '_';
        };

        std::string mounted_path;
        if (auto resolved = find_bnk_by_virtual_path(vfs_stream_path)) {
            mounted_path = *resolved;
        }
        if (mounted_path.empty()) {
            for (const auto& p : S.bnk_paths) {
                std::string leaf =
                    std::filesystem::path(p).filename().string();
                std::transform(leaf.begin(), leaf.end(), leaf.begin(), ::tolower);
                if (leaf_matches(leaf)) { mounted_path = p; break; }
            }
        }
        if (mounted_path.empty()) {
            for (const auto& p : S.nested_bnk_paths) {
                std::string leaf =
                    std::filesystem::path(p).filename().string();
                std::transform(leaf.begin(), leaf.end(),
                               leaf.begin(), ::tolower);
                if (leaf_matches(leaf)) { mounted_path = p; break; }
            }
        }
        if (mounted_path.empty()) {
            OutputLog::warn("streaming bnk not mounted: " + vfs_stream_path);
            continue;
        }

        try {
            const BnkCache::Entry bnk = BnkCache::get(mounted_path);
            const auto& files = bnk.reader->list_files();
            size_t hkx_count = 0;
            size_t total_rb  = 0;
            size_t total_inst = 0;

            for (size_t i = 0; i < files.size(); ++i) {
                const auto& name = files[i].name;
                std::string lower = name;
                std::transform(lower.begin(), lower.end(),
                               lower.begin(), ::tolower);

                if (lower.size() < 4 ||
                    lower.compare(lower.size() - 4, 4, ".hkx") != 0) continue;
                ++hkx_count;
                std::vector<uint8_t> hkx_bytes;
                try {
                    hkx_bytes = bnk.reader->extract_index_bytes((int)i);
                } catch (...) { continue; }
                auto pf = Havok::LoadPackFileFromBytes(
                    std::move(hkx_bytes), name);
                if (!pf) continue;
                total_inst += pf->virtual_fixups.size();
                const auto* rb_class = pf->find_class("hkpRigidBody");
                size_t this_rb = 0;
                if (rb_class) {
                    for (const auto& vf : pf->virtual_fixups) {
                        if (vf.classnames_offset ==
                            rb_class->classnames_offset) {
                            ++total_rb;
                            ++this_rb;
                        }
                    }
                }

            }

            std::ostringstream os;
            os << "streaming bnk '"
               << std::filesystem::path(mounted_path).filename().string()
               << "':  " << files.size() << " files, " << hkx_count
               << " .hkx,  " << total_rb << " rigid bodies across "
               << total_inst << " havok instances";
            OutputLog::success(os.str());

        } catch (const std::exception& ex) {
            OutputLog::warn(std::string("streaming bnk scan failed: ") + ex.what());
        }
    }

    if (bail_if_cancelled("pre-heightfield")) return false;

    if (!res.ehf_path.empty() || !res.ghf_path.empty()) {
        HeightfieldFiles hf;
        loader_progress_update(32, 100, "Loading heightfield files...");
        if (!LoadHeightfieldFiles(res.ehf_path, res.ghf_path,
                                  res.hdb_path, res.genv_path, hf)) {
            OutputLog::error("heightfield load failed: " + hf.error);
        } else if (S.cancel_requested.load()) {
            OutputLog::warn("level load cancelled during heightfield load");
            return false;
        } else {
            std::ostringstream hos;
            hos << "heightfield loaded:"
                << "  ehf=" << hf.ehf_bytes.size() << "B"
                << "  ghf=" << hf.ghf_bytes_compressed.size() << "B (gz)"
                << " -> " << hf.ghf_bytes_raw.size() << "B (raw)";
            OutputLog::success(hos.str());

            if (!hf.ghf_bytes_raw.empty()) {
                GhfHeights hg;
                loader_progress_update(45, 100, "Decoding height grid...");
                if (!DecodeGhfHeights(hf.ghf_bytes_raw, hg)) {
                    OutputLog::error("  .ghf decode failed: " + hg.error);
                } else {
                    if (hg.tile_size <= 0.0f) {
                        const float ehf_tile = hf.ehf_header.ok
                                             ? hf.ehf_header.f2 : 0.0f;
                        const float fallback =
                            (ehf_tile > 0.0f && std::isfinite(ehf_tile))
                                ? ehf_tile : 0.5f;
                        std::ostringstream tos;
                        tos << "  .ghf tile_size was 0 - using .ehf f2 = "
                            << fallback << " (world = "
                            << (hg.width  - 1) * fallback << " x "
                            << (hg.height - 1) * fallback << ")";
                        OutputLog::info(tos.str());
                        hg.tile_size = fallback;
                    }

                    std::ostringstream gos;
                    gos << "  .ghf heightmap: " << hg.width << "x" << hg.height
                        << "  tile=" << hg.tile_size
                        << "  h=[" << hg.min_height << ".." << hg.max_height << "]";
                    OutputLog::success(gos.str());

                    TerrainMesh mesh;
                    loader_progress_update(58, 100, "Building terrain mesh...");
                    if (S.cancel_requested.load()) {
                        OutputLog::warn("level load cancelled before terrain mesh build");
                        return false;
                    }
#ifdef _WIN32
                    const bool terrain_built = BuildTerrainMesh(hg, mesh);
#else
                    constexpr size_t kLinuxTerrainPreviewVertices = 262144;
                    const size_t full_vertex_count =
                        size_t(hg.width) * size_t(hg.height);
                    Level::GhfHeights preview_hg;
                    const Level::GhfHeights* render_hg = &hg;
                    if (full_vertex_count > kLinuxTerrainPreviewVertices) {
                        preview_hg = make_linux_preview_heightfield(
                            hg, kLinuxTerrainPreviewVertices);
                        render_hg = &preview_hg;
                    }
                    const bool terrain_built =
                        BuildTerrainMesh(*render_hg, mesh);
#endif
                    if (!terrain_built) {
                        OutputLog::error("  terrain mesh build failed");
                    } else {
                        const size_t tri_count = mesh.indices.size() / 3;
                        std::ostringstream mos;
                        mos << "  terrain mesh: verts=" << (mesh.positions.size() / 3)
                            << "  tris=" << tri_count;
                        OutputLog::success(mos.str());

                        g_pending_terrain_mesh        = std::move(mesh);
                        g_pending_terrain_label       = entry.name;
                        g_pending_terrain_level_entry = entry;
                        g_pending_terrain_ehf_bytes   = hf.ehf_bytes;
                        g_pending_adjacent_terrain_meshes.clear();

                        auto norm_path = [](std::string s) {
                            std::replace(s.begin(), s.end(), '\\', '/');
                            std::transform(s.begin(), s.end(), s.begin(),
                                [](unsigned char c) { return (char)std::tolower(c); });
                            return s;
                        };
                        auto with_ext = [](std::string p, const char* ext) {
                            const size_t slash = p.find_last_of("/\\");
                            const size_t dot = p.find_last_of('.');
                            if (dot != std::string::npos &&
                                (slash == std::string::npos || dot > slash)) {
                                p.resize(dot);
                            }
                            p += ext;
                            return p;
                        };
                        auto path_leaf = [&](const std::string& p) {
                            const std::string n = norm_path(p);
                            const size_t slash = n.find_last_of('/');
                            return (slash == std::string::npos)
                                ? n
                                : n.substr(slash + 1);
                        };
                        auto has_ext = [&](const std::string& p,
                                           const char* ext) {
                            const std::string n = norm_path(p);
                            const size_t len = std::strlen(ext);
                            return n.size() >= len &&
                                n.compare(n.size() - len, len, ext) == 0;
                        };
                        auto heightfield_id = [&](const std::string& p) {
                            const std::string n = norm_path(p);
                            const size_t id_pos = n.rfind("_id_");
                            if (id_pos == std::string::npos) {
                                return std::string{};
                            }
                            size_t first = id_pos + 4;
                            size_t last = first;
                            while (last < n.size()) {
                                const unsigned char c =
                                    static_cast<unsigned char>(n[last]);
                                if (!std::isxdigit(c)) break;
                                ++last;
                            }
                            return last > first ? n.substr(first, last - first)
                                                : std::string{};
                        };
                        auto path_overlap_score =
                            [&](const std::string& a,
                                const std::string& b) {
                                const std::string an = norm_path(a);
                                const std::string bn = norm_path(b);
                                int score = 0;
                                size_t start = 0;
                                while (start < an.size()) {
                                    const size_t end = an.find('/', start);
                                    const std::string part = an.substr(
                                        start,
                                        end == std::string::npos
                                            ? std::string::npos
                                            : end - start);
                                    if (part.size() > 2 &&
                                        bn.find(part) != std::string::npos) {
                                        ++score;
                                    }
                                    if (end == std::string::npos) break;
                                    start = end + 1;
                                }
                                return score;
                            };
                        auto resolve_adjacent_ghf =
                            [&](const std::string& ehf_path) {
                                const std::string exact =
                                    with_ext(ehf_path, ".ghf");
                                if (const FlatAssetEntry* fe =
                                        Level::FindHeightfieldByPath(exact)) {
                                    return fe->full_path.empty()
                                        ? exact
                                        : fe->full_path;
                                }

                                const std::string exact_leaf =
                                    path_leaf(exact);
                                const std::string id =
                                    heightfield_id(ehf_path);
                                const FlatAssetEntry* best = nullptr;
                                int best_score = 0;
                                for (const auto& fe : S.all_heightfield_files) {
                                    const std::string full =
                                        fe.full_path.empty()
                                            ? fe.name
                                            : fe.full_path;
                                    if (!has_ext(full, ".ghf") &&
                                        !has_ext(fe.name, ".ghf")) {
                                        continue;
                                    }

                                    int score = 0;
                                    if (path_leaf(full) == exact_leaf ||
                                        path_leaf(fe.name) == exact_leaf) {
                                        score += 1000;
                                    }
                                    if (!id.empty()) {
                                        const std::string cand_id =
                                            heightfield_id(full.empty()
                                                ? fe.name
                                                : full);
                                        if (cand_id == id) score += 700;
                                    }
                                    if (score == 0) continue;
                                    score += path_overlap_score(ehf_path,
                                                               full);
                                    if (score > best_score) {
                                        best_score = score;
                                        best = &fe;
                                    }
                                }
                                return best
                                    ? (best->full_path.empty()
                                        ? best->name
                                        : best->full_path)
                                    : std::string{};
                            };
                        auto build_ehf_proxy_mesh =
                            [&](const HeightfieldFiles& src,
                                float fallback_height,
                                TerrainMesh& out) {
                                out = {};
                                EhfParsedBody body;
                                if (!ParseEhfBody(src.ehf_bytes, body) ||
                                    body.chunks.empty() ||
                                    body.chunk_w == 0 ||
                                    body.chunk_h == 0) {
                                    return false;
                                }

                                float min_x = std::numeric_limits<float>::infinity();
                                float min_z = std::numeric_limits<float>::infinity();
                                float max_x = -std::numeric_limits<float>::infinity();
                                float max_z = -std::numeric_limits<float>::infinity();
                                for (const auto& c : body.chunks) {
                                    if (!std::isfinite(c.origin[0]) ||
                                        !std::isfinite(c.origin[1]) ||
                                        !std::isfinite(c.extent[0]) ||
                                        !std::isfinite(c.extent[1])) {
                                        continue;
                                    }
                                    min_x = std::min(min_x, c.origin[0]);
                                    min_z = std::min(min_z, c.origin[1]);
                                    max_x = std::max(max_x, c.extent[0]);
                                    max_z = std::max(max_z, c.extent[1]);
                                }
                                if (!std::isfinite(min_x) ||
                                    !std::isfinite(min_z) ||
                                    !std::isfinite(max_x) ||
                                    !std::isfinite(max_z) ||
                                    max_x <= min_x || max_z <= min_z) {
                                    return false;
                                }

                                uint32_t W = src.ehf_header.u0;
                                uint32_t H = src.ehf_header.u1;
                                if (W < 2 || H < 2 ||
                                    uint64_t(W) * uint64_t(H) > 600000ull) {
                                    W = body.chunk_w + 1;
                                    H = body.chunk_h + 1;
                                }
                                if (W < 2 || H < 2) return false;

                                const uint32_t CW = body.chunk_w;
                                const uint32_t CH = body.chunk_h;
                                const size_t corner_count =
                                    size_t(CW + 1) * size_t(CH + 1);
                                std::vector<float> corner_sum(
                                    corner_count, 0.0f);
                                std::vector<uint32_t> corner_count_hits(
                                    corner_count, 0);
                                auto corner_index =
                                    [&](uint32_t x, uint32_t y) {
                                        return size_t(y) * size_t(CW + 1) + x;
                                    };
                                const float chunk_span_x =
                                    (max_x - min_x) / float(CW);
                                const float chunk_span_z =
                                    (max_z - min_z) / float(CH);
                                auto add_corner =
                                    [&](uint32_t x, uint32_t y, float h) {
                                        const size_t ci = corner_index(x, y);
                                        corner_sum[ci] += h;
                                        ++corner_count_hits[ci];
                                    };
                                for (const auto& c : body.chunks) {
                                    int cx = int(std::lround(
                                        (c.origin[0] - min_x) /
                                        std::max(chunk_span_x, 1e-6f)));
                                    int cy = int(std::lround(
                                        (c.origin[1] - min_z) /
                                        std::max(chunk_span_z, 1e-6f)));
                                    cx = std::clamp(cx, 0, int(CW) - 1);
                                    cy = std::clamp(cy, 0, int(CH) - 1);

                                    float h = fallback_height;
                                    if (std::isfinite(c.origin[2]) &&
                                        std::isfinite(c.extent[2])) {
                                        h = 0.5f * (c.origin[2] + c.extent[2]);
                                    } else if (std::isfinite(c.origin[2])) {
                                        h = c.origin[2];
                                    } else if (std::isfinite(c.extent[2])) {
                                        h = c.extent[2];
                                    }

                                    const uint32_t ux = uint32_t(cx);
                                    const uint32_t uy = uint32_t(cy);
                                    add_corner(ux,     uy,     h);
                                    add_corner(ux + 1, uy,     h);
                                    add_corner(ux,     uy + 1, h);
                                    add_corner(ux + 1, uy + 1, h);
                                }

                                std::vector<float> corner_h(corner_count,
                                                            fallback_height);
                                for (size_t i = 0; i < corner_count; ++i) {
                                    if (corner_count_hits[i] > 0) {
                                        corner_h[i] = corner_sum[i] /
                                            float(corner_count_hits[i]);
                                    }
                                }
                                auto corner_h_at =
                                    [&](uint32_t x, uint32_t y) {
                                        x = std::min(x, CW);
                                        y = std::min(y, CH);
                                        return corner_h[corner_index(x, y)];
                                    };
                                auto bilerp_h =
                                    [&](float fx, float fy) {
                                        const int ix = std::clamp(
                                            int(std::floor(fx)), 0,
                                            int(CW) - 1);
                                        const int iy = std::clamp(
                                            int(std::floor(fy)), 0,
                                            int(CH) - 1);
                                        const float tx = std::clamp(
                                            fx - float(ix), 0.0f, 1.0f);
                                        const float ty = std::clamp(
                                            fy - float(iy), 0.0f, 1.0f);
                                        const float h00 = corner_h_at(
                                            uint32_t(ix), uint32_t(iy));
                                        const float h10 = corner_h_at(
                                            uint32_t(ix + 1), uint32_t(iy));
                                        const float h01 = corner_h_at(
                                            uint32_t(ix), uint32_t(iy + 1));
                                        const float h11 = corner_h_at(
                                            uint32_t(ix + 1), uint32_t(iy + 1));
                                        const float hx0 = h00 + (h10 - h00) * tx;
                                        const float hx1 = h01 + (h11 - h01) * tx;
                                        return hx0 + (hx1 - hx0) * ty;
                                    };

                                const size_t N = size_t(W) * size_t(H);
                                out.width = W;
                                out.height = H;
                                out.positions.resize(N * 3);
                                out.normals.resize(N * 3);
                                out.uvs.resize(N * 2);
                                out.min_height =
                                    std::numeric_limits<float>::infinity();
                                out.max_height =
                                    -std::numeric_limits<float>::infinity();

                                for (uint32_t y = 0; y < H; ++y) {
                                    const float vy = (H > 1)
                                        ? float(y) / float(H - 1)
                                        : 0.0f;
                                    const float fcy = vy * float(CH);
                                    for (uint32_t x = 0; x < W; ++x) {
                                        const float vx = (W > 1)
                                            ? float(x) / float(W - 1)
                                            : 0.0f;
                                        const float fcx = vx * float(CW);
                                        const float ph =
                                            bilerp_h(fcx, fcy);
                                        const size_t i = size_t(y) * W + x;
                                        out.positions[i * 3 + 0] =
                                            min_x + vx * (max_x - min_x);
                                        out.positions[i * 3 + 1] = ph;
                                        out.positions[i * 3 + 2] =
                                            min_z + vy * (max_z - min_z);
                                        out.uvs[i * 2 + 0] = vx;
                                        out.uvs[i * 2 + 1] = vy;
                                        out.min_height =
                                            std::min(out.min_height, ph);
                                        out.max_height =
                                            std::max(out.max_height, ph);
                                    }
                                }

                                const float step_x =
                                    (max_x - min_x) /
                                    float(std::max<uint32_t>(1, W - 1));
                                const float step_z =
                                    (max_z - min_z) /
                                    float(std::max<uint32_t>(1, H - 1));
                                auto height_at =
                                    [&](int x, int y) {
                                        x = std::clamp(x, 0, int(W) - 1);
                                        y = std::clamp(y, 0, int(H) - 1);
                                        return out.positions[
                                            (size_t(y) * W + size_t(x)) * 3 + 1];
                                    };
                                for (uint32_t y = 0; y < H; ++y) {
                                    for (uint32_t x = 0; x < W; ++x) {
                                        const float hl = height_at(
                                            int(x) - 1, int(y));
                                        const float hr = height_at(
                                            int(x) + 1, int(y));
                                        const float hd = height_at(
                                            int(x), int(y) - 1);
                                        const float hu = height_at(
                                            int(x), int(y) + 1);
                                        float nx = (hl - hr) * step_z;
                                        float ny = 2.0f * step_x * step_z;
                                        float nz = (hd - hu) * step_x;
                                        float len =
                                            std::sqrt(nx * nx + ny * ny +
                                                      nz * nz);
                                        if (len > 1e-6f) {
                                            nx /= len;
                                            ny /= len;
                                            nz /= len;
                                        } else {
                                            nx = 0.0f;
                                            ny = 1.0f;
                                            nz = 0.0f;
                                        }
                                        const size_t i = size_t(y) * W + x;
                                        out.normals[i * 3 + 0] = nx;
                                        out.normals[i * 3 + 1] = ny;
                                        out.normals[i * 3 + 2] = nz;
                                    }
                                }

                                out.indices.resize(
                                    size_t(W - 1) * size_t(H - 1) * 6);
                                size_t k = 0;
                                for (uint32_t y = 0; y + 1 < H; ++y) {
                                    for (uint32_t x = 0; x + 1 < W; ++x) {
                                        const uint32_t i00 =
                                            uint32_t(size_t(y) * W + x);
                                        const uint32_t i10 =
                                            uint32_t(size_t(y) * W + x + 1);
                                        const uint32_t i01 =
                                            uint32_t(size_t(y + 1) * W + x);
                                        const uint32_t i11 =
                                            uint32_t(size_t(y + 1) * W + x + 1);
                                        out.indices[k++] = i00;
                                        out.indices[k++] = i01;
                                        out.indices[k++] = i10;
                                        out.indices[k++] = i10;
                                        out.indices[k++] = i01;
                                        out.indices[k++] = i11;
                                    }
                                }
                                out.ok = true;
                                return true;
                            };
                        const std::string main_ehf_norm = norm_path(res.ehf_path);
                        for (const auto& adj_ehf_path : all_ehf_refs) {
                            if (norm_path(adj_ehf_path) == main_ehf_norm) continue;

                            HeightfieldFiles adj_hf;
                            if (!LoadHeightfieldFiles(adj_ehf_path, {},
                                                      {}, {}, adj_hf)) {
                                OutputLog::warn("adjacent terrain load failed: " +
                                                adj_ehf_path + " (" + adj_hf.error + ")");
                                continue;
                            }

                            if (S.cancel_requested.load()) {
                                OutputLog::warn("level load cancelled during adjacent terrain loop");
                                return false;
                            }

                            TerrainMesh adj_mesh;
                            std::vector<Level::VistaPatchGeom> adj_patch_geoms;
                            bool used_vista_mesh = false;
                            bool used_ehf_render_mesh = false;
                            {
                                std::string vista_stats;
                                if (BuildEhfVistaPatchMesh(
                                        adj_hf.ehf_bytes, adj_mesh,
                                        &vista_stats)) {
                                    used_vista_mesh = true;
                                    OutputLog::info("adjacent terrain using "
                                                    ".ehf bg patches: " +
                                                    adj_ehf_path + " (" +
                                                    vista_stats + ")");

                                    std::string geom_stats;
                                    if (BuildEhfVistaPatchGeoms(
                                            adj_hf.ehf_bytes, adj_patch_geoms,
                                            &geom_stats)) {
                                        OutputLog::info(
                                            "adjacent terrain per-patch geoms: " +
                                            geom_stats);
                                    }
                                }
                            }

                            if (!adj_mesh.ok) {
                                const std::string adj_ghf_path =
                                    resolve_adjacent_ghf(adj_ehf_path);
                                HeightfieldFiles adj_ghf;
                                if (!adj_ghf_path.empty() &&
                                    LoadHeightfieldFiles({}, adj_ghf_path,
                                                         {}, {}, adj_ghf) &&
                                    !adj_ghf.ghf_bytes_raw.empty()) {
                                    GhfHeights adj_hg;
                                    if (!DecodeGhfHeights(adj_ghf.ghf_bytes_raw,
                                                          adj_hg)) {
                                        OutputLog::warn("adjacent terrain .ghf decode failed: " +
                                                        adj_ghf_path + " (" + adj_hg.error + ")");
                                    } else {
                                        if (adj_hg.tile_size <= 0.0f) {
                                            const float ehf_tile = adj_hf.ehf_header.ok
                                                ? adj_hf.ehf_header.f2 : 0.0f;
                                            adj_hg.tile_size =
                                                (ehf_tile > 0.0f &&
                                                 std::isfinite(ehf_tile))
                                                    ? ehf_tile : hg.tile_size;
                                        }
#ifdef _WIN32
                                        const bool adjacent_built =
                                            BuildTerrainMesh(adj_hg, adj_mesh);
#else
                                        constexpr size_t
                                            kLinuxAdjacentPreviewVertices =
                                                65536;
                                        const size_t adjacent_vertex_count =
                                            size_t(adj_hg.width) *
                                            size_t(adj_hg.height);
                                        Level::GhfHeights preview_adj_hg;
                                        const Level::GhfHeights* render_adj_hg =
                                            &adj_hg;
                                        if (adjacent_vertex_count >
                                            kLinuxAdjacentPreviewVertices) {
                                            preview_adj_hg =
                                                make_linux_preview_heightfield(
                                                    adj_hg,
                                                    kLinuxAdjacentPreviewVertices);
                                            render_adj_hg = &preview_adj_hg;
                                        }
                                        const bool adjacent_built =
                                            BuildTerrainMesh(*render_adj_hg,
                                                             adj_mesh);
#endif
                                        if (!adjacent_built) {
                                            OutputLog::warn("adjacent terrain mesh build failed: " +
                                                            adj_ehf_path);
                                        }
                                    }
                                }
                                if (adj_mesh.ok) {
                                    EhfParsedBody adj_body;
                                    if (ParseEhfBody(adj_hf.ehf_bytes, adj_body) &&
                                        !adj_body.chunks.empty()) {
                                        float min_x = 1e30f, min_z = 1e30f;
                                        for (const auto& c : adj_body.chunks) {
                                            min_x = std::min(min_x, c.origin[0]);
                                            min_z = std::min(min_z, c.origin[1]);
                                        }
                                        if (std::isfinite(min_x) &&
                                            std::isfinite(min_z) &&
                                            (std::fabs(min_x) > 1e-4f ||
                                             std::fabs(min_z) > 1e-4f)) {
                                            for (size_t pi = 0;
                                                 pi + 2 < adj_mesh.positions.size();
                                                 pi += 3) {
                                                adj_mesh.positions[pi + 0] += min_x;
                                                adj_mesh.positions[pi + 2] += min_z;
                                            }
                                        }
                                    }
                                    OutputLog::info("adjacent terrain using .ghf "
                                                    "grid (no bg patches): " +
                                                    adj_ehf_path);
                                }
                            }
                            if (!adj_mesh.ok) {
                                std::string render_stats;
                                if (BuildEhfRenderStripMesh(
                                        adj_hf.ehf_bytes, adj_mesh,
                                        &render_stats)) {
                                    used_ehf_render_mesh = true;
                                    OutputLog::info("adjacent terrain using "
                                                    ".ehf render mesh: " +
                                                    adj_ehf_path + " (" +
                                                    render_stats + ")");
                                } else if (build_ehf_proxy_mesh(adj_hf,
                                                                 hg.min_height,
                                                                 adj_mesh)) {
                                    OutputLog::info("adjacent terrain using .ehf "
                                                    "chunk proxy (last resort): " +
                                                    adj_ehf_path);
                                } else {
                                    OutputLog::info("adjacent terrain skipped "
                                                    "(no usable .ghf/.ehf mesh): " +
                                                    adj_ehf_path);
                                    continue;
                                }
                            }

                            Level::PendingAdjacentTerrain adj;
                            adj.label = std::filesystem::path(adj_ehf_path)
                                            .filename().string();
                            adj.preferred_bnk = g_pending_terrain_level_entry.bnk_path;
                            adj.ehf_bytes = std::move(adj_hf.ehf_bytes);
                            adj.mesh = std::move(adj_mesh);
                            adj.patch_geoms = std::move(adj_patch_geoms);
                            adj.preserve_mesh_uvs =
                                used_vista_mesh || used_ehf_render_mesh;
                            adj.prefer_embedded_albedo = true;
                            g_pending_adjacent_terrain_meshes.push_back(std::move(adj));
                        }
                        if (!g_pending_adjacent_terrain_meshes.empty()) {
                            OutputLog::success("adjacent terrain meshes loaded: " +
                                std::to_string(g_pending_adjacent_terrain_meshes.size()));
                        }

                        g_pending_terrain_ghf_payload   = hf.ghf_bytes_raw;
                        g_pending_terrain_ghf_heights =
                            std::move(hg.heights);
                        g_pending_terrain_ghf_tile_size = hg.tile_size;
                        g_pending_terrain_ghf_width     = (int)hg.width;
                        g_pending_terrain_ghf_height    = (int)hg.height;
                        {
                            const FlatAssetEntry* fe =
                                Level::FindHeightfieldByPath(res.ghf_path);
                            g_pending_terrain_ghf_entry =
                                fe ? *fe : FlatAssetEntry{};
                        }

                        {
                            std::vector<Level::PropBlock> hkx_blocks =
                                std::move(g_pending_level_prop_blocks);
                            g_pending_level_prop_blocks = info.prop_blocks;
                            g_pending_level_prop_blocks.insert(
                                g_pending_level_prop_blocks.end(),
                                std::make_move_iterator(hkx_blocks.begin()),
                                std::make_move_iterator(hkx_blocks.end()));
                        }

                        {
                            std::vector<LevelEdit::Addition> adds;
                            LevelEdit::GetAdditions(adds);
                            for (size_t ai = 0; ai < adds.size(); ++ai) {
                                const auto& a = adds[ai];
                                if (a.removed) continue;
                                if (a.model_path.empty()) {
                                    if (a.entity_kind ==
                                        LevelEdit::AdditionEntityKind::Chest) {
                                        LevelSpawnMarker marker;
                                        marker.x = a.pos[0];
                                        marker.y = a.pos[1];
                                        marker.z = a.pos[2];
                                        marker.kind = a.is_dig_spot ? 4 : 5;
                                        marker.is_container = true;
                                        marker.pending_addition_index =
                                            int(ai);
                                        marker.name = a.entity_name.empty()
                                            ? (a.is_dig_spot
                                                   ? "New dig spot"
                                                   : "New container")
                                            : a.entity_name;
                                        g_level_spawn_markers.push_back(
                                            std::move(marker));
                                    }
                                    continue;
                                }
                                if (a.entity_kind ==
                                        LevelEdit::AdditionEntityKind::
                                            GenericProp &&
                                    !a.entity_name.empty()) {
                                    const bool already_marked =
                                        std::any_of(
                                            g_level_spawn_markers.begin(),
                                            g_level_spawn_markers.end(),
                                            [&](const LevelSpawnMarker&
                                                    existing) {
                                                return existing
                                                           .pending_addition_index ==
                                                       int(ai);
                                            });
                                    if (!already_marked) {
                                        LevelSpawnMarker marker;
                                        marker.x = a.pos[0];
                                        marker.y = a.pos[1];
                                        marker.z = a.pos[2];
                                        marker.kind = 6;
                                        marker.pending_addition_index =
                                            int(ai);
                                        marker.name = a.entity_name;
                                        std::string model_path =
                                            a.model_path;
                                        std::transform(
                                            model_path.begin(),
                                            model_path.end(),
                                            model_path.begin(),
                                            [](unsigned char c) {
                                                return static_cast<char>(
                                                    std::tolower(c));
                                            });
                                        std::replace(model_path.begin(),
                                                     model_path.end(), '/',
                                                     '\\');
                                        uint32_t model_hash =
                                            0x811C9DC5u;
                                        for (unsigned char c :
                                             model_path) {
                                            model_hash *= 0x01000193u;
                                            model_hash ^= uint32_t(c);
                                        }
                                        marker.model_hashes.push_back(
                                            model_hash);
                                        g_level_spawn_markers.push_back(
                                            std::move(marker));
                                    }
                                }
                                Level::PropBlock pb;
                                pb.type = 0xB3;
                                pb.model_path = a.model_path;
                                Level::PropInstance pi;
                                pi.hash = 0xADD0000000000000ull + ai;
                                pi.values[0] = a.pos[0];
                                pi.values[1] = a.pos[1];
                                pi.values[2] = a.pos[2];
                                const float yaw =
                                    a.yaw_deg * 0.01745329252f;
                                pi.values[6] = std::sin(yaw);
                                pi.values[7] = std::cos(yaw);
                                pi.values[9] = pi.values[10] =
                                    pi.values[11] = 1.0f;
                                pi.lev_rec_kind = 5;
                                pi.pos_file_offset = (uint32_t)ai + 1;
                                pb.instances.push_back(pi);
                                g_pending_level_prop_blocks.push_back(
                                    std::move(pb));
                            }
                            if (!adds.empty()) {
                                OutputLog::success(
                                    "level edit: injected " +
                                    std::to_string(adds.size()) +
                                    " placed model(s)");
                            }
                        }

                        bridge_debug_dump_blocks(
                            "FINAL RENDER PROP PIPELINE",
                            g_pending_level_prop_blocks);

                        if (!g_pending_terrain_ghf_heights.empty() &&
                            g_pending_terrain_ghf_width > 0 &&
                            g_pending_terrain_ghf_height > 0)
                        {
                            const int   gw = g_pending_terrain_ghf_width;
                            const int   gh = g_pending_terrain_ghf_height;
                            const float tile =
                                g_pending_terrain_ghf_tile_size > 0.0f
                                    ? g_pending_terrain_ghf_tile_size : 0.5f;
                            const auto& heights = g_pending_terrain_ghf_heights;
                            auto sample_h = [&](float wx, float wy) -> float {
                                float gx = wx / tile;
                                float gy = wy / tile;
                                int ix = int(gx); int iy = int(gy);
                                if (ix < 0) ix = 0; else if (ix >= gw) ix = gw - 1;
                                if (iy < 0) iy = 0; else if (iy >= gh) iy = gh - 1;
                                return heights[size_t(iy) * size_t(gw) + size_t(ix)];
                            };
                            size_t authored_z_count = 0;
                            size_t terrain_delta_count = 0;
                            float max_abs_delta = 0.0f;
                            for (auto& pb : g_pending_level_prop_blocks) {
                                if (pb.type != 0xB1) continue;
                                for (auto& inst : pb.instances) {
                                    const float terrain_z =
                                        sample_h(inst.values[0], inst.values[1]);
                                    const float delta = inst.values[2] - terrain_z;
                                    if (std::isfinite(delta)) {
                                        max_abs_delta =
                                            std::max(max_abs_delta,
                                                     std::fabs(delta));
                                        if (std::fabs(delta) > 0.25f) {
                                            ++terrain_delta_count;
                                        }
                                    }
                                    ++authored_z_count;
                                }
                            }
                            std::ostringstream gs;
                            gs << "preserved authored Z for "
                               << authored_z_count
                               << " GDB-derived placements";
                            if (authored_z_count > 0) {
                                gs << " ("
                                   << terrain_delta_count
                                   << " differ from terrain by >0.25m, max="
                                   << max_abs_delta << ")";
                            }
                            OutputLog::info(gs.str());
                        }

                        g_pending_level_water_present = false;
                        g_pending_level_water_scene = Level::WaterScene{};
                        g_pending_level_water_theme = Gdb::WaterTheme{};
                        {
                            std::vector<std::string> water_candidates;
                            auto add_unique_water = [&](const std::string& path) {
                                if (path.empty()) return;
                                const std::string norm = norm_path(path);
                                for (const auto& existing : water_candidates) {
                                    if (norm_path(existing) == norm) return;
                                }
                                water_candidates.push_back(path);
                            };

                            const std::string main_base =
                                basename_no_ext(res.ehf_path.empty()
                                    ? res.ghf_path : res.ehf_path);
                            for (const auto& water_ref : all_water_refs) {
                                if (!main_base.empty() &&
                                    basename_no_ext(water_ref) == main_base) {
                                    add_unique_water(water_ref);
                                }
                            }
                            if (!res.ehf_path.empty()) {
                                add_unique_water(with_ext(res.ehf_path, ".water"));
                            }
                            if (!res.ghf_path.empty()) {
                                add_unique_water(with_ext(res.ghf_path, ".water"));
                            }
                            for (const auto& water_ref : all_water_refs) {
                                add_unique_water(water_ref);
                            }

                            bool found_water_file = false;
                            Level::WaterScene merged;
                            for (const auto& water_path : water_candidates) {
                                std::vector<uint8_t> water_bytes;
                                if (!load_text_sibling(water_path, water_bytes) ||
                                    water_bytes.empty()) {
                                    continue;
                                }

                                found_water_file = true;
                                Level::WaterScene scene;
                                if (Level::ParseWaterFile(water_bytes, scene)) {
                                    size_t total_tiles = 0;
                                    for (const auto& b : scene.bodies)
                                        total_tiles += b.tiles.size();
                                    OutputLog::success(
                                        ".water parsed: " +
                                        std::to_string(scene.bodies.size()) +
                                        " bodies, " +
                                        std::to_string(total_tiles) + " tiles from " +
                                        water_path);
                                    merged.version = scene.version;
                                    merged.tile_count += scene.tile_count;
                                    for (auto& b : scene.bodies) {
                                        merged.bodies.push_back(std::move(b));
                                    }
                                    continue;
                                }

                                
                                
                                
                                if (water_bytes.size() > 16) {
                                    OutputLog::warn(
                                        ".water sibling found but failed "
                                        "to parse: " + water_path);
                                }
                            }
                            if (!merged.bodies.empty()) {
                                merged.body_count =
                                    uint32_t(merged.bodies.size());
                                OutputLog::success(
                                    ".water merged scene: " +
                                    std::to_string(merged.bodies.size()) +
                                    " bodies, " +
                                    std::to_string(merged.tile_count) +
                                    " tiles across all heightfields");
                                g_pending_level_water_scene = std::move(merged);
                                g_pending_level_water_present = true;
                            }

                            if (!found_water_file && !water_candidates.empty()) {
                                OutputLog::info(
                                    ".water not found in level BNK; first tried " +
                                    water_candidates.front());
                            }
                        }

                        g_pending_level_model_body_bnk.clear();
                        if (!res.model_body_bnk.empty()) {
                            auto found_model_bnk =
                                find_bnk_by_virtual_path(res.model_body_bnk);
                            if (!found_model_bnk) {
                                size_t slash =
                                    res.model_body_bnk.find_last_of("/\\");
                                std::string model_leaf =
                                    (slash == std::string::npos)
                                        ? res.model_body_bnk
                                        : res.model_body_bnk.substr(slash + 1);
                                std::transform(model_leaf.begin(),
                                               model_leaf.end(),
                                               model_leaf.begin(), ::tolower);
                                found_model_bnk = find_bnk_by_filename(model_leaf);
                            }
                            if (found_model_bnk) {
                                g_pending_level_model_body_bnk = *found_model_bnk;
                                OutputLog::info("level props: resolved model BNK " +
                                                res.model_body_bnk + " -> " +
                                                std::filesystem::path(*found_model_bnk)
                                                    .filename().string());
                            } else {
                                OutputLog::warn("level props: model BNK not mounted: " +
                                                res.model_body_bnk);
                            }
                        }

                        if (S.cancel_requested.load()) {
                            OutputLog::warn("level load cancelled before handoff to terrain stage");
                            return false;
                        }
                        g_pending_terrain_load =
                            !g_level_export_only_load.load();

                        {
                            auto pal = EhfPalette::Parse(hf.ehf_bytes);
                            if (pal.ok) {
                                std::ostringstream pos;
                                pos << "ehf palette: " << pal.entries.size()
                                    << " ground-texture entr"
                                    << (pal.entries.size() == 1 ? "y" : "ies")
                                    << " @ 0x" << std::hex << pal.palette_offset;
                                OutputLog::info(pos.str());
                                const size_t n_show = std::min<size_t>(pal.entries.size(), 6);
                                for (size_t pi = 0; pi < n_show; ++pi) {
                                    const auto& e = pal.entries[pi];
                                    std::filesystem::path d_p = e.diffuse_path;
                                    std::filesystem::path n_p = e.normal_path;
                                    std::ostringstream l;
                                    l << "  [" << pi << "] tile=" << e.tile_scale
                                      << " int=" << e.intensity
                                      << "  diff=" << d_p.filename().string()
                                      << "  norm=" << n_p.filename().string();
                                    OutputLog::info(l.str());
                                }
                                if (pal.entries.size() > n_show) {
                                    OutputLog::info("  ... (+ "
                                        + std::to_string(pal.entries.size() - n_show)
                                        + " more)");
                                }
                            }
                        }

                    }
                }
            }
        }
    } else {
        OutputLog::warn("no .ehf or .ghf path in level - can't load terrain");
    }

    return true;
}

}
