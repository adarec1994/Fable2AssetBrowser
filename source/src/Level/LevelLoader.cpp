#include "LevelLoader.h"
#include "HeightfieldLoader.h"
#include "TextureAtlasDecoder.h"
#include "EhfPalette.h"
#include "EhfChunkParser.h"
#include "TerrainTextureRegistry.h"

#include "../Utilities/State.h"
#include "../BNKCore.cpp"
#include "../UI/OutputLog.h"
#include "../textures/TexParser.h"
#include "../textures/LhTexCodec.h"
#include "../textures/export/TextureExport.h"
#include <zlib.h>

/* Forward — defined in src/UI/ModelPreview.cpp.  We don't include
   ModelPreview.h here because it drags in D3D11 headers we don't
   need; the function signature is small enough to declare inline. */
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
#include <iomanip>
#include <sstream>

/* Pending-load globals — populated by Level::Open, consumed in
   process_pending_loads(device) on the renderer thread. */
std::atomic<bool>   g_pending_terrain_load{false};
Level::TerrainMesh  g_pending_terrain_mesh;
std::string         g_pending_terrain_label;
FlatAssetEntry      g_pending_terrain_level_entry;
std::vector<uint8_t> g_pending_terrain_ehf_bytes;

/* Companion .ghf payload + heights snapshot — used by TerrainEdit so
   the user can mutate heights and save the modified .ghf back into
   the BNK / ISO without redoing the whole load.                     */
std::vector<uint8_t>  g_pending_terrain_ghf_payload;
std::vector<float>    g_pending_terrain_ghf_heights;
float                 g_pending_terrain_ghf_tile_size = 1.f;
int                   g_pending_terrain_ghf_width = 0;
int                   g_pending_terrain_ghf_height = 0;
FlatAssetEntry        g_pending_terrain_ghf_entry;

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
    const int kMaxLog      = 16;       

    /* Capture EVERY `.ehf` path the level references in its entry
       table.  The companion `.list` file doesn't include the .ehf —
       only .ghf/.hdb/.genv/.ama/.amm/.amr — but the level's t4
       entries do.

       A level can reference multiple .ehf's: typically one for the
       main heightfield and several for distant "vista" / "filler"
       backdrops.  We can't tell which is the main one from the
       entry list alone — they're all type-4 strings — so we keep
       all of them and match against the .ghf basename later (the
       .ghf reliably names the playable terrain).                  */
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

    /* The `.list` text file lists .ghf/.hdb/.genv/.am? but NOT the
       .ehf — Fable 2 stores the .ehf reference in the level's t4
       entry table instead.  We collected every .ehf path the level
       references into `all_ehf_refs`; pick the one whose basename
       matches the .ghf basename (that's the main playable terrain;
       the others are distant vistas / fillers).                   */
    auto basename_no_ext = [](const std::string& p) -> std::string {
        std::string s = std::filesystem::path(p).filename().string();
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
        /* If nothing matched the .ghf basename, fall back to the
           first .ehf — better than nothing on levels that don't
           even have a .ghf reference. */
        if (res.ehf_path.empty()) res.ehf_path = all_ehf_refs.front();
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

                /* Sanity check: body offset+size should fit inside the
                   .ehf file.  If it doesn't, the IDA-derived layout is
                   wrong (or .ehf is wrapped in some outer container we
                   haven't accounted for). */
                const uint64_t body_end =
                    uint64_t(h.body_offset) + uint64_t(h.body_size);
                if (body_end > hf.ehf_bytes.size()) {
                    std::ostringstream wos;
                    wos << "  .ehf body extent (" << body_end
                        << "B) exceeds file size (" << hf.ehf_bytes.size()
                        << "B) — header layout may be wrong";
                    OutputLog::warn(wos.str());
                } else {
                    /* Hex-dump the first 64 bytes of the body so we
                       can eyeball what sub_82A85DB0 / sub_82A85F20
                       reads next.  Format: "[+0x000] AA BB CC DD ..." */
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

                        /* Hand off to the renderer thread.  Also
                           stash the raw .ehf bytes so the texture
                           decode in PendingLoads can scan them for
                           the baked-terrain BC1 page without a
                           second BNK lookup.                       */
                        g_pending_terrain_mesh        = std::move(mesh);
                        g_pending_terrain_label       = entry.name;
                        g_pending_terrain_level_entry = entry;
                        g_pending_terrain_ehf_bytes   = hf.ehf_bytes;

                        /* Stash the .ghf data so TerrainEdit can do
                           snapshot/restore + (later) save-to-iso.   */
                        g_pending_terrain_ghf_payload   = hf.ghf_bytes_raw;
                        g_pending_terrain_ghf_heights   = hg.heights;
                        g_pending_terrain_ghf_tile_size = hg.tile_size;
                        g_pending_terrain_ghf_width     = (int)hg.width;
                        g_pending_terrain_ghf_height    = (int)hg.height;
                        {
                            /* Resolve the .ghf entry in the BNK so we
                               know where to write back later.       */
                            const FlatAssetEntry* fe =
                                Level::FindHeightfieldByPath(res.ghf_path);
                            g_pending_terrain_ghf_entry =
                                fe ? *fe : FlatAssetEntry{};
                        }

                        g_pending_terrain_load        = true;

                        /* Parse + log the ground-texture palette so
                           we can see what `.tex` files the level
                           expects to render the terrain with.    */
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

                        /* DEBUG: dump the .ehf alongside the
                           extracted/ folder so we can walk it
                           offline when the BC1 picker grabs the
                           wrong page.  Cheap (just a file write)
                           and the path is predictable. */
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

                        /* The .ehf body contains a PF=24 lightmap and
                           a PF=40 BC5 normal map (plus a still-WIP
                           PF=99 baked albedo).  We don't auto-export
                           anything here — PendingLoads will decode +
                           bind them as clickable thumbnails on the
                           terrain mesh, and the right-click "Export
                           to" menu on each thumbnail handles export
                           on demand.                                  */
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

bool RenderHeightmapToRGBA(const FlatAssetEntry& entry,
                           std::vector<uint8_t>& out_rgba,
                           int&                  out_w,
                           int&                  out_h)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;

    /* Locate the sibling `.list` text file and pull the .ghf path
       out of it.  Mirrors the logic in Open(), kept independent so
       the export path doesn't depend on a successful full level
       load. */
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

    /* Load + gunzip the .ghf and decode it. */
    HeightfieldFiles hf;
    if (!LoadHeightfieldFiles(/*ehf*/{}, ghf_path, {}, {}, hf)) {
        OutputLog::error("View Heightmap: .ghf load failed: " + hf.error);
        return false;
    }

    GhfHeights hg;
    if (!DecodeGhfHeights(hf.ghf_bytes_raw, hg)) {
        OutputLog::error("View Heightmap: .ghf decode failed: " + hg.error);
        return false;
    }

    /* Normalise heights to [0, 255] and pack as grayscale RGBA8.
       Use the actual span if it's well-defined; otherwise emit
       black so the viewer at least shows the dimensions. */
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

    /* The .texture_atlas sibling — same dir + same base name as the
       .engine_level, different extension. */
    std::filesystem::path atlas_path = level_entry.full_path;
    atlas_path.replace_extension(".texture_atlas");
    const std::string atlas_full = atlas_path.string();

    std::string atlas_key = atlas_full;
    std::transform(atlas_key.begin(), atlas_key.end(), atlas_key.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    std::replace(atlas_key.begin(), atlas_key.end(), '\\', '/');

    /* Try the same BNK first (cheapest); fall back to every loaded
       BNK if the level's sibling isn't there. */
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

    /* First fallback: walk the global heightfield-files index that
       TreeBuilder populates with .texture_atlas (alongside the
       .ehf / .ghf / .hdb / .genv / .am? group).  Cheaper than
       scanning every BNK from scratch and works regardless of
       which BNK actually holds the atlas. */
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

    /* `.texture_atlas` files have their own layout
       ([u32 raw][u32 comp][zlib BCn → tiled+endian-swapped]) that
       the generic .tex parser in TexParser.cpp misroutes through
       the buggy CompFlag=7 untile (32-block-collision formula).
       Use the dedicated TextureAtlas::DecodeAtlas path which
       mirrors ImageHeat exactly. */
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

    /* NEW PATH: the `.ehf` is actually a CONTAINER of embedded `.tex`
       files (same `0xFFFFFFFE` magic header layout as `.texture_atlas`).
       Each header describes the texture that follows: W × H in
       pixels, PixelFormat (35=BC1, 39=BC3, 40=BC5), mip_table_offset
       at +0x20 pointing at [u32 raw][u32 comp][zlib stream].

       Walk every 0xFFFFFFFE we find, pick the LARGEST-area BC1
       header — that's the per-cell baked albedo for the terrain.
       Feed the (header + data) blob through TextureAtlas::DecodeAtlas
       which already knows this exact layout.                       */
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

        /* Read the actual raw_size from the embedded mip table — that's
           the PADDED storage size which is the most reliable measure of
           "biggest texture" (logical W×H from header can lie about size
           — some entries have logical 256×256 but raw_size = 32768 = the
           same 256×256, while LOD entries have logical 624×648 but raw
           size = 245760 = 640×768 padded.  Sorting by raw bytes picks
           the densest baked-albedo entry per .ehf, which is what we
           actually want for highest visual fidelity).                */
        if (i + mip_off + 4 > eh_n) continue;
        const uint32_t raw_size = u32_at(i + mip_off);
        if (raw_size > best_raw) {
            best_raw  = raw_size;
            best_off  = i;
            best_W = W; best_H = H;
        }
    }

    if (best_off != SIZE_MAX) {
        /* The embedded .tex's mip-table offset is at +0x20, and
           points at [u32 raw_size][u32 comp_size][zlib stream].
           The zlib INFLATED bytes are NOT raw BC1 — they're the
           game's Huffman-coded BC1 bitstream (decoded by IDA-named
           `tex_decode_BC1_compressed` @ 0x82B8C1C8), which we have
           a working port of in `lh_decode_compressed_mip` (used by
           the regular `.tex` path when CompFlag == 1).

           Pipeline: zlib inflate → lh_decode_compressed_mip →
           raw row-major BC1 blocks → DecodeRawBc1ToRgba → RGBA. */
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
                /* zlib inflate to raw_size bytes — that's the Huffman
                   bitstream the game's BC1 codec consumes. */
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
                    /* lh_decode_compressed_mip reads W/H from the
                       bitstream itself (first 32 bits = W|H as
                       16-bit each).  We pass the inflated body in
                       full and trust its declared dimensions. */
                    std::vector<uint8_t> bc1;
                    int dec_w = 0, dec_h = 0;
                    std::string err;
                    if (lh_decode_compressed_mip(body.data(), body.size(),
                                                 dec_w, dec_h, bc1, &err,
                                                 /*comp11_layout=*/false))
                    {
                        std::vector<uint8_t> rgba;
                        if (TextureAtlas::DecodeRawBc1ToRgba(
                                bc1.data(), bc1.size(),
                                dec_w, dec_h, rgba))
                        {
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
    /* Fall through to the old "scan zlib sections by raw_size match"
       logic below — kept as a safety net for any .ehf that doesn't
       have the standard embedded-.tex layout we just learned.    */

    /* The baked terrain albedo isn't always sized to "cells rounded
       down to a multiple of 4" — Bloodstone defaultscenario IS, but
       the other Albion levels store the page at a power-of-2 size
       like 1024×1024.  Build a ranked candidate list (largest first)
       and pick the first one that actually exists in the .ehf.    */
    auto round_up_pow2 = [](uint32_t n) {
        uint32_t p = 1; while (p < n) p <<= 1; return p;
    };
    const uint32_t pow2_W = round_up_pow2(cells_w);
    const uint32_t pow2_H = round_up_pow2(cells_h);

    /* Each pair (W, H) → BC1 byte count.  Ordered roughly by total
       pixels descending so the FIRST match in the .ehf is the
       highest-resolution page.  We dedupe further down. */
    struct Cand { uint32_t W, H; size_t bytes; };
    std::vector<Cand> cands;
    auto add = [&](uint32_t w, uint32_t h) {
        if (w == 0 || h == 0) return;
        if ((w & 3u) != 0 || (h & 3u) != 0) return;       
        cands.push_back({w, h, (size_t)w * h / 2});
    };
    /* Power-of-2 ≥ cells (the most common case across Albion). */
    add(pow2_W, pow2_H);
    /* Cells rounded DOWN to 4 (Bloodstone defaultscenario uses this). */
    add(cells_w & ~3u, cells_h & ~3u);
    /* Common 1024-aligned aspect ratios as fallbacks. */
    add(1024, 1024);
    add(1024,  768);
    add( 768, 1024);
    add(1024,  512);
    add( 512, 1024);
    add( 768,  768);
    add( 512,  512);
    add( 256,  256);
    /* Sort by area desc, dedupe, drop anything wildly off-aspect.   */
    const float terrain_aspect = (float)cells_w / (float)cells_h;
    std::sort(cands.begin(), cands.end(),
              [](const Cand& a, const Cand& b){ return a.bytes > b.bytes; });
    cands.erase(std::unique(cands.begin(), cands.end(),
        [](const Cand& a, const Cand& b){
            return a.W == b.W && a.H == b.H; }), cands.end());
    auto aspect_ok = [&](uint32_t w, uint32_t h) -> bool {
        float a = (float)w / (float)h;
        /* Allow up to 4× aspect deviation — terrain-shaped pages and
           streaming/normal pages have different proportions.        */
        return a > terrain_aspect * 0.25f && a < terrain_aspect * 4.0f;
    };

    /* Walk every zlib section.  Each section is preceded by an
       8-byte header `[u32 BE raw_size][u32 BE comp_size]`, then a
       zlib stream (78 DA / 78 9C / 78 01 / 78 5E).  Collect every
       offset whose raw_size matches one of our BC1 candidates, then
       decode the largest one.                                       */
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
    /* Pick the LARGEST hit — that's the highest-resolution mip. */
    std::sort(hits.begin(), hits.end(),
              [](const Hit& a, const Hit& b){ return a.bytes > b.bytes; });
    const Hit& best = hits.front();

    /* Only return success for pages large enough to be per-cell
       baked albedo (≥ half the cells*0.5 byte-count).  Smaller
       pages are material-atlas sub-tiles whose per-cell index
       mapping we haven't decoded yet; falling back to the
       `.texture_atlas` with tiled UVs is the safer visual.       */
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

    /* Fast path: PendingLoads has already extracted the .ehf during
       the mesh build and parked the bytes in `g_pending_terrain_ehf_bytes`.
       Use that if it matches the level we were asked about. */
    if (!g_pending_terrain_ehf_bytes.empty() &&
        g_pending_terrain_level_entry.full_path == level_entry.full_path)
    {
        return DecodeEhfTerrainAlbedoFromBytes(
            g_pending_terrain_ehf_bytes,
            cells_w, cells_h, out_rgba, out_w, out_h);
    }

    /* Slow path: walk every heightfield file in the global index
       looking for a `.ehf` whose basename matches one of the level's
       references.  The .ehf is inside a BNK; the entry's
       `full_path` is its in-BNK path, NOT a disk path.              */
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

    /* Walk the palette in order; for each entry try to locate the
       diffuse .tex by basename in any loaded BNK.  First successful
       decode wins.  Palette entries are usually ordered with the
       "primary" terrain material first (e.g. grass for Bloodstone /
       Bowerlake, brightwood_earth for Brightwood). */
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

        /* Walk the global indexed list S.all_tex_files (already
           populated by TreeBuilder from every loaded BNK).  Match
           by lowercased basename — much faster than reopening
           BNKReaders per entry, and the index is guaranteed to
           cover everything the user mounted.                     */
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

        /* Decode via the existing .tex pipeline. */
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
    return BakeEhfTerrainCompositeWithBnk(ehf, /*preferred_bnk=*/{},
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
    /* Hook the (otherwise discarded) splat visualisation pointer into
       the bake so it can stash a copy alongside the composite.  Single-
       threaded — bake is called from the renderer thread only.       */
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

    /* 1) Parse the 63-byte header — that gives us the body offset + size,
       which name the PF=24 lightmap blob.  Refusing to bake without a
       valid header keeps us from emitting a "composite" that's just the
       stretched material with no lightmap modulation.                 */
    HeightfieldHeader hdr;
    {
        /* Re-use the local helper by reaching into the parser.  We
           duplicate a few lines here rather than expose `parse_ehf_header`
           publicly — the file is anyway tightly coupled to this TU.   */
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

    /* 2) Decode the PF=24 lightmap from the body slice.
       TextureAtlas::DecodeAtlas now handles PF=24 by emitting RGBA
       with R = high byte (smooth baked AO/lightmap), G = low byte
       (detail / dither), B = 0, A = 255.                            */
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

    /* 3) Parse the full .ehf body to extract the LOD vector (palette
       materials) and the chunk grid (per-region material indices +
       blends).  Validated by tools/ehf_body_walker.py against
       Bloodstone chapter3 (final offset == body_end exactly).      */
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

    /* Publish the LOD palette (diffuse+normal pairs per material) so
       the Materials & Textures window can list them.  These strings
       are the per-material .tex paths embedded in the .ehf body —
       e.g. LOD[0] = grass_diffuse.tex + grass_normal.tex.  Each LOD
       entry has 6 strings; strs[0..2] = BaseLayer (diffuse, normal,
       blank), strs[3..5] = DetailLayer (diffuse, normal, blank). */
    {
        std::vector<TerrainTextureRegistry::LodPaletteEntry> pe;
        pe.reserve(parsed.lods.size());
        for (const auto& L : parsed.lods) {
            TerrainTextureRegistry::LodPaletteEntry e;
            e.base_diffuse   = L.strs[0];
            e.base_normal    = L.strs[1];
            e.detail_diffuse = L.strs[3];
            e.detail_normal  = L.strs[4];
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

    /* 4) Decode each LOD's PRIMARY diffuse texture (LOD strs[0]).
       The chunk grid's per-layer `texture_idx` references the LOD
       vector, so we need one diffuse per LOD slot.  Materials that
       fail to decode fall back to the first successful one.

       Each LOD has a corresponding palette tile_scale (from
       EhfPalette).  LOD[N] → palette[N*2] for the primary diffuse,
       since each LOD bundles 2 (BaseLayer, DetailLayer) pairs.    */
    /* Decoded LOD diffuse.  The composite uses simple bilinear
       sampling.  The texture's tile rate (the per-material
       tile_scale, typically 0.125 = 8 wu/repeat) is high enough that
       at this composite resolution we're inherently undersampling —
       proper anti-aliasing requires either (a) much higher composite
       resolution or (b) GPU-side mipmap filtering on the live mesh.
       Don't pretend to fix that here. */
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
        /* Pick the palette entry whose diffuse path matches.  The
           20-LOD vector and 40-entry palette typically pair up as
           LOD[N] ↔ palette[N*2] (BaseLayer) + palette[N*2+1]
           (DetailLayer), but rather than rely on that alignment we
           match by basename. */
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

    /* Log which LOD materials decoded. */
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

    /* No more splat-debug output. */
    if (g_capture_splat_debug && g_splat_debug_rgba) {
        g_splat_debug_rgba->clear();
        if (g_splat_debug_w) *g_splat_debug_w = 0;
        if (g_splat_debug_h) *g_splat_debug_h = 0;
    }

    /* 5) Bake.  Composite is sized to the LIGHTMAP (= heightfield grid).
       The textures tile so heavily (e.g. 1/0.125 = 8 wu/repeat over a
       2500 wu map = 300+ repeats across 769 lightmap-pixels) that a
       single bilinear sample per output pixel gives severe aliasing
       which reads as both "tiling pattern" and "low detail".

       The fix is mipmapped sampling: at bake time we pre-build a small
       box-filter mip pyramid for each LOD diffuse, and at sample time
       we pick the right mip for the texel footprint (texels-of-source-
       per-output-pixel) and trilinearly blend two adjacent levels.

       For each composite texel:
         - map to a chunk index: (cx, cy) = (x / cell_per_chunk_x, ...)
         - get the chunk's layer stack and alpha-stack each layer
         - sample LODs[layer.texture_idx[corner]] at per-material tile
         - multiply by lightmap's R-channel AO

       The chunks form a regular W×H grid covering the heightfield.    */
    const size_t pix = size_t(lm_w) * size_t(lm_h);
    out_rgba.assign(pix * 4, 0);
    out_w = lm_w;
    out_h = lm_h;

    /* Heightfield-cell → chunk-grid mapping: just proportional. */
    const float chunk_per_texel_x = float(parsed.chunk_w) / float(lm_w);
    const float chunk_per_texel_y = float(parsed.chunk_h) / float(lm_h);

    /* Bilinear-sampled material lookup.  Given a LOD index and
       world-space coords, return the diffuse colour at that point.

       This is INTENTIONALLY a single bilinear sample.  The composite
       is too small to anti-alias the texture's heavy tile rate via
       supersampling/mipmapping in any meaningful way without much
       higher resolution — that's a separate problem.  Use the per-
       material tile_scale from the EhfPalette (e.g. 0.125 = 8 world
       units per texture repeat in chapter3). */
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

    constexpr float kBlendMax     = 3.0f;  

    /* Multi-layer per-chunk bake.  For each composite texel:
         1. Find the chunk + bilinear position within it (4 corner weights).
         2. Walk the chunk's layers, alpha-stacking each one:
            - Sample 4 corner textures at the layer's tile_uv (each
              corner may use a different LOD index).
            - Bilinear-mix the 4 sampled colours by the corner weights.
            - Bilinear-mix the 4 corner blend amounts → per-pixel alpha.
            - Alpha-over composite onto the running accumulator.
         3. Multiply final colour by the baked lightmap AO.            */
    for (int y = 0; y < lm_h; ++y) {
        const float fy_chunk = float(y) * chunk_per_texel_y;
        const int   cy       = std::min<int>(parsed.chunk_h - 1, int(fy_chunk));
        const float fy_in    = std::clamp(fy_chunk - float(cy), 0.f, 1.f);
        for (int x = 0; x < lm_w; ++x) {
            const float fx_chunk = float(x) * chunk_per_texel_x;
            const int   cx       = std::min<int>(parsed.chunk_w - 1, int(fx_chunk));
            const float fx_in    = std::clamp(fx_chunk - float(cx), 0.f, 1.f);

            const float w00 = (1.f - fx_in) * (1.f - fy_in);
            const float w10 =        fx_in  * (1.f - fy_in);
            const float w01 = (1.f - fx_in) *        fy_in;
            const float w11 =        fx_in  *        fy_in;

            const EhfChunk& chunk =
                parsed.chunks[size_t(cy) * parsed.chunk_w + cx];

            float accum_r = 0.f, accum_g = 0.f, accum_b = 0.f;
            float accum_a = 0.f;

            const float wu = float(x);
            const float wv = float(y);

            for (const auto& L : chunk.layers) {
                const float blend_px =
                    w00 * float(L.blend[0]) + w10 * float(L.blend[1]) +
                    w01 * float(L.blend[2]) + w11 * float(L.blend[3]);
                const float alpha = std::clamp(blend_px / kBlendMax,
                                               0.f, 1.f);
                if (alpha < 1.f / 255.f) continue;

                uint8_t c00[3], c10[3], c01[3], c11[3];
                sample_mat(int(L.texture_idx[0]), wu, wv, c00);
                sample_mat(int(L.texture_idx[1]), wu, wv, c10);
                sample_mat(int(L.texture_idx[2]), wu, wv, c01);
                sample_mat(int(L.texture_idx[3]), wu, wv, c11);

                const float r = w00 * c00[0] + w10 * c10[0]
                              + w01 * c01[0] + w11 * c11[0];
                const float g = w00 * c00[1] + w10 * c10[1]
                              + w01 * c01[1] + w11 * c11[1];
                const float b = w00 * c00[2] + w10 * c10[2]
                              + w01 * c01[2] + w11 * c11[2];

                const float one_minus_alpha = 1.f - alpha;
                accum_r = accum_r * one_minus_alpha + r * alpha;
                accum_g = accum_g * one_minus_alpha + g * alpha;
                accum_b = accum_b * one_minus_alpha + b * alpha;
                accum_a = std::min(1.f,
                                   accum_a + alpha * (1.f - accum_a));
            }

            /* Fall back to LOD[first_decoded] if all layers were
               transparent (shouldn't happen in practice but keep a
               safety net).                                          */
            if (accum_a < 0.05f) {
                uint8_t base[3];
                sample_mat(first_decoded, wu, wv, base);
                accum_r = base[0]; accum_g = base[1]; accum_b = base[2];
            }

            /* AO modulation with a higher floor (0.45) than before so
               deep-shadow regions stay readable instead of going to
               near-black.                                              */
            const uint8_t ao = lm_rgba[(size_t(y) * lm_w + x) * 4 + 0];
            const float k  = (ao / 255.0f) * 0.55f + 0.45f;
            uint8_t* dst = out_rgba.data() + (size_t(y) * lm_w + x) * 4;
            dst[0] = uint8_t(std::clamp(accum_r * k, 0.f, 255.f));
            dst[1] = uint8_t(std::clamp(accum_g * k, 0.f, 255.f));
            dst[2] = uint8_t(std::clamp(accum_b * k, 0.f, 255.f));
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
