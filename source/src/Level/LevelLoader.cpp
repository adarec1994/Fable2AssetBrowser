#include "LevelLoader.h"
#include "HeightfieldLoader.h"
#include "TextureAtlasDecoder.h"
#include "EhfPalette.h"
#include "EhfChunkParser.h"
#include "TerrainTextureRegistry.h"
#include "VfsConfig.h"
#include "GdbModelHashlist.h"
#include "GdbParser.h"
#include "../Havok/HavokPackfileReader.h"

#include "../Utilities/State.h"
#include "../Utilities/Utils.h"
#include "../Utilities/Progress.h"
#include "../BNKCore.cpp"
#include "../UI/OutputLog.h"
#include "../textures/TexParser.h"
#include "../textures/LhTexCodec.h"
#include "../textures/export/TextureExport.h"
#include <zlib.h>

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
#include <cstdint>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>
#include <iomanip>
#include <climits>
#include <limits>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

std::atomic<bool>   g_pending_terrain_load{false};
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
std::vector<std::string>           g_level_vfs_texture_body_bnks;
std::vector<std::string>           g_level_vfs_model_bnks;
std::vector<std::string>           g_level_vfs_streaming_bnks;
std::vector<HavokCollisionMesh>    g_level_havok_collision;
std::vector<GdbWorldPlacement>     g_level_gdb_placements;
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
            g_level_havok_collision.clear();
            g_level_gdb_placements.clear();
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

struct BeReader {
    const uint8_t* p = nullptr;
    size_t         n = 0;
    size_t         i = 0;

    bool need(size_t k) const { return i + k <= n; }

    bool u8(uint8_t& v) {
        if (!need(1)) return false;
        v = p[i++];
        return true;
    }
    bool u32(uint32_t& v) {
        if (!need(4)) return false;
        v  = (uint32_t(p[i + 0]) << 24)
           | (uint32_t(p[i + 1]) << 16)
           | (uint32_t(p[i + 2]) << 8)
           |  uint32_t(p[i + 3]);
        i += 4;
        return true;
    }
    bool u64(uint64_t& v) {
        uint32_t hi = 0;
        uint32_t lo = 0;
        if (!u32(hi) || !u32(lo)) return false;
        v = (uint64_t(hi) << 32) | uint64_t(lo);
        return true;
    }
    bool f32(float& f) {
        uint32_t u = 0;
        if (!u32(u)) return false;
        std::memcpy(&f, &u, sizeof(f));
        return true;
    }
    bool skip(size_t k) {
        if (!need(k)) return false;
        i += k;
        return true;
    }
    bool half(float& out) {
        if (!need(2)) return false;
        const uint16_t h =
            (uint16_t(p[i]) << 8) | uint16_t(p[i + 1]);
        i += 2;
        const uint32_t sign = (uint32_t(h & 0x8000)) << 16;
        const uint32_t exp_h = (h >> 10) & 0x1f;
        const uint32_t mant = h & 0x3ff;
        uint32_t bits;
        if (exp_h == 0) {
            if (mant == 0) {
                bits = sign;
            } else {
                uint32_t e = 127 - 14;
                uint32_t m = mant;
                while ((m & 0x400) == 0) { m <<= 1; --e; }
                m &= 0x3ff;
                bits = sign | (e << 23) | (m << 13);
            }
        } else if (exp_h == 31) {
            bits = sign | (0xff << 23) | (mant << 13);
        } else {
            bits = sign | ((exp_h + (127 - 15)) << 23) | (mant << 13);
        }
        std::memcpy(&out, &bits, sizeof(out));
        return true;
    }
    bool cstr(std::string& s) {
        s.clear();
        const size_t start = i;
        const size_t limit = std::min(n, start + 4096);
        while (i < limit) {
            const uint8_t c = p[i++];
            if (c == 0) return true;
            s.push_back(static_cast<char>(c));
        }
        return false;
    }
};

struct StreamingModelCandidate {
    std::string hint_path;
    std::string resolved_path;
    std::string key;
    std::string display_name;
    const FlatAssetEntry* entry = nullptr;
    bool from_gmd = false;
};

void mat3_mul(const float a[9], const float b[9], float out[9])
{
    float r[9] = {};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            r[row * 3 + col] =
                a[row * 3 + 0] * b[0 * 3 + col] +
                a[row * 3 + 1] * b[1 * 3 + col] +
                a[row * 3 + 2] * b[2 * 3 + col];
        }
    }
    for (int i = 0; i < 9; ++i) {
        out[i] = r[i];
    }
}

bool is_gdb_pi_pair_yaw_rotation(float rx, float ry)
{
    constexpr float kPi = 3.14159265358979323846f;
    return std::isfinite(rx) && std::isfinite(ry) &&
           std::fabs(std::fabs(rx) - kPi) < 1e-4f &&
           std::fabs(std::fabs(ry) - kPi) < 1e-4f;
}

void fill_gdb_rotation_matrix(Level::PropInstance& pi,
                              float rx,
                              float ry,
                              float rz,
                              float scale)
{
    if (!std::isfinite(rx)) rx = 0.0f;
    if (!std::isfinite(ry)) ry = 0.0f;
    if (!std::isfinite(rz)) rz = 0.0f;
    if (!std::isfinite(scale) || scale <= 0.01f || scale >= 100.0f) {
        scale = 1.0f;
    }

    float game[9] = {};
    const float a = -rz;
    const float b = ry;
    const float c = rx;
    const float sa = std::sin(a);
    const float ca = std::cos(a);
    const float sb = std::sin(b);
    const float cb = std::cos(b);
    const float sc = std::sin(c);
    const float cc = std::cos(c);

    // Matches the game's Euler-to-matrix helper in default.xex (0x8257ACA0).
    game[0] = cb * ca;
    game[1] = sb * sc - cb * cc * sa;
    game[2] = cb * sc * sa + sb * cc;
    game[3] = sa;
    game[4] = cc * ca;
    game[5] = -sc * ca;
    game[6] = -sb * ca;
    game[7] = sb * cc * sa + cb * sc;
    game[8] = cb * cc - sb * sc * sa;

    const int axis_map[3] = {0, 2, 1};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            pi.values[3 + row * 3 + col] =
                game[axis_map[row] * 3 + axis_map[col]];
        }
    }
    pi.values[12] = scale;
    pi.has_full_transform = true;
}

const StreamingModelCandidate*
choose_streaming_model_for_gdb(const std::string& entity_name,
                               const std::vector<StreamingModelCandidate>& candidates,
                               int* out_score = nullptr,
                               uint32_t parent_hash = 0);
std::vector<StreamingModelCandidate>
collect_streaming_model_candidates(const std::vector<std::string>& streaming_bnks);

std::string lower_slash(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

uint32_t fnv1_model_path_hash(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    std::replace(s.begin(), s.end(), '/', '\\');

    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : s) {
        h *= 0x01000193u;
        h ^= uint32_t(c);
    }
    return h;
}

std::string strip_model_suffixes(std::string s)
{
    auto strip = [](std::string& v, const char* suffix) {
        const size_t n = std::strlen(suffix);
        if (v.size() >= n && v.compare(v.size() - n, n, suffix) == 0) {
            v.resize(v.size() - n);
        }
    };
    strip(s, ".gmd");
    strip(s, ".mdl");
    return s;
}

std::string compact_match_key(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (std::isalnum(c)) {
            out.push_back(char(std::tolower(c)));
        }
    }
    return out;
}

std::string model_name_from_path(const std::string& path)
{
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    p = strip_model_suffixes(p);
    const size_t slash = p.find_last_of('/');
    return (slash == std::string::npos) ? p : p.substr(slash + 1);
}

bool is_gdb_authored_level_shell_model(
    const std::string& model_path,
    const std::unordered_set<std::string>& authored_level_model_paths)
{
    const std::string p = lower_slash(model_path);
    if (authored_level_model_paths.find(p) ==
        authored_level_model_paths.end())
    {
        return false;
    }

    // GDB adds gameplay ownership/contents around buildings, while engine_level
    // already authors the large exterior shells.  Only suppress exact path
    // collisions, so distinct variants and interior shell pieces still render.
    return p.find("/buildings/") != std::string::npos ||
           p.find("/structures/") != std::string::npos;
}

bool is_gdb_shell_audit_model(const std::string& model_path)
{
    const std::string p = lower_slash(model_path);
    return p.find("/buildings/") != std::string::npos ||
           p.find("/structures/") != std::string::npos;
}

std::string hex_u32(uint32_t v)
{
    std::ostringstream os;
    os << "0x" << std::uppercase << std::hex
       << std::setw(8) << std::setfill('0') << v;
    return os.str();
}

std::string gdb_shell_sample_text(
    const Gdb::Placement& p,
    const std::string& model_path)
{
    std::ostringstream os;
    os << (p.entity_name.empty() ? "<unnamed>" : p.entity_name)
       << " parent=" << hex_u32(p.parent_hash);
    if (p.model_path_hash != 0) {
        os << " modelHash=" << hex_u32(p.model_path_hash);
    }
    os << " pos=(" << p.x << ", " << p.y << ", " << p.z << ")"
       << " model=" << model_path;
    return os.str();
}

std::string gdb_representative_name(const std::vector<std::string>& examples)
{
    if (examples.empty()) return {};
    std::string s = examples.front();
    const size_t us = s.find_last_of('_');
    if (us != std::string::npos && us + 1 < s.size()) {
        bool digits = true;
        for (size_t i = us + 1; i < s.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
                digits = false;
                break;
            }
        }
        if (digits) s.resize(us);
    }
    return s;
}

std::string gdb_entity_key(std::string s)
{
    static const char* prefixes[] = {
        "NewObjectBuilding", "ObjectBuilding",
        "NewObjectFurniture", "ObjectFurniture",
        "NewObjectStatic", "ObjectStatic",
        "NewObject", "Object",
        "New"
    };
    for (const char* pfx : prefixes) {
        const size_t n = std::strlen(pfx);
        if (s.size() > n && s.compare(0, n, pfx) == 0) {
            s = s.substr(n);
            break;
        }
    }
    return compact_match_key(s);
}

bool is_gdb_landmark_name(const std::string& entity_name)
{
    const std::string key = gdb_entity_key(entity_name);
    if (key.empty()) return false;
    const char* needles[] = {
        "bridge",
        "clocktower",
        "grandfatherclock",
        "wallclock",
        "dockarch",
        "gatehouse",
        "lockgate",
        "walltower",
        "wallgate",
        "archway",
        "guardpost",
        "marketstairs",
        "scaffoldingstairs",
        "castlearch",
        "dockswall",
        "oilamp",
        "oillantern",
        "statue",
    };
    for (const char* needle : needles) {
        if (key.find(needle) != std::string::npos) return true;
    }
    return false;
}

bool bytes_contain_be_u32(const std::vector<uint8_t>& bytes, uint32_t value)
{
    const uint8_t a = uint8_t(value >> 24);
    const uint8_t b = uint8_t(value >> 16);
    const uint8_t c = uint8_t(value >> 8);
    const uint8_t d = uint8_t(value);
    for (size_t i = 0; i + 4 <= bytes.size(); ++i) {
        if (bytes[i] == a && bytes[i + 1] == b &&
            bytes[i + 2] == c && bytes[i + 3] == d) {
            return true;
        }
    }
    return false;
}

std::string hex32_for_log(uint32_t value)
{
    std::ostringstream os;
    os << "0x" << std::hex << std::uppercase
       << std::setw(8) << std::setfill('0') << value;
    return os.str();
}

void log_curated_hashlist_miss(const std::string& entity_name,
                               uint32_t parent_hash,
                               const char* target_model_path)
{
    static std::mutex logged_mutex;
    static std::unordered_set<std::string> logged_keys;

    std::string key = hex32_for_log(parent_hash) + "|" +
                      gdb_entity_key(entity_name) + "|" +
                      (target_model_path ? target_model_path : "");
    {
        std::lock_guard<std::mutex> lock(logged_mutex);
        if (!logged_keys.insert(key).second) return;
    }

    OutputLog::warn(
        "GDB hashlist: curated model target missing in streaming candidates; "
        "parent=" + hex32_for_log(parent_hash) +
        " entity='" + entity_name +
        "' target='" + (target_model_path ? target_model_path : "") + "'");
}

std::string resolve_streaming_bnk_path(const std::string& vfs_stream_path)
{
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

    if (auto resolved = find_bnk_by_virtual_path(vfs_stream_path)) {
        return *resolved;
    }
    for (const auto& p : S.bnk_paths) {
        std::string leaf = std::filesystem::path(p).filename().string();
        std::transform(leaf.begin(), leaf.end(), leaf.begin(), ::tolower);
        if (leaf_matches(leaf)) return p;
    }
    for (const auto& p : S.nested_bnk_paths) {
        std::string leaf = std::filesystem::path(p).filename().string();
        std::transform(leaf.begin(), leaf.end(), leaf.begin(), ::tolower);
        if (leaf_matches(leaf)) return p;
    }
    return {};
}

std::vector<StreamingModelCandidate>
collect_streaming_model_candidates(const std::vector<std::string>& streaming_bnks)
{
    std::unordered_map<std::string, const FlatAssetEntry*> mdl_by_path;
    mdl_by_path.reserve(S.all_mdl_files.size());
    std::unordered_map<std::string, std::vector<const FlatAssetEntry*>> mdl_by_key;
    mdl_by_key.reserve(S.all_mdl_files.size());
    for (const auto& e : S.all_mdl_files) {
        mdl_by_path.emplace(lower_slash(e.full_path), &e);
        mdl_by_key[compact_match_key(model_name_from_path(e.full_path))]
            .push_back(&e);
    }
    auto choose_global_model = [&](const std::string& hint_path) {
        const std::string hint_lower = lower_slash(hint_path);
        if (auto exact = mdl_by_path.find(hint_lower); exact != mdl_by_path.end()) {
            return exact->second;
        }
        for (const auto& kv : mdl_by_path) {
            const std::string& model_path = kv.first;
            if (model_path.size() >= hint_lower.size() &&
                model_path.compare(model_path.size() - hint_lower.size(),
                                   hint_lower.size(),
                                   hint_lower) == 0) {
                return kv.second;
            }
        }

        const std::string hint_key =
            compact_match_key(model_name_from_path(hint_path));
        if (hint_key.empty()) return static_cast<const FlatAssetEntry*>(nullptr);

        auto choose_best = [](const std::vector<const FlatAssetEntry*>& hits) {
            const FlatAssetEntry* best = nullptr;
            int best_score = INT_MIN;
            for (const FlatAssetEntry* e : hits) {
                if (!e) continue;
                int score = 0;
                const std::string lower = lower_slash(e->full_path);
                if (lower.find("/globals_models.bnk") == std::string::npos) {
                    score += 500;
                }
                if (e->from_nested) score += 250;
                score -= int(std::min<size_t>(e->full_path.size(), 240));
                if (!best || score > best_score) {
                    best = e;
                    best_score = score;
                }
            }
            return best;
        };

        if (auto it = mdl_by_key.find(hint_key); it != mdl_by_key.end()) {
            return choose_best(it->second);
        }

        std::vector<const FlatAssetEntry*> fuzzy;
        for (const auto& kv : mdl_by_key) {
            const std::string& model_key = kv.first;
            if (model_key.size() < 5) continue;
            const bool related =
                model_key.find(hint_key) != std::string::npos ||
                hint_key.find(model_key) != std::string::npos;
            if (!related) continue;
            fuzzy.insert(fuzzy.end(), kv.second.begin(), kv.second.end());
        }
        return choose_best(fuzzy);
    };

    std::vector<StreamingModelCandidate> out;
    std::unordered_set<std::string> seen;
    for (const auto& vfs_path : streaming_bnks) {
        const std::string mounted = resolve_streaming_bnk_path(vfs_path);
        if (mounted.empty()) continue;
        try {
            BnkCache::Entry& bnk = BnkCache::get(mounted);
            const auto& files = bnk.reader->list_files();
            auto add_candidate = [&](std::string hint, bool from_gmd) {
                std::string norm = lower_slash(hint);
                auto [seen_it, inserted] = seen.insert(norm);
                if (!inserted) {
                    if (from_gmd) {
                        for (auto& existing : out) {
                            if (lower_slash(existing.hint_path) == norm) {
                                existing.from_gmd = true;
                                break;
                            }
                        }
                    }
                    return;
                }

                StreamingModelCandidate c;
                c.hint_path = std::move(hint);
                c.display_name = model_name_from_path(c.hint_path);
                c.key = compact_match_key(c.display_name);
                c.from_gmd = from_gmd;
                c.entry = choose_global_model(c.hint_path);
                if (c.entry) {
                    c.resolved_path = c.entry->full_path;
                }
                out.push_back(std::move(c));
            };

            for (const auto& f : files) {
                std::string lower = lower_slash(f.name);
                if (lower.size() >= 8 &&
                    lower.compare(lower.size() - 8, 8, ".mdl.gmd") == 0) {
                    std::string mdl = f.name;
                    mdl.resize(mdl.size() - 4);
                    add_candidate(std::move(mdl), true);
                    continue;
                }
                if (lower.size() >= 4 &&
                    lower.compare(lower.size() - 4, 4, ".hkx") == 0) {
                    std::string mdl = f.name;
                    mdl.resize(mdl.size() - 4);
                    mdl += ".mdl";
                    add_candidate(std::move(mdl), false);
                }
            }
        } catch (...) {
        }
    }
    return out;
}

int streaming_model_score(const std::string& entity_name,
                          const StreamingModelCandidate& c)
{
    const std::string entity_key = gdb_entity_key(entity_name);
    if (entity_key.empty() || c.key.empty()) return INT_MIN;

    auto has = [&](const char* needle) {
        return entity_key.find(needle) != std::string::npos;
    };
    auto cand_has = [&](const char* needle) {
        return c.key.find(needle) != std::string::npos;
    };
    const std::string path_key = compact_match_key(c.hint_path + " " +
                                                   c.resolved_path);
    auto cand_path_has = [&](const char* needle) {
        return path_key.find(needle) != std::string::npos;
    };

    int score = INT_MIN;
    if (entity_key == c.key) {
        score = 12000;
    } else if (c.key.find(entity_key) != std::string::npos) {
        score = 9000 + int(entity_key.size());
    } else if (entity_key.find(c.key) != std::string::npos) {
        score = 7000 + int(c.key.size());
    }

    struct Alias { const char* entity; const char* model; int score; };
    static const Alias aliases[] = {
        { "smallwallpost",         "stonewallmediumpostspiked",     15000 },
        { "wallpost",              "stonewallmediumpostspiked",     14500 },
        { "smallwallstraight",     "stonewallmediumstraightspiked", 15000 },
        { "smallwallcurved",       "stonewallmediumcurvedspiked",   15000 },
        { "smallwallcorner",       "stonewallmediumcurvedspiked",   14500 },
        { "smallwallbroken",       "stonewallmediumbrokenspiked",   15000 },
        { "shelflong",             "esashelflong",                  15000 },
        { "woodenbucket",          "esabucketwooden",               15000 },
        { "lightsceiling",         "bslightceiling",                15000 },
        { "lightfixingceiling",    "bslightceiling",                15000 },
        { "candleholder",          "bscandleholder",                14000 },
        { "grainsack",             "esasackgrain",                  14500 },
        { "shippingcrate",         "esashippingcrate",              14500 },
        { "weaponrackwallmulti",   "esashopweaponswallrackmulti",   15000 },
        { "weaponrackwallsingle",  "esashopweaponswallracksingle",  15000 },
        { "weaponrack",            "esashopweaponrack",             13500 },
        { "booksgroup",            "esabooksblock",                 13000 },
        { "pubtable",              "esatabletavern",                14500 },
        { "largesquareultradecorative","esaftableultradecorative",  13800 },
        { "largesquareupgradeable","esaftabledecorative",           12500 },
        { "standardultradecorative","esaftableultradecorative",     13600 },
        { "standardupgradeable",   "esaftabledecorative",           12300 },
        { "bookcaseultradecorative","esafbookcaseultradecorative",  15000 },
        { "bookcaseworn",          "esafbookcaseworn",              14500 },
        { "dresserupgradeable",    "esafdresserultradecorative",    13000 },
        { "kitchensinkupgradeable", "esakitchensink",               12000 },
        { "buildingsalesign",      "buildingsalesign",              14000 },
        { "bsmarketbridge",        "bsmarketbridge",                16000 },
        { "marketbridge",          "bsmarketbridge",                15800 },
        { "bridge",                "bsmarketbridge",                12000 },
        { "bsmarketclocktower",    "bsmarketclocktower",            16000 },
        { "marketclocktower",      "bsmarketclocktower",            15800 },
        { "clocktower",            "bsmarketclocktower",            14500 },
        { "grandfatherclock",      "bsgrandfatherclock",            15500 },
        { "wallclock",             "bswallclock",                   15500 },
        { "bsmarketdockarch",      "bsmarketdocksarch",             15000 },
        { "dockarch",              "bsmarketdocksarch",             14500 },
        { "bsmarketarchway",       "bsmarketarchway",               15000 },
        { "archway",               "bsmarketarchway",               13000 },
        { "bsmarketgatehouse",     "bsmarketgatehouse",             15000 },
        { "bsgatehouse",           "bsmarketgatehouse",             14500 },
        { "bsmarketlockgate",      "bsmarketlockgates",             15000 },
        { "lockgate",              "bsmarketlockgates",             14000 },
        { "bsmarketwalltower",     "bsmarketwalltower",             15000 },
        { "walltower",             "bsmarketwalltower",             13500 },
        { "bsmarketwallgate",      "bsmarketwallgate",              15000 },
        { "closedgate",            "bsmarketwallgate",              13000 },
        { "guardpost",             "bsmarketguardpost",             14500 },
        { "marketstairs",          "bsmarketstairs",                14500 },
        { "generalstorestairsfloor","bsmarketgeneralshopstairsfloor",14500 },
        { "generalshopstairsfloor", "bsmarketgeneralshopstairsfloor",14500 },
        { "scaffoldingstairs",     "bsmarketscaffoldingstairs",     14500 },
        { "scaffoldstairs",        "bsmarketscaffoldingstairs",     14500 },
        { "scaffoldstraight",      "bsmarketscaffoldingstraight",   14000 },
        { "marketwalljoiner",      "bsmarketwallbuffer",            13500 },
        { "walljoiner",            "bsmarketwallbuffer",            13000 },
        { "castlearch",            "bsmarketcastlearch",            14500 },
        { "dockswall",             "bsmarketdockswall",             14500 },
        { "dockwall",              "bsmarketdockswall",             14500 },
        { "bsdockwall",            "bsmarketdockswall",             14500 },
        { "slumswall",             "bsslumsthinwallv1",             14000 },
        { "slumsthinwall",         "bsslumsthinwallv1",             14500 },
        { "windowsmallarched",     "esasmarchedwin",                14000 },
        { "smallarchedwin",        "esasmarchedwin",                14000 },
        { "smarchedwin",           "esasmarchedwin",                14000 },
        { "marketdocksjetty",      "bsmarketdocksjetty",            14000 },
        { "docksjetty",            "bsmarketdocksjetty",            13500 },
        { "docksplatform",         "bsmarketdocksplatform",         13500 },
        { "dockscrane",            "bsmarketdockscrane",            13500 },
        { "oillanternsingle",      "bscemetaryoillampsingle",       13000 },
        { "oillampsingle",         "bscemetaryoillampsingle",       13000 },
        { "statue",                "okstatuedolphinv1",             12000 },
        { "cellarlargeroom",       "cellarlargeroom",               15000 },
        { "cellarsmallroom",       "cellarsmallroom",               15000 },
        { "bsmarkettownhousesmall", "bstownhousebasicfacademid",    13500 },
        { "bwsmarkettownhousesmall","bstownhousebasicfacademid",    13500 },
        { "townhousev1",           "bstownhousev1facademid",        14000 },
        { "townhousev2",           "bstownhousev2facademid",        14000 },
        { "townhousev3",           "bstownhousev3exterior",         14500 },
    };
    for (const auto& a : aliases) {
        if (has(a.entity) && (cand_has(a.model) || cand_path_has(a.model))) {
            score = std::max(score, a.score);
        }
    }

    if (score == INT_MIN) return score;
    if (c.entry) score += 500;
    if (c.from_gmd) score += 150;
    if (has("facademid") && path_key.find("facademid") != std::string::npos) {
        score += 500;
    }
    if (has("facade") && path_key.find("facade") != std::string::npos) {
        score += 150;
    }
    return score - int(std::min<size_t>(c.hint_path.size(), 200));
}

const StreamingModelCandidate*
choose_streaming_model_for_gdb(const std::string& entity_name,
                               const std::vector<StreamingModelCandidate>& candidates,
                               int* out_score,
                               uint32_t parent_hash)
{
    auto path_suffix_matches = [](const std::string& path,
                                  const std::string& target) {
        if (path.empty() || target.empty()) return false;
        if (path == target) return true;
        return path.size() > target.size() &&
               path.compare(path.size() - target.size(),
                            target.size(), target) == 0 &&
               (path[path.size() - target.size() - 1] == '/' ||
                path[path.size() - target.size() - 1] == '\\');
    };

    auto choose_curated_override =
        [&](const char* target_model_path, int* score_out) {
            if (!target_model_path || !*target_model_path) {
                return static_cast<const StreamingModelCandidate*>(nullptr);
            }
            const std::string target_lower = lower_slash(target_model_path);
            const std::string target_key =
                compact_match_key(model_name_from_path(target_model_path));
            const StreamingModelCandidate* best = nullptr;
            int best_score = INT_MIN;
            for (const auto& c : candidates) {
                int score = INT_MIN;
                const std::string resolved_lower = lower_slash(c.resolved_path);
                const std::string hint_lower = lower_slash(c.hint_path);
                if (resolved_lower == target_lower) {
                    score = std::max(score, 50000);
                } else if (path_suffix_matches(resolved_lower, target_lower)) {
                    score = std::max(score, 49250);
                }
                if (hint_lower == target_lower) {
                    score = std::max(score, 49000);
                } else if (path_suffix_matches(hint_lower, target_lower)) {
                    score = std::max(score, 48250);
                }
                if (!target_key.empty()) {
                    if (c.key == target_key) {
                        score = std::max(score, 46000);
                    }
                    const std::string path_key =
                        compact_match_key(c.hint_path + " " +
                                          c.resolved_path);
                    if (path_key.find(target_key) != std::string::npos) {
                        score = std::max(score, 44000);
                    }
                }
                if (score == INT_MIN) continue;
                if (c.entry) score += 500;
                if (c.from_gmd) score += 150;
                score -= int(std::min<size_t>(c.hint_path.size(), 200));
                if (!best || score > best_score) {
                    best = &c;
                    best_score = score;
                }
            }
            if (score_out) *score_out = best_score;
            return best;
        };

    const std::string entity_key = gdb_entity_key(entity_name);
    const char* curated_model =
        GdbModelHashlist::LookupParentHash(parent_hash);
    if (!curated_model) {
        curated_model = GdbModelHashlist::LookupEntityKey(entity_key);
    }
    if (curated_model && *curated_model) {
        if (const StreamingModelCandidate* curated =
                choose_curated_override(curated_model, out_score)) {
            return curated;
        }
        log_curated_hashlist_miss(entity_name, parent_hash, curated_model);
        if (out_score) *out_score = INT_MIN;
        return nullptr;
    }

    const StreamingModelCandidate* best = nullptr;
    int best_score = INT_MIN;
    for (const auto& c : candidates) {
        const int score = streaming_model_score(entity_name, c);
        if (!best || score > best_score) {
            best = &c;
            best_score = score;
        }
    }
    if (out_score) *out_score = best_score;
    return (best_score >= 6500) ? best : nullptr;
}

constexpr char kEngineLevelMagic[]  = "LevelGraphicsFile";
constexpr size_t kEngineLevelMagicLen = sizeof(kEngineLevelMagic) - 1;

}

bool ParseEngineLevel(const std::vector<uint8_t>& bytes,
                      EngineLevelInfo&            out)
{
    out = {};
    if (bytes.size() < kEngineLevelMagicLen + 8) {
        out.error = "file too small for header";
        return false;
    }

    BeReader r{bytes.data(), bytes.size(), 0};

    if (std::memcmp(r.p, kEngineLevelMagic, kEngineLevelMagicLen) != 0) {
        out.error = "magic mismatch (expected \"LevelGraphicsFile\")";
        return false;
    }
    if (!r.skip(kEngineLevelMagicLen)) {
        out.error = "truncated reading magic";
        return false;
    }

    if (!r.u32(out.version)) {
        out.error = "truncated reading version";
        return false;
    }
    if (out.version < 11 || out.version > 12) {
        std::ostringstream os;
        os << "unsupported version " << out.version
           << " (engine accepts 11..12)";
        out.error = os.str();
        return false;
    }

    if (!r.u32(out.entry_count)) {
        out.error = "truncated reading entry_count";
        return false;
    }
    if (out.entry_count > (1u << 20)) {
        out.error = "entry_count looks corrupt";
        return false;
    }
    out.entries.reserve(out.entry_count);

    for (uint32_t mi = 0; mi < out.entry_count; ++mi) {
        EngineLevelEntry e;
        e.offset = r.i;

        if (!r.u32(e.type)) {
            std::ostringstream os;
            os << "truncated at entry " << mi << " of " << out.entry_count;
            out.error = os.str();
            out.ok = false;
            return false;
        }

        switch (e.type) {
            case 2: {
                PropBlock block;
                block.offset = e.offset;
                block.type = e.type;
                if (!r.cstr(block.model_path) ||
                    !r.cstr(block.shadow_model_path) ||
                    !r.cstr(block.lod_model_path) ||
                    !r.cstr(block.extra_model_path)) {
                    out.error = "truncated reading type-2 model paths";
                    return false;
                }

                e.str_a = block.model_path;
                e.str_b = block.lod_model_path;

                uint32_t instance_count = 0;
                if (!r.u32(instance_count)) {
                    out.error = "truncated reading type-2 instance count";
                    return false;
                }
                if (instance_count > 100000) {
                    out.error = "type-2 instance count looks corrupt";
                    return false;
                }

                block.instances.reserve(instance_count);
                for (uint32_t pi = 0; pi < instance_count; ++pi) {
                    PropInstance inst;
                    if (!r.u8(inst.flags[0]) ||
                        !r.u8(inst.flags[1]) ||
                        !r.u8(inst.flags[2]) ||
                        !r.u64(inst.hash)) {
                        out.error = "truncated reading type-2 instance header";
                        return false;
                    }
                    for (float& v : inst.values) {
                        if (!r.f32(v)) {
                            out.error = "truncated reading type-2 instance floats";
                            return false;
                        }
                    }
                    block.instances.push_back(inst);
                }

                out.prop_blocks.push_back(std::move(block));
                break;
            }
            case 4:
            case 5:
            case 32: {
                if (!r.cstr(e.str_a)) {
                    out.error = "truncated reading string for type "
                              + std::to_string(e.type);
                    return false;
                }
                if (e.type == 4) {
                    if (!r.skip(8)) {
                        out.error = "truncated reading type-4 tail";
                        return false;
                    }
                }
                break;
            }
            case 21: {
                e.str_b.clear();
                if (!r.cstr(e.str_a)) {
                    out.error = "truncated reading string A for type 21";
                    return false;
                }
                if (!r.cstr(e.str_b)) {
                    out.error = "truncated reading string B for type 21";
                    return false;
                }
                if (!r.skip(8 + 1 + 1)) {
                    out.error = "truncated reading type-21 hash+flags";
                    return false;
                }

                uint32_t loop1_count = 0;
                if (!r.u32(loop1_count)) {
                    out.error = "truncated type-21 ext header";
                    return false;
                }
                if (!r.skip(7 * 4 + 12 + 4 + 24)) {
                    out.error = "truncated type-21 ext header (mid)";
                    return false;
                }
                if (loop1_count > 100000) {
                    out.error = "type-21 loop1 count looks corrupt";
                    return false;
                }

                PropBlock t21_block;
                t21_block.offset = e.offset;
                t21_block.type = e.type;
                t21_block.model_path = e.str_a;
                t21_block.lod_model_path = e.str_b;
                t21_block.instances.reserve(loop1_count);

                if (out.version == 11) {
                    for (uint32_t k = 0; k < loop1_count; ++k) {
                        PropInstance inst;
                        for (int j = 0; j < 4; ++j) {
                            if (!r.f32(inst.values[j])) {
                                out.error = "truncated type-21 v11 loop1 body";
                                return false;
                            }
                        }
                        inst.values[7] = 1.0f;
                        inst.values[9] = inst.values[10] = inst.values[11] = 1.0f;
                        t21_block.instances.push_back(inst);
                    }
                } else {
                    for (uint32_t k = 0; k < loop1_count; ++k) {
                        float pos[3];
                        if (!r.f32(pos[0]) || !r.f32(pos[1]) || !r.f32(pos[2])) {
                            out.error = "truncated type-21 v12 instance vec3";
                            return false;
                        }
                        float qx, qy, qz, qw, scale;
                        if (!r.half(qx) || !r.half(qy) ||
                            !r.half(qz) || !r.half(qw) ||
                            !r.half(scale)) {
                            out.error = "truncated type-21 v12 instance quat/scale";
                            return false;
                        }
                        PropInstance inst;
                        inst.values[0] = pos[0];
                        inst.values[1] = pos[1];
                        inst.values[2] = pos[2];

                        const float num = 2.0f * (qw * qz + qx * qy);
                        const float den = 1.0f - 2.0f * (qy * qy + qz * qz);
                        const float mag = std::sqrt(num * num + den * den);
                        if (mag > 1e-6f) {
                            inst.values[6] = num / mag;
                            inst.values[7] = den / mag;
                        } else {
                            inst.values[6] = 0.0f;
                            inst.values[7] = 1.0f;
                        }

                        const float s = (scale > 0.0f) ? scale : 1.0f;
                        inst.values[9]  = s;
                        inst.values[10] = s;
                        inst.values[11] = s;
                        t21_block.instances.push_back(inst);
                    }
                }

                uint32_t loop2_count = 0;
                if (!r.u32(loop2_count)) {
                    out.error = "truncated type-21 loop2 count";
                    return false;
                }
                if (loop2_count > 100000) {
                    out.error = "type-21 loop2 count looks corrupt";
                    return false;
                }

                size_t loop2_emitted = 0;
                size_t loop2_skipped = 0;
                for (uint32_t k = 0; k < loop2_count; ++k) {
                    float a_val, b_val;
                    float p1[3], p2[3];
                    if (!r.f32(a_val) || !r.f32(b_val) ||
                        !r.f32(p1[0]) || !r.f32(p1[1]) || !r.f32(p1[2]) ||
                        !r.f32(p2[0]) || !r.f32(p2[1]) || !r.f32(p2[2]))
                    {
                        out.error = "truncated type-21 loop2 body";
                        return false;
                    }

                    auto in_bounds = [](float v, float lo, float hi) {
                        return std::isfinite(v) && v >= lo && v <= hi;
                    };
                    const bool plausible =
                        in_bounds(p1[0], -2048.0f, 2048.0f) &&
                        in_bounds(p1[1], -2048.0f, 2048.0f) &&
                        in_bounds(p1[2], -512.0f,   512.0f) &&
                        (std::fabs(p1[0]) + std::fabs(p1[1]) + std::fabs(p1[2]) > 0.5f);
                    if (!plausible) {
                        ++loop2_skipped;
                        continue;
                    }

                    PropInstance inst;
                    inst.values[0] = p1[0];
                    inst.values[1] = p1[1];
                    inst.values[2] = p1[2];
                    const float fxy =
                        std::sqrt(p2[0] * p2[0] + p2[1] * p2[1]);
                    if (std::isfinite(fxy) && fxy > 0.001f && fxy < 100.0f) {
                        inst.values[6] = p2[1] / fxy;
                        inst.values[7] = p2[0] / fxy;
                    } else {
                        inst.values[6] = 0.0f;
                        inst.values[7] = 1.0f;
                    }
                    const float s = (std::isfinite(a_val) &&
                                     a_val > 0.05f && a_val < 100.0f)
                                        ? a_val : 1.0f;
                    inst.values[9]  = s;
                    inst.values[10] = s;
                    inst.values[11] = s;
                    t21_block.instances.push_back(inst);
                    ++loop2_emitted;
                }
                (void)loop2_emitted;
                (void)loop2_skipped;

                if (!t21_block.instances.empty()) {
                    out.prop_blocks.push_back(std::move(t21_block));
                }
                break;
            }
            default: {
                std::ostringstream uos;
                uos << "  unknown entry type 0x"
                    << std::hex << e.type << std::dec
                    << " (" << e.type << ") at offset 0x"
                    << std::hex << e.offset << std::dec
                    << " — last 6 entries:";
                OutputLog::warn(uos.str());
                const size_t n = out.entries.size();
                for (size_t k = (n > 6 ? n - 6 : 0); k < n; ++k) {
                    const auto& pe = out.entries[k];
                    std::ostringstream ros;
                    ros << "    [" << k << "] type=" << pe.type
                        << " @ 0x" << std::hex << pe.offset
                        << "  size=" << std::dec << pe.size;
                    if (!pe.str_a.empty()) ros << "  a=" << pe.str_a;
                    if (!pe.str_b.empty()) ros << "  b=" << pe.str_b;
                    OutputLog::info(ros.str());
                }
                e.size = 0;
                out.entries.push_back(e);
                out.ok = true;
                return true;
            }
        }
        e.size = r.i - e.offset;
        out.entries.push_back(std::move(e));
    }

    out.ok = true;
    return true;
}

bool Open(const FlatAssetEntry& entry)
{
    OutputLog::info("loading level '" + entry.name + "' …");
    progress_update(8, 100, "Extracting " + entry.name);

    auto bail_if_cancelled = [&](const char* where) -> bool {
        if (!S.cancel_requested.load()) return false;
        OutputLog::warn(std::string("level load cancelled at ") + where);
        return true;
    };

    g_pending_level_prop_blocks.clear();
    g_pending_adjacent_terrain_meshes.clear();

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

    progress_update(18, 100, "Parsing level entries...");
    EngineLevelInfo info;
    if (!ParseEngineLevel(bytes, info)) {
        OutputLog::error("parse failed for '" + entry.name + "': "
                         + info.error);
        return false;
    }
    if (bail_if_cancelled("after-parse")) return false;

    info.source_path = entry.full_path;

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
            (e.str_a.find("heightfield") != std::string::npos) ||
            (e.str_a.find("Heightfield") != std::string::npos);

        if (is_heightfield_like) {
            ++n_heightfield_refs;
            OutputLog::info("  heightfield ref: t" + std::to_string(e.type)
                            + "  " + e.str_a);
            if (ends_with_ci(e.str_a, ".ehf")) {
                all_ehf_refs.push_back(e.str_a);
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
        OutputLog::warn("level references no .ehf/.ghf/heightfield* strings — "
                        "checking sibling .list file for the heightfield "
                        "names instead.");
    }

    auto sibling_with_ext = [&](const std::string& new_ext) {
        std::filesystem::path p = entry.full_path;
        p.replace_extension(new_ext);
        return p.string();
    };

    auto load_text_sibling = [&](const std::string& sibling_full_path,
                                 std::vector<uint8_t>& out_bytes) -> bool
    {
        std::string key = sibling_full_path;
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        std::replace(key.begin(), key.end(), '\\', '/');
        int idx = BnkCache::find_index(entry.bnk_path, key);
        if (idx < 0) return false;
        try {
            out_bytes = BnkCache::extract_bytes(entry.bnk_path, idx);
            return !out_bytes.empty();
        } catch (...) {
            return false;
        }
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
    g_level_gdb_placements.clear();
    {
        std::vector<std::pair<uint32_t, std::string>> save_hash_to_name;
        struct SavePhysicsPlacement {
            uint32_t hash = 0;
            std::string entity_name;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;
        };
        std::vector<SavePhysicsPlacement> save_physics_placements;
        {
            std::vector<uint8_t> save_bytes;
            const std::string save_path = sibling_with_ext(".save");
            if (load_text_sibling(save_path, save_bytes)) {
                std::string xml(reinterpret_cast<const char*>(save_bytes.data()),
                                save_bytes.size());
                auto tag_payload = [](const std::string& s,
                                      const char* tag,
                                      std::string& out) -> bool {
                    const std::string open = std::string("<") + tag;
                    const std::string close = std::string("</") + tag + ">";
                    size_t a = s.find(open);
                    if (a == std::string::npos) return false;
                    a = s.find('>', a);
                    if (a == std::string::npos) return false;
                    size_t b = s.find(close, a + 1);
                    if (b == std::string::npos) return false;
                    out = s.substr(a + 1, b - (a + 1));
                    return true;
                };
                auto parse_float_text = [](const std::string& s,
                                           float& out) -> bool {
                    const char* p = s.c_str();
                    char* end = nullptr;
                    float v = std::strtof(p, &end);
                    if (end == p || !std::isfinite(v)) return false;
                    out = v;
                    return true;
                };
                auto read_float_tag = [&](const std::string& s,
                                          const char* tag,
                                          float& out) -> bool {
                    std::string payload;
                    return tag_payload(s, tag, payload) &&
                           parse_float_text(payload, out);
                };
                auto read_vec3_tag = [&](const std::string& s,
                                         const char* tag,
                                         float& x,
                                         float& y,
                                         float& z) -> bool {
                    std::string payload;
                    return tag_payload(s, tag, payload) &&
                           read_float_tag(payload, "X", x) &&
                           read_float_tag(payload, "Y", y) &&
                           read_float_tag(payload, "Z", z);
                };
                auto read_quat_tag = [&](const std::string& s,
                                         const char* tag,
                                         float& x,
                                         float& y,
                                         float& z,
                                         float& w) -> bool {
                    std::string payload;
                    if (!tag_payload(s, tag, payload)) return false;
                    bool ok = read_float_tag(payload, "X", x) &&
                              read_float_tag(payload, "Y", y) &&
                              read_float_tag(payload, "Z", z);
                    float rw = 1.0f;
                    if (read_float_tag(payload, "W", rw)) w = rw;
                    return ok;
                };
                const std::string tag_open  = "<Entity name=\"";
                const std::string tag_close = "</Entity>";
                size_t pos = 0;
                while (true) {
                    size_t a = xml.find(tag_open, pos);
                    if (a == std::string::npos) break;
                    a += tag_open.size();
                    size_t name_end = xml.find('"', a);
                    if (name_end == std::string::npos) break;
                    std::string name = xml.substr(a, name_end - a);
                    size_t hash_start = xml.find("0x", name_end);
                    if (hash_start == std::string::npos) break;
                    size_t hash_end = xml.find('<', hash_start);
                    if (hash_end == std::string::npos) break;
                    std::string hex = xml.substr(hash_start + 2, hash_end - hash_start - 2);
                    size_t entity_close = xml.find(tag_close, hash_end);
                    if (entity_close == std::string::npos) break;
                    uint32_t h = 0;
                    for (char c : hex) {
                        h <<= 4;
                        if (c >= '0' && c <= '9') h |= (c - '0');
                        else if (c >= 'A' && c <= 'F') h |= (c - 'A' + 10);
                        else if (c >= 'a' && c <= 'f') h |= (c - 'a' + 10);
                    }
                    std::string entity_xml =
                        xml.substr(name_end + 1, entity_close - (name_end + 1));
                    std::string physics_xml;
                    SavePhysicsPlacement sp;
                    sp.hash = h;
                    sp.entity_name = name;
                    if (tag_payload(entity_xml, "PhysicsData", physics_xml) &&
                        read_vec3_tag(physics_xml, "Position", sp.x, sp.y, sp.z)) {
                        read_quat_tag(physics_xml, "Orientation",
                                      sp.qx, sp.qy, sp.qz, sp.qw);
                        save_physics_placements.push_back(std::move(sp));
                    }
                    save_hash_to_name.emplace_back(h, std::move(name));
                    pos = entity_close + tag_close.size();
                }
                OutputLog::info("save: " + std::to_string(save_hash_to_name.size())
                                + " entity hash→name mappings");
                if (!save_physics_placements.empty()) {
                    OutputLog::info("save: " +
                                    std::to_string(save_physics_placements.size()) +
                                    " PhysicsData transform(s)");
                }
            } else {
                OutputLog::warn("no .save sibling in BNK");
            }
        }

        std::vector<uint8_t> gdb_bytes;
        const std::string gdb_path = sibling_with_ext(".gdb");
        if (load_text_sibling(gdb_path, gdb_bytes)) {
            auto info = Gdb::ParseWithSaveMap(gdb_bytes, save_hash_to_name);
            g_level_gdb_placements.reserve(info.placements.size());

            size_t fixed_count = 0, var_count = 0, named_count = 0;
            size_t model_hash_count = 0;
            for (const auto& p : info.placements) {
                GdbWorldPlacement gp;
                gp.x      = p.x;
                gp.y      = p.y;
                gp.z      = p.z;
                gp.yaw    = p.yaw;
                gp.rot_x  = p.rot_x;
                gp.rot_y  = p.rot_y;
                gp.rot_z  = p.rot_z;
                gp.scale  = p.scale;
                gp.hash   = p.hash_a;
                gp.parent_hash = p.parent_hash;
                gp.model_path_hash = p.model_path_hash;
                gp.marker = p.marker;
                g_level_gdb_placements.push_back(gp);
                if (p.marker == 0x00004B40) ++fixed_count;
                else                         ++var_count;
                if (!p.entity_name.empty())  ++named_count;
                if (p.model_path_hash != 0)  ++model_hash_count;
            }
            std::ostringstream os;
            os << "gdb: " << g_level_gdb_placements.size()
               << " placements (" << fixed_count << " fixed + "
               << var_count << " variable, "
               << named_count << " resolved to .save entity names, "
               << model_hash_count << " with model path hashes)";
            OutputLog::success(os.str());

            {
                std::unordered_map<uint32_t, const Gdb::Placement*> parsed_by_hash;
                parsed_by_hash.reserve(info.placements.size() * 2);
                for (const auto& p : info.placements) {
                    if (p.hash_a != 0) parsed_by_hash.emplace(p.hash_a, &p);
                }

                std::vector<std::pair<uint32_t, std::string>> landmark_save_names;
                for (const auto& kv : save_hash_to_name) {
                    if (is_gdb_landmark_name(kv.second)) {
                        landmark_save_names.push_back(kv);
                    }
                }
                if (!landmark_save_names.empty()) {
                    OutputLog::info("gdb direct landmark hash probe (.save -> .gdb):");
                    size_t shown = 0;
                    size_t parsed = 0;
                    size_t raw_only = 0;
                    size_t absent = 0;
                    for (const auto& kv : landmark_save_names) {
                        const uint32_t hash = kv.first;
                        const std::string& name = kv.second;
                        const bool raw_hit = bytes_contain_be_u32(gdb_bytes, hash);
                        auto it = parsed_by_hash.find(hash);
                        if (it != parsed_by_hash.end()) ++parsed;
                        else if (raw_hit) ++raw_only;
                        else ++absent;

                        if (shown >= 64) continue;
                        const bool important =
                            gdb_entity_key(name).find("bridge") != std::string::npos ||
                            gdb_entity_key(name).find("clocktower") != std::string::npos ||
                            gdb_entity_key(name).find("dockarch") != std::string::npos ||
                            gdb_entity_key(name).find("lockgate") != std::string::npos ||
                            gdb_entity_key(name).find("gatehouse") != std::string::npos ||
                            gdb_entity_key(name).find("walltower") != std::string::npos;
                        if (!important && it == parsed_by_hash.end()) continue;

                        std::ostringstream los;
                        los << "  0x" << std::hex << std::uppercase
                            << std::setw(8) << std::setfill('0') << hash
                            << std::dec << "  " << name << "  ";
                        if (it != parsed_by_hash.end()) {
                            const Gdb::Placement* p = it->second;
                            los << "PARSED parent=0x" << std::hex << std::uppercase
                                << std::setw(8) << std::setfill('0')
                                << p->parent_hash << std::dec
                                << " pos=(" << p->x << ", " << p->y
                                << ", " << p->z << ")";
                        } else if (raw_hit) {
                            los << "RAW-HASH-ONLY (parser did not emit placement)";
                        } else {
                            los << "NOT-IN-GDB";
                        }
                        OutputLog::info(los.str());
                        ++shown;
                    }
                    std::ostringstream sum;
                    sum << "  landmark hash probe summary: "
                        << parsed << " parsed, " << raw_only
                        << " raw-only, " << absent << " absent from raw gdb";
                    OutputLog::info(sum.str());
                }
            }

            struct GdbArchetypeDiag {
                size_t count = 0;
                std::vector<std::string> examples;
            };
            std::unordered_map<uint32_t, GdbArchetypeDiag> archetype_diag;
            for (const auto& p : info.placements) {
                if (p.marker != 0x00004B80 || p.parent_hash == 0) continue;
                auto& d = archetype_diag[p.parent_hash];
                ++d.count;
                if (!p.entity_name.empty() && d.examples.size() < 12) {
                    d.examples.push_back(p.entity_name);
                }
            }
            std::vector<std::pair<uint32_t, GdbArchetypeDiag*>> archetypes;
            archetypes.reserve(archetype_diag.size());
            for (auto& kv : archetype_diag) {
                archetypes.push_back({kv.first, &kv.second});
            }
            std::sort(archetypes.begin(), archetypes.end(),
                      [](const auto& a, const auto& b) {
                          return a.second->count > b.second->count;
                      });
            OutputLog::info("gdb archetype groups: " +
                            std::to_string(archetypes.size()) +
                            " parent hashes (top 24)");
            const size_t archetype_log_count =
                std::min<size_t>(archetypes.size(), 24);
            for (size_t ai = 0; ai < archetype_log_count; ++ai) {
                const auto& kv = archetypes[ai];
                std::ostringstream aos;
                aos << "  0x" << std::hex << std::uppercase
                    << std::setw(8) << std::setfill('0') << kv.first
                    << std::dec << "  " << kv.second->count << " instance(s)";
                if (!kv.second->examples.empty()) {
                    aos << "  e.g. ";
                    for (size_t ei = 0; ei < kv.second->examples.size(); ++ei) {
                        if (ei) aos << ", ";
                        aos << kv.second->examples[ei];
                    }
                }
                OutputLog::info(aos.str());
            }
            if (!streaming_model_candidates.empty()) {
                auto best_gdb_name_for_examples =
                    [&](const std::vector<std::string>& examples,
                        uint32_t parent_hash) {
                        std::string best_name =
                            gdb_representative_name(examples);
                        int best_score = INT_MIN;
                        for (const auto& ex : examples) {
                            const std::string repr =
                                gdb_representative_name(std::vector<std::string>{ex});
                            int score = INT_MIN;
                            const StreamingModelCandidate* hit =
                                choose_streaming_model_for_gdb(
                                    repr, streaming_model_candidates, &score,
                                    parent_hash);
                            if (hit && score > best_score) {
                                best_name = repr;
                                best_score = score;
                            }
                        }
                        return best_name;
                    };
                OutputLog::info("gdb archetype -> streaming .mdl.gmd candidates (top 24):");
                for (size_t ai = 0; ai < archetype_log_count; ++ai) {
                    const auto& kv = archetypes[ai];
                    const std::string repr =
                        best_gdb_name_for_examples(kv.second->examples,
                                                   kv.first);
                    int score = INT_MIN;
                    const StreamingModelCandidate* hit =
                        choose_streaming_model_for_gdb(
                            repr, streaming_model_candidates, &score,
                            kv.first);
                    std::ostringstream mos;
                    mos << "  0x" << std::hex << std::uppercase
                        << std::setw(8) << std::setfill('0') << kv.first
                        << std::dec << "  " << kv.second->count << "x  "
                        << (repr.empty() ? "(unnamed)" : repr) << " -> ";
                    if (hit) {
                        if (hit->entry) {
                            mos << hit->entry->full_path;
                        } else {
                            mos << "hint-only:" << hit->hint_path;
                        }
                        mos << "  score=" << score;
                        if (hit->from_gmd) mos << "  (from .gmd name)";
                        else               mos << "  (from .hkx name)";
                        if (!hit->entry) mos << "  (no global .mdl)";
                    } else {
                        mos << "NONE";
                        if (score != INT_MIN) mos << "  best_score=" << score;
                    }
                    OutputLog::info(mos.str());
                }
                OutputLog::info("gdb landmark/structure candidate hashes:");
                const char* needles[] = {
                    "bridge", "clock", "arch", "gate", "tower", "wall",
                    "stair", "lamp", "lantern", "statue"
                };
                size_t keyword_lines = 0;
                for (const auto& kv : archetypes) {
                    const std::string repr =
                        best_gdb_name_for_examples(kv.second->examples,
                                                   kv.first);
                    const std::string key = gdb_entity_key(repr);
                    bool interesting = false;
                    for (const char* n : needles) {
                        if (key.find(n) != std::string::npos) {
                            interesting = true;
                            break;
                        }
                    }
                    if (!interesting) continue;
                    int score = INT_MIN;
                    const StreamingModelCandidate* hit =
                        choose_streaming_model_for_gdb(
                            repr, streaming_model_candidates, &score,
                            kv.first);
                    std::ostringstream kos;
                    kos << "  0x" << std::hex << std::uppercase
                        << std::setw(8) << std::setfill('0') << kv.first
                        << std::dec << "  " << kv.second->count << "x  "
                        << repr << " -> ";
                    if (hit && hit->entry) {
                        kos << hit->entry->full_path << "  score=" << score;
                    } else if (hit) {
                        kos << "hint-only:" << hit->hint_path
                            << "  score=" << score;
                    } else {
                        kos << "NONE";
                        if (score != INT_MIN) kos << "  best_score=" << score;
                    }
                    OutputLog::info(kos.str());
                    if (++keyword_lines >= 40) break;
                }
            }

            auto strip_suffix = [](std::string s, const char* suf) {
                size_t n = std::strlen(suf);
                if (s.size() > n && s.compare(s.size() - n, n, suf) == 0) {
                    s.resize(s.size() - n);
                }
                return s;
            };
            auto canonicalize_for_match = [&strip_suffix](std::string s) {
                size_t us = s.find_last_of('_');
                if (us != std::string::npos && us + 1 < s.size()) {
                    bool all_digits = true;
                    for (size_t k = us + 1; k < s.size(); ++k) {
                        if (s[k] < '0' || s[k] > '9') { all_digits = false; break; }
                    }
                    if (all_digits) s.resize(us);
                }
                static const char* prefixes[] = {
                    "NewObjectBuilding", "ObjectBuilding",
                    "NewObjectFurniture", "ObjectFurniture",
                    "NewObjectStatic", "ObjectStatic",
                    "NewObject", "Object",
                    "Static", "New"
                };
                for (const char* pfx : prefixes) {
                    size_t pn = std::strlen(pfx);
                    if (s.size() > pn && s.compare(0, pn, pfx) == 0) {
                        s = s.substr(pn);
                        break;
                    }
                }
                std::string out;
                out.reserve(s.size());
                for (char c : s) {
                    if (c == '_') continue;
                    out.push_back(char(std::tolower(static_cast<unsigned char>(c))));
                }
                out = strip_suffix(out, "facademid");
                out = strip_suffix(out, "facade");
                out = strip_suffix(out, "lod1");
                out = strip_suffix(out, "lod0");
                out = strip_suffix(out, "mid");
                return out;
            };

            auto best_gdb_name_for_examples_for_matching =
                [&](const std::vector<std::string>& examples,
                    uint32_t parent_hash) {
                    std::string best_name = gdb_representative_name(examples);
                    int best_score = INT_MIN;
                    for (const auto& ex : examples) {
                        const std::string repr =
                            gdb_representative_name(std::vector<std::string>{ex});
                        int score = INT_MIN;
                        const StreamingModelCandidate* hit =
                            choose_streaming_model_for_gdb(
                                repr, streaming_model_candidates, &score,
                                parent_hash);
                        if (hit && score > best_score) {
                            best_name = repr;
                            best_score = score;
                        }
                    }
                    return best_name;
                };

            std::unordered_map<uint32_t, std::string> parent_match_names;
            if (!streaming_model_candidates.empty()) {
                parent_match_names.reserve(archetype_diag.size());
                for (const auto& kv : archetype_diag) {
                    std::string repr =
                        best_gdb_name_for_examples_for_matching(
                            kv.second.examples, kv.first);
                    if (repr.empty()) continue;
                    if (choose_streaming_model_for_gdb(
                            repr, streaming_model_candidates, nullptr,
                            kv.first)) {
                        parent_match_names.emplace(kv.first, std::move(repr));
                    }
                }
            }
            std::vector<std::string> preferred_model_bnks;
            auto add_preferred_model_bnk = [&](const std::string& bnk) {
                if (bnk.empty()) return;
                std::string norm = bnk;
                std::transform(norm.begin(), norm.end(), norm.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                std::replace(norm.begin(), norm.end(), '\\', '/');
                if (std::find(preferred_model_bnks.begin(),
                              preferred_model_bnks.end(),
                              norm) == preferred_model_bnks.end()) {
                    preferred_model_bnks.push_back(std::move(norm));
                }
            };
            auto resolve_preferred_model_bnk = [&](const std::string& vpath) {
                if (vpath.empty()) return;
                if (auto found = find_bnk_by_virtual_path(vpath)) {
                    add_preferred_model_bnk(*found);
                    return;
                }
                size_t slash = vpath.find_last_of("/\\");
                std::string leaf = (slash == std::string::npos)
                    ? vpath : vpath.substr(slash + 1);
                std::transform(leaf.begin(), leaf.end(), leaf.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                if (auto found = find_bnk_by_filename(leaf)) {
                    add_preferred_model_bnk(*found);
                }
            };
            resolve_preferred_model_bnk(res.model_body_bnk);
            for (const auto& bnk : g_level_vfs_model_bnks) {
                resolve_preferred_model_bnk(bnk);
            }

            std::unordered_map<std::string, std::vector<const FlatAssetEntry*>> mdl_by_token;
            mdl_by_token.reserve(S.all_mdl_files.size() * 2);
            for (const auto& m : S.all_mdl_files) {
                std::string base = m.name;
                size_t dot = base.find_last_of('.');
                if (dot != std::string::npos) base.resize(dot);
                std::string lc;
                lc.reserve(base.size());
                for (char c : base) {
                    if (c == '_') continue;
                    lc.push_back(char(std::tolower(static_cast<unsigned char>(c))));
                }
                lc = strip_suffix(lc, "facademid");
                lc = strip_suffix(lc, "facade");
                lc = strip_suffix(lc, "lod1");
                lc = strip_suffix(lc, "lod0");
                lc = strip_suffix(lc, "mid");
                if (!lc.empty()) {
                    mdl_by_token[lc].push_back(&m);
                }
            }
            auto normalized_path = [](std::string s) {
                std::transform(s.begin(), s.end(), s.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                std::replace(s.begin(), s.end(), '\\', '/');
                return s;
            };
            auto model_bank_score = [&](const FlatAssetEntry* e) {
                if (!e) return 0;
                const std::string bnk = normalized_path(e->bnk_path);
                for (size_t i = 0; i < preferred_model_bnks.size(); ++i) {
                    if (bnk == preferred_model_bnks[i]) {
                        return 4000 - int(i);
                    }
                }
                if (bnk.find("/globals_models.bnk") != std::string::npos ||
                    bnk == "globals_models.bnk") {
                    return 100;
                }
                return 0;
            };
            auto choose_model_candidate =
                [&](const std::vector<const FlatAssetEntry*>& candidates) {
                    const FlatAssetEntry* best = nullptr;
                    int best_score = INT_MIN;
                    for (const FlatAssetEntry* e : candidates) {
                        int score = model_bank_score(e);
                        if (e && e->from_nested) score += 250;
                        score -= int(std::min<size_t>(e ? e->full_path.size() : 0, 200));
                        if (!best || score > best_score) {
                            best = e;
                            best_score = score;
                        }
                    }
                    return best;
                };

            std::unordered_map<uint32_t, std::vector<const FlatAssetEntry*>>
                mdl_by_model_path_hash;
            mdl_by_model_path_hash.reserve(S.all_mdl_files.size() * 2);
            for (const auto& m : S.all_mdl_files) {
                if (m.full_path.empty()) continue;
                mdl_by_model_path_hash[fnv1_model_path_hash(m.full_path)]
                    .push_back(&m);
            }
            auto resolve_model_by_path_hash = [&](uint32_t model_path_hash) {
                if (model_path_hash == 0) {
                    return static_cast<const FlatAssetEntry*>(nullptr);
                }
                auto it = mdl_by_model_path_hash.find(model_path_hash);
                if (it == mdl_by_model_path_hash.end()) {
                    return static_cast<const FlatAssetEntry*>(nullptr);
                }
                return choose_model_candidate(it->second);
            };
            auto resolve_model_for_entity = [&](const std::string& entity_name) {
                std::string tok = canonicalize_for_match(entity_name);
                if (tok.empty()) return static_cast<const FlatAssetEntry*>(nullptr);

                auto exact = mdl_by_token.find(tok);
                if (exact != mdl_by_token.end()) {
                    return choose_model_candidate(exact->second);
                }

                if (tok.size() < 5) {
                    return static_cast<const FlatAssetEntry*>(nullptr);
                }

                const FlatAssetEntry* best = nullptr;
                int best_score = INT_MIN;
                for (const auto& kv : mdl_by_token) {
                    const std::string& mk = kv.first;
                    if (mk.size() < 5) continue;

                    int relation = INT_MIN;
                    if (mk.find(tok) != std::string::npos) {
                        relation = 5000 + int(tok.size() * 30)
                                 - int((mk.size() > tok.size())
                                           ? (mk.size() - tok.size()) : 0);
                    } else if (tok.find(mk) != std::string::npos &&
                               mk.size() * 2 >= tok.size()) {
                        relation = 2500 + int(mk.size() * 20);
                    } else {
                        continue;
                    }

                    const FlatAssetEntry* candidate =
                        choose_model_candidate(kv.second);
                    const int score = relation + model_bank_score(candidate);
                    if (!best || score > best_score) {
                        best = candidate;
                        best_score = score;
                    }
                }
                return best;
            };

            // GDB/save names are instance labels. Prefer exact model-path hashes
            // from the GDB parent records, then fall back to streaming hints and
            // the global .mdl index.
            constexpr bool emit_gdb_render_placements = true;
            constexpr bool emit_derived_render_placements = false;
            std::unordered_map<std::string, Level::PropBlock> blocks_by_path;
            size_t save_physics_instances_emitted = 0;
            if (emit_derived_render_placements) {
                for (const auto& p : save_physics_placements) {
                    if (p.entity_name.empty()) continue;
                    std::string tok = canonicalize_for_match(p.entity_name);
                    if (tok.empty()) continue;

                    const FlatAssetEntry* hit =
                        resolve_model_for_entity(p.entity_name);
                    if (!hit) continue;

                    auto& pb = blocks_by_path[hit->full_path];
                    if (pb.model_path.empty()) {
                        pb.type = 0xB2;
                        pb.model_path = hit->full_path;
                    }

                    Level::PropInstance pi;
                    pi.hash = p.hash;
                    pi.values[0] = p.x;
                    pi.values[1] = p.y;
                    pi.values[2] = p.z;
                    float qx = p.qx, qy = p.qy, qz = p.qz, qw = p.qw;
                    const float qmag =
                        std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
                    if (std::isfinite(qmag) && qmag > 1e-6f) {
                        qx /= qmag; qy /= qmag; qz /= qmag; qw /= qmag;
                        const float num = 2.0f * (qw * qz + qx * qy);
                        const float den = 1.0f - 2.0f * (qy * qy + qz * qz);
                        const float mag = std::sqrt(num * num + den * den);
                        if (std::isfinite(mag) && mag > 1e-6f) {
                            pi.values[6] = num / mag;
                            pi.values[7] = den / mag;
                        } else {
                            pi.values[6] = 0.0f;
                            pi.values[7] = 1.0f;
                        }
                    } else {
                        pi.values[6] = 0.0f;
                        pi.values[7] = 1.0f;
                    }
                    pi.values[9] = pi.values[10] = pi.values[11] = 1.0f;
                    pb.instances.push_back(pi);
                    ++save_physics_instances_emitted;
                }
            }
            if (save_physics_instances_emitted > 0) {
                OutputLog::success(
                    "save-derived placements: " +
                    std::to_string(save_physics_instances_emitted) +
                    " PhysicsData instance(s) appended to prop pipeline");
            }

            size_t resolved = 0;
            size_t gdb_instances_emitted = 0;
            size_t gdb_hint_only_skipped = 0;
            size_t gdb_full_euler_rotations = 0;
            size_t gdb_yaw_only_rotations = 0;
            size_t gdb_identity_rotations = 0;
            size_t gdb_pi_pair_yaw_rotations = 0;
            size_t gdb_model_hash_hits = 0;
            size_t gdb_model_hash_misses = 0;
            size_t gdb_authored_shell_skipped = 0;
            std::unordered_map<std::string, size_t>
                gdb_authored_shell_skip_paths;
            std::unordered_map<std::string, std::vector<std::string>>
                gdb_authored_shell_skip_samples;
            std::unordered_map<std::string, size_t>
                gdb_emitted_shell_paths;
            std::unordered_map<std::string, std::vector<std::string>>
                gdb_emitted_shell_samples;
            for (const auto& p : info.placements) {
                if (!emit_gdb_render_placements) continue;
                const bool has_model_hash = p.model_path_hash != 0;
                if (p.entity_name.empty() && !has_model_hash) continue;
                std::string tok = canonicalize_for_match(p.entity_name);
                if (tok.empty() && !has_model_hash) continue;
                auto parent_name_it = parent_match_names.find(p.parent_hash);
                const std::string* parent_match_name =
                    (parent_name_it == parent_match_names.end())
                        ? nullptr : &parent_name_it->second;

                const FlatAssetEntry* hit = nullptr;
                bool matched_model = false;
                bool hint_only = false;
                if (p.model_path_hash != 0) {
                    hit = resolve_model_by_path_hash(p.model_path_hash);
                    if (hit) {
                        matched_model = true;
                        ++gdb_model_hash_hits;
                    } else {
                        ++gdb_model_hash_misses;
                    }
                }

                if (!matched_model && !streaming_model_candidates.empty()) {
                    const StreamingModelCandidate* stream_hit =
                        choose_streaming_model_for_gdb(
                            p.entity_name, streaming_model_candidates,
                            nullptr, p.parent_hash);
                    if (!stream_hit && parent_match_name) {
                        stream_hit = choose_streaming_model_for_gdb(
                            *parent_match_name, streaming_model_candidates,
                            nullptr, p.parent_hash);
                    }
                    if (!stream_hit) continue;
                    matched_model = true;
                    hit = stream_hit->entry;
                    if (!hit) {
                        hint_only = true;
                    }
                } else if (!matched_model) {
                    hit = resolve_model_for_entity(p.entity_name);
                    if (!hit) continue;
                    matched_model = true;
                }

                if (!matched_model) continue;
                ++resolved;
                if (hint_only || !hit) {
                    ++gdb_hint_only_skipped;
                    continue;
                }
                if (is_gdb_authored_level_shell_model(
                        hit->full_path, authored_level_model_paths))
                {
                    ++gdb_authored_shell_skipped;
                    ++gdb_authored_shell_skip_paths[hit->full_path];
                    auto& samples =
                        gdb_authored_shell_skip_samples[hit->full_path];
                    if (samples.size() < 4) {
                        samples.push_back(
                            gdb_shell_sample_text(p, hit->full_path));
                    }
                    continue;
                }

                auto& pb = blocks_by_path[hit->full_path];
                if (pb.model_path.empty()) {
                    pb.type = 0xB1;
                    pb.model_path = hit->full_path;
                }

                Level::PropInstance pi;
                pi.hash = p.hash_a;
                pi.values[0] = p.x;
                pi.values[1] = p.y;
                pi.values[2] = p.z;
                const float scale =
                    (std::isfinite(p.scale) && p.scale > 0.01f && p.scale < 100.0f)
                        ? p.scale : 1.0f;
                if (p.has_rotation) {
                    const bool pi_pair_yaw =
                        is_gdb_pi_pair_yaw_rotation(p.rot_x, p.rot_y);
                    if (pi_pair_yaw) {
                        ++gdb_pi_pair_yaw_rotations;
                    }
                    fill_gdb_rotation_matrix(pi, p.rot_x, p.rot_y, p.rot_z, scale);
                    if (pi_pair_yaw) {
                        // Counted separately below; these are authored 180-degree facings.
                    } else if (std::fabs(p.rot_x) > 1e-4f ||
                               std::fabs(p.rot_y) > 1e-4f) {
                        ++gdb_full_euler_rotations;
                    } else if (std::fabs(p.rot_z) > 1e-4f) {
                        ++gdb_yaw_only_rotations;
                    } else {
                        ++gdb_identity_rotations;
                    }
                } else {
                    const float s_yaw = std::sin(p.yaw);
                    const float c_yaw = std::cos(p.yaw);
                    if (std::isfinite(s_yaw) && std::isfinite(c_yaw)) {
                        pi.values[6] = s_yaw;
                        pi.values[7] = c_yaw;
                    } else {
                        pi.values[6] = 0.0f;
                        pi.values[7] = 1.0f;
                    }
                    pi.values[9] = pi.values[10] = pi.values[11] = scale;
                    if (std::fabs(p.yaw) > 1e-4f) {
                        ++gdb_yaw_only_rotations;
                    } else {
                        ++gdb_identity_rotations;
                    }
                }
                pb.instances.push_back(pi);
                if (is_gdb_shell_audit_model(hit->full_path)) {
                    ++gdb_emitted_shell_paths[hit->full_path];
                    auto& samples = gdb_emitted_shell_samples[hit->full_path];
                    if (samples.size() < 4) {
                        samples.push_back(
                            gdb_shell_sample_text(p, hit->full_path));
                    }
                }
                ++gdb_instances_emitted;
            }

            std::ostringstream os3;
            os3 << "gdb-derived placements: "
                << resolved << " entities matched a model";
            if (gdb_instances_emitted > 0) {
                os3 << ", emitted " << gdb_instances_emitted
                    << " instance(s)";
                OutputLog::success(os3.str());
                OutputLog::info(
                    "gdb-derived rotations: full-euler=" +
                    std::to_string(gdb_full_euler_rotations) +
                    ", yaw-only=" +
                    std::to_string(gdb_yaw_only_rotations) +
                    ", identity=" +
                    std::to_string(gdb_identity_rotations) +
                    ", pi-pair-full=" +
                    std::to_string(gdb_pi_pair_yaw_rotations));
                if (gdb_model_hash_hits > 0 || gdb_model_hash_misses > 0) {
                    OutputLog::info(
                        "gdb-derived model path hashes: hit=" +
                        std::to_string(gdb_model_hash_hits) +
                        ", miss=" +
                        std::to_string(gdb_model_hash_misses));
                }
                if (gdb_hint_only_skipped > 0) {
                    OutputLog::info(
                        "gdb-derived skipped after model match: hint-only=" +
                        std::to_string(gdb_hint_only_skipped));
                }
                if (gdb_authored_shell_skipped > 0) {
                    OutputLog::info(
                        "gdb-derived skipped exact authored building/structure duplicates: " +
                        std::to_string(gdb_authored_shell_skipped) +
                        " instance(s) across " +
                        std::to_string(gdb_authored_shell_skip_paths.size()) +
                        " model(s)");
                    std::vector<std::pair<std::string, size_t>> skipped_paths(
                        gdb_authored_shell_skip_paths.begin(),
                        gdb_authored_shell_skip_paths.end());
                    std::sort(skipped_paths.begin(), skipped_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(skipped_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  skip exact authored: " +
                            std::to_string(skipped_paths[i].second) +
                            "x  " + skipped_paths[i].first);
                        auto sample_it =
                            gdb_authored_shell_skip_samples.find(
                                skipped_paths[i].first);
                        if (sample_it !=
                            gdb_authored_shell_skip_samples.end())
                        {
                            for (const auto& sample : sample_it->second) {
                                OutputLog::info("    e.g. " + sample);
                            }
                        }
                    }
                }
                if (!gdb_emitted_shell_paths.empty()) {
                    size_t total_shells = 0;
                    for (const auto& kv : gdb_emitted_shell_paths) {
                        total_shells += kv.second;
                    }
                    OutputLog::info(
                        "gdb-derived emitted building/structure audit: " +
                        std::to_string(total_shells) +
                        " instance(s) across " +
                        std::to_string(gdb_emitted_shell_paths.size()) +
                        " model(s)");
                    std::vector<std::pair<std::string, size_t>> emitted_paths(
                        gdb_emitted_shell_paths.begin(),
                        gdb_emitted_shell_paths.end());
                    std::sort(emitted_paths.begin(), emitted_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(emitted_paths.size(), 12);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  emit shell: " +
                            std::to_string(emitted_paths[i].second) +
                            "x  " + emitted_paths[i].first);
                        auto sample_it =
                            gdb_emitted_shell_samples.find(
                                emitted_paths[i].first);
                        if (sample_it != gdb_emitted_shell_samples.end()) {
                            for (const auto& sample : sample_it->second) {
                                OutputLog::info("    e.g. " + sample);
                            }
                        }
                    }
                }
            } else {
                os3 << " (not emitted: GDB has entity names, not model paths)";
                OutputLog::warn(os3.str());
            }

            std::vector<uint8_t> hk_scan_bytes;
            const std::string hk_scan_path = sibling_with_ext(".havok_scenario");
            if (!emit_derived_render_placements) {
                OutputLog::info(
                    "derived render placements disabled");
            } else if (save_physics_instances_emitted > 0) {
                OutputLog::info(
                    "havok entity-scan: skipped render placement fallback; using .save PhysicsData transforms");
            } else if (load_text_sibling(hk_scan_path, hk_scan_bytes)) {
                auto be_f32 = [&](size_t off) -> float {
                    if (off + 4 > hk_scan_bytes.size())
                        return std::numeric_limits<float>::quiet_NaN();
                    uint32_t u =
                        (uint32_t(hk_scan_bytes[off    ]) << 24) |
                        (uint32_t(hk_scan_bytes[off + 1]) << 16) |
                        (uint32_t(hk_scan_bytes[off + 2]) <<  8) |
                         uint32_t(hk_scan_bytes[off + 3]);
                    float f; std::memcpy(&f, &u, 4); return f;
                };

                std::unordered_map<uint32_t, std::string> hash_to_name;
                hash_to_name.reserve(save_hash_to_name.size());
                for (const auto& kv : save_hash_to_name) {
                    hash_to_name.emplace(kv.first, kv.second);
                }

                size_t found = 0;
                size_t resolved_hk = 0;
                size_t in_terrain = 0;

                auto looks_pos = [](float x, float y, float z) {
                    if (!std::isfinite(x) || !std::isfinite(y) ||
                        !std::isfinite(z)) return false;
                    if (x < -100 || x > 500) return false;
                    if (y < -100 || y > 500) return false;
                    if (z < -100 || z > 500) return false;
                    int nonzero = 0;
                    if (std::fabs(x) > 0.5f) ++nonzero;
                    if (std::fabs(y) > 0.5f) ++nonzero;
                    if (std::fabs(z) > 0.5f) ++nonzero;
                    return nonzero >= 3;
                };
                auto in_main_terrain = [](float x, float y, float z) {
                    return (x >= 0 && x <= 290) &&
                           (y >= 0 && y <= 390) &&
                           (z >= -10 && z <= 250);
                };

                for (size_t i = 0; i + 4 <= hk_scan_bytes.size(); i += 4) {
                    uint32_t v =
                        (uint32_t(hk_scan_bytes[i    ]) << 24) |
                        (uint32_t(hk_scan_bytes[i + 1]) << 16) |
                        (uint32_t(hk_scan_bytes[i + 2]) <<  8) |
                         uint32_t(hk_scan_bytes[i + 3]);
                    auto it = hash_to_name.find(v);
                    if (it == hash_to_name.end()) continue;
                    ++found;

                    float best_x = 0, best_y = 0, best_z = 0;
                    int   best_dist = INT_MAX;
                    bool  best_in_terrain = false;
                    bool  found_any = false;

                    const size_t lo = (i >= 128) ? i - 128 : 0;
                    const size_t hi = std::min(hk_scan_bytes.size() - 12, i + 64);
                    for (size_t q = lo; q <= hi; q += 4) {
                        float x = be_f32(q);
                        float y = be_f32(q + 4);
                        float z = be_f32(q + 8);
                        if (!looks_pos(x, y, z)) continue;
                        const bool inT = in_main_terrain(x, y, z);
                        int dist = (int)(q > i ? q - i : i - q);
                        bool better = false;
                        if (!found_any) better = true;
                        else if (inT && !best_in_terrain) better = true;
                        else if (inT == best_in_terrain && dist < best_dist) {
                            better = true;
                        }
                        if (better) {
                            best_x = x; best_y = y; best_z = z;
                            best_dist = dist;
                            best_in_terrain = inT;
                            found_any = true;
                        }
                    }
                    if (!found_any) continue;
                    ++resolved_hk;
                    if (best_in_terrain) ++in_terrain;

                    std::string tok = canonicalize_for_match(it->second);
                    if (tok.empty()) continue;
                    const FlatAssetEntry* hit =
                        resolve_model_for_entity(it->second);
                    if (!hit) continue;

                    auto& pb = blocks_by_path[hit->full_path];
                    if (pb.model_path.empty()) {
                        pb.type = 0xB2;
                        pb.model_path = hit->full_path;
                    }
                    Level::PropInstance pi;
                    pi.values[0]  = best_x;
                    pi.values[1]  = best_y;
                    pi.values[2]  = best_z;
                    pi.values[6]  = 0.0f;
                    pi.values[7]  = 1.0f;
                    pi.values[9]  = pi.values[10] = pi.values[11] = 1.0f;
                    pb.instances.push_back(pi);
                }

                std::ostringstream hos;
                hos << "havok entity-scan: " << found
                    << " save hashes matched in havok_scenario, "
                    << resolved_hk << " got positions ("
                    << in_terrain << " in main terrain bounds)";
                if (resolved_hk > 0) OutputLog::success(hos.str());
                else                  OutputLog::warn(hos.str());

            } else {
                OutputLog::warn("havok entity-scan skipped: no .havok_scenario");
            }
            size_t extra_blocks = 0, extra_insts = 0;
            for (auto& kv : blocks_by_path) {
                if (kv.second.instances.empty()) continue;
                ++extra_blocks;
                extra_insts += kv.second.instances.size();
                g_pending_level_prop_blocks.push_back(std::move(kv.second));
            }
            std::ostringstream eos;
            eos << "derived placements: "
                << extra_blocks << " unique models / "
                << extra_insts << " instances appended to prop pipeline";
            if (extra_insts > 0) OutputLog::success(eos.str());
            else                 OutputLog::warn(eos.str());
        } else {
            OutputLog::warn("no .gdb sibling in BNK");
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
            BnkCache::Entry& bnk = BnkCache::get(mounted_path);
            const auto& files = bnk.reader->list_files();
            size_t hkx_count = 0;
            size_t total_rb  = 0;
            size_t total_inst = 0;

            constexpr size_t kProbeLimit = 8;
            size_t probed = 0;
            size_t with_world_pos = 0;
            std::vector<std::string> world_pos_hits;
            world_pos_hits.reserve(kProbeLimit);

            auto looks_world_scale = [](float v) {
                return std::isfinite(v) && std::fabs(v) > 5.0f &&
                       std::fabs(v) < 1000.0f;
            };
            auto looks_world_pos = [&](float x, float y, float z) {
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
                    return false;
                if (std::fabs(x) > 1000.0f || std::fabs(y) > 1000.0f ||
                    std::fabs(z) > 1000.0f) return false;
                return looks_world_scale(x) || looks_world_scale(y) ||
                       looks_world_scale(z);
            };

            std::map<std::string, size_t> streaming_extensions;
            std::map<std::string, std::vector<size_t>> ext_size_samples;
            std::map<std::string, int>                 ext_sample_idx;
            std::map<std::string, std::string>         ext_sample_name;

            for (size_t i = 0; i < files.size(); ++i) {
                const auto& name = files[i].name;
                std::string lower = name;
                std::transform(lower.begin(), lower.end(),
                               lower.begin(), ::tolower);

                {
                    size_t dot = lower.find_last_of('.');
                    std::string ext = (dot != std::string::npos)
                                        ? lower.substr(dot) : "(no-ext)";
                    streaming_extensions[ext]++;
                    ext_size_samples[ext].push_back(files[i].size());
                    if (ext_sample_idx.find(ext) == ext_sample_idx.end()) {
                        ext_sample_idx[ext]  = (int)i;
                        ext_sample_name[ext] = name;
                    }
                }

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

                if (this_rb > 0 && rb_class && probed < kProbeLimit) {
                    ++probed;
                    auto rdf32 = [&](size_t o) -> float {
                        if (o + 4 > pf->bytes.size()) return 0.0f;
                        uint32_t u =
                            (uint32_t(pf->bytes[o])     << 24) |
                            (uint32_t(pf->bytes[o + 1]) << 16) |
                            (uint32_t(pf->bytes[o + 2]) <<  8) |
                             uint32_t(pf->bytes[o + 3]);
                        float f; std::memcpy(&f, &u, 4); return f;
                    };
                    for (const auto& vf : pf->virtual_fixups) {
                        if (vf.classnames_offset !=
                            rb_class->classnames_offset) continue;
                        const size_t base = pf->data_section.absolute_data_start
                                          + vf.data_offset;
                        if (base + 12 > pf->bytes.size()) break;
                        const size_t end_ok =
                            std::min(base + 0x200, pf->bytes.size());
                        for (size_t q = base; q + 12 <= end_ok; q += 4) {
                            float x = rdf32(q);
                            float y = rdf32(q + 4);
                            float z = rdf32(q + 8);
                            if (looks_world_pos(x, y, z)) {
                                ++with_world_pos;
                                std::ostringstream osp;
                                osp << "    [probe " << with_world_pos
                                    << "] " << name << "  vec3 @+0x"
                                    << std::hex << (q - base) << std::dec
                                    << " = (" << x << ", " << y << ", "
                                    << z << ")";
                                world_pos_hits.push_back(osp.str());
                                break;
                            }
                        }
                        break;
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

            std::ostringstream osp;
            osp << "  world-pos probe: " << with_world_pos << " / "
                << probed << " HKX have a world-scale vec3 in their first "
                << "rigid body (diagnostic only — no placements emitted)";
            if (with_world_pos > 0) OutputLog::info(osp.str());
            else                     OutputLog::info(osp.str());
            for (const auto& line : world_pos_hits) {
                OutputLog::info(line);
            }

            std::vector<std::pair<std::string, size_t>> ext_sorted(
                streaming_extensions.begin(), streaming_extensions.end());
            std::sort(ext_sorted.begin(), ext_sorted.end(),
                      [](const auto& a, const auto& b){
                          return a.second > b.second;
                      });
            OutputLog::info("  streaming BNK file extensions:");
            for (const auto& [ext, n] : ext_sorted) {
                auto& sizes = ext_size_samples[ext];
                size_t mn = sizes.empty() ? 0 : *std::min_element(sizes.begin(), sizes.end());
                size_t mx = sizes.empty() ? 0 : *std::max_element(sizes.begin(), sizes.end());
                size_t total = 0;
                for (auto s : sizes) total += s;
                size_t avg = sizes.empty() ? 0 : total / sizes.size();

                std::ostringstream osx;
                osx << "    " << ext << "  ×" << n
                    << "  sizes: min=" << mn << " avg=" << avg << " max=" << mx;
                OutputLog::info(osx.str());

                if (ext != ".hkx" && ext_sample_idx.count(ext)) {
                    int idx = ext_sample_idx[ext];
                    try {
                        auto bytes = bnk.reader->extract_index_bytes(idx);
                        const size_t dump_n = std::min<size_t>(bytes.size(), 128);
                        OutputLog::info("      sample: " + ext_sample_name[ext]
                                        + "  (" + std::to_string(bytes.size())
                                        + " bytes)");
                        for (size_t off = 0; off < dump_n; off += 16) {
                            std::ostringstream lineh, linea;
                            lineh << "        +0x" << std::hex
                                  << std::setw(3) << std::setfill('0')
                                  << off << "  ";
                            for (size_t k = 0; k < 16 && off + k < dump_n; ++k) {
                                lineh << std::setw(2) << std::setfill('0')
                                      << (unsigned)bytes[off + k] << " ";
                                unsigned char c = bytes[off + k];
                                linea << (char)((c >= 32 && c < 127) ? c : '.');
                            }
                            OutputLog::info(lineh.str() + "  " + linea.str());
                        }
                    } catch (...) {
                        OutputLog::warn("      (failed to extract sample)");
                    }
                }
            }
        } catch (const std::exception& ex) {
            OutputLog::warn(std::string("streaming bnk scan failed: ") + ex.what());
        }
    }

    if (bail_if_cancelled("pre-heightfield")) return false;

    if (!res.ehf_path.empty() || !res.ghf_path.empty()) {
        HeightfieldFiles hf;
        progress_update(32, 100, "Loading heightfield files...");
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
                << " → " << hf.ghf_bytes_raw.size() << "B (raw)";
            OutputLog::success(hos.str());

            if (hf.ehf_header.ok) {
                const auto& h = hf.ehf_header;
                std::ostringstream eos;
                eos << "  .ehf header (63B): magic=\"" << h.magic
                    << "\"  version=" << h.version
                    << "\n    floats: f0=" << h.f0 << "  f1=" << h.f1
                    << "  f2=" << h.f2 << "  f3=" << h.f3 << "  f4=" << h.f4
                    << "\n    u0=" << h.u0 << "  u1=" << h.u1
                    << "\n    body: offset=0x" << std::hex << h.body_offset
                    << "  size=" << std::dec << h.body_size << "B"
                    << "  (file=" << hf.ehf_bytes.size() << "B"
                    << ", end=0x" << std::hex
                    << (h.body_offset + h.body_size) << std::dec << ")";
                OutputLog::info(eos.str());

                const uint64_t body_end =
                    uint64_t(h.body_offset) + uint64_t(h.body_size);
                if (body_end > hf.ehf_bytes.size()) {
                    std::ostringstream wos;
                    wos << "  .ehf body extent (" << body_end
                        << "B) exceeds file size (" << hf.ehf_bytes.size()
                        << "B) — header layout may be wrong";
                    OutputLog::warn(wos.str());
                } else {
                    const size_t dump_start = h.body_offset;
                    const size_t dump_end   = std::min<size_t>(
                        h.body_offset + 64, hf.ehf_bytes.size());
                    std::ostringstream dos;
                    dos << "  .ehf body[0..63]:";
                    for (size_t i = dump_start; i < dump_end; ++i) {
                        if ((i - dump_start) % 16 == 0) {
                            dos << "\n    +0x" << std::hex
                                << std::setw(3) << std::setfill('0')
                                << (i - dump_start) << "  ";
                        }
                        dos << std::hex << std::setw(2)
                            << std::setfill('0')
                            << int(hf.ehf_bytes[i]) << ' ';
                    }
                    OutputLog::info(dos.str());
                }
            } else {
                OutputLog::warn("  .ehf header missing or invalid (need 63B + magic)");
            }

            if (!hf.ghf_bytes_raw.empty()) {
                GhfHeights hg;
                progress_update(45, 100, "Decoding height grid...");
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
                        tos << "  .ghf tile_size was 0 — using .ehf f2 = "
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
                    progress_update(58, 100, "Building terrain mesh...");
                    if (S.cancel_requested.load()) {
                        OutputLog::warn("level load cancelled before terrain mesh build");
                        return false;
                    }
                    if (!BuildTerrainMesh(hg, mesh)) {
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
                        const std::string main_ehf_norm = norm_path(res.ehf_path);
                        for (const auto& adj_ehf_path : all_ehf_refs) {
                            if (norm_path(adj_ehf_path) == main_ehf_norm) continue;
                            const std::string adj_ghf_path = with_ext(adj_ehf_path, ".ghf");
                            if (!Level::FindHeightfieldByPath(adj_ghf_path)) {
                                OutputLog::info("adjacent terrain skipped (no .ghf): " +
                                                adj_ehf_path);
                                continue;
                            }

                            HeightfieldFiles adj_hf;
                            if (!LoadHeightfieldFiles(adj_ehf_path, adj_ghf_path,
                                                      {}, {}, adj_hf)) {
                                OutputLog::warn("adjacent terrain load failed: " +
                                                adj_ehf_path + " (" + adj_hf.error + ")");
                                continue;
                            }
                            GhfHeights adj_hg;
                            if (!DecodeGhfHeights(adj_hf.ghf_bytes_raw, adj_hg)) {
                                OutputLog::warn("adjacent terrain .ghf decode failed: " +
                                                adj_ghf_path + " (" + adj_hg.error + ")");
                                continue;
                            }
                            if (adj_hg.tile_size <= 0.0f) {
                                const float ehf_tile = adj_hf.ehf_header.ok
                                    ? adj_hf.ehf_header.f2 : 0.0f;
                                adj_hg.tile_size =
                                    (ehf_tile > 0.0f && std::isfinite(ehf_tile))
                                        ? ehf_tile : hg.tile_size;
                            }

                            if (S.cancel_requested.load()) {
                                OutputLog::warn("level load cancelled during adjacent terrain loop");
                                return false;
                            }
                            TerrainMesh adj_mesh;
                            if (!BuildTerrainMesh(adj_hg, adj_mesh)) {
                                OutputLog::warn("adjacent terrain mesh build failed: " +
                                                adj_ehf_path);
                                continue;
                            }

                            EhfParsedBody adj_body;
                            if (ParseEhfBody(adj_hf.ehf_bytes, adj_body) &&
                                !adj_body.chunks.empty()) {
                                float min_x = 1e30f, min_z = 1e30f;
                                for (const auto& c : adj_body.chunks) {
                                    min_x = std::min(min_x, c.origin[0]);
                                    min_z = std::min(min_z, c.origin[1]);
                                }
                                if (std::isfinite(min_x) && std::isfinite(min_z) &&
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

                            Level::PendingAdjacentTerrain adj;
                            adj.label = std::filesystem::path(adj_ehf_path)
                                            .filename().string();
                            adj.preferred_bnk = g_pending_terrain_level_entry.bnk_path;
                            adj.ehf_bytes = std::move(adj_hf.ehf_bytes);
                            adj.mesh = std::move(adj_mesh);
                            g_pending_adjacent_terrain_meshes.push_back(std::move(adj));
                        }
                        if (!g_pending_adjacent_terrain_meshes.empty()) {
                            OutputLog::success("adjacent terrain meshes loaded: " +
                                std::to_string(g_pending_adjacent_terrain_meshes.size()));
                        }

                        g_pending_terrain_ghf_payload   = hf.ghf_bytes_raw;
                        g_pending_terrain_ghf_heights   = hg.heights;
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

                        // Probe the .water sibling that pairs with this
                        // heightfield. We replace the .ghf extension with
                        // .water and ask the same BNK to extract it.
                        g_pending_level_water_present = false;
                        g_pending_level_water_scene = Level::WaterScene{};
                        if (!res.ghf_path.empty()) {
                            std::filesystem::path wp = res.ghf_path;
                            wp.replace_extension(".water");
                            std::string water_path = wp.string();
                            std::vector<uint8_t> water_bytes;
                            if (load_text_sibling(water_path, water_bytes) &&
                                !water_bytes.empty())
                            {
                                Level::WaterScene scene;
                                if (Level::ParseWaterFile(water_bytes, scene)) {
                                    size_t total_tiles = 0;
                                    for (const auto& b : scene.bodies)
                                        total_tiles += b.tiles.size();
                                    OutputLog::success(
                                        ".water parsed: " +
                                        std::to_string(scene.bodies.size()) +
                                        " bodies, " +
                                        std::to_string(total_tiles) + " tiles");
                                    g_pending_level_water_scene = std::move(scene);
                                    g_pending_level_water_present = true;
                                } else {
                                    OutputLog::warn(
                                        ".water sibling found but failed to parse");
                                }
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
                        g_pending_terrain_load        = true;

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

                        try {
                            std::filesystem::path dump =
                                std::filesystem::path("extracted") /
                                ("debug_" +
                                 std::filesystem::path(res.ehf_path)
                                     .filename().string());
                            std::ofstream f(dump, std::ios::binary);
                            if (f) {
                                f.write(reinterpret_cast<const char*>(hf.ehf_bytes.data()),
                                        (std::streamsize)hf.ehf_bytes.size());
                                OutputLog::info("debug dump: " + dump.string()
                                                + "  ("
                                                + std::to_string(hf.ehf_bytes.size())
                                                + " bytes)");
                            }
                        } catch (...) {}

                    }
                }
            }
        }
    } else {
        OutputLog::warn("no .ehf or .ghf path in level — can't load terrain");
    }

    return true;
}

bool RenderHeightmapToRGBA(const FlatAssetEntry& entry,
                           std::vector<uint8_t>& out_rgba,
                           int&                  out_w,
                           int&                  out_h)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;

    std::filesystem::path lp = entry.full_path;
    lp.replace_extension(".list");
    std::string list_full = lp.string();
    std::string list_key  = list_full;
    std::transform(list_key.begin(), list_key.end(), list_key.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    std::replace(list_key.begin(), list_key.end(), '\\', '/');

    int list_idx = BnkCache::find_index(entry.bnk_path, list_key);
    if (list_idx < 0) {
        OutputLog::error("View Heightmap: no companion .list ("
                         + list_full + ") in BNK");
        return false;
    }

    std::vector<uint8_t> list_bytes;
    try {
        list_bytes = BnkCache::extract_bytes(entry.bnk_path, list_idx);
    } catch (...) {
        OutputLog::error("View Heightmap: failed to extract .list");
        return false;
    }
    std::string list_str(reinterpret_cast<const char*>(list_bytes.data()),
                         list_bytes.size());

    std::string ghf_path;
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
        if (low.size() >= 4 && low.compare(low.size()-4, 4, ".ghf") == 0) {
            ghf_path = line;
            break;
        }
    }
    if (ghf_path.empty()) {
        OutputLog::error("View Heightmap: no .ghf entry in .list");
        return false;
    }

    HeightfieldFiles hf;
    if (!LoadHeightfieldFiles({}, ghf_path, {}, {}, hf)) {
        OutputLog::error("View Heightmap: .ghf load failed: " + hf.error);
        return false;
    }

    GhfHeights hg;
    if (!DecodeGhfHeights(hf.ghf_bytes_raw, hg)) {
        OutputLog::error("View Heightmap: .ghf decode failed: " + hg.error);
        return false;
    }

    const float lo   = hg.min_height;
    const float hi   = hg.max_height;
    const float span = (hi > lo) ? (hi - lo) : 1.f;

    out_w = static_cast<int>(hg.width);
    out_h = static_cast<int>(hg.height);
    out_rgba.resize(static_cast<size_t>(out_w) * static_cast<size_t>(out_h) * 4);

    for (size_t i = 0; i < hg.heights.size(); ++i) {
        float t = (hg.heights[i] - lo) / span;
        if (t < 0.f) t = 0.f;
        if (t > 1.f) t = 1.f;
        const uint8_t v = static_cast<uint8_t>(t * 255.0f + 0.5f);
        out_rgba[i * 4 + 0] = v;
        out_rgba[i * 4 + 1] = v;
        out_rgba[i * 4 + 2] = v;
        out_rgba[i * 4 + 3] = 0xFF;
    }

    return true;
}

bool DecodeLevelTextureAtlas(const FlatAssetEntry& level_entry,
                             std::vector<uint8_t>& out_rgba,
                             int&                  out_w,
                             int&                  out_h)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;

    std::filesystem::path atlas_path = level_entry.full_path;
    atlas_path.replace_extension(".texture_atlas");
    const std::string atlas_full = atlas_path.string();

    std::string atlas_key = atlas_full;
    std::transform(atlas_key.begin(), atlas_key.end(), atlas_key.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    std::replace(atlas_key.begin(), atlas_key.end(), '\\', '/');

    auto try_bnk = [&](const std::string& bnk_path,
                       std::vector<uint8_t>& out_blob) -> bool {
        int idx = BnkCache::find_index(bnk_path, atlas_key);
        if (idx < 0) return false;
        try {
            auto v = BnkCache::extract_bytes(bnk_path, idx);
            if (v.empty()) return false;
            out_blob.assign(v.begin(), v.end());
            return true;
        } catch (...) {
            return false;
        }
    };

    std::vector<uint8_t> blob;
    bool found = try_bnk(level_entry.bnk_path, blob);

    if (!found) {
        const std::string base_lower = std::filesystem::path(atlas_full)
                                           .filename().string();
        std::string base_low = base_lower;
        std::transform(base_low.begin(), base_low.end(), base_low.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        for (const auto& fe : S.all_heightfield_files) {
            std::string nlow = fe.name;
            std::transform(nlow.begin(), nlow.end(), nlow.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            if (nlow != base_low) continue;
            try {
                auto v = BnkCache::extract_bytes(fe.bnk_path, fe.file_index);
                if (!v.empty()) {
                    blob.assign(v.begin(), v.end());
                    found = true;
                    break;
                }
            } catch (...) {}
        }
    }

    if (!found) {
        for (const auto& bnk_path : S.bnk_paths) {
            if (bnk_path == level_entry.bnk_path) continue;
            if (try_bnk(bnk_path, blob)) { found = true; break; }
        }
    }
    if (!found) {
        OutputLog::warn("texture_atlas: no '" + atlas_full +
                        "' found in any loaded BNK");
        return false;
    }

    TextureAtlas::DecodedAtlas dec = TextureAtlas::DecodeAtlas(blob);
    if (!dec.ok) {
        OutputLog::error("texture_atlas: " + dec.error +
                         "  (file=" + atlas_full + ")");
        return false;
    }
    out_rgba = std::move(dec.rgba);
    out_w    = dec.width;
    out_h    = dec.height;
    return true;
}

namespace {

struct EhfRenderTileDesc {
    uint32_t cell_x = 0;
    uint32_t cell_y = 0;
    uint32_t cell_w = 0;
    uint32_t cell_h = 0;
    uint32_t sub_w  = 0;
    uint32_t sub_h  = 0;
};

struct EhfEmbeddedBc1Mip {
    size_t   offset = 0;
    uint32_t header_w = 0;
    uint32_t header_h = 0;
    uint32_t raw_size = 0;
    uint32_t comp_size = 0;
};

static uint32_t ehf_be32(const std::vector<uint8_t>& d, size_t off)
{
    return (uint32_t(d[off + 0]) << 24) |
           (uint32_t(d[off + 1]) << 16) |
           (uint32_t(d[off + 2]) <<  8) |
            uint32_t(d[off + 3]);
}

static bool ehf_skip_tex_blob(const std::vector<uint8_t>& ehf,
                              size_t limit,
                              size_t& pos)
{
    if (pos + 0x60 > limit) return false;
    if (ehf_be32(ehf, pos) != 0xFFFFFFFEu) return false;

    const uint32_t pf = ehf_be32(ehf, pos + 0x18);
    const uint32_t mt = ehf_be32(ehf, pos + 0x20);
    if (mt < 0x54 || mt > 0x200) return false;

    const size_t table = pos + mt;
    if (table + 8 > limit) return false;
    const uint32_t raw_size  = ehf_be32(ehf, table);
    const uint32_t comp_size = ehf_be32(ehf, table + 4);
    const size_t next = (pf == 98u)
        ? table + 4 + size_t(raw_size)
        : table + 8 + size_t(comp_size);
    if (next > limit) return false;
    pos = next;
    return true;
}

static bool parse_ehf_render_tiles(const std::vector<uint8_t>& ehf,
                                   uint32_t terrain_cells_w,
                                   std::vector<EhfRenderTileDesc>& out)
{
    out.clear();
    if (ehf.size() < 63) return false;
    const uint32_t body_off  = ehf_be32(ehf, 55);
    const uint32_t body_size = ehf_be32(ehf, 59);
    const size_t body_end = size_t(body_off) + size_t(body_size);
    if (body_end > ehf.size()) return false;

    size_t pos = body_off;
    if (!ehf_skip_tex_blob(ehf, body_end, pos)) return false;
    if (!ehf_skip_tex_blob(ehf, body_end, pos)) return false;
    if (pos + 8 > body_end) return false;

    pos += 4;
    const uint32_t count = ehf_be32(ehf, pos);
    pos += 4;
    if (count == 0 || count > 4096) return false;

    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (pos + 16 > body_end) return false;
        EhfRenderTileDesc t;
        t.cell_w = ehf_be32(ehf, pos + 0);
        t.cell_h = ehf_be32(ehf, pos + 4);
        t.sub_w  = ehf_be32(ehf, pos + 8);
        t.sub_h  = ehf_be32(ehf, pos + 12);
        pos += 16;
        if (t.cell_w == 0 || t.cell_h == 0 ||
            t.sub_w == 0 || t.sub_h == 0 ||
            t.sub_w > 1024 || t.sub_h > 1024)
        {
            return false;
        }
        const size_t grid_bytes =
            size_t(t.sub_w) * size_t(t.sub_h) * 160u + 24u;
        if (pos + grid_bytes > body_end) return false;
        pos += grid_bytes;
        out.push_back(t);
    }

    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t row_h = 0;
    for (EhfRenderTileDesc& t : out) {
        if (terrain_cells_w > 0 &&
            x > 0 &&
            x + t.cell_w > terrain_cells_w)
        {
            y += row_h;
            x = 0;
            row_h = 0;
        }
        t.cell_x = x;
        t.cell_y = y;
        x += t.cell_w;
        row_h = std::max(row_h, t.cell_h);
        if (terrain_cells_w > 0 && x >= terrain_cells_w) {
            y += row_h;
            x = 0;
            row_h = 0;
        }
    }
    return true;
}

static std::vector<EhfEmbeddedBc1Mip>
collect_ehf_embedded_bc1_primaries(const std::vector<uint8_t>& ehf)
{
    std::vector<EhfEmbeddedBc1Mip> all;
    if (ehf.size() < 63) return all;
    const uint32_t body_off  = ehf_be32(ehf, 55);
    const uint32_t body_size = ehf_be32(ehf, 59);
    const size_t body_end = size_t(body_off) + size_t(body_size);
    if (body_end > ehf.size()) return all;

    for (size_t i = body_end; i + 0x60 < ehf.size(); ++i) {
        if (ehf[i] != 0xFF || ehf[i + 1] != 0xFF ||
            ehf[i + 2] != 0xFF || ehf[i + 3] != 0xFE) continue;
        const uint32_t w  = ehf_be32(ehf, i + 0x10);
        const uint32_t h  = ehf_be32(ehf, i + 0x14);
        const uint32_t pf = ehf_be32(ehf, i + 0x18);
        const uint32_t mt = ehf_be32(ehf, i + 0x20);
        if (pf != 35u || w == 0 || h == 0 ||
            w > 8192 || h > 8192 ||
            mt < 0x54 || mt > 0x200) {
            continue;
        }
        const size_t table = i + mt;
        if (table + 8 > ehf.size()) continue;
        const uint32_t raw_size  = ehf_be32(ehf, table);
        const uint32_t comp_size = ehf_be32(ehf, table + 4);
        const size_t zlib_at = table + 8;
        if (comp_size < 2 || zlib_at + size_t(comp_size) > ehf.size()) continue;
        if (ehf[zlib_at] != 0x78) continue;
        all.push_back({i, w, h, raw_size, comp_size});
    }

    std::vector<EhfEmbeddedBc1Mip> primaries;
    for (size_t i = 0; i < all.size();) {
        if (i + 2 < all.size() &&
            all[i + 1].header_w * 2u == all[i].header_w &&
            all[i + 2].header_w * 4u == all[i].header_w)
        {
            primaries.push_back(all[i]);
            i += 3;
        } else {
            ++i;
        }
    }
    return primaries;
}

static bool decode_ehf_embedded_bc1(const std::vector<uint8_t>& ehf,
                                    const EhfEmbeddedBc1Mip& mip,
                                    std::vector<uint8_t>& rgba,
                                    int& w,
                                    int& h)
{
    rgba.clear();
    w = 0;
    h = 0;
    const uint32_t mt = ehf_be32(ehf, mip.offset + 0x20);
    const size_t table = mip.offset + mt;
    const size_t zlib_at = table + 8;
    if (zlib_at + size_t(mip.comp_size) > ehf.size()) return false;

    std::vector<uint8_t> body(mip.raw_size);
    z_stream zs{};
    zs.next_in   = const_cast<Bytef*>(ehf.data() + zlib_at);
    zs.avail_in  = (uInt)mip.comp_size;
    zs.next_out  = body.data();
    zs.avail_out = (uInt)mip.raw_size;
    const int rc_init = inflateInit2(&zs, 15);
    const int rc = (rc_init == Z_OK) ? inflate(&zs, Z_FINISH) : Z_ERRNO;
    const size_t produced = size_t(mip.raw_size) - size_t(zs.avail_out);
    inflateEnd(&zs);
    if (rc_init != Z_OK || rc != Z_STREAM_END || produced != mip.raw_size) {
        return false;
    }

    std::vector<uint8_t> bc1;
    std::string err;
    if (!lh_decode_compressed_mip(body.data(), body.size(),
                                  w, h, bc1, &err,
                                  false)) {
        return false;
    }
    return TextureAtlas::DecodeRawBc1ToRgba(bc1.data(), bc1.size(),
                                            w, h, rgba);
}

static void blit_resampled_rgba(const std::vector<uint8_t>& src,
                                int src_w,
                                int src_h,
                                std::vector<uint8_t>& dst,
                                int dst_w,
                                int dst_h,
                                int dst_x,
                                int dst_y,
                                int copy_w,
                                int copy_h)
{
    if (src.empty() || src_w <= 0 || src_h <= 0 ||
        dst.empty() || dst_w <= 0 || dst_h <= 0 ||
        copy_w <= 0 || copy_h <= 0) return;

    const int clipped_w = std::min(copy_w, dst_w - dst_x);
    const int clipped_h = std::min(copy_h, dst_h - dst_y);
    if (dst_x < 0 || dst_y < 0 || clipped_w <= 0 || clipped_h <= 0) return;

    for (int y = 0; y < clipped_h; ++y) {
        const float sy = (float(y) + 0.5f) * float(src_h) / float(copy_h) - 0.5f;
        const int y0 = std::clamp(int(std::floor(sy)), 0, src_h - 1);
        const int y1 = std::min(y0 + 1, src_h - 1);
        const float fy = std::clamp(sy - float(y0), 0.0f, 1.0f);
        for (int x = 0; x < clipped_w; ++x) {
            const float sx = (float(x) + 0.5f) * float(src_w) / float(copy_w) - 0.5f;
            const int x0 = std::clamp(int(std::floor(sx)), 0, src_w - 1);
            const int x1 = std::min(x0 + 1, src_w - 1);
            const float fx = std::clamp(sx - float(x0), 0.0f, 1.0f);

            const uint8_t* p00 = src.data() + (size_t(y0) * src_w + x0) * 4;
            const uint8_t* p10 = src.data() + (size_t(y0) * src_w + x1) * 4;
            const uint8_t* p01 = src.data() + (size_t(y1) * src_w + x0) * 4;
            const uint8_t* p11 = src.data() + (size_t(y1) * src_w + x1) * 4;
            uint8_t* out = dst.data() + (size_t(dst_y + y) * dst_w + (dst_x + x)) * 4;
            const float w00 = (1.0f - fx) * (1.0f - fy);
            const float w10 = fx * (1.0f - fy);
            const float w01 = (1.0f - fx) * fy;
            const float w11 = fx * fy;
            for (int c = 0; c < 4; ++c) {
                out[c] = uint8_t(std::clamp(
                    w00 * p00[c] + w10 * p10[c] +
                    w01 * p01[c] + w11 * p11[c],
                    0.0f, 255.0f));
            }
        }
    }
}

static bool DecodeEhfEmbeddedTileComposite(const std::vector<uint8_t>& ehf,
                                           uint32_t terrain_vertices_w,
                                           uint32_t terrain_vertices_h,
                                           std::vector<uint8_t>& out_rgba,
                                           int& out_w,
                                           int& out_h)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;
    const uint32_t cells_w =
        terrain_vertices_w > 1 ? terrain_vertices_w - 1 : terrain_vertices_w;
    const uint32_t cells_h =
        terrain_vertices_h > 1 ? terrain_vertices_h - 1 : terrain_vertices_h;
    if (cells_w == 0 || cells_h == 0) return false;

    std::vector<EhfRenderTileDesc> tiles;
    if (!parse_ehf_render_tiles(ehf, cells_w, tiles) || tiles.empty()) {
        return false;
    }
    std::vector<EhfEmbeddedBc1Mip> primaries =
        collect_ehf_embedded_bc1_primaries(ehf);
    if (primaries.size() < tiles.size()) return false;

    struct DecodedTile {
        bool ok = false;
        std::vector<uint8_t> rgba;
        int w = 0;
        int h = 0;
    };
    std::vector<DecodedTile> decoded(tiles.size());
    std::vector<float> ratios_x;
    std::vector<float> ratios_y;
    size_t ok_count = 0;
    for (size_t i = 0; i < tiles.size(); ++i) {
        DecodedTile dt;
        if (!decode_ehf_embedded_bc1(ehf, primaries[i], dt.rgba, dt.w, dt.h)) {
            decoded[i] = std::move(dt);
            continue;
        }
        dt.ok = true;
        if (tiles[i].cell_w > 0 && tiles[i].cell_h > 0) {
            ratios_x.push_back(float(dt.w) / float(tiles[i].cell_w));
            ratios_y.push_back(float(dt.h) / float(tiles[i].cell_h));
        }
        decoded[i] = std::move(dt);
        ++ok_count;
    }
    if (ok_count < std::max<size_t>(4, tiles.size() / 4)) return false;

    auto median_ratio = [](std::vector<float>& v) -> float {
        if (v.empty()) return 1.0f;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    const float median_x = median_ratio(ratios_x);
    const float median_y = median_ratio(ratios_y);
    if (median_x < 0.5f || median_y < 0.5f ||
        median_x > 8.0f || median_y > 8.0f)
    {
        std::ostringstream os;
        os << "ehf: embedded BC1 tile pages look strip-like "
           << "(median scale=" << median_x << "x" << median_y
           << "), skipping as terrain albedo";
        OutputLog::info(os.str());
        return false;
    }

    const int scale_x = std::clamp(int(std::lround(median_x)), 1, 8);
    const int scale_y = std::clamp(int(std::lround(median_y)), 1, 8);

    out_w = int(cells_w) * scale_x;
    out_h = int(cells_h) * scale_y;
    if (out_w <= 0 || out_h <= 0 || out_w > 8192 || out_h > 8192) {
        return false;
    }
    out_rgba.assign(size_t(out_w) * size_t(out_h) * 4, 0);
    for (size_t i = 3; i < out_rgba.size(); i += 4) {
        out_rgba[i] = 0xFF;
    }

    for (size_t i = 0; i < tiles.size(); ++i) {
        const DecodedTile& dt = decoded[i];
        if (!dt.ok) continue;
        const EhfRenderTileDesc& t = tiles[i];
        const int dx = int(t.cell_x) * scale_x;
        const int dy = int(t.cell_y) * scale_y;
        const int dw = int(t.cell_w) * scale_x;
        const int dh = int(t.cell_h) * scale_y;
        blit_resampled_rgba(dt.rgba, dt.w, dt.h,
                            out_rgba, out_w, out_h,
                            dx, dy, dw, dh);
    }

    std::ostringstream os;
    os << "ehf: embedded tile composite " << out_w << "x" << out_h
       << " from " << ok_count << "/" << tiles.size()
       << " tile texture pages (scale=" << scale_x << "x" << scale_y << ")";
    OutputLog::success(os.str());
    return true;
}

}

bool DecodeEhfTerrainAlbedoFromBytes(const std::vector<uint8_t>& ehf,
                                     uint32_t              cells_w,
                                     uint32_t              cells_h,
                                     std::vector<uint8_t>& out_rgba,
                                     int&                  out_w,
                                     int&                  out_h)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;
    if (ehf.empty() || cells_w == 0 || cells_h == 0) return false;

    if (DecodeEhfEmbeddedTileComposite(ehf, cells_w, cells_h,
                                       out_rgba, out_w, out_h)) {
        return true;
    }

    const uint8_t* eh_d = ehf.data();
    const size_t   eh_n = ehf.size();
    size_t  best_off = SIZE_MAX;
    uint32_t best_W = 0, best_H = 0;
    uint32_t best_raw = 0;

    auto u32_at = [&](size_t off) -> uint32_t {
        return (uint32_t(eh_d[off  ]) << 24) | (uint32_t(eh_d[off+1]) << 16) |
               (uint32_t(eh_d[off+2]) <<  8) |  uint32_t(eh_d[off+3]);
    };

    for (size_t i = 0; i + 84 < eh_n; ++i) {
        if (eh_d[i] != 0xFF || eh_d[i+1] != 0xFF ||
            eh_d[i+2] != 0xFF || eh_d[i+3] != 0xFE) continue;

        const uint32_t W  = u32_at(i + 16);
        const uint32_t H  = u32_at(i + 20);
        const uint32_t PF = u32_at(i + 24);
        const uint32_t mip_off = u32_at(i + 32);
        if (W == 0 || H == 0 || W > 8192 || H > 8192) continue;
        if (PF != 35u) continue;
        if (mip_off != 0x54) continue;

        if (i + mip_off + 4 > eh_n) continue;
        const uint32_t raw_size = u32_at(i + mip_off);
        if (raw_size > best_raw) {
            best_raw  = raw_size;
            best_off  = i;
            best_W = W; best_H = H;
        }
    }

    if (best_off != SIZE_MAX) {
        auto u32 = [&](size_t off) -> uint32_t {
            return (uint32_t(eh_d[off  ]) << 24) | (uint32_t(eh_d[off+1]) << 16) |
                   (uint32_t(eh_d[off+2]) <<  8) |  uint32_t(eh_d[off+3]);
        };
        const uint32_t mip_table_offset = u32(best_off + 32);
        const size_t mip_at = best_off + mip_table_offset;
        if (mip_at + 8 < eh_n) {
            const uint32_t raw_size  = u32(mip_at);
            const uint32_t comp_size = u32(mip_at + 4);
            const size_t   zlib_at   = mip_at + 8;

            if (zlib_at + comp_size <= eh_n) {
                std::vector<uint8_t> body(raw_size);
                z_stream zs{};
                zs.next_in   = const_cast<Bytef*>(eh_d + zlib_at);
                zs.avail_in  = (uInt)comp_size;
                zs.next_out  = body.data();
                zs.avail_out = (uInt)raw_size;
                int rc_init = inflateInit2(&zs, 15);
                int rc      = (rc_init == Z_OK) ? inflate(&zs, Z_FINISH) : Z_ERRNO;
                const size_t produced = raw_size - zs.avail_out;
                inflateEnd(&zs);

                if (rc_init == Z_OK && produced == raw_size) {
                    std::vector<uint8_t> bc1;
                    int dec_w = 0, dec_h = 0;
                    std::string err;
                    if (lh_decode_compressed_mip(body.data(), body.size(),
                                                 dec_w, dec_h, bc1, &err,
                                                 false))
                    {
                        std::vector<uint8_t> rgba;
                        if (TextureAtlas::DecodeRawBc1ToRgba(
                                bc1.data(), bc1.size(),
                                dec_w, dec_h, rgba))
                        {
                            const uint32_t terrain_cells_w =
                                cells_w > 1 ? cells_w - 1 : cells_w;
                            const uint32_t terrain_cells_h =
                                cells_h > 1 ? cells_h - 1 : cells_h;
                            const size_t terrain_area =
                                size_t(terrain_cells_w) *
                                size_t(terrain_cells_h);
                            const size_t decoded_area =
                                size_t(dec_w) * size_t(dec_h);
                            if (decoded_area < terrain_area / 2) {
                                std::ostringstream os;
                                os << "ehf: embedded BC1 page @0x"
                                   << std::hex << best_off << std::dec
                                   << " decoded as " << dec_w << "x" << dec_h
                                   << ", too small for full terrain";
                                OutputLog::info(os.str());
                            } else {
                                out_rgba = std::move(rgba);
                                out_w    = dec_w;
                                out_h    = dec_h;
                                std::ostringstream os;
                                os << "ehf: huffman BC1 baked albedo @0x"
                                   << std::hex << best_off << std::dec
                                   << "  header=" << best_W << "x" << best_H
                                   << "  decoded=" << dec_w << "x" << dec_h;
                                OutputLog::success(os.str());
                                return true;
                            }
                        }
                    } else {
                        OutputLog::warn("ehf: lh_decode_compressed_mip failed: "
                                        + err);
                    }
                } else {
                    std::ostringstream os;
                    os << "ehf: zlib inflate failed rc=" << rc
                       << " produced=" << produced << " of " << raw_size;
                    OutputLog::warn(os.str());
                }
            }
        }
    }

    auto round_up_pow2 = [](uint32_t n) {
        uint32_t p = 1; while (p < n) p <<= 1; return p;
    };
    const uint32_t pow2_W = round_up_pow2(cells_w);
    const uint32_t pow2_H = round_up_pow2(cells_h);

    struct Cand { uint32_t W, H; size_t bytes; };
    std::vector<Cand> cands;
    auto add = [&](uint32_t w, uint32_t h) {
        if (w == 0 || h == 0) return;
        if ((w & 3u) != 0 || (h & 3u) != 0) return;
        cands.push_back({w, h, (size_t)w * h / 2});
    };
    add(pow2_W, pow2_H);
    add(cells_w & ~3u, cells_h & ~3u);
    add(1024, 1024);
    add(1024,  768);
    add( 768, 1024);
    add(1024,  512);
    add( 512, 1024);
    add( 768,  768);
    add( 512,  512);
    add( 256,  256);
    const float terrain_aspect = (float)cells_w / (float)cells_h;
    std::sort(cands.begin(), cands.end(),
              [](const Cand& a, const Cand& b){ return a.bytes > b.bytes; });
    cands.erase(std::unique(cands.begin(), cands.end(),
        [](const Cand& a, const Cand& b){
            return a.W == b.W && a.H == b.H; }), cands.end());
    auto aspect_ok = [&](uint32_t w, uint32_t h) -> bool {
        float a = (float)w / (float)h;
        return a > terrain_aspect * 0.25f && a < terrain_aspect * 4.0f;
    };

    const size_t n = ehf.size();
    const uint8_t* d = ehf.data();
    auto u32be = [](const uint8_t* p) -> uint32_t {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
               (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
    };

    struct Hit { uint32_t W, H; size_t bytes; size_t offset; uint32_t comp; };
    std::vector<Hit> hits;
    size_t i = 8;
    while (i + 2 < n) {
        if (d[i] == 0x78 &&
            (d[i+1] == 0xDA || d[i+1] == 0x9C ||
             d[i+1] == 0x01 || d[i+1] == 0x5E))
        {
            const uint32_t rs = u32be(d + i - 8);
            const uint32_t cs = u32be(d + i - 4);
            if (cs > 16 && (size_t)i + cs <= n) {
                for (const auto& c : cands) {
                    if (rs == (uint32_t)c.bytes &&
                        aspect_ok(c.W, c.H))
                    {
                        hits.push_back({c.W, c.H, c.bytes, i, cs});
                        break;
                    }
                }
            }
        }
        ++i;
    }
    if (hits.empty()) {
        OutputLog::warn("ehf: no BC1 section matching any candidate (tried " +
                        std::to_string(cands.size()) + " sizes) found in " +
                        std::to_string(n) + "-byte .ehf");
        return false;
    }
    std::sort(hits.begin(), hits.end(),
              [](const Hit& a, const Hit& b){ return a.bytes > b.bytes; });
    const Hit& best = hits.front();

    const size_t per_cell_bytes = (size_t)(cells_w & ~3u) *
                                  (size_t)(cells_h & ~3u) / 2;
    {
        std::ostringstream os;
        os << "ehf: " << hits.size() << " BC1 candidate(s); picked "
           << best.W << "x" << best.H << " BC1 @0x" << std::hex
           << best.offset;
        OutputLog::info(os.str());
        if (best.bytes < per_cell_bytes / 2) {
            OutputLog::warn("ehf: picked page too small to be per-cell"
                            " baked albedo — falling back to atlas");
            return false;
        }
    }
    std::vector<uint8_t> rgba;
    if (!TextureAtlas::DecodeZlibBc1Page(d + best.offset, best.comp,
                                         best.bytes, (int)best.W, (int)best.H,
                                         rgba)) {
        OutputLog::warn("ehf: candidate at 0x" +
                        std::to_string((unsigned long long)best.offset) +
                        " (" + std::to_string(best.W) + "x" +
                        std::to_string(best.H) + " BC1) failed to decode");
        return false;
    }
    out_rgba = std::move(rgba);
    out_w    = (int)best.W;
    out_h    = (int)best.H;
    return true;
}

bool DecodeEhfTerrainAlbedo(const FlatAssetEntry& level_entry,
                            uint32_t              cells_w,
                            uint32_t              cells_h,
                            std::vector<uint8_t>& out_rgba,
                            int&                  out_w,
                            int&                  out_h)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;

    if (!g_pending_terrain_ehf_bytes.empty() &&
        g_pending_terrain_level_entry.full_path == level_entry.full_path)
    {
        return DecodeEhfTerrainAlbedoFromBytes(
            g_pending_terrain_ehf_bytes,
            cells_w, cells_h, out_rgba, out_w, out_h);
    }

    for (const auto& fe : S.all_heightfield_files) {
        std::string nlow = fe.name;
        std::transform(nlow.begin(), nlow.end(), nlow.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (nlow.size() < 4 ||
            nlow.compare(nlow.size() - 4, 4, ".ehf") != 0) continue;
        try {
            auto v = BnkCache::extract_bytes(fe.bnk_path, fe.file_index);
            if (v.empty()) continue;
            std::vector<uint8_t> blob(v.begin(), v.end());
            if (DecodeEhfTerrainAlbedoFromBytes(blob, cells_w, cells_h,
                                                out_rgba, out_w, out_h))
                return true;
        } catch (...) {}
    }
    OutputLog::warn("ehf: no usable .ehf found for level "
                    + level_entry.name);
    return false;
}

bool DecodeEhfPaletteFirstDiffuse(const std::vector<uint8_t>& ehf,
                                  std::vector<uint8_t>& out_rgba,
                                  int&                  out_w,
                                  int&                  out_h,
                                  float&                out_tile_scale,
                                  std::string&          out_picked_name)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;
    out_tile_scale = 1.0f;
    out_picked_name.clear();
    if (ehf.empty()) return false;

    EhfPalette::Palette pal = EhfPalette::Parse(ehf);
    if (!pal.ok || pal.entries.empty()) {
        OutputLog::warn("ehf palette: parse failed or empty");
        return false;
    }

    auto basename_lower = [](const std::string& path) {
        std::string base = std::filesystem::path(path).filename().string();
        std::transform(base.begin(), base.end(), base.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return base;
    };

    OutputLog::info("ehf palette: searching " +
                    std::to_string(S.all_tex_files.size()) +
                    " indexed .tex files for " +
                    std::to_string(pal.entries.size()) +
                    " palette diffuse references...");

    for (size_t pi = 0; pi < pal.entries.size(); ++pi) {
        const auto& e = pal.entries[pi];
        const std::string want = basename_lower(e.diffuse_path);
        if (want.empty()) continue;

        const FlatAssetEntry* hit = nullptr;
        for (const auto& tex : S.all_tex_files) {
            std::string nm = std::filesystem::path(tex.name)
                                 .filename().string();
            std::transform(nm.begin(), nm.end(), nm.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            if (nm == want) { hit = &tex; break; }
        }
        if (!hit) {
            if (pi < 6) {
                OutputLog::info("  [" + std::to_string(pi) +
                                "] not found: " + want);
            }
            continue;
        }

        std::vector<uint8_t> blob;
        try {
            auto v = BnkCache::extract_bytes(hit->bnk_path, hit->file_index);
            if (!v.empty()) blob.assign(v.begin(), v.end());
        } catch (...) {}
        if (blob.empty()) {
            OutputLog::warn("  [" + std::to_string(pi) +
                            "] " + want + " found in " + hit->bnk_path +
                            " but extract returned empty");
            continue;
        }

        std::vector<unsigned char> blob_uc(blob.begin(), blob.end());
        std::vector<uint8_t> rgba;
        bool has_alpha = false;
        int w = 0, h = 0;
        if (!decode_tex_to_rgba(blob_uc, rgba, w, h, &has_alpha, -1)) {
            const std::string& reason = mp_last_decode_fail_reason();
            const std::string& info   = mp_last_decode_info();
            OutputLog::warn("  [" + std::to_string(pi) + "] " + want +
                            " decode failed: " + reason +
                            (info.empty() ? "" : " (" + info + ")"));
            continue;
        }

        out_rgba = std::move(rgba);
        out_w = w;
        out_h = h;
        out_tile_scale = e.tile_scale;
        out_picked_name = basename_lower(e.diffuse_path);
        std::ostringstream os;
        os << "ehf palette: picked entry " << pi << " '" << out_picked_name
           << "' (" << w << "x" << h
           << ", tile_scale=" << e.tile_scale << ")";
        OutputLog::success(os.str());
        return true;
    }

    OutputLog::warn("ehf palette: NONE of " +
                    std::to_string(pal.entries.size()) +
                    " palette diffuse .tex files found in the "
                    + std::to_string(S.all_tex_files.size())
                    + "-entry global .tex index");
    return false;
}

bool BakeEhfTerrainComposite(const std::vector<uint8_t>& ehf,
                             std::vector<uint8_t>&  out_rgba,
                             int&                   out_w,
                             int&                   out_h,
                             std::string&           out_picked_name)
{
    return BakeEhfTerrainCompositeWithBnk(ehf, {},
                                          out_rgba, out_w, out_h,
                                          out_picked_name);
}

namespace { bool g_capture_splat_debug = false;
            std::vector<uint8_t>* g_splat_debug_rgba = nullptr;
            int* g_splat_debug_w = nullptr;
            int* g_splat_debug_h = nullptr; }

bool BakeEhfTerrainCompositeAndSplatDebug(
    const std::vector<uint8_t>& ehf,
    const std::string& preferred_bnk,
    std::vector<uint8_t>& out_rgba,
    int& out_w, int& out_h,
    std::string& out_picked_name,
    std::vector<uint8_t>& out_splat_rgba,
    int& out_splat_w, int& out_splat_h)
{
    g_capture_splat_debug = true;
    g_splat_debug_rgba    = &out_splat_rgba;
    g_splat_debug_w       = &out_splat_w;
    g_splat_debug_h       = &out_splat_h;
    bool ok = BakeEhfTerrainCompositeWithBnk(ehf, preferred_bnk,
                                             out_rgba, out_w, out_h,
                                             out_picked_name);
    g_capture_splat_debug = false;
    g_splat_debug_rgba = nullptr;
    g_splat_debug_w = nullptr;
    g_splat_debug_h = nullptr;
    return ok;
}

bool BakeEhfTerrainCompositeWithBnk(const std::vector<uint8_t>& ehf,
                                    const std::string& preferred_bnk,
                                    std::vector<uint8_t>&  out_rgba,
                                    int&                   out_w,
                                    int&                   out_h,
                                    std::string&           out_picked_name)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;
    out_picked_name.clear();
    if (ehf.empty()) return false;

    HeightfieldHeader hdr;
    {
        static constexpr char   kMagic[]   = "HeightFieldGraphicsFile";
        static constexpr size_t kMagicLen  = sizeof(kMagic) - 1;
        static constexpr size_t kHeaderLen = 63;
        if (ehf.size() < kHeaderLen) return false;
        if (std::memcmp(ehf.data(), kMagic, kMagicLen) != 0) return false;
        auto be_u32 = [&](size_t off) -> uint32_t {
            return (uint32_t(ehf[off]) << 24) | (uint32_t(ehf[off+1]) << 16)
                 | (uint32_t(ehf[off+2]) << 8) |  uint32_t(ehf[off+3]);
        };
        hdr.magic.assign(kMagic);
        hdr.version     = be_u32(kMagicLen);
        hdr.u0          = be_u32(35);
        hdr.u1          = be_u32(39);
        hdr.body_offset = be_u32(55);
        hdr.body_size   = be_u32(59);
        hdr.ok          = (uint64_t(hdr.body_offset) + hdr.body_size <= ehf.size());
    }
    if (!hdr.ok || hdr.u0 == 0 || hdr.u1 == 0) {
        OutputLog::warn("bake composite: bad .ehf header");
        return false;
    }

    if (DecodeEhfTerrainAlbedoFromBytes(ehf, hdr.u0, hdr.u1,
                                        out_rgba, out_w, out_h))
    {
        out_picked_name = "embedded_tile_albedo";
        return true;
    }

    std::vector<uint8_t> lm_rgba;
    int lm_w = 0, lm_h = 0;
    {
        const uint8_t* p = ehf.data() + hdr.body_offset;
        std::vector<uint8_t> body_slice(p, p + hdr.body_size);
        auto dec = TextureAtlas::DecodeAtlas(body_slice);
        if (!dec.ok || dec.pixel_format != 24u) {
            OutputLog::warn("bake composite: .ehf body decode failed: " +
                            dec.error);
            return false;
        }
        lm_rgba = std::move(dec.rgba);
        lm_w    = dec.width;
        lm_h    = dec.height;
    }

    EhfParsedBody parsed;
    if (!ParseEhfBody(ehf, parsed)) {
        OutputLog::warn("bake composite: chunk parse failed: " + parsed.error);
        return false;
    }
    {
        std::ostringstream pos;
        pos << "ehf chunk parse: " << parsed.chunk_w << "x"
            << parsed.chunk_h << " chunks, "
            << parsed.lods.size() << " LODs"
            << "  (consumed " << parsed.bytes_consumed
            << "B, remaining " << parsed.bytes_remaining << "B)";
        OutputLog::success(pos.str());
    }

    {
        std::vector<TerrainTextureRegistry::LodPaletteEntry> pe;
        pe.reserve(parsed.lods.size());
        for (const auto& L : parsed.lods) {
            TerrainTextureRegistry::LodPaletteEntry e;
            e.base_diffuse   = L.strs[0];
            e.base_normal    = L.strs[1];
            e.detail_diffuse = L.strs[3];
            e.detail_normal  = L.strs[4];
            e.base_tile_scale   = L.params[0][0];
            e.base_intensity    = L.params[0][1];
            e.detail_tile_scale = L.params[1][0];
            e.detail_intensity  = L.params[1][1];
            pe.push_back(std::move(e));
        }
        TerrainTextureRegistry::SetLodPalette(std::move(pe));
    }

    auto basename_lower = [](const std::string& path) {
        std::string base = std::filesystem::path(path).filename().string();
        std::transform(base.begin(), base.end(), base.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return base;
    };

    struct Mat {
        bool                 decoded = false;
        std::vector<uint8_t> rgba;
        int                  w = 0, h = 0;
        std::string          name;
        float                tile_scale = 0.125f;
    };
    EhfPalette::Palette pal = EhfPalette::Parse(ehf);
    std::vector<Mat> mats(parsed.lods.size());
    int first_decoded = -1;
    for (size_t li = 0; li < parsed.lods.size(); ++li) {
        const std::string diffuse_path = parsed.lods[li].strs[0];
        if (diffuse_path.empty()) continue;
        const std::string want = basename_lower(diffuse_path);

        std::vector<unsigned char> blob_uc;
        bool stitched = false;
        try {
            stitched = build_any_tex_buffer_for_name(want, blob_uc,
                                                    preferred_bnk);
        } catch (...) { stitched = false; }
        if (!stitched || blob_uc.empty()) {
            const FlatAssetEntry* hit = nullptr;
            for (const auto& tex : S.all_tex_files) {
                std::string nm = std::filesystem::path(tex.name)
                                     .filename().string();
                std::transform(nm.begin(), nm.end(), nm.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                if (nm == want) { hit = &tex; break; }
            }
            if (!hit) continue;
            try {
                auto v = BnkCache::extract_bytes(hit->bnk_path,
                                                 hit->file_index);
                if (!v.empty()) blob_uc.assign(v.begin(), v.end());
            } catch (...) {}
            if (blob_uc.empty()) continue;
        }

        std::vector<uint8_t> rgba;
        bool has_alpha = false;
        int w = 0, h = 0;
        if (!decode_tex_to_rgba(blob_uc, rgba, w, h, &has_alpha, -1)) continue;
        mats[li].decoded = true;
        mats[li].rgba    = std::move(rgba);
        mats[li].w       = w;
        mats[li].h       = h;
        mats[li].name    = want;
        for (const auto& pe : pal.entries) {
            std::string pn = std::filesystem::path(pe.diffuse_path)
                                 .filename().string();
            std::transform(pn.begin(), pn.end(), pn.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            if (pn == want) {
                mats[li].tile_scale = pe.tile_scale;
                break;
            }
        }
        if (first_decoded < 0) first_decoded = (int)li;
    }

    if (first_decoded < 0) {
        OutputLog::warn("bake composite: no LOD diffuse texture decoded");
        return false;
    }

    {
        int n = 0;
        for (auto& m : mats) if (m.decoded) ++n;
        std::ostringstream os;
        os << "decoded " << n << " of " << mats.size() << " LOD diffuses:";
        OutputLog::info(os.str());
        for (size_t i = 0; i < mats.size() && i < 8; ++i) {
            if (!mats[i].decoded) continue;
            OutputLog::info("  LOD[" + std::to_string(i) + "] "
                            + mats[i].name);
        }
    }
    out_picked_name = "chunkgrid["
        + std::to_string(parsed.chunk_w) + "x"
        + std::to_string(parsed.chunk_h) + " × "
        + std::to_string(mats.size()) + " LODs]";

    if (g_capture_splat_debug && g_splat_debug_rgba) {
        g_splat_debug_rgba->clear();
        if (g_splat_debug_w) *g_splat_debug_w = 0;
        if (g_splat_debug_h) *g_splat_debug_h = 0;
    }

    const size_t pix = size_t(lm_w) * size_t(lm_h);
    out_rgba.assign(pix * 4, 0);
    out_w = lm_w;
    out_h = lm_h;

    float world_min_x =  1e30f;
    float world_min_z =  1e30f;
    float world_max_x = -1e30f;
    float world_max_z = -1e30f;
    for (const auto& c : parsed.chunks) {
        world_min_x = std::min(world_min_x, c.origin[0]);
        world_min_z = std::min(world_min_z, c.origin[1]);
        world_max_x = std::max(world_max_x, c.extent[0]);
        world_max_z = std::max(world_max_z, c.extent[1]);
    }
    const float world_span_x = std::max(1e-6f, world_max_x - world_min_x);
    const float world_span_z = std::max(1e-6f, world_max_z - world_min_z);
    const float chunk_size_x = world_span_x / std::max(1u, parsed.chunk_w);
    const float chunk_size_z = world_span_z / std::max(1u, parsed.chunk_h);
    {
        std::ostringstream os;
        os << "ehf chunk world bounds: x=[" << world_min_x << ".."
           << world_max_x << "] z=[" << world_min_z << ".."
           << world_max_z << "] chunk=(" << chunk_size_x << ","
           << chunk_size_z << ")";
        OutputLog::info(os.str());
    }

    auto sample_mat = [&](int idx, float u_world, float v_world,
                          uint8_t out_rgb[3])
    {
        const Mat& m = (idx >= 0 && idx < (int)mats.size() && mats[idx].decoded)
            ? mats[idx] : mats[first_decoded];
        const float ts = (m.tile_scale > 0.f && m.tile_scale < 1.f)
                            ? m.tile_scale : 0.125f;
        float u = (u_world * ts);
        float v = (v_world * ts);
        u = u - std::floor(u);
        v = v - std::floor(v);
        const float fx = u * m.w;
        const float fy = v * m.h;
        const int x0 = int(fx);
        const int y0 = int(fy);
        const int x1 = (x0 + 1) % m.w;
        const int y1 = (y0 + 1) % m.h;
        const float dx = fx - float(x0);
        const float dy = fy - float(y0);
        const uint8_t* p00 = m.rgba.data() + (size_t(y0) * m.w + x0) * 4;
        const uint8_t* p10 = m.rgba.data() + (size_t(y0) * m.w + x1) * 4;
        const uint8_t* p01 = m.rgba.data() + (size_t(y1) * m.w + x0) * 4;
        const uint8_t* p11 = m.rgba.data() + (size_t(y1) * m.w + x1) * 4;
        const float w00b = (1.f - dx) * (1.f - dy);
        const float w10b =        dx  * (1.f - dy);
        const float w01b = (1.f - dx) *        dy;
        const float w11b =        dx  *        dy;
        for (int c = 0; c < 3; ++c) {
            out_rgb[c] = uint8_t(
                w00b * p00[c] + w10b * p10[c] +
                w01b * p01[c] + w11b * p11[c]);
        }
    };

    auto sample_mask = [&](const EhfChunkLayer& L,
                           float local_x, float local_z) -> float
    {
        if (parsed.splat_indices.empty() ||
            parsed.splat_w == 0 || parsed.splat_h == 0 ||
            parsed.splat_indices.size() !=
                size_t(parsed.splat_w) * size_t(parsed.splat_h))
        {
            return 1.0f;
        }

        const float scale_u = (L.mask_scale[0] > 0.0f)
            ? L.mask_scale[0]
            : 32.0f / float(parsed.splat_w);
        const float scale_v = (L.mask_scale[1] > 0.0f)
            ? L.mask_scale[1]
            : 32.0f / float(parsed.splat_h);

        const float u = L.tile_uv[0]
            + std::clamp(local_x, 0.0f, 1.0f)
            * scale_u;
        const float v = L.tile_uv[1]
            + std::clamp(local_z, 0.0f, 1.0f)
            * scale_v;

        float px = u * float(parsed.splat_w) - 0.5f;
        float py = v * float(parsed.splat_h) - 0.5f;
        px = std::clamp(px, 0.0f, float(parsed.splat_w - 1));
        py = std::clamp(py, 0.0f, float(parsed.splat_h - 1));

        const int x0 = int(px);
        const int y0 = int(py);
        const int x1 = std::min<int>(x0 + 1, int(parsed.splat_w) - 1);
        const int y1 = std::min<int>(y0 + 1, int(parsed.splat_h) - 1);
        const float dx = px - float(x0);
        const float dy = py - float(y0);
        auto at = [&](int x, int y) -> float {
            return parsed.splat_indices[
                size_t(y) * size_t(parsed.splat_w) + size_t(x)] / 255.0f;
        };
        const float w00m = (1.0f - dx) * (1.0f - dy);
        const float w10m =         dx  * (1.0f - dy);
        const float w01m = (1.0f - dx) *         dy;
        const float w11m =         dx  *         dy;
        return std::clamp(at(x0, y0) * w00m + at(x1, y0) * w10m
                        + at(x0, y1) * w01m + at(x1, y1) * w11m,
                          0.0f, 1.0f);
    };

    constexpr float kBlendMax     = 3.0f;

    for (int y = 0; y < lm_h; ++y) {
        const float v_norm = (lm_h > 1)
            ? float(y) / float(lm_h - 1)
            : 0.0f;
        const float world_z = world_min_z + v_norm * world_span_z;
        const float fy_chunk = (world_z - world_min_z) / chunk_size_z;
        const int   cy       = std::min<int>(parsed.chunk_h - 1, int(fy_chunk));
        const float fy_in    = std::clamp(fy_chunk - float(cy), 0.f, 1.f);
        for (int x = 0; x < lm_w; ++x) {
            const float u_norm = (lm_w > 1)
                ? float(x) / float(lm_w - 1)
                : 0.0f;
            const float world_x = world_min_x + u_norm * world_span_x;
            const float fx_chunk = (world_x - world_min_x) / chunk_size_x;
            const int   cx       = std::min<int>(parsed.chunk_w - 1, int(fx_chunk));
            const float fx_in    = std::clamp(fx_chunk - float(cx), 0.f, 1.f);

            const float w00 = (1.f - fx_in) * (1.f - fy_in);
            const float w10 =        fx_in  * (1.f - fy_in);
            const float w01 = (1.f - fx_in) *        fy_in;
            const float w11 =        fx_in  *        fy_in;

            const EhfChunk& chunk =
                parsed.chunks[size_t(cx) * parsed.chunk_h + cy];

            float accum_r = 0.f, accum_g = 0.f, accum_b = 0.f;
            float accum_w = 0.f;

            const float wu = world_x;
            const float wv = world_z;

            for (const auto& L : chunk.layers) {
                const float blend_px =
                    w00 * float(L.blend[0]) + w10 * float(L.blend[1]) +
                    w01 * float(L.blend[2]) + w11 * float(L.blend[3]);
                const float weight = std::clamp(blend_px / kBlendMax,
                                                0.f, 1.f)
                                   * sample_mask(L, fx_in, fy_in);
                if (weight < 1.f / 255.f) continue;

                uint8_t rgb[3];
                sample_mat(int(L.material_idx), wu, wv, rgb);
                accum_r += float(rgb[0]) * weight;
                accum_g += float(rgb[1]) * weight;
                accum_b += float(rgb[2]) * weight;
                accum_w += weight;
            }

            if (accum_w > 1e-4f) {
                accum_r /= accum_w;
                accum_g /= accum_w;
                accum_b /= accum_w;
            } else {
                uint8_t base[3];
                sample_mat(first_decoded, wu, wv, base);
                accum_r = base[0]; accum_g = base[1]; accum_b = base[2];
            }

            uint8_t* dst = out_rgba.data() + (size_t(y) * lm_w + x) * 4;
            dst[0] = uint8_t(std::clamp(accum_r, 0.f, 255.f));
            dst[1] = uint8_t(std::clamp(accum_g, 0.f, 255.f));
            dst[2] = uint8_t(std::clamp(accum_b, 0.f, 255.f));
            dst[3] = 0xFF;
        }
    }

    std::ostringstream os;
    os << "bake composite: " << lm_w << "x" << lm_h
       << " (chunk grid " << parsed.chunk_w << "x" << parsed.chunk_h
       << " × " << parsed.lods.size() << " LODs × multi-layer)";
    OutputLog::success(os.str());
    return true;
}

}
