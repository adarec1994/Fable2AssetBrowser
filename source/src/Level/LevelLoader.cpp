#include "LevelLoader.h"
#include "HeightfieldLoader.h"

#include "../Utilities/State.h"
#include "../BNKCore.cpp"
#include "../UI/OutputLog.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <sstream>

/* Pending-load globals — populated by Level::Open, consumed in
   process_pending_loads(device) on the renderer thread. */
std::atomic<bool>   g_pending_terrain_load{false};
Level::TerrainMesh  g_pending_terrain_mesh;
std::string         g_pending_terrain_label;

namespace Level {

namespace {

/* Big-endian readers matching the format documented in
   docs/level_format.md.  The engine's parser is also BE since it
   runs on Xbox 360 (PowerPC).  */
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
    bool skip(size_t k) {
        if (!need(k)) return false;
        i += k;
        return true;
    }
    /* Engine string format (per `stream_read_length_prefixed_string`
       @ `0x82A1D5C8` — IDA-confirmed): the function reads from the
       current stream position until it hits a NUL byte, then advances
       the stream by `strlen + 1` to skip past the terminator.  No
       length prefix — the original name was misleading.

       We cap at 4 KiB to keep a corrupt file from spinning forever. */
    bool cstr(std::string& s) {
        s.clear();
        const size_t start = i;
        const size_t limit = std::min(n, start + 4096);
        while (i < limit) {
            const uint8_t c = p[i++];
            if (c == 0) return true;
            s.push_back(static_cast<char>(c));
        }
        /* No NUL within range — fail rather than silently truncate. */
        return false;
    }
};

constexpr char kEngineLevelMagic[]  = "LevelGraphicsFile";
constexpr size_t kEngineLevelMagicLen = sizeof(kEngineLevelMagic) - 1;  // 17

}  // namespace

bool ParseEngineLevel(const std::vector<uint8_t>& bytes,
                      EngineLevelInfo&            out)
{
    out = {};
    if (bytes.size() < kEngineLevelMagicLen + 8) {
        out.error = "file too small for header";
        return false;
    }

    BeReader r{bytes.data(), bytes.size(), 0};

    /* Magic — read 17 chars and compare against "LevelGraphicsFile". */
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
    /* Engine accepts 11 or 12 — observed sample is 12. */
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

    /* Walk the typed-entry list.  We only fully decode the entry
       header (`type`) for now; payload-length recovery for the
       arbitrary types comes later when we have the full parsers
       ported.  For each entry we record the byte offset where it
       starts so future code can seek directly to a specific entry. */
    for (uint32_t mi = 0; mi < out.entry_count; ++mi) {
        EngineLevelEntry e;
        e.offset = r.i;

        if (!r.u32(e.type)) {
            std::ostringstream os;
            os << "truncated at entry " << mi << " of " << out.entry_count;
            out.error = os.str();
            /* Keep what we managed to read so the UI can still show
               *something*. */
            out.ok = false;
            return false;
        }

        switch (e.type) {
            case 4:
            case 5:
            case 32: {
                /* All three of these start with a single
                   null-terminated string read via
                   `stream_read_length_prefixed_string` (sic — the
                   IDA name is misleading; see the cstr() helper). */
                if (!r.cstr(e.str_a)) {
                    out.error = "truncated reading string for type "
                              + std::to_string(e.type);
                    return false;
                }
                if (e.type == 4) {
                    /* Type 4 follows the string with an 8-byte
                       inline payload (engine resource ref). */
                    if (!r.skip(8)) {
                        out.error = "truncated reading type-4 tail";
                        return false;
                    }
                }
                break;
            }
            case 21: {
                /* Two null-terminated strings + 8-byte hash + 2 flag
                   bytes per docs/level_format.md.  We skip the
                   trailing payload conservatively. */
                if (!r.cstr(e.str_a) || !r.cstr(e.str_b)) {
                    out.error = "truncated reading strings for type 21";
                    return false;
                }
                if (!r.skip(8 + 2)) {
                    out.error = "truncated reading type-21 tail";
                    return false;
                }
                break;
            }
            default: {
                /* Type 2 (instance placements) and unrecognised types:
                   we don't know the full length yet without the
                   per-entry sub-parser, so stop scanning further.
                   The entries we already captured are still valid. */
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

    EngineLevelInfo info;
    if (!ParseEngineLevel(bytes, info)) {
        OutputLog::error("parse failed for '" + entry.name + "': "
                         + info.error);
        return false;
    }

    /* Stash the source path so anyone resuming the load knows where
       it came from. */
    info.source_path = entry.full_path;

    /* Summary: per-type entry tally. */
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

    /* Walk the entries and surface anything that looks like a
       heightfield reference.  The engine's type-4 entries are the
       generic "engine resource reference" with a path string +
       8-byte payload — the heightfield's `.ehf` / `.ghf` siblings
       will appear here if they're tracked by the level graphics
       file (vs. being loaded by the world streamer side-channel).
       Even if they're not, logging the strings is the fastest way
       to learn the level's resource layout for the first time.

       Also pull out type-5 (texture composites) and type-32
       (streaming-index file) references so we can see which BNKs
       the level wants to mount. */
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
    const int kMaxLog      = 16;       // keep the spam reasonable

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

    /* Try the companion .list file.  It lives in the same BNK with
       the same basename but with extension `.list`, and is plain
       text — one resource path per line.  Faster + more reliable
       than guessing from the .engine_level's referenced strings. */
    auto sibling_with_ext = [&](const std::string& new_ext) {
        std::filesystem::path p = entry.full_path;
        p.replace_extension(new_ext);
        return p.string();
    };

    auto load_text_sibling = [&](const std::string& sibling_full_path,
                                 std::vector<uint8_t>& out_bytes) -> bool
    {
        /* Look it up in the same BNK first — that's where the
           .engine_level just came from.  Use a lowercased, forward-
           slashed key like BnkCache::find_index expects. */
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

            /* Stream the .list line-by-line — Windows-style line
               endings are common, handle CRLF / LF / CR. */
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

                /* Bucket each path by its extension into the
                   LevelResources record. */
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

                OutputLog::info("  " + line);
            }
        } else {
            OutputLog::warn("no companion .list (" + list_path + ") in BNK");
        }
    }

    /* Summarise what we found. */
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

    /* Load the heightfield triplet (raw .ehf + gunzipped .ghf) and
       surface a header line so we know what we're working with. */
    if (!res.ehf_path.empty() || !res.ghf_path.empty()) {
        HeightfieldFiles hf;
        if (!LoadHeightfieldFiles(res.ehf_path, res.ghf_path,
                                  res.hdb_path, res.genv_path, hf)) {
            OutputLog::error("heightfield load failed: " + hf.error);
        } else {
            std::ostringstream hos;
            hos << "heightfield loaded:"
                << "  ehf=" << hf.ehf_bytes.size() << "B"
                << "  ghf=" << hf.ghf_bytes_compressed.size() << "B (gz)"
                << " → " << hf.ghf_bytes_raw.size() << "B (raw)";
            OutputLog::success(hos.str());

            if (!hf.ehf_header.magic.empty()) {
                std::ostringstream eos;
                eos << "  .ehf header: magic=\"" << hf.ehf_header.magic
                    << "\"  version=" << hf.ehf_header.version
                    << "  prefix_float=" << hf.ehf_header.prefix_float;
                OutputLog::info(eos.str());
            } else {
                OutputLog::warn("  .ehf header missing magic");
            }

            /* Decode the .ghf height grid.  We now know its layout:
               20-byte header (tile_size + W + H) followed by W*H
               14-byte records, first 4 bytes of each = f32 BE height. */
            if (!hf.ghf_bytes_raw.empty()) {
                GhfHeights hg;
                if (!DecodeGhfHeights(hf.ghf_bytes_raw, hg)) {
                    OutputLog::error("  .ghf decode failed: " + hg.error);
                } else {
                    std::ostringstream gos;
                    gos << "  .ghf heightmap: " << hg.width << "x" << hg.height
                        << "  tile=" << hg.tile_size
                        << "  h=[" << hg.min_height << ".." << hg.max_height << "]";
                    OutputLog::success(gos.str());

                    /* Build the renderable mesh and hand it to the
                       renderer thread via the pending-load globals. */
                    TerrainMesh mesh;
                    if (!BuildTerrainMesh(hg, mesh)) {
                        OutputLog::error("  terrain mesh build failed");
                    } else {
                        const size_t tri_count = mesh.indices.size() / 3;
                        std::ostringstream mos;
                        mos << "  terrain mesh: verts=" << (mesh.positions.size() / 3)
                            << "  tris=" << tri_count;
                        OutputLog::success(mos.str());

                        /* Hand off to the renderer thread. */
                        g_pending_terrain_mesh  = std::move(mesh);
                        g_pending_terrain_label = entry.name;
                        g_pending_terrain_load  = true;
                    }
                }
            }
        }
    } else {
        OutputLog::warn("no .ehf or .ghf path in level — can't load terrain");
    }

    /* TODO(terrain): with heights in hand, build a renderable terrain
       mesh (W*H verts, (W-1)*(H-1)*2 tris, smooth normals from height
       gradients) and feed it to ModelPreview.  Texture mapping will
       come next from the level's texture_atlas. */

    return true;
}

}  // namespace Level
