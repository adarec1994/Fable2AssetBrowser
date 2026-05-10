// AnimBank — fable2_anims.animation_toc reader.
//
// Layout (all big-endian, see docs/ANIM_FORMAT.md):
//
//   header     28 bytes
//     "AnimBank"  (8)
//     u32 magic1     == 1
//     u32 magic2     == 5
//     u32 num_anim_records
//     u32 num_special_records
//     u32 num_strings
//
//   string_table  (num_strings null-terminated UTF-8 strings, packed)
//
//   anim_records  (num_anim_records of:)
//     u32 key0
//     u32 key1
//     u32 data_offset    -- byte offset into .animation_data
//     u32 data_length
//     f32 duration
//     u32 event_count
//     event[event_count]:
//       f32 time
//       u32 name_idx     -- index into string_table (or 0xFFFFFFFF)
//       u32 param_idx    -- index into string_table (or 0xFFFFFFFF)
//
//   special_records  (num_special_records of metadata; we skip these
//     for now — they encode animation graph bindings the player runtime
//     uses to map clips to characters, but we don't need that until
//     Phase B+.)

#include "AnimBank.h"
#include "AnimDataFile.h"

#include "../UI/OutputLog.h"
#include "../ISO/IsoMount.h"
#include "../Utilities/State.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string_view>
#include <unordered_map>

// Forward declaration — defined in Lua.cpp at global scope. Reads a
// single .lua file via the BNK reader, working in both folder and
// ISO modes (returns decoded source, decompiling bytecode if
// needed).
std::string read_lua_file_content(const std::string& path);

namespace Anim {

namespace {

// Cursor over the loaded TOC blob. Big-endian readers; bounds-checked so
// a truncated / malformed file produces a clean parse failure instead of
// a buffer over-read.
struct Reader {
    const uint8_t* base = nullptr;
    size_t         size = 0;
    size_t         pos  = 0;
    bool           ok   = true;

    bool need(size_t n) {
        if (!ok) return false;
        if (pos + n > size) { ok = false; return false; }
        return true;
    }
    uint32_t u32() {
        if (!need(4)) return 0;
        const uint8_t* p = base + pos;
        pos += 4;
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
    }
    float f32() {
        uint32_t u = u32();
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    // Read a null-terminated C string in place. Advances past the
    // terminator.
    std::string cstr() {
        if (!ok) return {};
        size_t start = pos;
        while (pos < size && base[pos] != 0) ++pos;
        if (pos >= size) { ok = false; return {}; }
        std::string s((const char*)(base + start), pos - start);
        ++pos;   // skip the NUL
        return s;
    }
    // Skip n bytes (used to advance past the SpecialRecords we don't
    // parse). Sets ok=false if we'd run off the end.
    void skip(size_t n) { need(n); if (ok) pos += n; }
};

// Resolve a string-table index. 0xFFFFFFFF (== -1u) is the engine's
// "unset" sentinel — return empty in that case rather than throwing.
const std::string& lookup(uint32_t idx,
                          const std::vector<std::string>& strings) {
    static const std::string empty;
    if (idx == 0xFFFFFFFFu || idx >= strings.size()) return empty;
    return strings[idx];
}

} // anonymous

bool load_toc_bytes(const uint8_t* data, size_t size,
                    std::vector<AnimClip>& out_clips) {
    out_clips.clear();
    if (!data || size == 0) {
        OutputLog::error("AnimBank: empty input");
        return false;
    }

    Reader r;
    r.base = data;
    r.size = size;

    // ---- Header ----------------------------------------------------------
    if (!r.need(8)) {
        OutputLog::error("AnimBank: file too short");
        return false;
    }
    if (std::memcmp(r.base + r.pos, "AnimBank", 8) != 0) {
        OutputLog::error("AnimBank: bad magic (not 'AnimBank')");
        return false;
    }
    r.pos += 8;

    uint32_t magic1            = r.u32();
    uint32_t magic2            = r.u32();
    uint32_t num_anim_records  = r.u32();
    uint32_t num_special       = r.u32();
    uint32_t num_strings       = r.u32();
    if (!r.ok) {
        OutputLog::error("AnimBank: header truncated");
        return false;
    }
    if (magic1 != 1 || magic2 != 5) {
        // Engine guards on these too. If the format ever bumps, we'll
        // need to revisit; for now refuse rather than silently
        // mis-parse.
        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "AnimBank: unsupported version %u/%u (expected 1/5)",
                      magic1, magic2);
        OutputLog::error(buf);
        return false;
    }

    // ---- String table ----------------------------------------------------
    std::vector<std::string> strings;
    strings.reserve(num_strings);
    for (uint32_t i = 0; i < num_strings; ++i) {
        strings.push_back(r.cstr());
        if (!r.ok) {
            OutputLog::error("AnimBank: string table truncated at #" +
                             std::to_string(i));
            return false;
        }
    }

    // ---- AnimRecords ------------------------------------------------------
    out_clips.reserve(num_anim_records);
    for (uint32_t i = 0; i < num_anim_records; ++i) {
        AnimClip c;
        c.key0        = r.u32();
        c.key1        = r.u32();
        c.data_offset = r.u32();
        c.data_length = r.u32();
        c.fps         = r.f32();
        uint32_t n_events = r.u32();
        if (!r.ok) {
            OutputLog::error("AnimBank: record header truncated at #" +
                             std::to_string(i));
            return false;
        }

        c.events.reserve(n_events);
        for (uint32_t e = 0; e < n_events; ++e) {
            AnimEvent ev;
            ev.time                = r.f32();
            uint32_t name_idx      = r.u32();
            uint32_t param_idx     = r.u32();
            ev.name  = lookup(name_idx,  strings);
            ev.param = lookup(param_idx, strings);
            c.events.push_back(std::move(ev));
        }
        if (!r.ok) {
            OutputLog::error("AnimBank: events truncated at clip #" +
                             std::to_string(i));
            return false;
        }

        // Clip name: the TOC doesn't carry global clip names —
        // human-readable names like "SE_HOBBE_FOOTSTEP_WALK" live on
        // the SpecialRecord side as graph-node templates, but the
        // TEMPLATES are shared across many clips (299 specials for
        // 4082 clips) so they can't uniquely identify a clip. The
        // actual per-character names live in separate character
        // config files (sub_82515540 calls sub_821F3C28("Animations")
        // to look them up via FNV-1 hash).
        //
        // Best universal name: format the clip's primary hash as
        // `id_HHHHHHHH` — exactly how the game itself names clips
        // when no friendly string is available (we found 463 of
        // these in the TOC string table). Lets users match clips
        // against their own config files / disassembly without us
        // pretending to know names we don't.
        char nbuf[16];
        std::snprintf(nbuf, sizeof(nbuf), "id_%08X", c.key0);
        c.name = nbuf;

        out_clips.push_back(std::move(c));
    }

    // ---- SpecialRecords ---------------------------------------------------
    // Phase A: skip past these. We don't yet need their contents, and the
    // record size is variable (8 fixed dwords + variable arg list), so we
    // walk them just to validate the stream stays consistent. If an
    // unexpected end-of-file shows up, we log and bail — the AnimRecords
    // are already in out_clips so the caller still gets useful data.
    for (uint32_t i = 0; i < num_special; ++i) {
        // Layout (in disk order, deduced from sub_82B80D70):
        //   8 × u32  fixed header
        //   u32      arg_count
        //   arg_count × (u32 key, u32 val)
        r.skip(8 * 4);
        if (!r.ok) {
            OutputLog::warn("AnimBank: special-record header truncated at #" +
                            std::to_string(i) + " (clips already parsed)");
            r.ok = true;
            break;
        }
        uint32_t n_args = r.u32();
        r.skip((size_t)n_args * 8);
        if (!r.ok) {
            OutputLog::warn("AnimBank: special-record args truncated at #" +
                            std::to_string(i) + " (clips already parsed)");
            r.ok = true;
            break;
        }
    }

    OutputLog::success("AnimBank: parsed " +
                       std::to_string(out_clips.size()) +
                       " clip(s), " +
                       std::to_string(strings.size()) + " string(s)");
    return true;
}

bool load_toc(const std::filesystem::path& toc_path,
              std::vector<AnimClip>& out_clips) {
    out_clips.clear();
    std::ifstream f(toc_path, std::ios::binary | std::ios::ate);
    if (!f) {
        OutputLog::error("AnimBank: cannot open " + toc_path.string());
        return false;
    }
    std::streamsize sz = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> blob((size_t)sz);
    if (!f.read(reinterpret_cast<char*>(blob.data()), sz)) {
        OutputLog::error("AnimBank: read failed for " + toc_path.string());
        return false;
    }
    return load_toc_bytes(blob.data(), blob.size(), out_clips);
}

bool load_toc_for_root(const std::string& root,
                       std::vector<AnimClip>& out_clips) {
    out_clips.clear();

    // Single canonical sub-path inside the data root. Game ships only
    // one TOC, named the same way in both ISO and folder layouts.
    constexpr const char* kRel = "data/animation/fable2_anims.animation_toc";

    // ISO mode — the IsoMount is what `root` maps to here. Pull the
    // bytes via the mount so we don't fight the missing-file path that
    // ifstream would take on a virtual iso:// path.
    if (ISO::IsoMount::instance().is_mounted()) {
        const ISO::MountedFile* mf =
            ISO::IsoMount::instance().find(kRel);
        if (!mf) {
            // Not every disc has the bank in the same exact path; do a
            // basename fallback so we still find it if the layout's
            // slightly different.
            mf = ISO::IsoMount::instance().find_by_basename(
                "fable2_anims.animation_toc");
        }
        if (!mf) {
            OutputLog::warn("AnimBank: TOC not found in ISO");
            return false;
        }
        auto bytes = ISO::IsoMount::instance().read_file(mf->path);
        if (bytes.empty()) {
            OutputLog::error("AnimBank: failed to read TOC from ISO ("
                             + mf->path + ")");
            return false;
        }
        return load_toc_bytes(bytes.data(), bytes.size(), out_clips);
    }

    // Folder mode — try the canonical sub-path first; if that's missing,
    // walk the root looking for the file by name (some extracted
    // builds drop everything flat).
    std::filesystem::path direct = std::filesystem::path(root) / kRel;
    if (std::filesystem::exists(direct)) {
        return load_toc(direct, out_clips);
    }
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (it->is_regular_file(ec) &&
            it->path().filename() == "fable2_anims.animation_toc") {
            return load_toc(it->path(), out_clips);
        }
    }
    OutputLog::warn("AnimBank: TOC not found under " + root);
    return false;
}

float clip_duration_seconds(const AnimClip& clip) {
    if (clip.fps <= 0.0f) return 0.0f;
    if (!global_data_file().is_open()) return 0.0f;
    auto h = global_data_file().parse_clip_header(clip);
    if (!h.ok || h.field_C == 0) return 0.0f;
    return (float)h.field_C / clip.fps;
}

// Forward declaration — defined in Lua.cpp. Reads a single .lua file's
// content via the BNK reader, working in both folder and ISO modes.
// Returns the decoded source (decompiles Lua bytecode if needed).
std::string read_lua_file_content(const std::string& path);

namespace {

// FNV-1 (NOT FNV-1a — this is the variant used by Fable 2's
// runtime, identified by reverse-engineering sub_821F3C28).
//   hash = 0x811C9DC5
//   for byte in s: hash = (hash * 0x01000193) ^ byte
// Note: case-sensitive, no null terminator included.
uint32_t fnv1_32(const char* s, size_t n) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < n; ++i) {
        h = ((h * 0x01000193u) & 0xFFFFFFFFu) ^ (uint8_t)s[i];
    }
    return h;
}

} // anonymous

size_t resolve_clip_names_from_luas(std::vector<AnimClip>& clips) {
    if (clips.empty() || S.lua_files.empty()) return 0;

    // Build the lookup: clip key0 → index in `clips`. We only
    // override where the current name still has the synthesised
    // `id_HHHHHHHH` shape, so a previous pass (e.g. .anim files
    // in a future build) gets to keep its work.
    std::unordered_map<uint32_t, size_t> by_key0;
    by_key0.reserve(clips.size());
    for (size_t i = 0; i < clips.size(); ++i) {
        by_key0[clips[i].key0] = i;
    }

    // Quoted-string scanner. Lua source has both single-quoted
    // and double-quoted strings; the regex below catches both
    // and cheaply filters to "name-shaped" content (starts with
    // letter/underscore, no whitespace except a single space, 4-80
    // chars). Using a hand-rolled scanner — std::regex on the
    // full Lua body would be ~50× slower.
    auto scan_quoted = [](const std::string& body,
                          std::vector<std::string_view>& out) {
        const char* p = body.data();
        const size_t n = body.size();
        for (size_t i = 0; i + 1 < n; ++i) {
            char q = p[i];
            if (q != '"' && q != '\'') continue;
            // Find the matching close quote — Lua strings can
            // contain escapes but for the names we want there
            // are no escapes, so a forward scan is enough.
            size_t end = i + 1;
            while (end < n && p[end] != q && p[end] != '\n') ++end;
            if (end >= n || p[end] != q) continue;
            const size_t len = end - i - 1;
            if (len < 4 || len > 80) {
                i = end;
                continue;
            }
            // Filter to name-shaped: first char is letter/_,
            // rest is alnum/_/space/dot/#/-.
            const char* s = p + i + 1;
            char c0 = s[0];
            bool ok = (c0 == '_' ||
                       (c0 >= 'A' && c0 <= 'Z') ||
                       (c0 >= 'a' && c0 <= 'z'));
            if (ok) {
                for (size_t k = 0; k < len; ++k) {
                    char c = s[k];
                    bool valid =
                        (c == '_' || c == ' ' || c == '.' ||
                         c == '#' || c == '-' ||
                         (c >= '0' && c <= '9') ||
                         (c >= 'A' && c <= 'Z') ||
                         (c >= 'a' && c <= 'z'));
                    if (!valid) { ok = false; break; }
                }
            }
            if (ok) out.emplace_back(s, len);
            i = end;
        }
    };

    OutputLog::info("Scanning " + std::to_string(S.lua_files.size()) +
                    " Lua scripts for clip names...");

    size_t overridden = 0;
    size_t scripts_read = 0;
    size_t strings_seen = 0;
    size_t scripts_empty = 0;
    size_t scripts_bytecode_failed = 0;

    for (const auto& lua : S.lua_files) {
        // Qualify with :: so the lookup hits the global-scope
        // free function (defined in Lua.cpp), not a namespaced
        // version inside Anim::.
        std::string body = ::read_lua_file_content(lua.path);
        if (body.empty()) { scripts_empty++; continue; }
        // read_lua_file_content prefixes errors with "-- Error" —
        // count those as failed reads so we know when bytecode
        // decompilation is the bottleneck rather than missing
        // matches per se.
        if (body.size() >= 9 && body.compare(0, 9, "-- Error:") == 0) {
            scripts_bytecode_failed++;
            continue;
        }
        scripts_read++;

        std::vector<std::string_view> quoted;
        quoted.reserve(64);
        scan_quoted(body, quoted);
        strings_seen += quoted.size();

        for (const auto& sv : quoted) {
            uint32_t h = fnv1_32(sv.data(), sv.size());
            auto it = by_key0.find(h);
            if (it == by_key0.end()) continue;
            auto& clip = clips[it->second];
            // Only override placeholder names (start with "id_").
            if (clip.name.size() == 11 &&
                clip.name.compare(0, 3, "id_") == 0) {
                clip.name.assign(sv.data(), sv.size());
                overridden++;
            }
        }
    }

    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "Lua scan: %zu scripts read, %zu empty, %zu bytecode "
                  "failed, %zu strings hashed, %zu names resolved (of %zu).",
                  scripts_read, scripts_empty, scripts_bytecode_failed,
                  strings_seen, overridden, clips.size());
    OutputLog::success(buf);
    return overridden;
}

} // namespace Anim
