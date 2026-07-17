#include "AnimBank.h"
#include "AnimDataFile.h"

#include "../UI/OutputLog.h"
#include "../ISO/IsoMount.h"
#include "../Utilities/State.h"
#include "../BNKCore.cpp"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

std::string read_lua_file_content(const std::string& path);

namespace Anim {

namespace {

std::vector<ModelAnimationBinding> g_model_animation_bindings;
uint64_t g_model_animation_binding_revision = 1;

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

    std::string cstr() {
        if (!ok) return {};
        size_t start = pos;
        while (pos < size && base[pos] != 0) ++pos;
        if (pos >= size) { ok = false; return {}; }
        std::string s((const char*)(base + start), pos - start);
        ++pos;
        return s;
    }

    void skip(size_t n) { need(n); if (ok) pos += n; }
};

const std::string& lookup(uint32_t idx,
                          const std::vector<std::string>& strings) {
    static const std::string empty;
    if (idx == 0xFFFFFFFFu || idx >= strings.size()) return empty;
    return strings[idx];
}

}

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

        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "AnimBank: unsupported version %u/%u (expected 1/5)",
                      magic1, magic2);
        OutputLog::error(buf);
        return false;
    }

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

    out_clips.reserve(num_anim_records);
    for (uint32_t i = 0; i < num_anim_records; ++i) {
        AnimClip c;
        c.key0            = r.u32();
        c.key1            = r.u32();
        c.data_offset     = r.u32();
        c.toc_frame_count = r.u32();
        c.data_length     = c.toc_frame_count;
        c.fps             = r.f32();
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

        char nbuf[16];
        std::snprintf(nbuf, sizeof(nbuf), "id_%08X", c.key0);
        c.name = nbuf;

        out_clips.push_back(std::move(c));
    }

    std::vector<size_t> order(out_clips.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](size_t a, size_t b) {
                  return out_clips[a].data_offset < out_clips[b].data_offset;
              });
    for (size_t i = 0; i < order.size(); ++i) {
        AnimClip& c = out_clips[order[i]];
        if (i + 1 < order.size()) {
            const uint32_t next = out_clips[order[i + 1]].data_offset;
            c.data_size_bytes = next > c.data_offset
                ? (next - c.data_offset)
                : 0;
        } else {
            c.data_size_bytes = 0;
        }
    }

    std::unordered_map<uint32_t,
                       std::shared_ptr<const std::vector<AnimTrackBone>>> track_maps;
    track_maps.reserve(num_special);

    for (uint32_t i = 0; i < num_special; ++i) {
        uint32_t header[8] = {};
        for (uint32_t h = 0; h < 8; ++h) {
            header[h] = r.u32();
        }
        if (!r.ok) {
            OutputLog::warn("AnimBank: special-record header truncated at #" +
                            std::to_string(i) + " (clips already parsed)");
            r.ok = true;
            break;
        }
        uint32_t n_args = r.u32();
        auto bones = std::make_shared<std::vector<AnimTrackBone>>();
        bones->reserve(n_args);
        for (uint32_t a = 0; a < n_args; ++a) {
            const uint32_t name_idx = r.u32();
            const uint32_t parent_idx = r.u32();
            AnimTrackBone tb;
            tb.name = lookup(name_idx, strings);
            tb.parent = (parent_idx == 0xFFFFFFFFu)
                ? -1
                : (int32_t)parent_idx;
            bones->push_back(std::move(tb));
        }
        if (!r.ok) {
            OutputLog::warn("AnimBank: special-record args truncated at #" +
                            std::to_string(i) + " (clips already parsed)");
            r.ok = true;
            break;
        }

        const uint32_t track_map_key = header[6];
        track_maps[track_map_key] = bones;
    }

    size_t mapped_clips = 0;
    for (auto& c : out_clips) {
        auto it = track_maps.find(c.key1);
        if (it == track_maps.end()) continue;
        c.track_map = it->second;
        ++mapped_clips;
    }

    OutputLog::success("AnimBank: parsed " +
                       std::to_string(out_clips.size()) +
                       " clip(s), " +
                       std::to_string(strings.size()) + " string(s), " +
                       std::to_string(track_maps.size()) + " track map(s), " +
                       std::to_string(mapped_clips) + " clip map link(s)");
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

    constexpr const char* kRel = "data/animation/fable2_anims.animation_toc";

    if (ISO::IsoMount::instance().is_mounted()) {
        const ISO::MountedFile* mf =
            ISO::IsoMount::instance().find(kRel);
        if (!mf) {

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
    uint32_t frames = clip.toc_frame_count;
    if (global_data_file().is_open()) {
        auto h = global_data_file().parse_clip_header(clip);
        if (h.ok && h.frame_count != 0) {
            frames = h.frame_count;
        }
    }
    if (frames == 0) return 0.0f;
    return (float)frames / clip.fps;
}

std::string read_lua_file_content(const std::string& path);

namespace {

uint32_t fnv1_32(const char* s, size_t n) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < n; ++i) {
        h = ((h * 0x01000193u) & 0xFFFFFFFFu) ^ (uint8_t)s[i];
    }
    return h;
}

uint32_t be_u32_at(const std::vector<uint8_t>& b, size_t off) {
    if (off + 4 > b.size()) return 0;
    const uint8_t* p = b.data() + off;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

std::string hex8(uint32_t v) {
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%08X", v);
    return buf;
}

bool is_default_clip_name(const AnimClip& clip) {
    if (clip.name.size() != 11 || clip.name.compare(0, 3, "id_") != 0) {
        return false;
    }
    return clip.name.substr(3) == hex8(clip.key0);
}

bool is_gdb_fallback_name(const AnimClip& clip) {
    if (clip.name.size() != 12 || clip.name.compare(0, 4, "gdb_") != 0) {
        return false;
    }
    return clip.name.substr(4) == hex8(clip.key0) ||
           std::all_of(clip.name.begin() + 4, clip.name.end(),
                       [](unsigned char c) {
                           return std::isxdigit(c) != 0;
                       });
}

std::string trim_enum_token(std::string s) {
    while (!s.empty() &&
           std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() &&
           std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

std::string decode_010_enum_token(std::string s) {
    s = trim_enum_token(std::move(s));
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s.compare(i, 2, "__") == 0) {
            out.push_back('\\');
            i += 2;
        } else if (s.compare(i, 3, "DOT") == 0) {
            out.push_back('.');
            i += 3;
        } else {
            out.push_back(s[i++]);
        }
    }
    return out;
}

const std::unordered_map<uint32_t, std::string>& external_gdb_enum_names() {
    static std::unordered_map<uint32_t, std::string> names;
    static bool loaded = false;
    if (loaded) return names;
    loaded = true;

    std::vector<std::string> candidates;
    candidates.emplace_back("F2GDBEnum.bt");
    if (const char* user_profile = std::getenv("USERPROFILE")) {
        candidates.emplace_back(
            std::string(user_profile) + "\\Downloads\\F2GDBEnum.bt");
    }

    std::ifstream in;
    for (const std::string& path : candidates) {
        in.open(path);
        if (in) break;
        in.clear();
    }
    if (!in) return names;

    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        const size_t hex = line.find("0x", eq == std::string::npos ? 0 : eq);
        if (eq == std::string::npos || hex == std::string::npos) continue;

        std::string token = decode_010_enum_token(line.substr(0, eq));
        if (token.empty()) continue;

        size_t end = hex + 2;
        while (end < line.size() &&
               std::isxdigit(static_cast<unsigned char>(line[end]))) {
            ++end;
        }
        if (end == hex + 2) continue;

        try {
            uint32_t h = static_cast<uint32_t>(
                std::stoul(line.substr(hex + 2, end - hex - 2), nullptr, 16));
            names.emplace(h, std::move(token));
        } catch (...) {
        }
    }
    return names;
}

struct GdbMiniView {
    static constexpr size_t kHeaderSize = 0x18;
    static constexpr uint32_t kHashParent = 0x5F6317D5u;

    struct Field {
        uint32_t hash = 0;
        uint8_t type = 0;
        uint32_t raw = 0;
    };

    const std::vector<uint8_t>& bytes;
    uint32_t count = 0;
    uint32_t size_a = 0;
    uint32_t size_b = 0;
    uint32_t tail_count = 0;
    uint32_t ext_count = 0;
    size_t schema_base = 0;
    size_t hash_base = 0;
    size_t body_end = 0;
    bool ok = false;

    std::vector<size_t> record_offsets;
    std::unordered_map<uint32_t, std::string> strings;

    explicit GdbMiniView(const std::vector<uint8_t>& b) : bytes(b) {
        if (bytes.size() < kHeaderSize ||
            bytes[0] != 'G' || bytes[1] != 'D' ||
            bytes[2] != 'B' || bytes[3] != 0) {
            return;
        }

        count = be_u32_at(bytes, 0x04);
        size_a = be_u32_at(bytes, 0x08);
        size_b = be_u32_at(bytes, 0x0C);
        tail_count = be_u32_at(bytes, 0x10);
        ext_count = be_u32_at(bytes, 0x14);
        if (count == 0) return;

        schema_base = kHeaderSize + size_t(size_a);
        hash_base = schema_base + size_t(size_b);
        body_end = schema_base;
        const size_t offset_base = hash_base + size_t(count) * 4;
        if (body_end > bytes.size() ||
            offset_base + size_t(count) * 2 > bytes.size()) {
            return;
        }

        if (!build_record_offsets()) return;
        parse_string_table();
        ok = true;
    }

    bool schema_at(size_t record, size_t& schema_off,
                   uint32_t& field_count) const {
        if (record + 4 > body_end) return false;
        schema_off = schema_base + size_t(be_u32_at(bytes, record));
        if (schema_off + 4 > hash_base) return false;
        uint32_t header = be_u32_at(bytes, schema_off);
        field_count = header >> 8;
        if (field_count > 256) {
            const uint8_t* p = bytes.data() + schema_off;
            field_count = (uint32_t(p[0]) | (uint32_t(p[1]) << 8)) +
                          uint32_t(p[2]);
            if (field_count > 1024) return false;
        }
        return schema_off + 4 + size_t(field_count) * 8 <= hash_base;
    }

    bool build_record_offsets() {
        record_offsets.clear();
        record_offsets.reserve(count);
        size_t cur = kHeaderSize;
        for (uint32_t i = 0; i < count; ++i) {
            if (cur + 4 > body_end) return false;
            record_offsets.push_back(cur);
            size_t schema_off = 0;
            uint32_t field_count = 0;
            if (!schema_at(cur, schema_off, field_count)) return false;
            const size_t entry_size = 4 + size_t(field_count) * 4;
            if (cur + entry_size > body_end) return false;
            cur += entry_size;
        }
        return true;
    }

    bool lookup(uint32_t hash, size_t& record) const {
        if (!ok) return false;
        size_t lo = 0;
        size_t hi = count;
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            const uint32_t v = be_u32_at(bytes, hash_base + mid * 4);
            if (v < hash) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo >= count) return false;
        if (be_u32_at(bytes, hash_base + lo * 4) != hash) return false;
        if (lo >= record_offsets.size()) return false;
        record = record_offsets[lo];
        return record + 4 <= body_end;
    }

    uint32_t record_hash(size_t index) const {
        if (index >= count) return 0;
        return be_u32_at(bytes, hash_base + index * 4);
    }

    uint32_t hash_for_record(size_t record) const {
        auto it = std::lower_bound(record_offsets.begin(),
                                   record_offsets.end(), record);
        if (it == record_offsets.end() || *it != record) return 0;
        return record_hash(size_t(it - record_offsets.begin()));
    }

    bool find_local(size_t record, uint32_t field_hash,
                    uint8_t expected_type, uint32_t& raw) const {
        size_t schema_off = 0;
        uint32_t field_count = 0;
        if (!schema_at(record, schema_off, field_count)) return false;
        const size_t hashes = schema_off + 4;
        const size_t descs = hashes + size_t(field_count) * 4;
        for (uint32_t i = 0; i < field_count; ++i) {
            if (be_u32_at(bytes, hashes + size_t(i) * 4) != field_hash) {
                continue;
            }
            const uint8_t type =
                uint8_t(be_u32_at(bytes, descs + size_t(i) * 4) >> 24);
            if (type != expected_type) continue;
            const size_t slot = record + 4 + size_t(i) * 4;
            if (slot + 4 > body_end) return false;
            raw = be_u32_at(bytes, slot);
            return true;
        }
        return false;
    }

    bool local_fields(size_t record, std::vector<Field>& out) const {
        out.clear();
        size_t schema_off = 0;
        uint32_t field_count = 0;
        if (!schema_at(record, schema_off, field_count)) return false;
        const size_t hashes = schema_off + 4;
        const size_t descs = hashes + size_t(field_count) * 4;
        out.reserve(field_count);
        for (uint32_t i = 0; i < field_count; ++i) {
            const size_t slot = record + 4 + size_t(i) * 4;
            if (slot + 4 > body_end) return false;
            Field f;
            f.hash = be_u32_at(bytes, hashes + size_t(i) * 4);
            f.type = uint8_t(be_u32_at(bytes, descs + size_t(i) * 4) >> 24);
            f.raw = be_u32_at(bytes, slot);
            out.push_back(f);
        }
        return true;
    }

    bool find_field(size_t record, uint32_t field_hash,
                    uint8_t expected_type, uint32_t& raw,
                    uint32_t* owner_hash = nullptr) const {
        std::unordered_set<size_t> visited;
        size_t cur = record;
        for (int depth = 0; depth < 64; ++depth) {
            if (!visited.insert(cur).second) return false;
            if (find_local(cur, field_hash, expected_type, raw)) {
                if (owner_hash) *owner_hash = hash_for_record(cur);
                return true;
            }

            uint32_t parent_hash = 0;
            if (!find_local(cur, kHashParent, 6, parent_hash) ||
                parent_hash == 0) {
                return false;
            }

            size_t parent_record = 0;
            if (!lookup(parent_hash, parent_record) ||
                parent_record == cur) {
                return false;
            }
            cur = parent_record;
        }
        return false;
    }

    void parse_string_table() {
        const size_t body_size =
            2 * (4 * (size_t(ext_count) + size_t(tail_count)) +
                 3 * size_t(count) + (size_t(count) & 1u)) +
            size_t(size_b) + size_t(size_a);
        const size_t table = kHeaderSize + body_size;
        if (table + 12 > bytes.size()) return;

        const uint32_t blob_size = be_u32_at(bytes, table + 4);
        const uint32_t string_count = be_u32_at(bytes, table + 8);
        const size_t blob = table + 12;
        if (string_count > 100000 ||
            blob + size_t(blob_size) + size_t(string_count) * 4 >
                bytes.size()) {
            return;
        }

        strings.reserve(string_count);
        for (uint32_t i = 0; i < string_count; ++i) {
            const size_t off = be_u32_at(bytes, blob + size_t(blob_size) +
                                               size_t(i) * 4);
            if (off + 4 >= blob_size) continue;
            const uint32_t h = be_u32_at(bytes, blob + off);
            size_t pos = blob + off + 4;
            size_t end = pos;
            const size_t limit = blob + size_t(blob_size);
            while (end < limit && bytes[end] != 0) ++end;
            if (end >= limit) continue;
            strings.emplace(h, std::string(
                reinterpret_cast<const char*>(bytes.data() + pos),
                end - pos));
        }
    }
};

struct GdbAnimScanStats {
    struct AnimationRecord {
        size_t clip_index = 0;
        uint32_t anim_hash = 0;
        std::string label;
    };

    struct SelectorRecord {
        uint32_t selector_hash = 0;
        std::string label;
    };

    size_t files_seen = 0;
    size_t files_parsed = 0;
    size_t animation_fields = 0;
    size_t inherited_animation_fields = 0;
    size_t animation_key_hits = 0;
    size_t selector_fields = 0;
    size_t selector_key_hits = 0;
    size_t selector_record_hits = 0;
    size_t selector_names_assigned = 0;
    size_t reference_record_hits = 0;
    size_t reference_key_hits = 0;
    size_t reference_names_assigned = 0;
    size_t bnks_seen = 0;
    size_t bnk_gdb_entries = 0;
    size_t bnk_nested_seen = 0;
    size_t model_binding_refs = 0;
    std::unordered_set<uint32_t> model_binding_models;
    size_t names_assigned = 0;
    std::unordered_set<uint32_t> unique_clip_hits;
    std::unordered_map<uint32_t, AnimationRecord> animation_records;
    std::vector<SelectorRecord> selectors;
};

std::string name_for_gdb_hash(const GdbMiniView& view, uint32_t hash) {
    auto str_it = view.strings.find(hash);
    if (str_it != view.strings.end() && !str_it->second.empty()) {
        return str_it->second;
    }

    const auto& enum_names = external_gdb_enum_names();
    auto enum_it = enum_names.find(hash);
    if (enum_it != enum_names.end() && !enum_it->second.empty()) {
        return enum_it->second;
    }

    return {};
}

std::string label_for_gdb_animation_source(const GdbMiniView& view,
                                           uint32_t record_hash,
                                           uint32_t anim_hash) {
    std::string name = name_for_gdb_hash(view, anim_hash);
    if (!name.empty()) return name;

    name = name_for_gdb_hash(view, record_hash);
    if (!name.empty()) return name;

    return "gdb_" + hex8(record_hash);
}

bool is_generic_animation_field_name(uint32_t field_hash,
                                     const std::string& label) {
    constexpr uint32_t kHashParent = 0x5F6317D5u;
    constexpr uint32_t kHashAnimationName = 0x78B1F79Cu;
    constexpr uint32_t kHashAnimName = 0x49BD6FC7u;
    constexpr uint32_t kHashAnimation = 0x8F32748Du;
    constexpr uint32_t kHashAnimations = 0xF96D7984u;
    if (field_hash == kHashParent ||
        field_hash == kHashAnimationName ||
        field_hash == kHashAnimName ||
        field_hash == kHashAnimation ||
        field_hash == kHashAnimations) {
        return true;
    }
    return label.empty() ||
           label == "Animation" ||
           label == "Animations" ||
           label == "AnimationName" ||
           label == "AnimName" ||
           label == "parent";
}

bool assign_gdb_clip_label(AnimClip& clip, const std::string& label) {
    if (label.empty()) return false;
    if (!is_default_clip_name(clip) && !is_gdb_fallback_name(clip)) {
        return false;
    }
    clip.name = label;
    return true;
}

void push_unique_u32(std::vector<uint32_t>& out, uint32_t value) {
    if (value == 0 || value == 0x811C9DC5u) return;
    if (std::find(out.begin(), out.end(), value) == out.end()) {
        out.push_back(value);
    }
}

bool find_inherited_hash_any(const GdbMiniView& view,
                             size_t record,
                             uint32_t field_hash,
                             uint32_t& raw) {
    std::unordered_set<size_t> visited;
    size_t cur = record;
    std::vector<GdbMiniView::Field> fields;
    for (int depth = 0; depth < 64; ++depth) {
        if (!visited.insert(cur).second) return false;
        if (!view.local_fields(cur, fields)) return false;
        for (const GdbMiniView::Field& field : fields) {
            if (field.hash != field_hash) continue;
            if (field.type != 3 && field.type != 4 && field.type != 6 &&
                field.type != 7) {
                continue;
            }
            raw = field.raw;
            return raw != 0 && raw != 0x811C9DC5u;
        }

        uint32_t parent_hash = 0;
        if (!view.find_local(cur, GdbMiniView::kHashParent, 6,
                             parent_hash) || parent_hash == 0) {
            return false;
        }

        size_t parent_record = 0;
        if (!view.lookup(parent_hash, parent_record) ||
            parent_record == cur) {
            return false;
        }
        cur = parent_record;
    }
    return false;
}

void visit_record_and_parents(
    const GdbMiniView& view,
    size_t record,
    const std::function<void(size_t)>& fn) {
    std::unordered_set<size_t> visited;
    size_t cur = record;
    for (int depth = 0; depth < 64; ++depth) {
        if (!visited.insert(cur).second) return;
        fn(cur);

        uint32_t parent_hash = 0;
        if (!view.find_local(cur, GdbMiniView::kHashParent, 6,
                             parent_hash) || parent_hash == 0) {
            return;
        }

        size_t parent_record = 0;
        if (!view.lookup(parent_hash, parent_record) ||
            parent_record == cur) {
            return;
        }
        cur = parent_record;
    }
}

void collect_model_file_hashes_from_record(
    const GdbMiniView& view,
    size_t record,
    std::vector<uint32_t>& out) {
    constexpr uint32_t kHashModelFile = 0x0C17DB4Eu;
    constexpr uint32_t kHashModelFile1 = 0x578E3BFBu;
    constexpr uint32_t kHashModelFile2 = 0x578E3BF8u;

    std::vector<GdbMiniView::Field> fields;
    visit_record_and_parents(view, record, [&](size_t cur) {
        if (!view.local_fields(cur, fields)) return;
        for (const GdbMiniView::Field& field : fields) {
            if (field.type != 3 && field.type != 4 && field.type != 7) {
                continue;
            }
            if (field.hash == kHashModelFile ||
                field.hash == kHashModelFile1 ||
                field.hash == kHashModelFile2) {
                push_unique_u32(out, field.raw);
            }
        }
    });
}

void collect_animated_model_hashes_for_record(
    const GdbMiniView& view,
    size_t record,
    std::vector<uint32_t>& out) {
    constexpr uint32_t kHashGraphicAppearanceComponent = 0xA7B6EF56u;
    constexpr uint32_t kHashGraphicAppearanceAnimatedMeshComponent =
        0x21D312CAu;
    constexpr uint32_t kHashGraphicAppearanceMorphComponent =
        0x0D4ADA1Au;
    constexpr uint32_t kHashCompositeModelRecord = 0x7CFF5EE2u;
    constexpr uint32_t kHashModel = 0x90347E14u;

    auto collect_component = [&](uint32_t component_hash) {
        size_t component_record = 0;
        if (!view.lookup(component_hash, component_record)) return;
        collect_model_file_hashes_from_record(view, component_record, out);
    };

    uint32_t direct_component_hash = 0;
    if (find_inherited_hash_any(view, record,
                                kHashGraphicAppearanceAnimatedMeshComponent,
                                direct_component_hash)) {
        collect_component(direct_component_hash);
    }

    uint32_t appearance_hash = 0;
    if (find_inherited_hash_any(view, record,
                                kHashGraphicAppearanceComponent,
                                appearance_hash)) {
        size_t appearance_record = 0;
        if (view.lookup(appearance_hash, appearance_record)) {
            visit_record_and_parents(view, appearance_record,
                                     [&](size_t cur) {
                uint32_t component_hash = 0;
                if (view.find_local(
                        cur, kHashGraphicAppearanceAnimatedMeshComponent,
                        6, component_hash)) {
                    collect_component(component_hash);
                }
            });
        }
    }

    uint32_t morph_hash = 0;
    if (!find_inherited_hash_any(view, record,
                                 kHashGraphicAppearanceMorphComponent,
                                 morph_hash)) {
        return;
    }
    size_t morph_record = 0;
    if (!view.lookup(morph_hash, morph_record)) return;
    uint32_t composite_hash = 0;
    if (!find_inherited_hash_any(view, morph_record,
                                 kHashCompositeModelRecord,
                                 composite_hash)) {
        return;
    }
    size_t composite_record = 0;
    if (!view.lookup(composite_hash, composite_record)) return;






    const auto is_model_field = [](uint32_t hash) {
        switch (hash) {
        case 0x90347E14u:
        case 0x0C17DB4Eu:
        case 0x578E3BFBu:
        case 0x578E3BF8u:
        case 0x1372D766u:
        case 0x05294B89u:
        case 0x547DDA3Eu:
        case 0xD622E5ADu:
        case 0x139ADC04u:
        case 0x8D09F54Fu:
        case 0x8D09F551u:
        case 0x8D09F56Eu:
        case 0xA0CFEC37u:
        case 0x017CDC23u:
        case 0x017CDC24u:
        case 0x017CDC25u:
        case 0x017CDC26u:
        case 0x017CDC27u:
            return true;
        default:
            return false;
        }
    };
    const auto is_appearance_link = [&](uint32_t hash) {
        if (is_model_field(hash)) return true;
        switch (hash) {
        case 0xA7B6EF56u:
        case 0x21D312CAu:
        case 0x0D4ADA1Au:
        case 0x7CFF5EE2u:
        case 0x29CF50D1u:
        case 0xCE642A15u:
        case 0x77679B84u:
        case 0x3C06A4E4u:
        case 0x31FF8FCFu:
        case 0x515A75DAu:
            return true;
        default:
            return false;
        }
    };
    std::vector<size_t> stack{composite_record};
    std::unordered_set<size_t> visited;
    std::vector<GdbMiniView::Field> fields;
    while (!stack.empty() && visited.size() < 512) {
        size_t current = stack.back();
        stack.pop_back();
        if (!visited.insert(current).second) continue;
        std::unordered_set<uint32_t> resolved_fields;
        std::unordered_set<size_t> inherited_records;
        for (int depth = 0; depth < 64; ++depth) {
            if (!inherited_records.insert(current).second ||
                !view.local_fields(current, fields)) {
                break;
            }
            for (const GdbMiniView::Field& field : fields) {
                if (field.hash == GdbMiniView::kHashParent) continue;
                if (!resolved_fields.insert(field.hash).second) continue;
                if (is_model_field(field.hash) &&
                    (field.type == 3 || field.type == 4 ||
                     field.type == 7)) {
                    push_unique_u32(out, field.raw);
                }
                if ((field.type == 6 || field.type == 7) &&
                    is_appearance_link(field.hash) &&
                    field.raw != 0 && field.raw != 0x811C9DC5u) {
                    size_t linked = 0;
                    if (view.lookup(field.raw, linked)) {
                        stack.push_back(linked);
                    }
                }
            }

            uint32_t parent_hash = 0;
            size_t parent_record = 0;
            if (!view.find_local(current, GdbMiniView::kHashParent, 6,
                                 parent_hash) ||
                !view.lookup(parent_hash, parent_record)) {
                break;
            }
            current = parent_record;
        }
    }
}

std::unordered_map<uint32_t, std::vector<size_t>>
build_children_by_parent_hash(const GdbMiniView& view) {
    std::unordered_map<uint32_t, std::vector<size_t>> out;
    for (size_t i = 0; i < view.record_offsets.size(); ++i) {
        const size_t record = view.record_offsets[i];
        uint32_t parent_hash = 0;
        if (!view.find_local(record, GdbMiniView::kHashParent, 6,
                             parent_hash) || parent_hash == 0) {
            continue;
        }
        out[parent_hash].push_back(record);
    }
    return out;
}

struct GdbClipRef {
    size_t clip_index = 0;
    uint32_t animation_record_hash = 0;
    uint32_t animation_key = 0;
    std::string label;
};

void push_unique_clip_ref(std::vector<GdbClipRef>& out,
                          GdbClipRef ref) {
    for (const GdbClipRef& existing : out) {
        if (existing.clip_index == ref.clip_index &&
            existing.animation_key == ref.animation_key) {
            return;
        }
    }
    out.push_back(std::move(ref));
}

void collect_clip_refs_from_animation_group(
    const GdbMiniView& view,
    uint32_t group_hash,
    const std::unordered_map<uint32_t, std::vector<size_t>>& children,
    const std::unordered_map<uint32_t,
                             GdbAnimScanStats::AnimationRecord>& records,
    const std::unordered_map<uint32_t, size_t>& by_key0,
    std::vector<GdbClipRef>& out) {
    size_t group_record = 0;
    if (!view.lookup(group_hash, group_record)) return;

    std::vector<size_t> stack;
    std::unordered_set<size_t> visited;
    stack.push_back(group_record);
    std::vector<GdbMiniView::Field> fields;
    while (!stack.empty()) {
        const size_t record = stack.back();
        stack.pop_back();
        if (!visited.insert(record).second) continue;

        if (view.local_fields(record, fields)) {
            for (const GdbMiniView::Field& field : fields) {
                std::string label = name_for_gdb_hash(view, field.hash);
                if (is_generic_animation_field_name(field.hash, label)) {
                    continue;
                }

                if (field.type == 6) {
                    auto rec_it = records.find(field.raw);
                    if (rec_it == records.end()) continue;
                    push_unique_clip_ref(
                        out,
                        GdbClipRef{rec_it->second.clip_index,
                                   field.raw,
                                   rec_it->second.anim_hash,
                                   label});
                } else if (field.type == 4) {
                    auto key_it = by_key0.find(field.raw);
                    if (key_it == by_key0.end()) continue;
                    push_unique_clip_ref(
                        out,
                        GdbClipRef{key_it->second, 0, field.raw, label});
                }
            }
        }

        const uint32_t record_hash = view.hash_for_record(record);
        auto child_it = children.find(record_hash);
        if (child_it == children.end()) continue;
        for (size_t child : child_it->second) {
            stack.push_back(child);
        }
    }
}

void collect_clip_refs_for_record(
    const GdbMiniView& view,
    size_t record,
    const std::unordered_map<uint32_t, std::vector<size_t>>& children,
    const std::unordered_map<uint32_t,
                             GdbAnimScanStats::AnimationRecord>& records,
    const std::unordered_map<uint32_t, size_t>& by_key0,
    std::vector<GdbClipRef>& out) {
    constexpr uint32_t kHashAnimation = 0x8F32748Du;
    constexpr uint32_t kHashAnimationList = 0x63565E85u;
    constexpr uint32_t kHashAnimations = 0xF96D7984u;
    constexpr uint32_t kHashAnimationSet = 0xE227399Fu;
    constexpr uint32_t kHashAnimationManagerComponent = 0x7C63CDDFu;

    auto collect_scope = [&](size_t scope_record) {
        uint32_t raw = 0;
        if (find_inherited_hash_any(view, scope_record, kHashAnimation,
                                    raw)) {
            auto key_it = by_key0.find(raw);
            if (key_it != by_key0.end()) {
                push_unique_clip_ref(
                    out,
                    GdbClipRef{
                        key_it->second, view.hash_for_record(scope_record),
                        raw,
                        label_for_gdb_animation_source(
                            view, view.hash_for_record(scope_record), raw)});
            }
        }

        const uint32_t group_fields[] = {
            kHashAnimationList, kHashAnimations, kHashAnimationSet
        };
        for (uint32_t field_hash : group_fields) {
            raw = 0;
            if (!find_inherited_hash_any(view, scope_record, field_hash,
                                         raw)) {
                continue;
            }
            collect_clip_refs_from_animation_group(
                view, raw, children, records, by_key0, out);
        }
    };

    collect_scope(record);

    uint32_t manager_hash = 0;
    if (find_inherited_hash_any(view, record,
                                kHashAnimationManagerComponent,
                                manager_hash)) {
        size_t manager_record = 0;
        if (view.lookup(manager_hash, manager_record) &&
            manager_record != record) {
            collect_scope(manager_record);
        }
    }
}

void append_model_animation_binding(uint32_t model_hash,
                                    uint32_t skeleton_hash,
                                    uint32_t retarget_hash,
                                    uint32_t source_record_hash,
                                    const GdbClipRef& ref,
                                    const AnimClip& clip,
                                    GdbAnimScanStats& stats) {
    for (const ModelAnimationBinding& existing :
         g_model_animation_bindings) {
        if (existing.model_path_hash == model_hash &&
            existing.clip_index == ref.clip_index &&
            existing.animation_key == ref.animation_key) {
            return;
        }
    }

    ModelAnimationBinding binding;
    binding.model_path_hash = model_hash;
    binding.skeleton_file_hash = skeleton_hash;
    binding.retarget_skeleton_file_hash = retarget_hash;
    binding.animation_record_hash = ref.animation_record_hash;
    binding.animation_key = ref.animation_key;
    binding.source_record_hash = source_record_hash;
    binding.clip_index = ref.clip_index;
    binding.animation_name = !ref.label.empty() ? ref.label : clip.name;
    binding.source_name = ref.label;
    g_model_animation_bindings.push_back(std::move(binding));
    ++stats.model_binding_refs;
    stats.model_binding_models.insert(model_hash);
}

void scan_gdb_animation_fields(
    const std::vector<uint8_t>& bytes,
    const std::unordered_map<uint32_t, size_t>& by_key0,
    std::vector<AnimClip>& clips,
    GdbAnimScanStats& stats) {
    constexpr uint32_t kHashAnimationName = 0x78B1F79Cu;
    constexpr uint32_t kHashAnimName = 0x49BD6FC7u;
    constexpr uint32_t kHashAnimation = 0x8F32748Du;

    ++stats.files_seen;
    GdbMiniView view(bytes);
    if (!view.ok) return;
    ++stats.files_parsed;

    std::unordered_map<uint32_t, GdbAnimScanStats::AnimationRecord>
        records_in_this_gdb;
    records_in_this_gdb.reserve(view.record_offsets.size() / 4 + 1);
    const auto children_by_parent_hash = build_children_by_parent_hash(view);

    for (size_t i = 0; i < view.record_offsets.size(); ++i) {
        const size_t record = view.record_offsets[i];
        const uint32_t record_hash = view.record_hash(i);

        uint32_t raw = 0;
        uint32_t animation_owner_hash = 0;
        if (view.find_field(record, kHashAnimation, 4, raw,
                            &animation_owner_hash)) {
            const bool is_local_animation = animation_owner_hash == record_hash;
            if (is_local_animation) {
                ++stats.animation_fields;
            } else {
                ++stats.inherited_animation_fields;
            }
            auto hit = by_key0.find(raw);
            if (hit != by_key0.end()) {
                ++stats.animation_key_hits;
                stats.unique_clip_hits.insert(raw);
                std::string label = label_for_gdb_animation_source(
                    view, record_hash, raw);
                GdbAnimScanStats::AnimationRecord rec{
                    hit->second, raw, label};
                stats.animation_records.emplace(record_hash, rec);
                records_in_this_gdb.emplace(record_hash, std::move(rec));

                AnimClip& clip = clips[hit->second];
                if (is_local_animation &&
                    assign_gdb_clip_label(clip, label)) {
                    ++stats.names_assigned;
                }
            }
        }

        raw = 0;
        if (view.find_local(record, kHashAnimationName, 4, raw) ||
            view.find_local(record, kHashAnimName, 4, raw)) {
            ++stats.selector_fields;
            if (by_key0.find(raw) != by_key0.end()) {
                ++stats.selector_key_hits;
            }
            stats.selectors.push_back(
                GdbAnimScanStats::SelectorRecord{
                    raw,
                    label_for_gdb_animation_source(view, record_hash, raw)});
        }
    }

    std::vector<GdbMiniView::Field> fields;
    for (size_t i = 0; i < view.record_offsets.size(); ++i) {
        const size_t record = view.record_offsets[i];
        if (!view.local_fields(record, fields)) continue;
        for (const GdbMiniView::Field& field : fields) {
            if (field.type == 6) {
                auto rec_it = records_in_this_gdb.find(field.raw);
                if (rec_it == records_in_this_gdb.end()) continue;
                std::string label = name_for_gdb_hash(view, field.hash);
                if (is_generic_animation_field_name(field.hash, label)) {
                    continue;
                }
                ++stats.reference_record_hits;
                AnimClip& clip = clips[rec_it->second.clip_index];
                if (assign_gdb_clip_label(clip, label)) {
                    ++stats.reference_names_assigned;
                }
            } else if (field.type == 4) {
                auto hit = by_key0.find(field.raw);
                if (hit == by_key0.end()) continue;
                std::string label = name_for_gdb_hash(view, field.hash);
                if (is_generic_animation_field_name(field.hash, label)) {
                    continue;
                }
                ++stats.reference_key_hits;
                AnimClip& clip = clips[hit->second];
                if (assign_gdb_clip_label(clip, label)) {
                    ++stats.reference_names_assigned;
                }
            }
        }
    }

    for (size_t i = 0; i < view.record_offsets.size(); ++i) {
        const size_t record = view.record_offsets[i];
        const uint32_t record_hash = view.record_hash(i);

        std::vector<uint32_t> model_hashes;
        collect_animated_model_hashes_for_record(view, record, model_hashes);
        if (model_hashes.empty()) continue;

        std::vector<GdbClipRef> refs;
        collect_clip_refs_for_record(view, record, children_by_parent_hash,
                                     records_in_this_gdb, by_key0, refs);
        if (refs.empty()) continue;

        uint32_t skeleton_hash = 0;
        uint32_t retarget_hash = 0;
        constexpr uint32_t kHashSkeletonFile = 0xC3D06E3Au;
        constexpr uint32_t kHashRetargetSkeletonFile = 0x64234AF2u;
        find_inherited_hash_any(view, record, kHashSkeletonFile,
                                skeleton_hash);
        find_inherited_hash_any(view, record, kHashRetargetSkeletonFile,
                                retarget_hash);

        for (uint32_t model_hash : model_hashes) {
            for (const GdbClipRef& ref : refs) {
                if (ref.clip_index >= clips.size()) continue;
                append_model_animation_binding(
                    model_hash, skeleton_hash, retarget_hash, record_hash,
                    ref, clips[ref.clip_index], stats);
            }
        }
    }
}

bool ends_with_lower_ci(const std::string& s, const char* tail) {
    const size_t n = std::strlen(tail);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c =
            static_cast<unsigned char>(s[s.size() - n + i]);
        if (std::tolower(c) != tail[i]) return false;
    }
    return true;
}

void scan_bnk_gdb_entries(
    BNKReader& reader,
    const std::unordered_map<uint32_t, size_t>& by_key0,
    std::vector<AnimClip>& clips,
    GdbAnimScanStats& stats,
    int depth) {
    const auto& files = reader.list_files();
    if (files.empty()) return;

    for (size_t i = 0; i < files.size(); ++i) {
        const FileEntry& entry = files[i];
        if (ends_with_lower_ci(entry.name, ".gdb")) {
            ++stats.bnk_gdb_entries;
            try {
                std::vector<uint8_t> bytes =
                    reader.extract_index_bytes(static_cast<int>(i));
                if (bytes.empty()) {
                    ++stats.files_seen;
                    continue;
                }
                scan_gdb_animation_fields(bytes, by_key0, clips, stats);
            } catch (...) {
                ++stats.files_seen;
            }
        } else if (depth < 4 && ends_with_lower_ci(entry.name, ".bnk")) {
            try {
                std::vector<uint8_t> bytes =
                    reader.extract_index_bytes(static_cast<int>(i));
                if (bytes.empty()) continue;
                ++stats.bnk_nested_seen;
                BNKReader nested(std::move(bytes));
                scan_bnk_gdb_entries(nested, by_key0, clips, stats,
                                     depth + 1);
            } catch (...) {
            }
        }
    }
}

void scan_bnk_path_for_gdbs(
    const std::string& bnk_path,
    const std::unordered_map<uint32_t, size_t>& by_key0,
    std::vector<AnimClip>& clips,
    GdbAnimScanStats& stats) {
    try {
        ++stats.bnks_seen;
        BNKReader reader(bnk_path);
        scan_bnk_gdb_entries(reader, by_key0, clips, stats, 0);
    } catch (...) {
    }
}

}

size_t resolve_clip_names_from_luas(std::vector<AnimClip>& clips) {
    if (clips.empty() || S.lua_files.empty()) return 0;

    std::unordered_map<uint32_t, size_t> by_key0;
    by_key0.reserve(clips.size());
    for (size_t i = 0; i < clips.size(); ++i) {
        by_key0[clips[i].key0] = i;
    }

    auto scan_quoted = [](const std::string& body,
                          std::vector<std::string_view>& out) {
        const char* p = body.data();
        const size_t n = body.size();
        for (size_t i = 0; i + 1 < n; ++i) {
            char q = p[i];
            if (q != '"' && q != '\'') continue;

            size_t end = i + 1;
            while (end < n && p[end] != q && p[end] != '\n') ++end;
            if (end >= n || p[end] != q) continue;
            const size_t len = end - i - 1;
            if (len < 4 || len > 80) {
                i = end;
                continue;
            }

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

        std::string body = ::read_lua_file_content(lua.path);
        if (body.empty()) { scripts_empty++; continue; }

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

size_t resolve_clip_names_from_gdb_animation_fields_for_root(
    const std::string& root,
    std::vector<AnimClip>& clips) {
    if (clips.empty()) return 0;

    g_model_animation_bindings.clear();

    std::unordered_map<uint32_t, size_t> by_key0;
    by_key0.reserve(clips.size());
    for (size_t i = 0; i < clips.size(); ++i) {
        by_key0.emplace(clips[i].key0, i);
    }

    GdbAnimScanStats stats;
    if (ISO::IsoMount::instance().is_mounted()) {
        for (const ISO::MountedFile& mf :
             ISO::IsoMount::instance().list_recursive(".gdb")) {
            auto bytes = ISO::IsoMount::instance().read_file(mf.path);
            if (bytes.empty()) {
                ++stats.files_seen;
                continue;
            }
            scan_gdb_animation_fields(bytes, by_key0, clips, stats);
        }
        for (const ISO::MountedFile& mf :
             ISO::IsoMount::instance().list_recursive(".bnk")) {
            scan_bnk_path_for_gdbs(
                ISO::IsoMount::make_iso_path(mf.path),
                by_key0, clips, stats);
        }
    } else {
        std::error_code ec;
        std::filesystem::path root_path(root);
        if (!std::filesystem::exists(root_path, ec)) return 0;
        for (auto it = std::filesystem::recursive_directory_iterator(
                 root_path,
                 std::filesystem::directory_options::skip_permission_denied,
                 ec);
             !ec && it != std::filesystem::recursive_directory_iterator();
             ++it) {
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });
            if (!it->is_regular_file(ec) || ext != ".gdb") {
                continue;
            }

            std::ifstream f(it->path(), std::ios::binary | std::ios::ate);
            if (!f) {
                ++stats.files_seen;
                continue;
            }
            std::streamsize sz = f.tellg();
            f.seekg(0, std::ios::beg);
            std::vector<uint8_t> bytes((size_t)sz);
            if (!f.read(reinterpret_cast<char*>(bytes.data()), sz)) {
                ++stats.files_seen;
                continue;
            }
            scan_gdb_animation_fields(bytes, by_key0, clips, stats);
        }

        ec.clear();
        for (auto it = std::filesystem::recursive_directory_iterator(
                 root_path,
                 std::filesystem::directory_options::skip_permission_denied,
                 ec);
             !ec && it != std::filesystem::recursive_directory_iterator();
             ++it) {
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });
            if (!it->is_regular_file(ec) || ext != ".bnk") {
                continue;
            }
            scan_bnk_path_for_gdbs(it->path().string(),
                                   by_key0, clips, stats);
        }
    }

    for (const GdbAnimScanStats::SelectorRecord& selector : stats.selectors) {
        auto rec = stats.animation_records.find(selector.selector_hash);
        if (rec == stats.animation_records.end()) continue;

        ++stats.selector_record_hits;
        AnimClip& clip = clips[rec->second.clip_index];
        if (is_default_clip_name(clip) || is_gdb_fallback_name(clip)) {
            clip.name = !selector.label.empty()
                ? selector.label
                : rec->second.label;
            ++stats.selector_names_assigned;
        }
    }

    char buf[512];
    std::snprintf(
        buf, sizeof(buf),
        "GDB animation scan: %zu/%zu GDB(s) parsed, %zu BNK(s) scanned "
        "(%zu nested, %zu GDB entry), %zu local + %zu inherited Animation "
        "field(s), %zu key hit(s) across %zu clip(s), %zu direct name(s) "
        "assigned; %zu named record ref(s), %zu named direct-key ref(s), "
        "%zu ref name(s) assigned; %zu AnimationName/AnimName selector "
        "field(s), %zu selector record hit(s), %zu selector name(s) "
        "assigned (%zu selector direct key hit(s)); %zu authored model "
        "animation binding(s) across %zu model hash(es).",
        stats.files_parsed, stats.files_seen,
        stats.bnks_seen, stats.bnk_nested_seen, stats.bnk_gdb_entries,
        stats.animation_fields, stats.inherited_animation_fields,
        stats.animation_key_hits,
        stats.unique_clip_hits.size(), stats.names_assigned,
        stats.reference_record_hits, stats.reference_key_hits,
        stats.reference_names_assigned,
        stats.selector_fields, stats.selector_record_hits,
        stats.selector_names_assigned, stats.selector_key_hits,
        stats.model_binding_refs, stats.model_binding_models.size());
    OutputLog::success(buf);

    ++g_model_animation_binding_revision;
    if (g_model_animation_binding_revision == 0) {
        g_model_animation_binding_revision = 1;
    }

    return stats.names_assigned + stats.reference_names_assigned +
           stats.selector_names_assigned;
}

namespace {

uint64_t cache_fnv64(uint64_t h, const void* data, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}



uint64_t clip_cache_fingerprint(const std::string& root,
                                const std::vector<AnimClip>& clips) {
    uint64_t h = 1469598103934665603ull;
    h = cache_fnv64(h, root.data(), root.size());
    std::error_code ec;
    auto stat_file = [&](const std::filesystem::path& p) {
        if (!std::filesystem::is_regular_file(p, ec)) return;
        const uint64_t sz = (uint64_t)std::filesystem::file_size(p, ec);
        const int64_t ticks = (int64_t)std::filesystem::last_write_time(
                                  p, ec)
                                  .time_since_epoch()
                                  .count();
        h = cache_fnv64(h, &sz, sizeof(sz));
        h = cache_fnv64(h, &ticks, sizeof(ticks));
    };
    const std::filesystem::path root_path(root);
    if (std::filesystem::is_regular_file(root_path, ec)) {
        stat_file(root_path);   
    } else {
        stat_file(root_path / "data" / "levels.bnk");
        stat_file(root_path / "data" / "streaming.bnk");
        stat_file(root_path / "data" / "Globals" / "globals.gdb");
    }
    const uint32_t count = (uint32_t)clips.size();
    h = cache_fnv64(h, &count, sizeof(count));
    for (const AnimClip& c : clips) {
        h = cache_fnv64(h, &c.key0, sizeof(c.key0));
    }
    return h;
}

std::filesystem::path clip_cache_path() {
    return std::filesystem::current_path() / "anim_names.cache";
}

void cache_put_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 24));
}

void cache_put_str(std::vector<uint8_t>& out, const std::string& s) {
    const uint16_t n = (uint16_t)std::min<size_t>(s.size(), 0xFFFF);
    out.push_back(uint8_t(n));
    out.push_back(uint8_t(n >> 8));
    out.insert(out.end(), s.begin(), s.begin() + n);
}

struct CacheReader {
    const uint8_t* p = nullptr;
    size_t n = 0;
    size_t pos = 0;
    bool ok = true;

    uint32_t u32() {
        if (pos + 4 > n) { ok = false; return 0; }
        const uint32_t v = uint32_t(p[pos]) | (uint32_t(p[pos + 1]) << 8) |
                           (uint32_t(p[pos + 2]) << 16) |
                           (uint32_t(p[pos + 3]) << 24);
        pos += 4;
        return v;
    }
    uint64_t u64() {
        const uint64_t lo = u32();
        const uint64_t hi = u32();
        return lo | (hi << 32);
    }
    std::string str() {
        if (pos + 2 > n) { ok = false; return {}; }
        const size_t len = size_t(p[pos]) | (size_t(p[pos + 1]) << 8);
        pos += 2;
        if (pos + len > n) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p + pos), len);
        pos += len;
        return s;
    }
};

constexpr uint32_t kClipCacheMagic = 0x43414632u;   
constexpr uint32_t kClipCacheVersion = 1;

}

bool load_clip_name_cache_for_root(const std::string& root,
                                   std::vector<AnimClip>& clips) {
    if (clips.empty()) return false;
    std::ifstream f(clip_cache_path(), std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff len = f.tellg();
    if (len < 20) return false;
    std::vector<uint8_t> bytes((size_t)len);
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(bytes.data()), len)) return false;

    CacheReader r{bytes.data(), bytes.size()};
    if (r.u32() != kClipCacheMagic) return false;
    if (r.u32() != kClipCacheVersion) return false;
    if (r.u64() != clip_cache_fingerprint(root, clips)) return false;
    const uint32_t clip_count = r.u32();
    if (!r.ok || clip_count != clips.size()) return false;

    std::vector<std::string> names(clip_count);
    for (uint32_t i = 0; i < clip_count; ++i) {
        const uint32_t key0 = r.u32();
        names[i] = r.str();
        if (!r.ok || key0 != clips[i].key0) return false;
    }
    const uint32_t binding_count = r.u32();
    if (!r.ok) return false;
    std::vector<ModelAnimationBinding> bindings;
    bindings.reserve(binding_count);
    for (uint32_t i = 0; i < binding_count; ++i) {
        ModelAnimationBinding b;
        b.model_path_hash = r.u32();
        b.skeleton_file_hash = r.u32();
        b.retarget_skeleton_file_hash = r.u32();
        b.animation_record_hash = r.u32();
        b.animation_key = r.u32();
        b.source_record_hash = r.u32();
        b.clip_index = r.u32();
        b.animation_name = r.str();
        b.source_name = r.str();
        if (!r.ok || b.clip_index >= clips.size()) return false;
        bindings.push_back(std::move(b));
    }

    for (uint32_t i = 0; i < clip_count; ++i) {
        if (!names[i].empty()) clips[i].name = std::move(names[i]);
    }
    g_model_animation_bindings = std::move(bindings);
    ++g_model_animation_binding_revision;
    if (g_model_animation_binding_revision == 0) {
        g_model_animation_binding_revision = 1;
    }
    OutputLog::success(
        "animation names restored from cache (" +
        std::to_string(clip_count) + " clips, " +
        std::to_string(binding_count) + " model bindings)");
    return true;
}

void save_clip_name_cache_for_root(const std::string& root,
                                   const std::vector<AnimClip>& clips) {
    if (clips.empty()) return;
    std::vector<uint8_t> out;
    out.reserve(clips.size() * 24 +
                g_model_animation_bindings.size() * 48 + 64);
    cache_put_u32(out, kClipCacheMagic);
    cache_put_u32(out, kClipCacheVersion);
    const uint64_t fp = clip_cache_fingerprint(root, clips);
    cache_put_u32(out, uint32_t(fp));
    cache_put_u32(out, uint32_t(fp >> 32));
    cache_put_u32(out, (uint32_t)clips.size());
    for (const AnimClip& c : clips) {
        cache_put_u32(out, c.key0);
        cache_put_str(out, c.name);
    }
    cache_put_u32(out, (uint32_t)g_model_animation_bindings.size());
    for (const ModelAnimationBinding& b : g_model_animation_bindings) {
        cache_put_u32(out, b.model_path_hash);
        cache_put_u32(out, b.skeleton_file_hash);
        cache_put_u32(out, b.retarget_skeleton_file_hash);
        cache_put_u32(out, b.animation_record_hash);
        cache_put_u32(out, b.animation_key);
        cache_put_u32(out, b.source_record_hash);
        cache_put_u32(out, (uint32_t)b.clip_index);
        cache_put_str(out, b.animation_name);
        cache_put_str(out, b.source_name);
    }
    std::ofstream f(clip_cache_path(), std::ios::binary | std::ios::trunc);
    if (!f) return;
    f.write(reinterpret_cast<const char*>(out.data()),
            (std::streamsize)out.size());
}

uint32_t gdb_model_path_hash(std::string path) {
    std::transform(path.begin(), path.end(), path.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    std::replace(path.begin(), path.end(), '/', '\\');

    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : path) {
        h *= 0x01000193u;
        h ^= uint32_t(c);
    }
    return h;
}

uint64_t model_animation_binding_revision() {
    return g_model_animation_binding_revision;
}

size_t build_model_animation_cache_for_hash(
    uint32_t model_path_hash,
    size_t clip_count,
    std::vector<uint8_t>& out_authored) {
    out_authored.assign(clip_count, 0);
    if (model_path_hash == 0 || clip_count == 0) return 0;

    size_t count = 0;
    for (const ModelAnimationBinding& binding :
         g_model_animation_bindings) {
        if (binding.model_path_hash != model_path_hash ||
            binding.clip_index >= clip_count) {
            continue;
        }
        if (!out_authored[binding.clip_index]) {
            out_authored[binding.clip_index] = 1;
            ++count;
        }
    }
    return count;
}

size_t model_animation_binding_count_for_hash(uint32_t model_path_hash) {
    if (model_path_hash == 0) return 0;
    size_t count = 0;
    for (const ModelAnimationBinding& binding :
         g_model_animation_bindings) {
        if (binding.model_path_hash == model_path_hash) ++count;
    }
    return count;
}

const std::vector<ModelAnimationBinding>& model_animation_bindings() {
    return g_model_animation_bindings;
}

}
