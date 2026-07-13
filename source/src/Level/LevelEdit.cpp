#include "LevelEdit.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <zlib.h>

#include "../BNKCore.cpp"
#include "../ISO/IsoMount.h"
#include "../UI/OutputLog.h"
#include "../Utilities/DebugTrace.h"
#include "../Utilities/Progress.h"
#include "../Utilities/State.h"
#include "BnkWriter.h"
#include "GdbEdit.h"
#include "LevelLoader.h"
#include "TextBank.h"

namespace LevelEdit {
namespace {

struct EditEntry {
    float delta[3] = {0, 0, 0};
    float rot_deg[3] = {0, 0, 0};
    float orig[3] = {0, 0, 0};
    float orig_rot[3] = {0, 0, 0};
    uint32_t lev_off = 0;
    uint8_t lev_kind = 0;
    uint32_t gdb_off[3] = {0, 0, 0};
    uint32_t gdb_rot_off[3] = {0, 0, 0};
    uint32_t gdb_entity_hash = 0;
    bool registered = false;
    bool deleted = false;

    bool moved() const {
        return delta[0] != 0.0f || delta[1] != 0.0f || delta[2] != 0.0f;
    }
    bool rotated() const {
        return rot_deg[0] != 0.0f || rot_deg[1] != 0.0f ||
               rot_deg[2] != 0.0f;
    }
    bool changed() const { return moved() || rotated() || deleted; }
};

struct FileTarget {
    std::string bnk_path;
    int         file_index = -1;
    std::string file_path;
    uint64_t disk_offset = 0;
    uint32_t on_disk_size = 0;
    bool compressed = false;
    bool in_iso = false;
    bool valid = false;
};

struct UndoState {
    float delta[3];
    float rot_deg[3];
    bool deleted;
};

struct UndoStep {
    std::vector<std::pair<uint32_t, UndoState>> before;
};

struct ModuleState {
    bool available = false;
    bool enabled   = false;
    bool dirty     = false;
    bool saving    = false;
    uint64_t revision = 0;

    FlatAssetEntry entry{};
    FileTarget lev;
    FileTarget gdb;

    std::unordered_map<uint32_t, EditEntry> edits;
    std::vector<UndoStep> undo_stack;
    std::vector<Addition> additions;

    std::unordered_map<uint32_t, std::vector<uint32_t>> contents_edits;
    std::unordered_map<uint32_t, std::string> text_edits;
    std::vector<GeneratorAddition> generators;
    struct SpawnPointAdd {
        uint32_t generator_entity = 0;
        uint32_t spawn_points_record = 0;
        float pos[3] = {0, 0, 0};
    };
    std::vector<SpawnPointAdd> spawn_point_adds;
    struct SpawnPointDelete {
        uint32_t generator_entity = 0;
        uint32_t spawn_points_record = 0;
        uint32_t spawn_point_entity = 0;
    };
    std::vector<SpawnPointDelete> spawn_point_deletes;
};

ModuleState& st() {
    static ModuleState s;
    return s;
}

std::mutex& mtx() {
    static std::mutex m;
    return m;
}

constexpr size_t kMaxUndoSteps = 128;
constexpr float kDegToRad = 0.01745329252f;

void fill_bnk_target(FileTarget& t) {
    t.valid = false;
    if (t.bnk_path.empty() || t.file_index < 0) return;
    t.in_iso = ISO::IsoMount::is_iso_path(t.bnk_path);
    try {
        const auto bc = BnkCache::get(t.bnk_path);
        const auto& files = bc.reader->list_files();
        if (t.file_index < (int)files.size()) {
            t.disk_offset = bc.reader->entry_disk_offset(t.file_index);
            t.on_disk_size = bc.reader->entry_on_disk_size(t.file_index);
            t.compressed = bc.reader->entry_is_compressed(t.file_index);
            t.valid = t.on_disk_size != 0;
        }
    } catch (...) {
    }
}

std::filesystem::path edited_levels_dir() {
    std::filesystem::path root_p(S.root_dir);
    std::error_code ec;
    if (!S.root_dir.empty() &&
        std::filesystem::is_regular_file(root_p, ec)) {
        root_p = root_p.parent_path();
    }
    if (root_p.empty()) root_p = std::filesystem::current_path();
    return root_p / "edited_levels";
}

std::filesystem::path slot_bak_path(const FileTarget& t,
                                    const char* tag) {
    std::string leaf = std::filesystem::path(
        !t.file_path.empty() ? t.file_path : st().entry.full_path)
        .filename().string();
    if (leaf.empty()) leaf = "level";
    return edited_levels_dir() / (leaf + "." + tag + ".slot.bak");
}

bool write_bytes_at(const std::string& path_or_bnk,
                    uint64_t offset,
                    const uint8_t* data,
                    size_t size,
                    std::string& err) {
    if (ISO::IsoMount::is_iso_path(path_or_bnk)) {
        const std::string vpath =
            ISO::IsoMount::strip_iso_prefix(path_or_bnk);
        if (!ISO::IsoMount::instance().write_at(vpath, offset, data,
                                                size)) {
            err = "ISO in-place write failed (" + vpath + ")";
            return false;
        }
        return true;
    }
    std::fstream f(path_or_bnk,
                   std::ios::binary | std::ios::in | std::ios::out);
    if (!f) {
        err = "could not open " + path_or_bnk + " for writing";
        return false;
    }
    f.seekp((std::streamoff)offset);
    f.write(reinterpret_cast<const char*>(data), (std::streamsize)size);
    if (!f) {
        err = "write failed at offset " + std::to_string(offset);
        return false;
    }
    return true;
}

void put_f32_be(uint8_t* p, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    p[0] = uint8_t(bits >> 24);
    p[1] = uint8_t(bits >> 16);
    p[2] = uint8_t(bits >> 8);
    p[3] = uint8_t(bits);
}

bool copy_file_with_progress(const std::string& src,
                             const std::string& dst,
                             const std::string& what,
                             std::string& msg,
                             bool cancellable = true,
                             bool remove_dst_on_fail = true) {
    std::error_code ec;
    const uint64_t total = std::filesystem::file_size(src, ec);
    if (ec) {
        msg = "cannot stat " + src;
        return false;
    }
    std::ifstream in(src, std::ios::binary);
    if (!in) {
        msg = "cannot open " + src;
        return false;
    }
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) {
        msg = "cannot write " + dst;
        return false;
    }
    const size_t kChunk = 8u << 20;
    std::vector<char> buf(kChunk);
    uint64_t done = 0;
    const int total_mb = (int)(total >> 20) + 1;
    auto fail = [&](const std::string& why) {
        out.close();
        if (remove_dst_on_fail) std::filesystem::remove(dst, ec);
        msg = why;
        return false;
    };
    while (done < total) {
        if (cancellable && S.cancel_requested.load()) {
            return fail("cancelled");
        }
        progress_update((int)(done >> 20), total_mb,
                        what + " (" + std::to_string(done >> 20) + " / " +
                        std::to_string(total_mb) + " MB)");
        const size_t n =
            (size_t)std::min<uint64_t>(kChunk, total - done);
        in.read(buf.data(), (std::streamsize)n);
        if ((size_t)in.gcount() != n) {
            return fail("short read from " + src);
        }
        out.write(buf.data(), (std::streamsize)n);
        if (!out) {
            return fail("short write to " + dst);
        }
        done += n;
    }
    return true;
}

bool ensure_backup(const FileTarget& t, const char* tag,
                   std::unordered_set<std::string>& backed,
                   std::string& msg) {
    if (!t.valid && t.file_path.empty()) return true;
    std::error_code ec;
    if (!t.file_path.empty()) {
        if (!backed.insert(t.file_path).second) return true;
        const std::filesystem::path bak(t.file_path + ".bak");
        if (std::filesystem::exists(bak, ec)) return true;
        std::string cerr;
        if (!copy_file_with_progress(
                t.file_path, bak.string(),
                "Backing up " +
                    std::filesystem::path(t.file_path).filename().string(),
                cerr)) {
            msg = "backup failed: " + cerr;
            return false;
        }
        OutputLog::success("level edit: backup written to " + bak.string());
        return true;
    }
    if (!t.in_iso) {
        if (!backed.insert(t.bnk_path).second) return true;
        const std::filesystem::path bak(t.bnk_path + ".bak");
        if (std::filesystem::exists(bak, ec)) return true;
        std::string cerr;
        if (!copy_file_with_progress(
                t.bnk_path, bak.string(),
                "Backing up " +
                    std::filesystem::path(t.bnk_path).filename().string(),
                cerr)) {
            msg = "backup failed: " + cerr;
            return false;
        }
        OutputLog::success("level edit: backup written to " + bak.string());
        return true;
    }
    if (t.compressed) return true;
    const auto bak = slot_bak_path(t, tag);
    if (std::filesystem::exists(bak, ec)) return true;
    std::vector<uint8_t> slot;
    try {
        slot = BnkCache::extract_bytes(t.bnk_path, t.file_index);
    } catch (...) {
        slot.clear();
    }
    if (slot.empty()) {
        msg = "backup failed: could not extract payload for " +
              std::string(tag);
        return false;
    }
    std::filesystem::create_directories(bak.parent_path(), ec);
    std::ofstream f(bak, std::ios::binary);
    if (!f) {
        msg = "backup failed: cannot write " + bak.string();
        return false;
    }
    f.write(reinterpret_cast<const char*>(slot.data()),
            (std::streamsize)slot.size());
    OutputLog::success("level edit: slot backup written to " +
                       bak.string());
    return true;
}

bool restore_target(const FileTarget& t, const char* tag,
                    std::unordered_set<std::string>& restored,
                    std::string& msg) {
    if (!t.valid && t.file_path.empty()) return true;
    std::error_code ec;
    if (!t.file_path.empty()) {
        if (!restored.insert(t.file_path).second) return true;
        const std::filesystem::path bak(t.file_path + ".bak");
        if (!std::filesystem::exists(bak, ec)) return true;
        std::string cerr;
        if (!copy_file_with_progress(
                bak.string(), t.file_path,
                "Restoring " +
                    std::filesystem::path(t.file_path).filename().string(),
                cerr, false, false)) {
            msg = "restore failed: " + cerr;
            return false;
        }
        return true;
    }
    if (!t.in_iso) {
        if (!restored.insert(t.bnk_path).second) return true;
        const std::filesystem::path bak(t.bnk_path + ".bak");
        if (!std::filesystem::exists(bak, ec)) return true;
        BnkCache::invalidate(t.bnk_path);
        std::string cerr;
        if (!copy_file_with_progress(
                bak.string(), t.bnk_path,
                "Restoring " +
                    std::filesystem::path(t.bnk_path).filename().string(),
                cerr, false, false)) {
            msg = "restore failed: " + cerr;
            return false;
        }
        return true;
    }
    if (t.compressed) return true;
    const auto bak = slot_bak_path(t, tag);
    if (!std::filesystem::exists(bak, ec)) return true;
    std::ifstream f(bak, std::ios::binary);
    std::vector<uint8_t> slot((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    if (slot.empty() || slot.size() > t.on_disk_size) {
        msg = "slot backup unreadable or larger than the BNK slot";
        return false;
    }
    std::string err;
    if (!write_bytes_at(t.bnk_path, t.disk_offset, slot.data(),
                        slot.size(), err)) {
        msg = "restore failed: " + err;
        return false;
    }
    return true;
}

bool patch_target(const FileTarget& t, uint32_t payload_off,
                  const float* vals, int count, std::string& err) {
    std::vector<uint8_t> buf((size_t)count * 4);
    for (int i = 0; i < count; ++i) put_f32_be(buf.data() + i * 4, vals[i]);
    if (!t.file_path.empty()) {
        return write_bytes_at(t.file_path, payload_off, buf.data(),
                              buf.size(), err);
    }
    return write_bytes_at(t.bnk_path, t.disk_offset + payload_off,
                          buf.data(), buf.size(), err);
}

bool target_patchable_in_place(const FileTarget& t) {
    if (!t.file_path.empty()) return true;
    return t.valid && !t.compressed;
}

void put_u64_be(std::vector<uint8_t>& v, uint64_t x) {
    for (int i = 7; i >= 0; --i) v.push_back(uint8_t(x >> (i * 8)));
}

uint32_t get_u32_be2(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

uint32_t fnv1_32(const std::string& s) {
    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : s) {
        h *= 0x01000193u;
        h ^= c;
    }
    return h;
}

std::string lower_model_path(const std::string& p) {
    std::string mp = p;
    for (char& c : mp) {
        if (c == '/') c = '\\';
        else c = (char)std::tolower((unsigned char)c);
    }
    return mp;
}

uint64_t addition_instance_hash(const std::string& lowered, size_t ai) {
    const uint32_t hi = fnv1_32(lowered);
    const uint32_t lo = fnv1_32(lowered + "#placed" + std::to_string(ai));
    return ((uint64_t)hi << 32) | lo;
}

bool gzip_inflate(const std::vector<uint8_t>& in,
                  std::vector<uint8_t>& out) {
    z_stream z{};
    if (inflateInit2(&z, 15 + 32) != Z_OK) return false;
    z.next_in = const_cast<Bytef*>(in.data());
    z.avail_in = (uInt)in.size();
    out.clear();
    std::vector<uint8_t> buf(1u << 16);
    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        z.next_out = buf.data();
        z.avail_out = (uInt)buf.size();
        ret = inflate(&z, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&z);
            return false;
        }
        out.insert(out.end(), buf.data(),
                   buf.data() + (buf.size() - z.avail_out));
        if (ret != Z_STREAM_END && z.avail_in == 0) {
            inflateEnd(&z);
            return false;
        }
    }
    inflateEnd(&z);
    return true;
}

bool gzip_deflate(const std::vector<uint8_t>& in,
                  std::vector<uint8_t>& out) {
    z_stream z{};
    if (deflateInit2(&z, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) return false;
    z.next_in = const_cast<Bytef*>(in.data());
    z.avail_in = (uInt)in.size();
    out.resize((size_t)deflateBound(&z, (uLong)in.size()) + 64);
    z.next_out = out.data();
    z.avail_out = (uInt)out.size();
    const int ret = deflate(&z, Z_FINISH);
    const bool ok = (ret == Z_STREAM_END);
    out.resize(out.size() - z.avail_out);
    deflateEnd(&z);
    return ok;
}

void put_u32_be(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 3; i >= 0; --i) v.push_back(uint8_t(x >> (i * 8)));
}

void put_f32_be_v(std::vector<uint8_t>& v, float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    put_u32_be(v, bits);
}

bool find_type2_template(const std::vector<uint8_t>& bytes,
                         float out_vals[20]) {
    static const char kMagic[] = "LevelGraphicsFile";
    const size_t magic_len = sizeof(kMagic) - 1;
    size_t pos = magic_len + 8;
    auto rd32 = [&](size_t p, uint32_t& v) -> bool {
        if (p + 4 > bytes.size()) return false;
        v = (uint32_t(bytes[p]) << 24) | (uint32_t(bytes[p + 1]) << 16) |
            (uint32_t(bytes[p + 2]) << 8) | uint32_t(bytes[p + 3]);
        return true;
    };
    auto skip_cstr = [&](size_t& p) -> bool {
        while (p < bytes.size() && bytes[p] != 0) ++p;
        if (p >= bytes.size()) return false;
        ++p;
        return true;
    };
    for (int guard = 0; guard < 4096; ++guard) {
        uint32_t t = 0;
        if (!rd32(pos, t)) return false;
        pos += 4;
        if (t == 2) {
            for (int k = 0; k < 4; ++k) {
                if (!skip_cstr(pos)) return false;
            }
            uint32_t n = 0;
            if (!rd32(pos, n) || n == 0 || n > 100000) return false;
            pos += 4;
            const size_t vals = pos + 3 + 8;
            if (vals + 80 > bytes.size()) return false;
            for (int k = 0; k < 20; ++k) {
                uint32_t bits = 0;
                rd32(vals + (size_t)k * 4, bits);
                std::memcpy(&out_vals[k], &bits, 4);
            }
            return true;
        } else if (t == 4) {
            if (!skip_cstr(pos)) return false;
            pos += 8;
        } else if (t == 5 || t == 32) {
            if (!skip_cstr(pos)) return false;
        } else {
            return false;
        }
    }
    return false;
}

bool append_additions_to_level(std::vector<uint8_t>& bytes,
                               const std::vector<Addition>& adds,
                               std::string& err) {
    static const char kMagic[] = "LevelGraphicsFile";
    const size_t magic_len = sizeof(kMagic) - 1;
    if (bytes.size() < magic_len + 8 ||
        std::memcmp(bytes.data(), kMagic, magic_len) != 0) {
        err = "level payload magic mismatch";
        return false;
    }
    uint32_t alive = 0;
    for (const auto& a : adds) {
        if (!a.removed) ++alive;
    }
    const size_t count_off = magic_len + 4;
    uint32_t entry_count =
        (uint32_t(bytes[count_off]) << 24) |
        (uint32_t(bytes[count_off + 1]) << 16) |
        (uint32_t(bytes[count_off + 2]) << 8) |
        uint32_t(bytes[count_off + 3]);
    entry_count += alive;
    bytes[count_off]     = uint8_t(entry_count >> 24);
    bytes[count_off + 1] = uint8_t(entry_count >> 16);
    bytes[count_off + 2] = uint8_t(entry_count >> 8);
    bytes[count_off + 3] = uint8_t(entry_count);

    float tmpl[20] = { 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1, 1, 1,
                       1024.0f, 0.025f, 16.0f, 64.0f, 32.0f, 0.0f, 0.1f };
    {
        float scanned[20];
        if (find_type2_template(bytes, scanned)) {
            for (int k = 13; k < 20; ++k) tmpl[k] = scanned[k];
        }
    }

    std::vector<uint8_t> tail;
    for (size_t ai = 0; ai < adds.size(); ++ai) {
        const Addition& a = adds[ai];
        if (a.removed) continue;
        put_u32_be(tail, 2);
        const std::string mp = lower_model_path(a.model_path);
        tail.insert(tail.end(), mp.begin(), mp.end());
        tail.push_back(0);
        tail.push_back(0);
        tail.push_back(0);
        tail.push_back(0);
        put_u32_be(tail, 1);
        tail.push_back(1);
        tail.push_back(0);
        tail.push_back(0);
        put_u64_be(tail, addition_instance_hash(mp, ai));
        float vals[20];
        std::memcpy(vals, tmpl, sizeof(vals));
        vals[0] = a.pos[0];
        vals[1] = a.pos[1];
        vals[2] = a.pos[2];
        vals[3] = 0.0f;
        vals[4] = 0.0f;
        vals[5] = 1.0f;
        const float yaw = a.yaw_deg * kDegToRad;
        vals[6] = std::sin(yaw);
        vals[7] = std::cos(yaw);
        vals[8] = 0.0f;
        vals[9] = vals[10] = vals[11] = vals[12] = 1.0f;
        for (int k = 0; k < 20; ++k) put_f32_be_v(tail, vals[k]);
    }
    bytes.insert(bytes.end(), tail.begin(), tail.end());
    return true;
}

bool patch_engine_resource_list(std::vector<uint8_t>& bytes,
                                const std::vector<uint32_t>& hashes,
                                bool& changed,
                                std::string& err) {
    changed = false;
    static const char kMagic[] = "EngineResourceList";
    const size_t magic_len = sizeof(kMagic) - 1;
    if (bytes.size() < magic_len + 9 ||
        std::memcmp(bytes.data(), kMagic, magic_len) != 0) {
        err = "engine_data magic mismatch";
        return false;
    }
    if (get_u32_be2(bytes.data() + magic_len) != 3) {
        err = "engine_data version != 3";
        return false;
    }
    const size_t cnt_off = magic_len + 5;
    uint32_t count = get_u32_be2(bytes.data() + cnt_off);
    const size_t list_off = cnt_off + 4;
    if (list_off + (uint64_t)count * 4 > bytes.size() ||
        count > (1u << 24)) {
        err = "engine_data resource list truncated";
        return false;
    }
    std::unordered_set<uint32_t> have;
    have.reserve(count * 2);
    for (uint32_t i = 0; i < count; ++i) {
        have.insert(get_u32_be2(bytes.data() + list_off + (size_t)i * 4));
    }
    std::vector<uint8_t> add;
    for (uint32_t h : hashes) {
        if (have.insert(h).second) put_u32_be(add, h);
    }
    if (add.empty()) return true;
    bytes.insert(bytes.begin() + (list_off + (size_t)count * 4),
                 add.begin(), add.end());
    count += (uint32_t)(add.size() / 4);
    bytes[cnt_off]     = uint8_t(count >> 24);
    bytes[cnt_off + 1] = uint8_t(count >> 16);
    bytes[cnt_off + 2] = uint8_t(count >> 8);
    bytes[cnt_off + 3] = uint8_t(count);
    changed = true;
    return true;
}

std::string clean_name(const std::string& s) {
    std::string n = s;
    while (!n.empty() && n.back() == '\0') n.pop_back();
    return n;
}

std::string norm_key(const std::string& s) {
    std::string k = clean_name(s);
    for (char& c : k) {
        if (c == '\\') c = '/';
        else c = (char)std::tolower((unsigned char)c);
    }
    return k;
}

bool nested_bank_has(BNKReader& nested, const std::string& want_key) {
    for (const auto& fe : nested.list_files()) {
        if (norm_key(fe.name) == want_key) return true;
    }
    return false;
}

bool find_in_nested_banks(const std::string& container_path,
                          const std::string& nested_suffix,
                          const std::string& want_key,
                          std::string& out_name,
                          std::vector<uint8_t>& out_payload) {
    const auto bc = BnkCache::get(container_path);
    const auto& files = bc.reader->list_files();
    for (size_t i = 0; i < files.size(); ++i) {
        const std::string key = norm_key(files[i].name);
        if (key.size() < nested_suffix.size() ||
            key.compare(key.size() - nested_suffix.size(),
                        nested_suffix.size(), nested_suffix) != 0) {
            continue;
        }
        std::vector<uint8_t> blob;
        try {
            blob = BnkCache::extract_bytes(container_path, (int)i);
        } catch (...) {
            continue;
        }
        try {
            BNKReader nested(std::move(blob));
            const auto& nf = nested.list_files();
            for (size_t j = 0; j < nf.size(); ++j) {
                if (norm_key(nf[j].name) == want_key) {
                    out_payload = nested.extract_index_bytes((int)j);
                    out_name = clean_name(nf[j].name);
                    return true;
                }
            }
        } catch (...) {
            continue;
        }
    }
    return false;
}

size_t collect_folder_from_nested_banks(
    const std::string& container_path,
    const std::string& nested_suffix,
    const std::string& folder_key,
    std::vector<BnkWriter::EntryAddition>& out) {
    const auto bc = BnkCache::get(container_path);
    const auto& files = bc.reader->list_files();
    for (size_t i = 0; i < files.size(); ++i) {
        const std::string key = norm_key(files[i].name);
        if (key.size() < nested_suffix.size() ||
            key.compare(key.size() - nested_suffix.size(),
                        nested_suffix.size(), nested_suffix) != 0) {
            continue;
        }
        std::vector<uint8_t> blob;
        try {
            blob = BnkCache::extract_bytes(container_path, (int)i);
        } catch (...) {
            continue;
        }
        try {
            BNKReader nested(std::move(blob));
            const auto& nf = nested.list_files();
            size_t found = 0;
            std::vector<BnkWriter::EntryAddition> local;
            for (size_t j = 0; j < nf.size(); ++j) {
                const std::string k = norm_key(nf[j].name);
                if (k.compare(0, folder_key.size(), folder_key) != 0) {
                    continue;
                }
                BnkWriter::EntryAddition a;
                a.name = clean_name(nf[j].name);
                a.payload = nested.extract_index_bytes((int)j);
                local.push_back(std::move(a));
                ++found;
            }
            if (found) {
                for (auto& a : local) out.push_back(std::move(a));
                return found;
            }
        } catch (...) {
            continue;
        }
    }
    return 0;
}

void collect_tex_refs(const std::vector<uint8_t>& mdl,
                      std::vector<std::string>& out) {
    size_t i = 0;
    const size_t n = mdl.size();
    while (i < n) {
        if (mdl[i] < 32 || mdl[i] > 126) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < n && mdl[j] >= 32 && mdl[j] <= 126) ++j;
        if (j - i >= 8) {
            std::string l = norm_key(
                std::string((const char*)mdl.data() + i, j - i));
            if (l.size() > 4 &&
                l.compare(l.size() - 4, 4, ".tex") == 0) {
                out.push_back(std::move(l));
            }
        }
        i = j + 1;
    }
}

bool patch_lmp_probes(std::vector<uint8_t>& gz,
                      const std::vector<Addition>& adds,
                      const std::vector<uint8_t>& lev_bytes,
                      bool& changed,
                      std::string& err) {
    changed = false;
    std::vector<uint8_t> raw;
    if (!gzip_inflate(gz, raw)) {
        err = "lmp gunzip failed";
        return false;
    }
    static const char kMagic[] = "LightmapFile";
    const size_t magic_len = sizeof(kMagic) - 1;
    if (raw.size() < magic_len + 64 ||
        std::memcmp(raw.data(), kMagic, magic_len) != 0) {
        err = "lmp magic mismatch";
        return false;
    }
    const size_t total = raw.size();
    size_t n = 0;
    for (size_t c = (total - 4) / 56; c >= 1; --c) {
        const size_t pos = total - 56 * c - 4;
        if (pos < magic_len + 4) continue;
        if (get_u32_be2(raw.data() + pos) == (uint32_t)c) {
            n = c;
            break;
        }
    }
    if (!n) {
        err = "lmp probe section not found";
        return false;
    }
    const size_t sec = total - 56 * n;

    std::unordered_map<uint64_t, size_t> probe_off;
    probe_off.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        uint64_t h = 0;
        for (int k = 0; k < 8; ++k) {
            h = (h << 8) | raw[sec + i * 56 + (size_t)k];
        }
        probe_off[h] = sec + i * 56;
    }

    struct Donor {
        float p[3];
        size_t off;
    };
    std::vector<Donor> donors;
    Level::EngineLevelInfo info;
    if (Level::ParseEngineLevel(lev_bytes, info)) {
        for (const auto& b : info.prop_blocks) {
            if (b.type != 2) continue;
            for (const auto& inst : b.instances) {
                auto it = probe_off.find(inst.hash);
                if (it == probe_off.end()) continue;
                Donor d;
                d.p[0] = inst.values[0];
                d.p[1] = inst.values[1];
                d.p[2] = inst.values[2];
                d.off = it->second;
                donors.push_back(d);
            }
        }
    }

    std::vector<uint8_t> add;
    for (size_t ai = 0; ai < adds.size(); ++ai) {
        const Addition& a = adds[ai];
        if (a.removed) continue;
        const uint64_t h =
            addition_instance_hash(lower_model_path(a.model_path), ai);
        if (probe_off.count(h)) continue;
        size_t donor_off = sec + (n - 1) * 56;
        float best = 3.4e38f;
        for (const auto& d : donors) {
            const float dx = d.p[0] - a.pos[0];
            const float dy = d.p[1] - a.pos[1];
            const float dz = d.p[2] - a.pos[2];
            const float dist = dx * dx + dy * dy + dz * dz;
            if (dist < best) {
                best = dist;
                donor_off = d.off;
            }
        }
        put_u64_be(add, h);
        add.insert(add.end(), raw.begin() + donor_off + 8,
                   raw.begin() + donor_off + 56);
    }
    if (add.empty()) return true;

    raw.insert(raw.end(), add.begin(), add.end());
    const uint32_t new_n = (uint32_t)(n + add.size() / 56);
    raw[sec - 4] = uint8_t(new_n >> 24);
    raw[sec - 3] = uint8_t(new_n >> 16);
    raw[sec - 2] = uint8_t(new_n >> 8);
    raw[sec - 1] = uint8_t(new_n);

    std::vector<uint8_t> gz_out;
    if (!gzip_deflate(raw, gz_out)) {
        err = "lmp gzip failed";
        return false;
    }
    gz.swap(gz_out);
    changed = true;
    return true;
}

void register_entry(EditEntry& e, const InstInfo& info) {
    if (e.registered) return;
    e.registered = true;
    e.lev_off = info.lev_off;
    e.lev_kind = info.lev_kind;
    if (info.gdb_off) {
        e.gdb_off[0] = info.gdb_off[0];
        e.gdb_off[1] = info.gdb_off[1];
        e.gdb_off[2] = info.gdb_off[2];
    }
    if (info.gdb_rot_off) {
        e.gdb_rot_off[0] = info.gdb_rot_off[0];
        e.gdb_rot_off[1] = info.gdb_rot_off[1];
        e.gdb_rot_off[2] = info.gdb_rot_off[2];
    }
    e.gdb_entity_hash = info.gdb_entity_hash;
    if (info.orig_pos) {
        e.orig[0] = info.orig_pos[0];
        e.orig[1] = info.orig_pos[1];
        e.orig[2] = info.orig_pos[2];
    }
    e.orig_rot[0] = info.orig_rot_deg[0];
    e.orig_rot[1] = info.orig_rot_deg[1];
    e.orig_rot[2] = info.orig_rot_deg[2];
}

std::filesystem::path additions_path() {
    std::string leaf = std::filesystem::path(st().entry.full_path)
        .filename().string();
    if (leaf.empty()) leaf = "level";
    return edited_levels_dir() / (leaf + ".additions.txt");
}

std::filesystem::path dirent_bak_path() {
    std::string leaf = std::filesystem::path(st().entry.full_path)
        .filename().string();
    if (leaf.empty()) leaf = "level";
    return edited_levels_dir() / (leaf + ".dirent.bak");
}

void record_dirent_bak(const std::string& vpath) {
    const auto p = dirent_bak_path();
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) return;
    const ISO::MountedFile* mf = ISO::IsoMount::instance().find(vpath);
    if (!mf) return;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return;
    f << vpath << '\t' << mf->sector << '\t' << mf->size << '\t'
      << ISO::IsoMount::instance().iso_size() << '\n';
    OutputLog::success("level edit: ISO dirent backup written to " +
                       p.string());
}

void restore_dirent_bak() {
    const auto p = dirent_bak_path();
    std::ifstream f(p);
    if (!f) return;
    std::string line;
    if (std::getline(f, line) && !line.empty()) {
        const size_t p0 = line.find('\t');
        if (p0 != std::string::npos) {
            const std::string vp = line.substr(0, p0);
            unsigned sec = 0, sz = 0;
            unsigned long long orig_iso = 0;
            const int got = std::sscanf(line.c_str() + p0 + 1,
                                        "%u\t%u\t%llu",
                                        &sec, &sz, &orig_iso);
            if (got >= 2 && !vp.empty()) {
                if (ISO::IsoMount::instance().repoint(vp, sec, sz)) {
                    OutputLog::success(
                        "level edit: ISO dirent restored for " + vp);
                }
                if (got >= 3 && orig_iso > 0) {
                    if (ISO::IsoMount::instance().truncate_to(orig_iso)) {
                        OutputLog::success(
                            "level edit: ISO trimmed back to original "
                            "size");
                    }
                }
            }
        }
    }
    f.close();
    std::error_code ec;
    std::filesystem::remove(p, ec);
}

void load_additions(ModuleState& s) {
    s.additions.clear();
    std::ifstream f(additions_path());
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        Addition a;
        size_t p0 = line.find('\t');
        if (p0 == std::string::npos) continue;
        a.model_path = line.substr(0, p0);
        if (a.model_path.empty()) continue;
        if (std::sscanf(line.c_str() + p0 + 1, "%f\t%f\t%f\t%f",
                        &a.pos[0], &a.pos[1], &a.pos[2],
                        &a.yaw_deg) < 3) continue;
        const size_t chest_tag = line.find("\tCHEST");
        const size_t key_tag = line.find("\tKEY");
        if (chest_tag != std::string::npos) {
            a.entity_kind = AdditionEntityKind::Chest;
            const size_t ctpl = line.find("\tCTPL", chest_tag);
            const size_t loot = line.find("\tLOOT", chest_tag);
            const size_t keys = line.find("\tKEYS", chest_tag);
            size_t stop = std::min(
                ctpl == std::string::npos ? line.size() : ctpl,
                loot == std::string::npos ? line.size() : loot);
            stop = std::min(
                stop, keys == std::string::npos ? line.size() : keys);
            size_t p = chest_tag + 6;
            while (p < line.size() && line[p] == '\t') {
                if (p >= stop) break;
                unsigned int h = 0;
                if (std::sscanf(line.c_str() + p + 1, "%x", &h) == 1 && h) {
                    a.chest_items.push_back(h);
                }
                p = line.find('\t', p + 1);
                if (p == std::string::npos) break;
            }
            if (ctpl != std::string::npos) {
                unsigned int tpl = 0, cf = 0, ct = 0, pf = 0;
                if (std::sscanf(line.c_str() + ctpl + 5,
                                "\t%x\t%x\t%x\t%x",
                                &tpl, &cf, &ct, &pf) >= 2 && tpl && cf) {
                    a.entity_template = tpl;
                    a.entity_comp_field = cf;
                    a.entity_comp_template = ct;
                    a.physics_file_hash = pf;
                }
            }
            if (loot != std::string::npos) {
                unsigned int lt = 0;
                if (std::sscanf(line.c_str() + loot + 5, "\t%x",
                                &lt) == 1) {
                    a.loot_table_record = lt;
                }
            }
            if (keys != std::string::npos) {
                int required = 0;
                if (std::sscanf(line.c_str() + keys + 5, "\t%d",
                                &required) == 1 && required > 0) {
                    a.silver_keys_needed = required;
                }
            }
        } else if (key_tag != std::string::npos) {
            a.entity_kind = AdditionEntityKind::SilverKey;
        } else {
            const size_t prop_tag = line.find("\tPROP");
            if (prop_tag != std::string::npos) {
                unsigned int tpl = 0, cf = 0, ct = 0, pf = 0, ht = 0;
                if (std::sscanf(line.c_str() + prop_tag + 5,
                                "\t%x\t%x\t%x\t%x\t%u",
                                &tpl, &cf, &ct, &pf, &ht) >= 3 &&
                    tpl && cf) {
                    a.entity_kind = AdditionEntityKind::GenericProp;
                    a.entity_template = tpl;
                    a.entity_comp_field = cf;
                    a.entity_comp_template = ct;
                    a.physics_file_hash = pf;
                    a.entity_has_text = ht != 0;
                    const size_t tt = line.find("\tTEXT\t", prop_tag);
                    if (tt != std::string::npos) {
                        a.entity_has_text = true;
                        const char* h = line.c_str() + tt + 6;
                        auto nib = [](char c) -> int {
                            if (c >= '0' && c <= '9') return c - '0';
                            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                            return -1;
                        };
                        for (size_t k = 0; h[k] && h[k + 1]; k += 2) {
                            const int hi = nib(h[k]);
                            const int lo = nib(h[k + 1]);
                            if (hi < 0 || lo < 0) break;
                            a.readable_text.push_back(
                                char((hi << 4) | lo));
                        }
                    }
                }
            }
        }
        if (a.entity_kind == AdditionEntityKind::Chest &&
            a.silver_keys_needed <= 0) {
            a.silver_keys_needed =
                SilverKeyChestRequirement(a.model_path);
        }
        s.additions.push_back(std::move(a));
    }
    if (!s.additions.empty()) {
        OutputLog::info("level edit: loaded " +
                        std::to_string(s.additions.size()) +
                        " placed model(s) from " +
                        additions_path().string());
    }
}

bool write_additions(const ModuleState& s, std::string& msg) {
    const auto path = additions_path();
    std::error_code ec;
    size_t alive = 0;
    for (const auto& a : s.additions) {
        if (!a.removed) ++alive;
    }
    if (alive == 0) {
        std::filesystem::remove(path, ec);
        return true;
    }
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        msg = "could not write " + path.string();
        return false;
    }
    for (const auto& a : s.additions) {
        if (a.removed) continue;
        f << a.model_path << '\t' << a.pos[0] << '\t' << a.pos[1] << '\t'
          << a.pos[2] << '\t' << a.yaw_deg;
        if (a.entity_kind == AdditionEntityKind::Chest) {
            f << "\tCHEST";
            char buf[80];
            for (uint32_t h : a.chest_items) {
                std::snprintf(buf, sizeof(buf), "%08X", h);
                f << '\t' << buf;
            }
            if (a.silver_keys_needed > 0) {
                f << "\tKEYS\t" << a.silver_keys_needed;
            }
            if (a.entity_template) {
                std::snprintf(buf, sizeof(buf),
                              "\tCTPL\t%08X\t%08X\t%08X\t%08X",
                              a.entity_template, a.entity_comp_field,
                              a.entity_comp_template,
                              a.physics_file_hash);
                f << buf;
            }
            if (a.loot_table_record) {
                std::snprintf(buf, sizeof(buf), "\tLOOT\t%08X",
                              a.loot_table_record);
                f << buf;
            }
        } else if (a.entity_kind == AdditionEntityKind::SilverKey) {
            f << "\tKEY";
        } else if (a.entity_kind == AdditionEntityKind::GenericProp) {
            char buf[80];
            std::snprintf(buf, sizeof(buf),
                          "\tPROP\t%08X\t%08X\t%08X\t%08X\t%d",
                          a.entity_template, a.entity_comp_field,
                          a.entity_comp_template, a.physics_file_hash,
                          a.entity_has_text ? 1 : 0);
            f << buf;
            if (a.entity_has_text && !a.readable_text.empty()) {
                f << "\tTEXT\t";
                static const char* hexd = "0123456789ABCDEF";
                for (unsigned char c : a.readable_text) {
                    f << hexd[c >> 4] << hexd[c & 15];
                }
            }
        }
        f << '\n';
    }
    return true;
}

void quat_mul(const float a[4], const float b[4], float out[4]) {
    out[0] = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
    out[1] = a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0];
    out[2] = a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3];
    out[3] = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
}

void quat_axis(const float axis[3], float deg, float out[4]) {
    const float h = deg * kDegToRad * 0.5f;
    const float s = std::sin(h);
    out[0] = axis[0] * s;
    out[1] = axis[1] * s;
    out[2] = axis[2] * s;
    out[3] = std::cos(h);
}

void euler_engine_to_preview_quat(const float rot_deg[3], float out[4]) {
    const float ax[3] = { 1, 0, 0 };
    const float ay[3] = { 0, 0, 1 };
    const float az[3] = { 0, 1, 0 };
    float qx[4], qy[4], qz[4], t[4];
    quat_axis(ax, rot_deg[0], qx);
    quat_axis(ay, rot_deg[1], qy);
    quat_axis(az, rot_deg[2], qz);
    quat_mul(qy, qx, t);
    quat_mul(qz, t, out);
}

}

int SilverKeyChestRequirement(const std::string& model_path)
{
    std::string leaf = model_path;
    const size_t slash = leaf.find_last_of("/\\");
    if (slash != std::string::npos) leaf.erase(0, slash + 1);

    std::string compact;
    compact.reserve(leaf.size());
    for (unsigned char c : leaf) {
        if (std::isalnum(c)) {
            compact.push_back(char(std::tolower(c)));
        }
    }
    constexpr const char* marker = "silverkeychest";
    const size_t marker_pos = compact.find(marker);
    if (marker_pos == std::string::npos) return 0;

    size_t p = marker_pos + std::strlen(marker);
    int required = 0;
    while (p < compact.size() && std::isdigit(
               static_cast<unsigned char>(compact[p]))) {
        required = std::min(999, required * 10 + (compact[p] - '0'));
        ++p;
    }
    return required > 0 ? required : 1;
}

void OnLevelLoaded(const FlatAssetEntry& entry) {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (s.dirty) {
        OutputLog::warn("level edit: unsaved object edits discarded "
                        "(level reloaded)");
    }
    const uint64_t rev = s.revision;
    s = ModuleState{};
    s.revision = rev + 1;
    s.entry = entry;
    s.available = true;
    s.lev.bnk_path = entry.bnk_path;
    s.lev.file_index = entry.file_index;
    fill_bnk_target(s.lev);
    load_additions(s);
    OutputLog::info(
        "level edit: tracking '" + entry.name + "' (" +
        (s.lev.compressed ? "chunked" : "raw") + " entry, slot " +
        std::to_string(s.lev.on_disk_size) + " B" +
        (s.lev.in_iso ? ", ISO-hosted)" : ")"));
}

void SetGdbSource(const std::string& bnk_path,
                  int file_index,
                  const std::string& loose_file) {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    s.gdb = FileTarget{};
    if (!loose_file.empty()) {
        s.gdb.file_path = loose_file;
        s.gdb.valid = true;
    } else {
        s.gdb.bnk_path = bnk_path;
        s.gdb.file_index = file_index;
        fill_bnk_target(s.gdb);
    }
    if (s.gdb.valid) {
        OutputLog::info(
            "level edit: gdb source " +
            (!s.gdb.file_path.empty()
                 ? s.gdb.file_path
                 : std::filesystem::path(s.gdb.bnk_path).filename()
                           .string() +
                       "#" + std::to_string(s.gdb.file_index) + " (" +
                       (s.gdb.compressed ? "chunked" : "raw") + ")"));
    }
}

bool Available() {
    std::lock_guard<std::mutex> lk(mtx());
    return st().available;
}
bool Enabled() {
    std::lock_guard<std::mutex> lk(mtx());
    return st().available && st().enabled;
}
bool Dirty() {
    std::lock_guard<std::mutex> lk(mtx());
    return st().dirty;
}

bool Saving() {
    std::lock_guard<std::mutex> lk(mtx());
    return st().saving;
}

size_t EditedCount() {
    std::lock_guard<std::mutex> lk(mtx());
    size_t n = 0;
    for (const auto& kv : st().edits) {
        if (kv.second.changed()) ++n;
    }
    return n;
}

uint64_t Revision() {
    std::lock_guard<std::mutex> lk(mtx());
    return st().revision;
}

void SetChestContents(uint32_t entity_hash,
                      const std::vector<uint32_t>& item_hashes)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (!s.available) return;
    s.contents_edits[entity_hash] = item_hashes;
    s.dirty = true;
    ++s.revision;
}

bool GetChestContents(uint32_t entity_hash, std::vector<uint32_t>& out)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    auto it = s.contents_edits.find(entity_hash);
    if (it == s.contents_edits.end()) return false;
    out = it->second;
    return true;
}

void ClearChestContents(uint32_t entity_hash)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    s.contents_edits.erase(entity_hash);
    ++s.revision;
}

size_t ChestContentsEditCount()
{
    std::lock_guard<std::mutex> lk(mtx());
    return st().contents_edits.size();
}

bool AdditionIsChest(int index)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    return index >= 0 && size_t(index) < s.additions.size() &&
           s.additions[size_t(index)].entity_kind ==
               AdditionEntityKind::Chest;
}

uint32_t GetAdditionLootTable(int index)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size()) return 0;
    return s.additions[size_t(index)].loot_table_record;
}


void SetAdditionLootTable(int index, uint32_t loot_record)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size()) return;
    s.additions[size_t(index)].loot_table_record = loot_record;
    s.dirty = true;
    ++s.revision;
}

bool GetAdditionChestItems(int index, std::vector<uint32_t>& out)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size() ||
        s.additions[size_t(index)].entity_kind !=
            AdditionEntityKind::Chest) {
        return false;
    }
    out = s.additions[size_t(index)].chest_items;
    return true;
}

void SetAdditionChestItems(int index, const std::vector<uint32_t>& items)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size()) return;
    s.additions[size_t(index)].chest_items = items;
    s.additions[size_t(index)].entity_kind = AdditionEntityKind::Chest;
    s.dirty = true;
    ++s.revision;
}

void MarkAdditionEntityKind(int index, AdditionEntityKind kind)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size()) return;
    auto& a = s.additions[size_t(index)];
    a.entity_kind = kind;
    if (kind != AdditionEntityKind::Chest) a.silver_keys_needed = 0;
    s.dirty = true;
    ++s.revision;
}

void MarkAdditionAsSilverKeyChest(int index, int silver_keys_needed)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size()) return;
    auto& a = s.additions[size_t(index)];
    a.entity_kind = AdditionEntityKind::Chest;
    a.silver_keys_needed = std::max(1, silver_keys_needed);
    s.dirty = true;
    ++s.revision;
}

void MarkAdditionAsPropEntity(int index,
                              uint32_t template_hash,
                              uint32_t comp_field_hash,
                              uint32_t comp_template_hash,
                              uint32_t physics_file_hash,
                              bool has_text_tags)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size()) return;
    auto& a = s.additions[size_t(index)];
    a.entity_kind = AdditionEntityKind::GenericProp;
    a.entity_template = template_hash;
    a.entity_comp_field = comp_field_hash;
    a.entity_comp_template = comp_template_hash;
    a.physics_file_hash = physics_file_hash;
    a.entity_has_text = has_text_tags;
    s.dirty = true;
    ++s.revision;
}

bool AdditionIsReadable(int index)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    return index >= 0 && size_t(index) < s.additions.size() &&
           s.additions[size_t(index)].entity_has_text;
}

bool GetAdditionReadableText(int index, std::string& out)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size() ||
        !s.additions[size_t(index)].entity_has_text) {
        return false;
    }
    out = s.additions[size_t(index)].readable_text;
    return true;
}

void SetAdditionReadableText(int index, const std::string& text)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size()) return;
    s.additions[size_t(index)].readable_text = text;
    s.dirty = true;
    ++s.revision;
}

void SetEntityTextEdit(uint32_t tag_hash, const std::string& utf8)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (!s.available || tag_hash == 0) return;
    s.text_edits[tag_hash] = utf8;
    s.dirty = true;
    ++s.revision;
}

bool GetEntityTextEdit(uint32_t tag_hash, std::string& out)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    auto it = s.text_edits.find(tag_hash);
    if (it == s.text_edits.end()) return false;
    out = it->second;
    return true;
}

size_t TextEditCount()
{
    std::lock_guard<std::mutex> lk(mtx());
    return st().text_edits.size();
}

int AddGenerator(const float pos[3], const std::string& creature_name,
                 uint32_t creature_entity,
                 const std::vector<std::string>& asset_models)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (!s.available || s.saving) return -1;
    GeneratorAddition g;
    g.pos[0] = pos[0];
    g.pos[1] = pos[1];
    g.pos[2] = pos[2];
    g.creature_name = creature_name;
    g.creature_entity = creature_entity;
    if (!g.creature_entity) {
        for (const auto& creature : g_level_creature_catalog) {
            if (creature.name == creature_name) {
                g.creature_entity = creature.entity_hash;
                break;
            }
        }
    }
    g.asset_models = asset_models;
    g.spawn_points.push_back({pos[0] + 1.5f, pos[1], pos[2]});
    s.generators.push_back(std::move(g));
    s.dirty = true;
    ++s.revision;
    return int(s.generators.size()) - 1;
}

void GetGenerators(std::vector<GeneratorAddition>& out)
{
    std::lock_guard<std::mutex> lk(mtx());
    out = st().generators;
}

void MovePendingGenerator(int index, const float pos[3])
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.generators.size()) return;
    auto& g = s.generators[size_t(index)];
    g.pos[0] = pos[0];
    g.pos[1] = pos[1];
    g.pos[2] = pos[2];
    s.dirty = true;
    ++s.revision;
}

void RemoveGenerator(int index)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.generators.size()) return;
    s.generators[size_t(index)].removed = true;
    s.dirty = true;
    ++s.revision;
}

void AddGeneratorSpawnPoint(int index, const float pos[3])
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.generators.size()) return;
    s.generators[size_t(index)].spawn_points.push_back(
        {pos[0], pos[1], pos[2]});
    s.dirty = true;
    ++s.revision;
}

void RemoveGeneratorSpawnPoint(int index, int sp_index)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.generators.size()) return;
    auto& sp = s.generators[size_t(index)].spawn_points;
    if (sp_index < 0 || size_t(sp_index) >= sp.size()) return;
    sp.erase(sp.begin() + sp_index);
    s.dirty = true;
    ++s.revision;
}

void AddSpawnPointToExisting(uint32_t generator_entity,
                             uint32_t spawn_points_record,
                             const float pos[3])
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (!s.available || s.saving || !spawn_points_record) return;
    ModuleState::SpawnPointAdd a;
    a.generator_entity = generator_entity;
    a.spawn_points_record = spawn_points_record;
    a.pos[0] = pos[0];
    a.pos[1] = pos[1];
    a.pos[2] = pos[2];
    s.spawn_point_adds.push_back(a);
    s.dirty = true;
    ++s.revision;
}

void RemoveSpawnPointFromExisting(uint32_t generator_entity,
                                  uint32_t spawn_points_record,
                                  uint32_t spawn_point_entity)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (!s.available || s.saving || !spawn_points_record ||
        !spawn_point_entity) {
        return;
    }
    for (const auto& deletion : s.spawn_point_deletes) {
        if (deletion.spawn_point_entity == spawn_point_entity) return;
    }
    ModuleState::SpawnPointDelete deletion;
    deletion.generator_entity = generator_entity;
    deletion.spawn_points_record = spawn_points_record;
    deletion.spawn_point_entity = spawn_point_entity;
    s.spawn_point_deletes.push_back(deletion);
    s.dirty = true;
    ++s.revision;
}

bool SpawnPointRemovalPending(uint32_t spawn_point_entity)
{
    std::lock_guard<std::mutex> lk(mtx());
    for (const auto& deletion : st().spawn_point_deletes) {
        if (deletion.spawn_point_entity == spawn_point_entity) return true;
    }
    return false;
}

size_t PendingSpawnPointCount()
{
    std::lock_guard<std::mutex> lk(mtx());
    return st().spawn_point_adds.size();
}

void GetPendingSpawnPoints(std::vector<PendingSpawnPoint>& out)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    out.clear();
    for (size_t gi = 0; gi < s.generators.size(); ++gi) {
        const auto& g = s.generators[gi];
        if (g.removed) continue;
        for (size_t si = 0; si < g.spawn_points.size(); ++si) {
            PendingSpawnPoint p;
            p.id = int((gi << 8) | si);
            p.pos[0] = g.spawn_points[si][0];
            p.pos[1] = g.spawn_points[si][1];
            p.pos[2] = g.spawn_points[si][2];
            p.label = "new spawn point (" + g.creature_name + ")";
            out.push_back(std::move(p));
        }
    }
    for (size_t i = 0; i < s.spawn_point_adds.size(); ++i) {
        PendingSpawnPoint p;
        p.id = 0x1000000 + int(i);
        p.pos[0] = s.spawn_point_adds[i].pos[0];
        p.pos[1] = s.spawn_point_adds[i].pos[1];
        p.pos[2] = s.spawn_point_adds[i].pos[2];
        p.label = "new spawn point";
        out.push_back(std::move(p));
    }
}

void MovePendingSpawnPoint(int id, const float pos[3])
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (id >= 0x1000000) {
        const size_t i = size_t(id - 0x1000000);
        if (i >= s.spawn_point_adds.size()) return;
        s.spawn_point_adds[i].pos[0] = pos[0];
        s.spawn_point_adds[i].pos[1] = pos[1];
        s.spawn_point_adds[i].pos[2] = pos[2];
    } else {
        const size_t gi = size_t(id) >> 8;
        const size_t si = size_t(id) & 0xFF;
        if (gi >= s.generators.size() ||
            si >= s.generators[gi].spawn_points.size()) {
            return;
        }
        s.generators[gi].spawn_points[si] = {pos[0], pos[1], pos[2]};
    }
    s.dirty = true;
    ++s.revision;
}

void RemovePendingSpawnPoint(int id)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (id >= 0x1000000) {
        const size_t i = size_t(id - 0x1000000);
        if (i >= s.spawn_point_adds.size()) return;
        s.spawn_point_adds.erase(s.spawn_point_adds.begin() + i);
    } else {
        const size_t gi = size_t(id) >> 8;
        const size_t si = size_t(id) & 0xFF;
        if (gi >= s.generators.size() ||
            si >= s.generators[gi].spawn_points.size()) {
            return;
        }
        auto& sp = s.generators[gi].spawn_points;
        sp.erase(sp.begin() + si);
    }
    s.dirty = true;
    ++s.revision;
}

bool SetEnabled(bool on, std::string& msg) {
    FileTarget lev_t, gdb_t;
    {
        std::lock_guard<std::mutex> lk(mtx());
        auto& s = st();
        if (!on) {
            s.enabled = false;
            msg = "level edit mode off";
            return true;
        }
        if (!s.available) {
            msg = "no level loaded";
            return false;
        }
        if (s.enabled) {
            msg = "level edit mode on";
            return true;
        }
        if (s.saving) {
            msg = "busy";
            return false;
        }
        s.saving = true;
        lev_t = s.lev;
        gdb_t = s.gdb;
    }

    std::unordered_set<std::string> backed;
    std::string berr;
    const bool ok = ensure_backup(lev_t, "lev", backed, berr) &&
                    ensure_backup(gdb_t, "gdb", backed, berr);

    {
        std::lock_guard<std::mutex> lk(mtx());
        auto& s = st();
        s.saving = false;
        if (!ok) {
            msg = berr;
            return false;
        }
        if (!s.available) {
            msg = "level changed during backup";
            return false;
        }
        s.enabled = true;
        msg = "level edit mode on";
    }
    return true;
}

bool EditFor(uint32_t selection_id,
             float out_pos_delta[3],
             float out_rot_delta_deg[3]) {
    std::lock_guard<std::mutex> lk(mtx());
    auto it = st().edits.find(selection_id);
    if (it == st().edits.end() || !it->second.changed()) return false;
    const EditEntry& e = it->second;
    if (out_pos_delta) {
        out_pos_delta[0] = e.delta[0];
        out_pos_delta[1] = e.delta[1];
        out_pos_delta[2] = e.delta[2];
    }
    if (out_rot_delta_deg) {
        out_rot_delta_deg[0] = e.rot_deg[0];
        out_rot_delta_deg[1] = e.rot_deg[1];
        out_rot_delta_deg[2] = e.rot_deg[2];
    }
    return true;
}

void AddMove(uint32_t selection_id, const float step[3],
             const InstInfo& info) {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (s.saving) return;
    auto& e = s.edits[selection_id];
    register_entry(e, info);
    e.delta[0] += step[0];
    e.delta[1] += step[1];
    e.delta[2] += step[2];
    s.dirty = true;
    ++s.revision;
}

void AddRotate(uint32_t selection_id, const float step_deg[3],
               const InstInfo& info) {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (s.saving) return;
    auto& e = s.edits[selection_id];
    register_entry(e, info);
    for (int i = 0; i < 3; ++i) {
        e.rot_deg[i] += step_deg[i];
        while (e.rot_deg[i] > 180.0f)  e.rot_deg[i] -= 360.0f;
        while (e.rot_deg[i] < -180.0f) e.rot_deg[i] += 360.0f;
    }
    s.dirty = true;
    ++s.revision;
}

void SetDeleted(uint32_t selection_id, const InstInfo& info) {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (s.saving) return;
    auto& e = s.edits[selection_id];
    register_entry(e, info);
    e.deleted = true;
    s.dirty = true;
    ++s.revision;
}

int AddPlacement(const std::string& model_path, const float pos[3]) {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (!s.available || s.saving || model_path.empty()) return -1;
    Addition a;
    a.model_path = model_path;
    a.pos[0] = pos[0];
    a.pos[1] = pos[1];
    a.pos[2] = pos[2];
    s.additions.push_back(std::move(a));
    s.dirty = true;
    ++s.revision;
    return (int)s.additions.size() - 1;
}

void GetAdditions(std::vector<Addition>& out) {
    std::lock_guard<std::mutex> lk(mtx());
    out = st().additions;
}

void CollectPreviewXforms(
    std::unordered_map<uint32_t, EditXform>& out) {
    std::lock_guard<std::mutex> lk(mtx());
    out.clear();
    for (const auto& kv : st().edits) {
        const EditEntry& e = kv.second;
        if (!e.changed()) continue;
        EditXform x;
        x.off[0] = e.delta[0];
        x.off[1] = e.delta[2];
        x.off[2] = e.delta[1];
        x.pivot[0] = e.orig[0];
        x.pivot[1] = e.orig[2];
        x.pivot[2] = e.orig[1];
        if (e.rotated()) {
            euler_engine_to_preview_quat(e.rot_deg, x.quat);
        }
        x.has_rs = e.rotated();
        x.deleted = e.deleted;
        out[kv.first] = x;
    }
}

void PushUndoSnapshot(const std::vector<uint32_t>& ids) {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (s.saving) return;
    UndoStep step;
    step.before.reserve(ids.size());
    for (uint32_t id : ids) {
        auto it = s.edits.find(id);
        UndoState u{{0, 0, 0}, {0, 0, 0}, false};
        if (it != s.edits.end()) {
            const EditEntry& e = it->second;
            u.delta[0] = e.delta[0];
            u.delta[1] = e.delta[1];
            u.delta[2] = e.delta[2];
            u.rot_deg[0] = e.rot_deg[0];
            u.rot_deg[1] = e.rot_deg[1];
            u.rot_deg[2] = e.rot_deg[2];
            u.deleted = e.deleted;
        }
        step.before.emplace_back(id, u);
    }
    s.undo_stack.push_back(std::move(step));
    if (s.undo_stack.size() > kMaxUndoSteps) {
        s.undo_stack.erase(s.undo_stack.begin());
    }
}

bool Undo() {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (s.saving) return false;
    if (s.undo_stack.empty()) return false;
    UndoStep step = std::move(s.undo_stack.back());
    s.undo_stack.pop_back();
    for (const auto& kv : step.before) {
        auto it = s.edits.find(kv.first);
        if (it == s.edits.end()) continue;
        EditEntry& e = it->second;
        const UndoState& u = kv.second;
        e.delta[0] = u.delta[0];
        e.delta[1] = u.delta[1];
        e.delta[2] = u.delta[2];
        e.rot_deg[0] = u.rot_deg[0];
        e.rot_deg[1] = u.rot_deg[1];
        e.rot_deg[2] = u.rot_deg[2];
        e.deleted = u.deleted;
    }
    s.dirty = true;
    ++s.revision;
    return true;
}

namespace {

bool apply_chest_contents(GdbEdit::GdbFile& g,
                          uint32_t entity_hash,
                          const std::vector<uint32_t>& items,
                          uint32_t loot_table_record,
                          std::string& err)
{
    constexpr uint32_t kParent = 0x5F6317D5u;
    constexpr uint32_t kNull = 0x811C9DC5u;
    constexpr uint32_t kInvCompA = 0x1C7D7B74u;
    constexpr uint32_t kInvCompB = 0x73AB8B6Au;
    constexpr uint32_t kInitialItems = 0x9C24A50Du;
    constexpr uint32_t kChestInventoryBase = 0xB5E7A074u;
    constexpr uint32_t kEmptyInitialItemsBase = 0xBE56F154u;
    constexpr uint32_t kItemRepopulationBase = 0x088F64E7u;

    if (g.FindRecord(entity_hash) < 0) {
        err = "entity record not in level gdb";
        return false;
    }

    std::vector<GdbEdit::Field> fields;
    fields.reserve(items.size() + 1);
    std::unordered_set<uint32_t> used{kParent};
    GdbEdit::Field parent_field;
    parent_field.hash = kParent;
    parent_field.type = 6;
    parent_field.value = kEmptyInitialItemsBase;
    parent_field.decl = 0;
    fields.push_back(parent_field);
    for (size_t i = 0; i < items.size(); ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "F2ABItem%zu", i);
        uint32_t fh = 0x811C9DC5u;
        for (const char* p = name; *p; ++p) {
            fh *= 0x01000193u;
            fh ^= uint32_t(uint8_t(*p));
        }
        while (!used.insert(fh).second) ++fh;
        GdbEdit::Field f;
        f.hash = fh;
        f.type = 7;
        f.value = items[i];
        f.decl = uint32_t(i + 1);
        fields.push_back(f);
    }
    const uint32_t list_hash = g.AllocRecordHash();
    if (!g.AddRecord(list_hash, std::move(fields), 1)) {
        err = "list record append failed";
        return false;
    }

    GdbEdit::Field f;
    uint32_t inv_hash = 0;
    uint32_t inv_field_hash = kInvCompA;
    bool have_local_field = false;
    if (g.FindLocalField(entity_hash, kInvCompA, f)) {
        have_local_field = true;
        inv_field_hash = kInvCompA;
    } else if (g.FindLocalField(entity_hash, kInvCompB, f)) {
        have_local_field = true;
        inv_field_hash = kInvCompB;
    }
    if (have_local_field && f.value != 0 && f.value != kNull &&
        g.FindRecord(f.value) >= 0) {
        inv_hash = f.value;
    } else {

        uint32_t inherit =
            (have_local_field && f.value != 0 && f.value != kNull)
                ? f.value
                : 0;
        if (!inherit) inherit = kChestInventoryBase;
        inv_hash = g.AllocRecordHash();
        std::vector<GdbEdit::Field> fs;
        if (inherit) {
            GdbEdit::Field pf;
            pf.hash = kParent;
            pf.type = 6;
            pf.value = inherit;
            pf.decl = 0;
            fs.push_back(pf);
        }
        if (!g.AddRecord(inv_hash, fs, 1)) {
            err = "inventory record append failed";
            return false;
        }
        if (have_local_field) {
            if (!g.SetFieldValue(entity_hash, inv_field_hash, inv_hash)) {
                err = "inventory field rewrite failed";
                return false;
            }
        } else if (!g.AddField(entity_hash, kInvCompA, 6, inv_hash, 1)) {
            err = "inventory field append failed";
            return false;
        }
    }

    GdbEdit::Field items_field;
    if (g.FindLocalField(inv_hash, kInitialItems, items_field)) {
        if (!g.SetFieldValue(inv_hash, kInitialItems, list_hash)) {
            err = "InitialItems rewrite failed";
            return false;
        }
    } else if (!g.AddField(inv_hash, kInitialItems, 6, list_hash, 1)) {
        err = "InitialItems append failed";
        return false;
    }

    if (loot_table_record != 0) {
        constexpr uint32_t kItemRepopulationData = 0xFDF2E63Au;
        constexpr uint32_t kPotentialItems = 0x4FB47937u;
        constexpr uint32_t kChanceOfRespawning = 0x993B9AA2u;
        const uint32_t repop_hash = g.AllocRecordHash();
        std::vector<GdbEdit::Field> rf;
        GdbEdit::Field f2;
        f2.hash = kParent;
        f2.type = 6;
        f2.value = kItemRepopulationBase;
        f2.decl = 0;
        rf.push_back(f2);
        f2.hash = kPotentialItems;
        f2.type = 6;
        f2.value = loot_table_record;
        f2.decl = 1;
        rf.push_back(f2);
        f2.hash = kChanceOfRespawning;
        f2.type = 3;
        f2.decl = 2;
        const float chance = 1.0f;
        std::memcpy(&f2.value, &chance, 4);
        rf.push_back(f2);
        if (!g.AddRecord(repop_hash, rf, 1)) {
            err = "repopulation record append failed";
            return false;
        }
        GdbEdit::Field rp;
        if (g.FindLocalField(inv_hash, kItemRepopulationData, rp)) {
            if (!g.SetFieldValue(inv_hash, kItemRepopulationData,
                                 repop_hash)) {
                err = "ItemRepopulationData rewrite failed";
                return false;
            }
        } else if (!g.AddField(inv_hash, kItemRepopulationData, 6,
                               repop_hash, 2)) {
            err = "ItemRepopulationData append failed";
            return false;
        }
    }
    return true;
}

uint32_t create_entity_addition(GdbEdit::GdbFile& g,
                                const Addition& a,
                                std::unordered_map<uint32_t, std::string>&
                                    babel_edits,
                                std::string& err)
{
    constexpr uint32_t kParent = 0x5F6317D5u;
    constexpr uint32_t kNull = 0x811C9DC5u;
    constexpr uint32_t kVecX = 0x050C5D47u;
    constexpr uint32_t kVecY = 0x050C5D46u;
    constexpr uint32_t kVecZ = 0x050C5D45u;
    constexpr uint32_t kPosition = 0xBD7C27D4u;
    constexpr uint32_t kRotation = 0x21EBC83Bu;
    constexpr uint32_t kKeyframed = 0x6B177DD0u;
    constexpr uint32_t kDynamic = 0xFC8A57C5u;

    constexpr uint32_t kChestTemplate = 0x8C13FB53u;
    constexpr uint32_t kChestTemplateKeyframed = 0x9AB9A90Cu;
    constexpr uint32_t kChestPositionTemplate = 0x2CA65C69u;
    constexpr uint32_t kChestRotationTemplate = 0xC10576FAu;
    constexpr uint32_t kChestBaseTemplate = 0x3CABC379u;
    constexpr uint32_t kChestGraphicBase = 0x8AAF44E3u;
    constexpr uint32_t kChestComponentBase = 0x8DD05F71u;
    constexpr uint32_t kChestSkeleton = 0xF94F44C0u;
    constexpr uint32_t kGraphicAppearanceAnimatedMesh = 0x21D312CAu;
    constexpr uint32_t kChestComponent = 0x379C25A9u;
    constexpr uint32_t kObjectComponent = 0xF1A5EEB9u;
    constexpr uint32_t kModelFile = 0x0C17DB4Eu;
    constexpr uint32_t kSkeletonFile = 0xC3D06E3Au;
    constexpr uint32_t kSilverKeysNeeded = 0xB208E419u;
    constexpr uint32_t kObjectComponentBase = 0xA4EB5624u;
    constexpr uint32_t kMaterial = 0x6D04D9A2u;
    constexpr uint32_t kSilverChestMaterial = 0xE294B870u;

    constexpr uint32_t kSilverKeyTemplate = 0x4AB4C31Au;
    constexpr uint32_t kSilverKeyTemplateDynamic = 0xEA7C60E5u;
    constexpr uint32_t kSilverKeyPositionTemplate = 0xEF585A66u;
    constexpr uint32_t kSilverKeyRotationTemplate = 0x9BB57EABu;

    const bool is_key = a.entity_kind == AdditionEntityKind::SilverKey;
    const bool is_prop = a.entity_kind == AdditionEntityKind::GenericProp;
    const bool is_silver_key_chest =
        a.entity_kind == AdditionEntityKind::Chest &&
        a.silver_keys_needed > 0;
    // Always author a fresh silver-chest template.  Older editor builds may
    // have left template-looking records with invalid component schemas in the
    // level, so reusing a model-matched donor can perpetuate the bad record.
    const bool author_silver_chest_graphics = is_silver_key_chest;
    uint32_t entity_template =
        is_key ? kSilverKeyTemplate : kChestTemplate;
    uint32_t comp_field = is_key ? kDynamic : kKeyframed;
    uint32_t comp_template =
        is_key ? kSilverKeyTemplateDynamic : kChestTemplateKeyframed;
    if (is_prop) {
        if (!a.entity_template || !a.entity_comp_field) {
            err = "prop entity missing template info";
            return 0;
        }
        entity_template = a.entity_template;
        comp_field = a.entity_comp_field;
        comp_template = a.entity_comp_template;
    } else if (a.entity_kind == AdditionEntityKind::Chest &&
               !is_silver_key_chest &&
               a.entity_template && a.entity_comp_field) {
        entity_template = a.entity_template;
        comp_field = a.entity_comp_field;
        comp_template = a.entity_comp_template;
    }
    uint32_t position_template = 0;
    uint32_t rotation_template = 0;
    GdbEdit::Field template_field;
    if (comp_template && comp_template != kNull) {
        if (g.FindLocalField(comp_template, kPosition, template_field) &&
            template_field.type == 6) {
            position_template = template_field.value;
        }
        if (g.FindLocalField(comp_template, kRotation, template_field) &&
            template_field.type == 6) {
            rotation_template = template_field.value;
        }
    }
    if (is_key) {
        if (!position_template) {
            position_template = kSilverKeyPositionTemplate;
        }
        if (!rotation_template) {
            rotation_template = kSilverKeyRotationTemplate;
        }
    } else if (a.entity_kind == AdditionEntityKind::Chest) {
        if (!position_template) position_template = kChestPositionTemplate;
        if (!rotation_template) rotation_template = kChestRotationTemplate;
    }

    auto fbits = [](float f) {
        uint32_t u;
        std::memcpy(&u, &f, 4);
        return u;
    };

    auto vec3_record = [&](float x, float y, float z,
                           uint32_t parent) -> uint32_t {
        const uint32_t h = g.AllocRecordHash();
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kVecZ; f.type = 3; f.value = fbits(z); f.decl = 3;
        fs.push_back(f);
        f.hash = kVecY; f.type = 3; f.value = fbits(y); f.decl = 2;
        fs.push_back(f);
        f.hash = kVecX; f.type = 3; f.value = fbits(x); f.decl = 1;
        fs.push_back(f);
        if (parent) {
            f.hash = kParent; f.type = 6; f.value = parent; f.decl = 0;
            fs.push_back(f);
        }
        return g.AddRecord(h, fs, 1) ? h : 0;
    };

    const uint32_t pos_rec = vec3_record(
        a.pos[0], a.pos[1], a.pos[2], position_template);
    const float yaw = a.yaw_deg * 0.01745329252f;
    const uint32_t rot_rec = vec3_record(
        yaw, 0.0f, 0.0f, rotation_template);
    if (!pos_rec || !rot_rec) {
        err = "transform record append failed";
        return 0;
    }

    const uint32_t comp_rec = g.AllocRecordHash();
    {
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kRotation; f.type = 6; f.value = rot_rec; f.decl = 1;
        fs.push_back(f);
        if (comp_template && comp_template != kNull) {
            f.hash = kParent; f.type = 6; f.value = comp_template;
            f.decl = 2;
            fs.push_back(f);
        }
        f.hash = kPosition; f.type = 6; f.value = pos_rec; f.decl = 0;
        fs.push_back(f);
        if (!g.AddRecord(comp_rec, fs, 1)) {
            err = "transform component append failed";
            return 0;
        }
    }

    uint32_t tags_rec = 0;
    uint32_t text_tag = 0;
    if (is_prop && a.entity_has_text && !a.readable_text.empty()) {
        constexpr uint32_t kTextTag = 0xB8F45248u;
        text_tag = TextBank::AllocTagHash(
            lower_model_path(a.model_path) + "#f2ab_text");
        while (babel_edits.count(text_tag) != 0 || text_tag == 0) {
            ++text_tag;
        }
        tags_rec = g.AllocRecordHash();
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kTextTag; f.type = 4; f.value = text_tag;
        f.decl = 0;
        fs.push_back(f);
        if (!g.AddRecord(tags_rec, fs, 1)) {
            err = "readable component record append failed";
            return 0;
        }
    }

    uint32_t graphic_rec = 0;
    if (author_silver_chest_graphics) {
        const std::string model_path = lower_model_path(a.model_path);
        const uint32_t model_hash = fnv1_32(model_path);
        graphic_rec = g.AllocRecordHash();
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kModelFile; f.type = 4; f.value = model_hash; f.decl = 1;
        fs.push_back(f);
        f.hash = kParent; f.type = 6; f.value = kChestGraphicBase;
        f.decl = 0;
        fs.push_back(f);
        f.hash = kSkeletonFile; f.type = 4; f.value = kChestSkeleton;
        f.decl = 2;
        fs.push_back(f);
        if (!g.AddRecord(graphic_rec, fs, 1)) {
            err = "silver-key chest graphics append failed";
            return 0;
        }
        g.AddDictString(model_hash, model_path);
    }

    uint32_t chest_rec = 0;
    if (author_silver_chest_graphics) {
        chest_rec = g.AllocRecordHash();
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kParent; f.type = 6; f.value = kChestComponentBase;
        f.decl = 0;
        fs.push_back(f);
        f.hash = kSilverKeysNeeded; f.type = 1;
        f.value = uint32_t(a.silver_keys_needed);
        f.decl = 1;
        fs.push_back(f);
        if (!g.AddRecord(chest_rec, fs, 1)) {
            err = "silver-key chest lock append failed";
            return 0;
        }
    }

    uint32_t object_rec = 0;
    if (author_silver_chest_graphics) {
        object_rec = g.AllocRecordHash();
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kParent; f.type = 6; f.value = kObjectComponentBase;
        f.decl = 0; fs.push_back(f);
        f.hash = kMaterial; f.type = 7; f.value = kSilverChestMaterial;
        f.decl = 1; fs.push_back(f);
        if (!g.AddRecord(object_rec, fs, 1)) {
            err = "silver-key chest object component append failed";
            return 0;
        }
    }

    // Native silver-key chests are a placed entity that inherits a separate
    // chest template.  The template owns the graphics, lock and physics while
    // the placed entity owns only its transform and inventory.  Putting these
    // components directly on the placed entity reloads in the editor, but the
    // game does not instantiate it as a chest.
    if (author_silver_chest_graphics) {
        const uint32_t silver_chest_template = g.AllocRecordHash();
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kGraphicAppearanceAnimatedMesh;
        f.type = 6; f.value = graphic_rec; f.decl = 2; fs.push_back(f);
        f.hash = kChestComponent;
        f.type = 6; f.value = chest_rec; f.decl = 0; fs.push_back(f);
        f.hash = kParent;
        f.type = 6; f.value = kChestBaseTemplate; f.decl = 1;
        fs.push_back(f);
        f.hash = kKeyframed;
        f.type = 6; f.value = kChestTemplateKeyframed; f.decl = 3;
        fs.push_back(f);
        f.hash = kObjectComponent;
        f.type = 6; f.value = object_rec; f.decl = 4; fs.push_back(f);
        if (!g.AddRecord(silver_chest_template, fs, 0)) {
            err = "silver-key chest template append failed";
            return 0;
        }
        entity_template = silver_chest_template;
    }

    const uint32_t entity_rec = g.AllocRecordHash();
    {
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kParent; f.type = 6; f.value = entity_template;
        f.decl = 2;
        fs.push_back(f);
        f.hash = comp_field; f.type = 6; f.value = comp_rec;
        f.decl = 0;
        fs.push_back(f);
        if (tags_rec) {
            f.hash = 0x89ABB47Eu; f.type = 6; f.value = tags_rec;
            f.decl = 1;
            fs.push_back(f);
        }
        if (!g.AddRecord(entity_rec, fs, 0)) {
            err = "entity record append failed";
            return 0;
        }
    }
    if (tags_rec && text_tag) {
        babel_edits[text_tag] = a.readable_text;
    }

    if (a.entity_kind == AdditionEntityKind::Chest &&
        !apply_chest_contents(g, entity_rec, a.chest_items,
                              a.loot_table_record, err)) {
        return 0;
    }
    return entity_rec;
}

uint32_t create_spawn_point_transform(GdbEdit::GdbFile& g,
                                      const Gdb::SpawnDonorInfo& d,
                                      const float pos[3],
                                      std::string& err)
{
    constexpr uint32_t kParent = 0x5F6317D5u;
    constexpr uint32_t kVecX = 0x050C5D47u;
    constexpr uint32_t kVecY = 0x050C5D46u;
    constexpr uint32_t kVecZ = 0x050C5D45u;
    constexpr uint32_t kPosition = 0xBD7C27D4u;
    constexpr uint32_t kRotation = 0x21EBC83Bu;
    constexpr uint32_t kDefaultTransformParent = 0x3E64FFF3u;
    constexpr uint32_t kDefaultPositionParent = 0x4771F72Fu;
    constexpr uint32_t kDefaultRotationParent = 0xEBB606E5u;
    auto fbits = [](float f) {
        uint32_t u;
        std::memcpy(&u, &f, 4);
        return u;
    };
    auto vec3_record = [&](float x, float y, float z,
                           uint32_t parent) -> uint32_t {
        const uint32_t h = g.AllocRecordHash();
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kVecZ; f.type = 3; f.value = fbits(z); f.decl = 3;
        fs.push_back(f);
        f.hash = kVecY; f.type = 3; f.value = fbits(y); f.decl = 2;
        fs.push_back(f);
        f.hash = kVecX; f.type = 3; f.value = fbits(x); f.decl = 1;
        fs.push_back(f);
        f.hash = kParent; f.type = 6; f.value = parent; f.decl = 0;
        fs.push_back(f);
        return g.AddRecord(h, fs, 1) ? h : 0;
    };
    const uint32_t pos_rec = vec3_record(
        pos[0], pos[1], pos[2],
        d.sp_position_parent ? d.sp_position_parent
                             : kDefaultPositionParent);
    const uint32_t rot_rec = vec3_record(
        0, 0, 0,
        d.sp_rotation_parent ? d.sp_rotation_parent
                             : kDefaultRotationParent);
    if (!pos_rec || !rot_rec) {
        err = "spawn point transform append failed";
        return 0;
    }
    const uint32_t comp_rec = g.AllocRecordHash();
    {
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kRotation; f.type = 6; f.value = rot_rec;
        f.decl = 1;
        fs.push_back(f);
        f.hash = kParent; f.type = 6;
        f.value = d.sp_transform_parent ? d.sp_transform_parent
                                        : kDefaultTransformParent;
        f.decl = 2;
        fs.push_back(f);
        f.hash = kPosition; f.type = 6; f.value = pos_rec;
        f.decl = 0;
        fs.push_back(f);
        if (!g.AddRecord(comp_rec, fs, 1)) {
            err = "spawn point transform append failed";
            return 0;
        }
    }
    return comp_rec;
}

uint32_t create_spawn_point_entity(GdbEdit::GdbFile& g,
                                   const Gdb::SpawnDonorInfo& d,
                                   const float pos[3],
                                   std::string& err)
{
    constexpr uint32_t kParent = 0x5F6317D5u;
    const uint32_t comp_rec =
        create_spawn_point_transform(g, d, pos, err);
    if (!comp_rec) return 0;

    const uint32_t ent = g.AllocRecordHash();
    {
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kParent; f.type = 6; f.value = d.sp_template;
        f.decl = 0;
        fs.push_back(f);
        f.hash = d.sp_transform_field; f.type = 6; f.value = comp_rec;
        f.decl = 1;
        fs.push_back(f);
        if (!g.AddRecord(ent, fs, 0)) {
            err = "spawn point entity append failed";
            return 0;
        }
    }
    return ent;
}

bool legacy_spawn_point_position(const GdbEdit::GdbFile& g,
                                 const Gdb::SpawnDonorInfo& d,
                                 uint32_t entity_hash,
                                 float out_pos[3])
{
    constexpr uint32_t kPosition = 0xBD7C27D4u;
    constexpr uint32_t kRotation = 0x21EBC83Bu;
    constexpr uint32_t kVec[3] = {
        0x050C5D47u, 0x050C5D46u, 0x050C5D45u,
    };
    GdbEdit::Field field;
    if (g.FindLocalField(entity_hash, d.sp_transform_field, field)) {
        return false;
    }
    if (!g.FindLocalField(entity_hash, d.sp_comp_field, field) ||
        field.type != 6) {
        return false;
    }
    const uint32_t malformed_component = field.value;
    GdbEdit::Field pos_field, rot_field;
    if (!g.FindLocalField(malformed_component, kPosition, pos_field) ||
        pos_field.type != 6 ||
        !g.FindLocalField(malformed_component, kRotation, rot_field) ||
        rot_field.type != 6) {
        return false;
    }
    for (size_t i = 0; i < 3; ++i) {
        GdbEdit::Field value;
        if (!g.FindLocalField(pos_field.value, kVec[i], value) ||
            value.type != 3) {
            return false;
        }
        std::memcpy(&out_pos[i], &value.value, sizeof(float));
    }
    return true;
}

bool has_legacy_spawn_points(const GdbEdit::GdbFile& g,
                             const Gdb::SpawnDonorInfo& d)
{
    if (!d.valid()) return false;
    float pos[3];
    for (size_t i = 0; i < g.RecordCount(); ++i) {
        if (legacy_spawn_point_position(
                g, d, g.RecordAt(i).hash, pos)) {
            return true;
        }
    }
    return false;
}

bool repair_legacy_spawn_points(GdbEdit::GdbFile& g,
                                const Gdb::SpawnDonorInfo& d,
                                size_t& repaired,
                                std::string& err)
{
    struct Candidate {
        uint32_t entity = 0;
        float pos[3] = {};
    };
    std::vector<Candidate> candidates;
    const size_t original_count = g.RecordCount();
    for (size_t i = 0; i < original_count; ++i) {
        Candidate candidate;
        candidate.entity = g.RecordAt(i).hash;
        if (legacy_spawn_point_position(
                g, d, candidate.entity, candidate.pos)) {
            candidates.push_back(candidate);
        }
    }

    for (const Candidate& candidate : candidates) {
        const uint32_t transform = create_spawn_point_transform(
            g, d, candidate.pos, err);
        if (!transform) return false;
        if (!g.AddField(candidate.entity, d.sp_transform_field, 6,
                        transform, 1) ||
            !g.RemoveField(candidate.entity, d.sp_comp_field)) {
            err = "legacy spawn point component migration failed";
            return false;
        }
        ++repaired;
    }
    return true;
}

uint32_t create_generator_transform(GdbEdit::GdbFile& g,
                                    const Gdb::SpawnDonorInfo& d,
                                    const float pos[3],
                                    std::string& err)
{
    constexpr uint32_t kParent = 0x5F6317D5u;
    constexpr uint32_t kVecX = 0x050C5D47u;
    constexpr uint32_t kVecY = 0x050C5D46u;
    constexpr uint32_t kVecZ = 0x050C5D45u;
    constexpr uint32_t kPosition = 0xBD7C27D4u;
    constexpr uint32_t kRotation = 0x21EBC83Bu;
    constexpr uint32_t kDefaultTransformParent = 0xFD37C2F6u;
    constexpr uint32_t kDefaultPositionParent = 0xFC1909D4u;
    constexpr uint32_t kDefaultRotationParent = 0xB3E58682u;
    auto fbits = [](float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    };
    auto vec3_record = [&](float x, float y, float z,
                           uint32_t parent) -> uint32_t {
        const uint32_t hash = g.AllocRecordHash();
        std::vector<GdbEdit::Field> fields;
        GdbEdit::Field field;
        field.hash = kVecZ; field.type = 3;
        field.value = fbits(z); field.decl = 3;
        fields.push_back(field);
        field.hash = kVecY; field.value = fbits(y); field.decl = 2;
        fields.push_back(field);
        field.hash = kVecX; field.value = fbits(x); field.decl = 1;
        fields.push_back(field);
        field.hash = kParent; field.type = 6;
        field.value = parent; field.decl = 0;
        fields.push_back(field);
        return g.AddRecord(hash, fields, 1) ? hash : 0;
    };

    const uint32_t pos_record = vec3_record(
        pos[0], pos[1], pos[2],
        d.gen_position_parent ? d.gen_position_parent
                              : kDefaultPositionParent);
    const uint32_t rot_record = vec3_record(
        0, 0, 0,
        d.gen_rotation_parent ? d.gen_rotation_parent
                              : kDefaultRotationParent);
    if (!pos_record || !rot_record) {
        err = "generator transform append failed";
        return 0;
    }

    const uint32_t transform = g.AllocRecordHash();
    std::vector<GdbEdit::Field> fields;
    GdbEdit::Field field;
    field.hash = kRotation; field.type = 6;
    field.value = rot_record; field.decl = 1;
    fields.push_back(field);
    field.hash = kParent;
    field.value = d.gen_transform_parent ? d.gen_transform_parent
                                         : kDefaultTransformParent;
    field.decl = 2;
    fields.push_back(field);
    field.hash = kPosition;
    field.value = pos_record; field.decl = 0;
    fields.push_back(field);
    if (!g.AddRecord(transform, fields, 1)) {
        err = "generator transform comp append failed";
        return 0;
    }
    return transform;
}

uint32_t create_generator_families(GdbEdit::GdbFile& g,
                                   const std::string& creature_name,
                                   uint32_t creature_entity,
                                   std::string& err)
{
    constexpr uint32_t kParent = 0x5F6317D5u;
    constexpr uint32_t kCreatures = 0xA1F7A17Du;
    constexpr uint32_t kFamiliesBase = 0x54C2CFF7u;
    constexpr uint32_t kFamilyBase = 0x751EBC11u;
    constexpr uint32_t kCreaturesBase = 0x731AB342u;
    if (!creature_entity || creature_name.empty()) {
        err = "selected creature has no GDB entity definition";
        return 0;
    }

    const uint32_t creature_field = fnv1_32(creature_name);
    g.AddDictString(creature_field, creature_name);

    const uint32_t creatures = g.AllocRecordHash();
    {
        std::vector<GdbEdit::Field> fields;
        GdbEdit::Field field;
        field.hash = kParent; field.type = 6;
        field.value = kCreaturesBase; field.decl = 0;
        fields.push_back(field);
        field.hash = creature_field; field.type = 7;
        field.value = creature_entity; field.decl = 1;
        fields.push_back(field);
        if (!g.AddRecord(creatures, fields, 1)) {
            err = "generator creature list append failed";
            return 0;
        }
    }

    const uint32_t family = g.AllocRecordHash();
    {
        std::vector<GdbEdit::Field> fields;
        GdbEdit::Field field;
        field.hash = kParent; field.type = 6;
        field.value = kFamilyBase; field.decl = 0;
        fields.push_back(field);
        field.hash = kCreatures; field.type = 6;
        field.value = creatures; field.decl = 1;
        fields.push_back(field);
        if (!g.AddRecord(family, fields, 1)) {
            err = "generator family append failed";
            return 0;
        }
    }

    const uint32_t families = g.AllocRecordHash();
    {
        std::vector<GdbEdit::Field> fields;
        GdbEdit::Field field;
        field.hash = kParent; field.type = 6;
        field.value = kFamiliesBase; field.decl = 0;
        fields.push_back(field);
        field.hash = creature_field; field.type = 6;
        field.value = family; field.decl = 1;
        fields.push_back(field);
        if (!g.AddRecord(families, fields, 1)) {
            err = "generator families append failed";
            return 0;
        }
    }
    return families;
}

bool creature_catalog_entity(const std::string& name, uint32_t& entity_hash)
{
    for (const auto& creature : g_level_creature_catalog) {
        if (creature.name == name && creature.entity_hash != 0) {
            entity_hash = creature.entity_hash;
            return true;
        }
    }
    return false;
}

bool legacy_generator_data(const GdbEdit::GdbFile& g,
                           const Gdb::SpawnDonorInfo& d,
                           uint32_t entity_hash,
                           uint32_t& component_hash,
                           uint32_t& list_hash,
                           std::string& creature_name,
                           uint32_t& creature_entity,
                           float out_pos[3])
{
    constexpr uint32_t kNull = 0x811C9DC5u;
    constexpr uint32_t kSpawnedCreatureName = 0x2A80DD7Bu;
    constexpr uint32_t kSpawnPoints = 0x559B5DBFu;
    constexpr uint32_t kFamilies = 0xF44CE155u;
    constexpr uint32_t kPosition = 0xBD7C27D4u;
    constexpr uint32_t kVec[3] = {
        0x050C5D47u, 0x050C5D46u, 0x050C5D45u,
    };
    creature_name.clear();
    creature_entity = 0;
    GdbEdit::Field field;
    if (!g.FindLocalField(entity_hash, d.gen_comp_field, field) ||
        field.type != 6) {
        return false;
    }
    component_hash = field.value;

    GdbEdit::Field families;
    const bool has_families =
        g.FindLocalField(component_hash, kFamilies, families) &&
        families.type == 6 && families.value != 0 &&
        families.value != kNull;
    GdbEdit::Field spawned_name;
    if (g.FindLocalField(component_hash, kSpawnedCreatureName,
                         spawned_name)) {
        const auto it = g.Dict().find(spawned_name.value);
        if (it != g.Dict().end()) {
            uint32_t catalog_entity = 0;
            if (creature_catalog_entity(it->second, catalog_entity)) {
                creature_name = it->second;
                creature_entity = catalog_entity;
            }
        }
    }
    const bool old_schema = g.SchemaHeaderLow(component_hash) == 0;
    const bool missing_creature_family =
        !has_families && creature_entity != 0;
    if (!old_schema && !missing_creature_family) {
        return false;
    }

    if (!g.FindLocalField(component_hash, kSpawnPoints, field) ||
        field.type != 6) {
        return false;
    }
    list_hash = field.value;
    if (!g.FindLocalField(entity_hash, d.gen_transform_field, field) ||
        field.type != 6) {
        return false;
    }
    GdbEdit::Field position;
    if (!g.FindLocalField(field.value, kPosition, position) ||
        position.type != 6) {
        return false;
    }
    for (size_t i = 0; i < 3; ++i) {
        GdbEdit::Field value;
        if (!g.FindLocalField(position.value, kVec[i], value) ||
            value.type != 3) {
            return false;
        }
        std::memcpy(&out_pos[i], &value.value, sizeof(float));
    }
    return true;
}

bool has_legacy_generators(const GdbEdit::GdbFile& g,
                           const Gdb::SpawnDonorInfo& d)
{
    if (!d.valid()) return false;
    uint32_t component = 0, list = 0;
    uint32_t creature_entity = 0;
    std::string creature_name;
    float pos[3];
    for (size_t i = 0; i < g.RecordCount(); ++i) {
        if (legacy_generator_data(g, d, g.RecordAt(i).hash,
                                  component, list, creature_name,
                                  creature_entity, pos)) {
            return true;
        }
    }
    return false;
}

bool repair_legacy_generators(GdbEdit::GdbFile& g,
                              const Gdb::SpawnDonorInfo& d,
                              size_t& repaired,
                              std::string& err)
{
    constexpr uint32_t kParent = 0x5F6317D5u;
    constexpr uint32_t kSpawnedCreatureName = 0x2A80DD7Bu;
    constexpr uint32_t kSpawnPoints = 0x559B5DBFu;
    constexpr uint32_t kFamilies = 0xF44CE155u;
    struct Candidate {
        uint32_t entity = 0;
        uint32_t component = 0;
        uint32_t list = 0;
        std::string creature_name;
        uint32_t creature_entity = 0;
        float pos[3] = {};
    };
    std::vector<Candidate> candidates;
    const size_t original_count = g.RecordCount();
    for (size_t i = 0; i < original_count; ++i) {
        Candidate candidate;
        candidate.entity = g.RecordAt(i).hash;
        if (legacy_generator_data(
                g, d, candidate.entity, candidate.component,
                candidate.list, candidate.creature_name,
                candidate.creature_entity, candidate.pos)) {
            candidates.push_back(candidate);
        }
    }

    for (const Candidate& candidate : candidates) {
        std::vector<GdbEdit::Field> list_fields;
        std::vector<GdbEdit::Field> component_fields;
        if (!g.Fields(g.FindRecord(candidate.list), list_fields) ||
            !g.Fields(g.FindRecord(candidate.component), component_fields)) {
            err = "legacy generator records are unreadable";
            return false;
        }
        const uint32_t new_list = g.AllocRecordHash();
        if (!g.AddRecord(new_list, list_fields, 1)) {
            err = "legacy generator spawn list migration failed";
            return false;
        }
        uint32_t families = 0;
        if (candidate.creature_entity != 0) {
            families = create_generator_families(
                g, candidate.creature_name, candidate.creature_entity, err);
            if (!families) return false;
        }
        bool found_families = false;
        uint32_t next_decl = 4;
        for (GdbEdit::Field& field : component_fields) {
            if (field.hash == kParent) {
                field.decl = 0;
            } else if (field.hash == kSpawnPoints) {
                field.value = new_list;
                field.decl = 1;
            } else if (field.hash == kSpawnedCreatureName) {
                field.decl = 2;
            } else if (field.hash == kFamilies) {
                if (families) field.value = families;
                field.decl = 3;
                found_families = true;
            } else {
                field.decl = next_decl++;
            }
        }
        if (!found_families && families) {
            GdbEdit::Field family_field;
            family_field.hash = kFamilies;
            family_field.type = 6;
            family_field.value = families;
            family_field.decl = 3;
            component_fields.push_back(family_field);
        }
        const uint32_t new_component = g.AllocRecordHash();
        if (!g.AddRecord(new_component, component_fields, 1)) {
            err = "legacy generator component migration failed";
            return false;
        }
        const uint32_t new_transform = create_generator_transform(
            g, d, candidate.pos, err);
        if (!new_transform ||
            !g.SetFieldValue(candidate.entity, d.gen_comp_field,
                             new_component) ||
            !g.SetFieldValue(candidate.entity, d.gen_transform_field,
                             new_transform)) {
            if (err.empty()) err = "legacy generator migration failed";
            return false;
        }
        ++repaired;
    }
    return true;
}

uint32_t create_generator_entity(
    GdbEdit::GdbFile& g,
    const Gdb::SpawnDonorInfo& d,
    const GeneratorAddition& ga,
    std::vector<std::pair<std::string, uint32_t>>& new_save_entities,
    std::string& err)
{
    constexpr uint32_t kParent = 0x5F6317D5u;
    constexpr uint32_t kSpawnedCreatureName = 0x2A80DD7Bu;
    constexpr uint32_t kSpawnPoints = 0x559B5DBFu;
    constexpr uint32_t kFamilies = 0xF44CE155u;
    constexpr uint32_t kDefaultGeneratorCompParent = 0x9B6881DAu;
    constexpr uint32_t kDefaultSpawnListParent = 0x2FAB69BFu;

    std::vector<uint32_t> sp_entities;
    for (const auto& p : ga.spawn_points) {
        const float pp[3] = {p[0], p[1], p[2]};
        const uint32_t sp = create_spawn_point_entity(g, d, pp, err);
        if (!sp) return 0;
        char nm[32];
        std::snprintf(nm, sizeof(nm), "F2AB_SP_%08X", sp);
        g.AddNameMapping(nm, sp);
        new_save_entities.emplace_back(nm, sp);
        sp_entities.push_back(sp);
    }

    const uint32_t list_rec = g.AllocRecordHash();
    {
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        for (size_t i = 0; i < sp_entities.size(); ++i) {
            const std::string fname =
                "SpawnPoint" + std::to_string(i + 1);
            f.hash = fnv1_32(fname);
            f.type = 7;
            f.value = sp_entities[i];
            f.decl = uint32_t(i);
            fs.push_back(f);
        }
        f.hash = kParent; f.type = 6;
        f.value = d.spawn_list_parent ? d.spawn_list_parent
                                      : kDefaultSpawnListParent;
        f.decl = uint32_t(sp_entities.size());
        fs.push_back(f);
        if (!g.AddRecord(list_rec, fs, 1)) {
            err = "spawn list append failed";
            return 0;
        }
    }

    const uint32_t families_rec = create_generator_families(
        g, ga.creature_name, ga.creature_entity, err);
    if (!families_rec) return 0;

    const uint32_t comp_rec = g.AllocRecordHash();
    {
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kSpawnedCreatureName; f.type = 4;
        f.value = fnv1_32(ga.creature_name);
        f.decl = 2;
        fs.push_back(f);
        f.hash = kSpawnPoints; f.type = 6; f.value = list_rec;
        f.decl = 1;
        fs.push_back(f);
        f.hash = kParent; f.type = 6;
        f.value = d.gen_comp_parent ? d.gen_comp_parent
                                    : kDefaultGeneratorCompParent;
        f.decl = 0;
        fs.push_back(f);
        f.hash = kFamilies; f.type = 6; f.value = families_rec;
        f.decl = 3;
        fs.push_back(f);
        if (!g.AddRecord(comp_rec, fs, 1)) {
            err = "generator comp append failed";
            return 0;
        }
    }
    g.AddDictString(fnv1_32(ga.creature_name), ga.creature_name);

    const uint32_t tf_rec =
        create_generator_transform(g, d, ga.pos, err);
    if (!tf_rec) return 0;

    const uint32_t ent = g.AllocRecordHash();
    {
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kParent; f.type = 6; f.value = d.gen_template;
        f.decl = 0;
        fs.push_back(f);
        f.hash = d.gen_comp_field; f.type = 6; f.value = comp_rec;
        f.decl = 2;
        fs.push_back(f);
        if (tf_rec) {
            f.hash = d.gen_transform_field; f.type = 6;
            f.value = tf_rec;
            f.decl = 1;
            fs.push_back(f);
        }
        if (!g.AddRecord(ent, fs, 0)) {
            err = "generator entity append failed";
            return 0;
        }
    }
    char nm[32];
    std::snprintf(nm, sizeof(nm), "F2AB_Gen_%08X", ent);
    g.AddNameMapping(nm, ent);
    new_save_entities.emplace_back(nm, ent);
    return ent;
}

bool remove_spawn_point_reference(
    GdbEdit::GdbFile& g,
    const Gdb::SpawnDonorInfo& donor,
    const ModuleState::SpawnPointDelete& deletion,
    std::string& err)
{
    constexpr uint32_t kSpawnPoints = 0x559B5DBFu;
    uint32_t list_hash = deletion.spawn_points_record;

    // Generator migration can replace the component and spawn-list records
    // during this same Save. Resolve the current list from the generator so
    // the removal is applied to the migrated graph, not its stale predecessor.
    GdbEdit::Field component;
    GdbEdit::Field points;
    if (deletion.generator_entity && donor.gen_comp_field &&
        g.FindLocalField(deletion.generator_entity,
                         donor.gen_comp_field, component) &&
        component.type == 6 &&
        g.FindLocalField(component.value, kSpawnPoints, points) &&
        points.type == 6) {
        list_hash = points.value;
    }

    std::vector<GdbEdit::Field> fields;
    if (!g.Fields(g.FindRecord(list_hash), fields)) {
        err = "spawn point list is unreadable";
        return false;
    }
    for (const auto& field : fields) {
        if (field.type == 7 &&
            field.value == deletion.spawn_point_entity) {
            if (!g.RemoveField(list_hash, field.hash)) {
                err = "spawn point list field removal failed";
                return false;
            }
            return true;
        }
    }
    err = "spawn point is no longer present in its generator";
    return false;
}

size_t remove_save_entities(
    std::vector<uint8_t>& xml_bytes,
    const std::unordered_set<uint32_t>& entity_hashes)
{
    if (entity_hashes.empty()) return 0;
    std::string xml(reinterpret_cast<const char*>(xml_bytes.data()),
                    xml_bytes.size());
    constexpr const char* kClose = "</Entity>";
    constexpr size_t kCloseLen = 9;
    size_t removed = 0;
    size_t pos = 0;
    while ((pos = xml.find("<Entity ", pos)) != std::string::npos) {
        const size_t value_begin = xml.find('>', pos);
        const size_t close = value_begin == std::string::npos
                                 ? std::string::npos
                                 : xml.find(kClose, value_begin + 1);
        if (value_begin == std::string::npos || close == std::string::npos) {
            break;
        }
        const std::string value =
            xml.substr(value_begin + 1, close - value_begin - 1);
        char* parse_end = nullptr;
        const unsigned long parsed =
            std::strtoul(value.c_str(), &parse_end, 0);
        if (parse_end != value.c_str() &&
            entity_hashes.count(uint32_t(parsed))) {
            size_t erase_begin = xml.rfind('\n', pos);
            erase_begin = erase_begin == std::string::npos
                              ? 0
                              : erase_begin + 1;
            for (size_t i = erase_begin; i < pos; ++i) {
                if (xml[i] != ' ' && xml[i] != '\t' && xml[i] != '\r') {
                    erase_begin = pos;
                    break;
                }
            }
            size_t erase_end = close + kCloseLen;
            if (erase_end < xml.size() && xml[erase_end] == '\r') {
                ++erase_end;
            }
            if (erase_end < xml.size() && xml[erase_end] == '\n') {
                ++erase_end;
            }
            xml.erase(erase_begin, erase_end - erase_begin);
            pos = erase_begin;
            ++removed;
        } else {
            pos = close + kCloseLen;
        }
    }
    xml_bytes.assign(xml.begin(), xml.end());
    return removed;
}

bool append_save_entities(
    std::vector<uint8_t>& xml_bytes,
    const std::vector<std::pair<std::string, uint32_t>>& entities,
    std::string& err)
{
    if (entities.empty()) return true;
    std::string xml(reinterpret_cast<const char*>(xml_bytes.data()),
                    xml_bytes.size());
    const size_t layer = xml.find("load=\"AlwaysOn\">");
    if (layer == std::string::npos) {
        err = "no AlwaysOn layer in .save";
        return false;
    }

    const size_t open = xml.rfind("<id_", layer);
    if (open == std::string::npos) {
        err = "no layer tag in .save";
        return false;
    }
    const size_t id_end = xml.find_first_of(" \t", open);
    if (id_end == std::string::npos) {
        err = "bad layer tag";
        return false;
    }
    const std::string close_tag =
        "</" + xml.substr(open + 1, id_end - open - 1) + ">";
    size_t close = xml.find(close_tag, layer);
    if (close == std::string::npos) {
        err = "layer close tag missing";
        return false;
    }

    while (close > 0 &&
           (xml[close - 1] == '\t' || xml[close - 1] == ' ')) {
        --close;
    }
    std::string ins;
    for (const auto& [name, hash] : entities) {
        char line[128];
        std::snprintf(line, sizeof(line),
                      "\t\t<Entity name=\"%s\">0x%08X</Entity>\r\n",
                      name.c_str(), hash);
        ins += line;
    }

    if (xml.find('\r') == std::string::npos) {
        std::string tmp;
        for (char c : ins) {
            if (c != '\r') tmp.push_back(c);
        }
        ins = tmp;
    }
    xml.insert(close, ins);
    xml_bytes.assign(xml.begin(), xml.end());
    return true;
}

struct SavePhysPatch {
    uint32_t hash = 0;
    float pos[3] = {0, 0, 0};
    float rot_deg[3] = {0, 0, 0};
    bool set_rot = false;
};

int find_level_save_index(const std::string& bnk_path, int lev_index) {
    if (bnk_path.empty() || lev_index < 0) return -1;
    try {
        const auto bc = BnkCache::get(bnk_path);
        std::string nm = bc.reader->list_files()[(size_t)lev_index].name;
        for (char& c : nm) c = (char)std::tolower((unsigned char)c);
        const std::string suffix = ".engine_level";
        const size_t sp = nm.rfind(suffix);
        if (sp == std::string::npos || sp + suffix.size() != nm.size()) {
            return -1;
        }
        std::string stem = nm.substr(0, sp);
        std::replace(stem.begin(), stem.end(), '\\', '/');
        return BnkCache::find_index(bnk_path, stem + ".save");
    } catch (...) {
        return -1;
    }
}

size_t apply_save_physics_patches(std::vector<uint8_t>& xml_bytes,
                                  const std::vector<SavePhysPatch>& patches)
{
    if (patches.empty()) return 0;
    std::unordered_map<uint32_t, const SavePhysPatch*> by_hash;
    by_hash.reserve(patches.size() * 2);
    for (const auto& p : patches) by_hash.emplace(p.hash, &p);

    std::string xml(reinterpret_cast<const char*>(xml_bytes.data()),
                    xml_bytes.size());

    auto replace_payload = [&](size_t region_start, size_t& region_end,
                               const char* tag, const std::string& text)
        -> bool {
        const std::string open = std::string("<") + tag;
        const std::string close = std::string("</") + tag + ">";
        size_t a = region_start;
        while (true) {
            a = xml.find(open, a);
            if (a == std::string::npos || a >= region_end) return false;
            const char nxt = a + open.size() < xml.size()
                                 ? xml[a + open.size()] : '\0';
            if (nxt == '>' || nxt == ' ' || nxt == '\t') break;
            a += open.size();
        }
        const size_t gt = xml.find('>', a);
        if (gt == std::string::npos || gt >= region_end) return false;
        const size_t vs = gt + 1;
        const size_t b = xml.find(close, vs);
        if (b == std::string::npos || b > region_end) return false;
        xml.replace(vs, b - vs, text);
        region_end += text.size() - (b - vs);
        return true;
    };
    auto fmt_f = [](float v) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%.6f", v);
        return std::string(buf);
    };
    auto find_open_tag = [&](size_t from, size_t limit,
                             const char* tag) -> size_t {
        const std::string open = std::string("<") + tag;
        size_t a = from;
        while (true) {
            a = xml.find(open, a);
            if (a == std::string::npos || a >= limit) {
                return std::string::npos;
            }
            const char nxt = a + open.size() < xml.size()
                                 ? xml[a + open.size()] : '\0';
            if (nxt == '>' || nxt == ' ' || nxt == '\t') return a;
            a += open.size();
        }
    };

    size_t patched = 0;
    const std::string tag_open = "<Entity name=\"";
    const std::string tag_close = "</Entity>";
    size_t pos = 0;
    while (true) {
        size_t a = xml.find(tag_open, pos);
        if (a == std::string::npos) break;
        const size_t name_end = xml.find('"', a + tag_open.size());
        if (name_end == std::string::npos) break;
        const size_t hash_start = xml.find("0x", name_end);
        if (hash_start == std::string::npos) break;
        size_t entity_close = xml.find(tag_close, hash_start);
        if (entity_close == std::string::npos) break;
        uint32_t h = 0;
        for (size_t i = hash_start + 2;
             i < xml.size() && std::isxdigit((unsigned char)xml[i]); ++i) {
            h <<= 4;
            const char c = xml[i];
            if (c >= '0' && c <= '9') h |= uint32_t(c - '0');
            else if (c >= 'A' && c <= 'F') h |= uint32_t(c - 'A' + 10);
            else h |= uint32_t(c - 'a' + 10);
        }
        auto it = by_hash.find(h);
        if (it == by_hash.end()) {
            pos = entity_close + tag_close.size();
            continue;
        }
        const SavePhysPatch& p = *it->second;

        const size_t phys = find_open_tag(name_end, entity_close,
                                          "PhysicsData");
        if (phys != std::string::npos) {
            size_t phys_end = xml.find("</PhysicsData>", phys);
            if (phys_end != std::string::npos && phys_end < entity_close) {
                const size_t pos_tag = find_open_tag(phys, phys_end,
                                                     "Position");
                if (pos_tag != std::string::npos) {
                    size_t pos_end = xml.find("</Position>", pos_tag);
                    if (pos_end != std::string::npos && pos_end < phys_end) {
                        const size_t before = pos_end;
                        replace_payload(pos_tag, pos_end, "X",
                                        fmt_f(p.pos[0]));
                        replace_payload(pos_tag, pos_end, "Y",
                                        fmt_f(p.pos[1]));
                        replace_payload(pos_tag, pos_end, "Z",
                                        fmt_f(p.pos[2]));
                        phys_end += pos_end - before;
                        entity_close = xml.find(tag_close, pos_tag);
                        ++patched;
                    }
                }
                if (p.set_rot) {
                    const size_t ori = find_open_tag(phys, phys_end,
                                                     "Orientation");
                    if (ori != std::string::npos) {
                        size_t ori_end = xml.find("</Orientation>", ori);
                        if (ori_end != std::string::npos &&
                            ori_end < phys_end) {
                            const float hx =
                                p.rot_deg[0] * kDegToRad * 0.5f;
                            const float hy =
                                p.rot_deg[1] * kDegToRad * 0.5f;
                            const float hz =
                                p.rot_deg[2] * kDegToRad * 0.5f;
                            const float qx4[4] = {std::sin(hx), 0, 0,
                                                  std::cos(hx)};
                            const float qy4[4] = {0, std::sin(hy), 0,
                                                  std::cos(hy)};
                            const float qz4[4] = {0, 0, std::sin(hz),
                                                  std::cos(hz)};
                            float t[4], q[4];
                            quat_mul(qy4, qx4, t);
                            quat_mul(qz4, t, q);
                            replace_payload(ori, ori_end, "X", fmt_f(q[0]));
                            replace_payload(ori, ori_end, "Y", fmt_f(q[1]));
                            replace_payload(ori, ori_end, "Z", fmt_f(q[2]));
                            replace_payload(ori, ori_end, "W", fmt_f(q[3]));
                            entity_close = xml.find(tag_close, ori);
                        }
                    }
                }
            }
        }
        if (entity_close == std::string::npos) break;
        pos = entity_close + tag_close.size();
    }
    if (patched) xml_bytes.assign(xml.begin(), xml.end());
    return patched;
}

}

bool Save(std::string& msg) {
    bool reload_needed = false;
    FlatAssetEntry reload_entry;
    bool need_bake = false;
    bool bake_iso = false;
    std::string bake_bnk_path;
    std::string bake_vpath;
    int bake_index = -1;
    size_t bake_count = 0;
    std::vector<uint8_t> bake_bytes;
    int bake_ed_index = -1;
    std::vector<uint8_t> bake_ed_bytes;
    int bake_lmp_index = -1;
    std::vector<uint8_t> bake_lmp_bytes;
    int bake_lvstream_index = -1;
    std::vector<uint8_t> bake_lvstream_bytes;
    std::string bake_streaming_path;
    int bake_models_index = -1;
    std::vector<uint8_t> bake_models_bytes;
    std::vector<BnkWriter::EntryReplacement> bake_more;

    std::vector<uint8_t> gdb_rewrite_bytes;
    std::string gdb_rewrite_bnk;
    int gdb_rewrite_index = -1;
    std::string gdb_rewrite_loose;
    bool gdb_rewrite_iso = false;
    size_t contents_applied = 0;

    std::vector<uint8_t> save_rewrite_bytes;
    int save_rewrite_index = -1;
    std::string save_rewrite_bnk;
    size_t chest_entities_created = 0;
    size_t generators_created = 0;
    size_t spawn_points_deleted = 0;
    size_t save_entities_deleted = 0;
    size_t spawn_points_repaired = 0;
    size_t generators_repaired = 0;
    size_t save_physics_patched = 0;
    std::unordered_map<uint32_t, std::string> babel_edits;
    bool deferred_work = false;
    {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (!s.available) { msg = "no level loaded"; return false; }
    if (s.saving) { msg = "a save is already in progress"; return false; }

    DebugTrace::log("save: lev bnk='%s' idx=%d iso=%d comp=%d valid=%d "
                    "gdb bnk='%s' additions=%zu edits=%zu",
                    s.lev.bnk_path.c_str(), s.lev.file_index,
                    s.lev.in_iso ? 1 : 0, s.lev.compressed ? 1 : 0,
                    s.lev.valid ? 1 : 0, s.gdb.bnk_path.c_str(),
                    s.additions.size(), s.edits.size());

    progress_update(2, 100, "Writing level patches...");

    size_t lev_written = 0, gdb_written = 0, rs_written = 0;
    size_t skipped = 0, rs_visual = 0;
    struct LevPatch { uint32_t off; float v[3]; int n; };
    std::vector<LevPatch> lev_patches;
    struct GdbPatch { uint32_t off; float v; };
    std::vector<GdbPatch> gdb_patches;
    std::vector<SavePhysPatch> save_physics_patches;

    size_t adds_updated = 0;
    for (const auto& kv : s.edits) {
        const EditEntry& e = kv.second;
        if (!e.changed()) continue;
        if (e.gdb_entity_hash != 0 && e.lev_kind != 5) {
            SavePhysPatch sp;
            sp.hash = e.gdb_entity_hash;
            sp.pos[0] = e.orig[0] + e.delta[0];
            sp.pos[1] = e.orig[1] + e.delta[1];
            sp.pos[2] = e.orig[2] + e.delta[2] -
                        (e.deleted ? 10000.0f : 0.0f);
            sp.rot_deg[0] = e.orig_rot[0] + e.rot_deg[0];
            sp.rot_deg[1] = e.orig_rot[1] + e.rot_deg[1];
            sp.rot_deg[2] = e.orig_rot[2] + e.rot_deg[2];
            sp.set_rot = e.rotated() && !e.deleted;
            save_physics_patches.push_back(sp);
        }
        if (e.lev_kind == 5) {
            if (e.lev_off >= 1 && e.lev_off <= s.additions.size()) {
                Addition& a = s.additions[e.lev_off - 1];
                if (e.deleted) {
                    a.removed = true;
                } else {
                    a.pos[0] = e.orig[0] + e.delta[0];
                    a.pos[1] = e.orig[1] + e.delta[1];
                    a.pos[2] = e.orig[2] + e.delta[2];
                    a.yaw_deg = e.orig_rot[2] + e.rot_deg[2];
                }
                ++adds_updated;
            }
            continue;
        }
        if (e.moved() || e.deleted) {
            const float np[3] = { e.orig[0] + e.delta[0],
                                  e.orig[1] + e.delta[1],
                                  e.orig[2] + e.delta[2] -
                                      (e.deleted ? 10000.0f : 0.0f) };

            bool wrote = false;
            if (e.lev_off != 0) {
                lev_patches.push_back({ e.lev_off,
                                        { np[0], np[1], np[2] }, 3 });
                wrote = true;
            }
            if (e.gdb_off[0] || e.gdb_off[1] || e.gdb_off[2]) {
                for (int i = 0; i < 3; ++i) {
                    if (e.gdb_off[i]) {
                        gdb_patches.push_back({ e.gdb_off[i], np[i] });
                    }
                }
                wrote = true;
            }
            if (!wrote) {
                ++skipped;
            }
        }
        if (e.rotated() && !e.deleted) {
            bool wrote_rot = false;
            if (e.lev_kind == 1 && e.lev_off != 0) {
                const float yaw =
                    (e.orig_rot[2] + e.rot_deg[2]) * kDegToRad;
                lev_patches.push_back({ e.lev_off + 24,
                                        { std::sin(yaw), std::cos(yaw),
                                          0 }, 2 });
                wrote_rot = true;
                if (e.rot_deg[0] != 0.0f || e.rot_deg[1] != 0.0f) {
                    ++rs_visual;
                }
            }
            if (e.gdb_rot_off[0] && e.gdb_rot_off[1] &&
                e.gdb_rot_off[2]) {
                gdb_patches.push_back({ e.gdb_rot_off[0],
                    (e.orig_rot[2] + e.rot_deg[2]) * kDegToRad });
                gdb_patches.push_back({ e.gdb_rot_off[1],
                    (e.orig_rot[1] + e.rot_deg[1]) * kDegToRad });
                gdb_patches.push_back({ e.gdb_rot_off[2],
                    (e.orig_rot[0] + e.rot_deg[0]) * kDegToRad });
                wrote_rot = true;
            }
            if (wrote_rot) {
                ++rs_written;
            } else {
                ++rs_visual;
            }
        }
    }
    babel_edits = s.text_edits;
    bool legacy_spawn_points_pending = false;
    bool legacy_generators_pending = false;
    if (g_level_spawn_donor.valid()) {
        std::vector<uint8_t> probe_bytes;
        if (!s.gdb.file_path.empty()) {
            std::ifstream f(s.gdb.file_path, std::ios::binary);
            if (f) {
                f.seekg(0, std::ios::end);
                probe_bytes.resize(size_t(f.tellg()));
                f.seekg(0);
                f.read(reinterpret_cast<char*>(probe_bytes.data()),
                       std::streamsize(probe_bytes.size()));
                if (!f) probe_bytes.clear();
            }
        } else if (s.gdb.valid) {
            try {
                probe_bytes = BnkCache::extract_bytes(
                    s.gdb.bnk_path, s.gdb.file_index);
            } catch (...) {
                probe_bytes.clear();
            }
        }
        if (!probe_bytes.empty()) {
            GdbEdit::GdbFile probe;
            std::string probe_err;
            if (probe.Parse(probe_bytes, probe_err)) {
                legacy_spawn_points_pending = has_legacy_spawn_points(
                    probe, g_level_spawn_donor);
                legacy_generators_pending = has_legacy_generators(
                    probe, g_level_spawn_donor);
            }
        }
    }
    if (lev_patches.empty() && gdb_patches.empty() && adds_updated == 0 &&
        s.additions.empty() && s.contents_edits.empty() &&
        save_physics_patches.empty() && babel_edits.empty() &&
        s.generators.empty() && s.spawn_point_adds.empty() &&
        s.spawn_point_deletes.empty() &&
        !legacy_spawn_points_pending && !legacy_generators_pending) {
        msg = (skipped || rs_visual)
                  ? "no file-backed changes to save (visual-only edits "
                    "skipped)"
                  : "no changes to save";
        return true;
    }

    std::string err;
    if (!lev_patches.empty()) {
        if (target_patchable_in_place(s.lev)) {
            for (const auto& p : lev_patches) {
                if ((uint64_t)p.off + (uint64_t)p.n * 4 >
                    s.lev.on_disk_size) continue;
                if (!patch_target(s.lev, p.off, p.v, p.n, err)) {
                    msg = "save failed (level file): " + err;
                    return false;
                }
                ++lev_written;
            }
            BnkCache::invalidate(s.lev.bnk_path);
        } else {
            std::vector<uint8_t> bytes;
            try {
                bytes = BnkCache::extract_bytes(s.lev.bnk_path,
                                                s.lev.file_index);
            } catch (...) { bytes.clear(); }
            if (bytes.empty()) {
                msg = "level re-extract failed";
                return false;
            }
            for (const auto& p : lev_patches) {
                if ((size_t)p.off + (size_t)p.n * 4 > bytes.size())
                    continue;
                for (int i = 0; i < p.n; ++i) {
                    put_f32_be(bytes.data() + p.off + i * 4, p.v[i]);
                }
                ++lev_written;
            }
            const auto out = edited_levels_dir() /
                std::filesystem::path(s.entry.full_path).filename();
            std::error_code ec;
            std::filesystem::create_directories(out.parent_path(), ec);
            std::ofstream f(out, std::ios::binary);
            if (!f) { msg = "could not write " + out.string(); return false; }
            f.write(reinterpret_cast<const char*>(bytes.data()),
                    (std::streamsize)bytes.size());
            OutputLog::warn("level edit: chunked level entry — patched "
                            "copy exported to " + out.string());
        }
    }
    if (!gdb_patches.empty()) {
        if (target_patchable_in_place(s.gdb)) {
            for (const auto& p : gdb_patches) {
                if (s.gdb.on_disk_size &&
                    (uint64_t)p.off + 4 > s.gdb.on_disk_size) continue;
                if (!patch_target(s.gdb, p.off, &p.v, 1, err)) {
                    msg = "save failed (.gdb): " + err;
                    return false;
                }
                ++gdb_written;
            }
            if (!s.gdb.bnk_path.empty()) {
                BnkCache::invalidate(s.gdb.bnk_path);
            }
        } else if (s.gdb.valid) {
            std::vector<uint8_t> bytes;
            try {
                bytes = BnkCache::extract_bytes(s.gdb.bnk_path,
                                                s.gdb.file_index);
            } catch (...) { bytes.clear(); }
            if (!bytes.empty()) {
                for (const auto& p : gdb_patches) {
                    if ((size_t)p.off + 4 > bytes.size()) continue;
                    put_f32_be(bytes.data() + p.off, p.v);
                    ++gdb_written;
                }
                const auto out = edited_levels_dir() /
                    (std::filesystem::path(s.entry.full_path)
                         .stem().string() + ".gdb");
                std::error_code ec;
                std::filesystem::create_directories(out.parent_path(), ec);
                std::ofstream f(out, std::ios::binary);
                if (f) {
                    f.write(reinterpret_cast<const char*>(bytes.data()),
                            (std::streamsize)bytes.size());
                    OutputLog::warn("level edit: chunked .gdb entry — "
                                    "patched copy exported to " +
                                    out.string());
                }
            }
        } else {
            skipped += gdb_patches.size() / 3;
        }
    }

    bool have_chest_adds = false;
    for (const auto& a : s.additions) {
        if (!a.removed && a.as_entity()) {
            have_chest_adds = true;
            break;
        }
    }
    if (!s.contents_edits.empty() || have_chest_adds ||
        !s.generators.empty() || !s.spawn_point_adds.empty() ||
        !s.spawn_point_deletes.empty() ||
        legacy_spawn_points_pending || legacy_generators_pending) {
        progress_update(6, 100, "Rewriting entity data...");
        std::vector<uint8_t> gbytes;
        if (!s.gdb.file_path.empty()) {
            std::ifstream f(s.gdb.file_path, std::ios::binary);
            if (f) {
                f.seekg(0, std::ios::end);
                gbytes.resize(size_t(f.tellg()));
                f.seekg(0);
                f.read(reinterpret_cast<char*>(gbytes.data()),
                       std::streamsize(gbytes.size()));
                if (!f) gbytes.clear();
            }
        } else if (s.gdb.valid) {
            try {
                gbytes = BnkCache::extract_bytes(s.gdb.bnk_path,
                                                 s.gdb.file_index);
            } catch (...) {
                gbytes.clear();
            }
            if (!gbytes.empty() && !target_patchable_in_place(s.gdb)) {

                for (const auto& p : gdb_patches) {
                    if (size_t(p.off) + 4 <= gbytes.size()) {
                        put_f32_be(gbytes.data() + p.off, p.v);
                    }
                }
            }
        }
        if (gbytes.empty()) {
            msg = "save failed: .gdb source unavailable for chest "
                  "contents edits";
            return false;
        }
        GdbEdit::GdbFile g;
        std::string gerr;
        if (!g.Parse(gbytes, gerr)) {
            msg = "save failed: .gdb parse for contents edits: " + gerr;
            return false;
        }
        if (legacy_spawn_points_pending) {
            std::string repair_err;
            if (!repair_legacy_spawn_points(
                    g, g_level_spawn_donor, spawn_points_repaired,
                    repair_err)) {
                msg = "save failed: " + repair_err;
                return false;
            }
        }
        if (legacy_generators_pending) {
            std::string repair_err;
            if (!repair_legacy_generators(
                    g, g_level_spawn_donor, generators_repaired,
                    repair_err)) {
                msg = "save failed: " + repair_err;
                return false;
            }
        }
        for (const auto& deletion : s.spawn_point_deletes) {
            std::string deletion_err;
            if (!remove_spawn_point_reference(
                    g, g_level_spawn_donor, deletion, deletion_err)) {
                msg = "save failed: " + deletion_err;
                return false;
            }
            ++spawn_points_deleted;
        }
        for (const auto& kv : s.contents_edits) {
            std::string aerr;
            if (apply_chest_contents(g, kv.first, kv.second, 0, aerr)) {
                ++contents_applied;
            } else {
                DebugTrace::log(
                    "save: contents edit 0x%08X skipped: %s",
                    kv.first, aerr.c_str());
            }
        }

        std::vector<std::pair<std::string, uint32_t>> new_save_entities;
        for (const auto& a : s.additions) {
            if (a.removed || !a.as_entity()) continue;
            std::string aerr;
            const uint32_t eh =
                create_entity_addition(g, a, babel_edits, aerr);
            if (!eh) {
                DebugTrace::log("save: entity addition skipped: %s",
                                aerr.c_str());
                continue;
            }
            const char* name_fmt = "F2AB_Chest_%08X";
            if (a.silver_keys_needed > 0) {
                name_fmt = "F2AB_SilverKeyChest_%08X";
            } else if (a.entity_kind == AdditionEntityKind::SilverKey) {
                name_fmt = "F2AB_Key_%08X";
            } else if (a.entity_kind == AdditionEntityKind::GenericProp) {
                name_fmt = "F2AB_Prop_%08X";
            }
            char name[48];
            std::snprintf(name, sizeof(name), name_fmt, eh);
            g.AddNameMapping(name, eh);
            new_save_entities.emplace_back(name, eh);
            ++chest_entities_created;
        }

        if (!s.generators.empty() || !s.spawn_point_adds.empty()) {
            const Gdb::SpawnDonorInfo& donor = g_level_spawn_donor;
            if (!donor.valid()) {
                DebugTrace::log(
                    "save: generator author skipped: no donor "
                    "generator/spawn point in this level");
            } else {
                for (const auto& ga : s.generators) {
                    if (ga.removed || ga.creature_name.empty()) {
                        continue;
                    }
                    std::string gerr2;
                    if (create_generator_entity(g, donor, ga,
                                                new_save_entities,
                                                gerr2)) {
                        ++generators_created;
                    } else {
                        DebugTrace::log(
                            "save: generator author failed: %s",
                            gerr2.c_str());
                    }
                }
                for (const auto& spa : s.spawn_point_adds) {
                    std::string serr2;
                    const uint32_t sp = create_spawn_point_entity(
                        g, donor, spa.pos, serr2);
                    if (!sp) {
                        DebugTrace::log(
                            "save: spawn point author failed: %s",
                            serr2.c_str());
                        continue;
                    }
                    char nm[32];
                    std::snprintf(nm, sizeof(nm), "F2AB_SP_%08X", sp);
                    std::string fname;
                    for (size_t n = 1; n < 10000; ++n) {
                        const std::string candidate =
                            "SpawnPoint" + std::to_string(n);
                        GdbEdit::Field existing;
                        if (!g.FindLocalField(
                                spa.spawn_points_record,
                                fnv1_32(candidate), existing)) {
                            fname = candidate;
                            break;
                        }
                    }
                    if (fname.empty()) {
                        DebugTrace::log(
                            "save: no free native spawn point field for "
                            "0x%08X", spa.spawn_points_record);
                        continue;
                    }
                    if (!g.AddField(spa.spawn_points_record,
                                    fnv1_32(fname), 7, sp)) {
                        DebugTrace::log(
                            "save: spawn list append failed for "
                            "0x%08X", spa.spawn_points_record);
                    } else {
                        g.AddNameMapping(nm, sp);
                        new_save_entities.emplace_back(nm, sp);
                        ++generators_created;
                    }
                }
            }
        }
        if (!new_save_entities.empty() || !s.spawn_point_deletes.empty()) {

            const int save_idx = find_level_save_index(s.lev.bnk_path,
                                                       s.lev.file_index);
            std::string serr;
            if (save_idx < 0) {
                serr = ".save entry not found";
            } else {
                try {
                    save_rewrite_bytes = BnkCache::extract_bytes(
                        s.lev.bnk_path, save_idx);
                } catch (...) {
                    save_rewrite_bytes.clear();
                }
                if (save_rewrite_bytes.empty()) {
                    serr = ".save extract failed";
                } else {
                    std::unordered_set<uint32_t> deleted_entities;
                    for (const auto& deletion : s.spawn_point_deletes) {
                        deleted_entities.insert(
                            deletion.spawn_point_entity);
                    }
                    save_entities_deleted = remove_save_entities(
                        save_rewrite_bytes, deleted_entities);
                    if (append_save_entities(save_rewrite_bytes,
                                             new_save_entities, serr)) {
                        save_rewrite_index = save_idx;
                        save_rewrite_bnk = s.lev.bnk_path;
                    }
                }
            }
            if (save_rewrite_index < 0) {
                DebugTrace::log(
                    "save: chest .save registry rewrite skipped: %s",
                    serr.c_str());
                save_rewrite_bytes.clear();
                chest_entities_created = 0;
                if (!s.spawn_point_deletes.empty()) {
                    msg = "save failed: could not update spawn point "
                          "registrations: " + serr;
                    return false;
                }
            }
        }

        if (contents_applied > 0 || chest_entities_created > 0 ||
            generators_created > 0 || spawn_points_deleted > 0 ||
            spawn_points_repaired > 0 ||
            generators_repaired > 0) {
            gdb_rewrite_bytes = g.Serialize();
            if (!s.gdb.file_path.empty()) {
                gdb_rewrite_loose = s.gdb.file_path;
            } else {
                gdb_rewrite_bnk = s.gdb.bnk_path;
                gdb_rewrite_index = s.gdb.file_index;
                gdb_rewrite_iso = s.gdb.in_iso;
            }
        }
    }

    if (!save_physics_patches.empty()) {
        if (save_rewrite_bytes.empty()) {
            const int save_idx = find_level_save_index(s.lev.bnk_path,
                                                       s.lev.file_index);
            if (save_idx >= 0) {
                try {
                    save_rewrite_bytes = BnkCache::extract_bytes(
                        s.lev.bnk_path, save_idx);
                } catch (...) {
                    save_rewrite_bytes.clear();
                }
                if (!save_rewrite_bytes.empty()) {
                    save_rewrite_index = save_idx;
                    save_rewrite_bnk = s.lev.bnk_path;
                }
            }
        }
        if (!save_rewrite_bytes.empty()) {
            save_physics_patched = apply_save_physics_patches(
                save_rewrite_bytes, save_physics_patches);
            DebugTrace::log(
                "save: .save PhysicsData patched %zu of %zu entit(ies)",
                save_physics_patched, save_physics_patches.size());
            if (save_physics_patched == 0 &&
                chest_entities_created == 0 &&
                generators_created == 0 &&
                save_entities_deleted == 0) {
                save_rewrite_bytes.clear();
                save_rewrite_index = -1;
                save_rewrite_bnk.clear();
            }
        } else {
            DebugTrace::log(
                "save: .save entry unavailable for PhysicsData patches");
        }
    }

    std::string bake_note;
    std::vector<std::string> gen_asset_models;
    for (const auto& ga : s.generators) {
        if (ga.removed) continue;
        for (const auto& mp : ga.asset_models) {
            gen_asset_models.push_back(mp);
        }
    }
    if (!s.additions.empty() || !gen_asset_models.empty()) {
        const bool rewritable = s.lev.valid && !s.lev.compressed &&
                                s.lev.file_path.empty();
        if (rewritable) {
            try {
                bake_bytes = BnkCache::extract_bytes(s.lev.bnk_path,
                                                     s.lev.file_index);
            } catch (...) {
                bake_bytes.clear();
            }
            std::string berr;

            std::vector<Addition> lev_additions;
            lev_additions.reserve(s.additions.size());
            for (const auto& a : s.additions) {
                if (!a.as_entity()) lev_additions.push_back(a);
            }
            if (bake_bytes.empty()) {
                bake_note = "; bake skipped: level re-extract failed";
            } else if (!append_additions_to_level(bake_bytes, lev_additions,
                                                  berr)) {
                bake_note = "; bake skipped: " + berr;
                bake_bytes.clear();
            } else {
                need_bake = true;
                bake_iso = s.lev.in_iso;
                bake_bnk_path = s.lev.bnk_path;
                bake_index = s.lev.file_index;
                bake_count = s.additions.size();
                if (bake_iso) {
                    bake_vpath =
                        ISO::IsoMount::strip_iso_prefix(s.lev.bnk_path);
                    record_dirent_bak(bake_vpath);
                } else {
                    std::vector<uint32_t> mdl_hashes;
                    for (const auto& a : s.additions) {
                        if (a.removed) continue;
                        mdl_hashes.push_back(
                            fnv1_32(lower_model_path(a.model_path)));

                        if (a.physics_file_hash) {
                            mdl_hashes.push_back(a.physics_file_hash);
                        } else if (a.silver_keys_needed > 0) {
                            // The authored silver-chest template uses the
                            // standard chest collision resource through the
                            // global keyframed component.
                            mdl_hashes.push_back(0x9FF26AA5u);
                        }
                    }
                    for (const auto& mp : gen_asset_models) {
                        mdl_hashes.push_back(
                            fnv1_32(lower_model_path(mp)));
                    }
                    try {
                        const auto bc = BnkCache::get(s.lev.bnk_path);
                        std::string ed_name =
                            bc.reader->list_files()
                                [(size_t)s.lev.file_index].name;
                        const std::string suffix = ".engine_level";
                        std::string low = ed_name;
                        for (char& c : low)
                            c = (char)std::tolower((unsigned char)c);
                        const size_t sp = low.rfind(suffix);
                        if (sp != std::string::npos &&
                            sp + suffix.size() == low.size()) {
                            std::string stem = low.substr(0, sp);
                            std::replace(stem.begin(), stem.end(), '\\',
                                         '/');
                            const std::string key =
                                stem + ".engine_data";
                            const int ed_idx = BnkCache::find_index(
                                s.lev.bnk_path, key);
                            if (ed_idx >= 0) {
                                bake_ed_bytes = BnkCache::extract_bytes(
                                    s.lev.bnk_path, ed_idx);
                                bool changed = false;
                                std::string perr;
                                if (patch_engine_resource_list(
                                        bake_ed_bytes, mdl_hashes,
                                        changed, perr)) {
                                    if (changed) {
                                        bake_ed_index = ed_idx;
                                        DebugTrace::log(
                                            "save: engine_data idx=%d "
                                            "resource list +%zu hash(es)",
                                            ed_idx, mdl_hashes.size());
                                    } else {
                                        bake_ed_bytes.clear();
                                        DebugTrace::log(
                                            "save: engine_data already "
                                            "lists all placed models");
                                    }
                                } else {
                                    bake_ed_bytes.clear();
                                    DebugTrace::log(
                                        "save: engine_data patch "
                                        "skipped: %s", perr.c_str());
                                }
                            } else {
                                DebugTrace::log(
                                    "save: engine_data entry not found "
                                    "(%s)", key.c_str());
                            }
                            const std::string lmp_key = stem + ".lmp";
                            const int lmp_idx = BnkCache::find_index(
                                s.lev.bnk_path, lmp_key);
                            if (lmp_idx >= 0) {
                                bake_lmp_bytes = BnkCache::extract_bytes(
                                    s.lev.bnk_path, lmp_idx);
                                bool changed = false;
                                std::string perr;
                                if (patch_lmp_probes(bake_lmp_bytes,
                                                     s.additions,
                                                     bake_bytes, changed,
                                                     perr)) {
                                    if (changed) {
                                        bake_lmp_index = lmp_idx;
                                        DebugTrace::log(
                                            "save: lmp idx=%d probe "
                                            "record(s) appended",
                                            lmp_idx);
                                    } else {
                                        bake_lmp_bytes.clear();
                                        DebugTrace::log(
                                            "save: lmp already has all "
                                            "placed instances");
                                    }
                                } else {
                                    bake_lmp_bytes.clear();
                                    DebugTrace::log(
                                        "save: lmp patch skipped: %s",
                                        perr.c_str());
                                }
                            } else {
                                DebugTrace::log(
                                    "save: lmp entry not found (%s)",
                                    lmp_key.c_str());
                            }

                            const std::filesystem::path data_dir =
                                std::filesystem::path(s.lev.bnk_path)
                                    .parent_path();
                            const std::string streaming_path =
                                (data_dir / "streaming.bnk").string();
                            const std::string globals_path =
                                (data_dir / "Globals" /
                                 "globals_models.bnk").string();
                            const std::string models_key =
                                stem + "_models.bnk";
                            std::vector<BnkWriter::EntryAddition>
                                mdl_adds;
                            std::vector<BnkWriter::EntryAddition>
                                stream_adds;
                            const int models_idx =
                                std::filesystem::exists(streaming_path)
                                    ? BnkCache::find_index(streaming_path,
                                                           models_key)
                                    : -1;
                            std::vector<uint8_t> models_blob;
                            if (models_idx >= 0) {
                                models_blob = BnkCache::extract_bytes(
                                    streaming_path, models_idx);
                            }
                            std::vector<std::string> inject_paths;
                            for (const auto& a : s.additions) {
                                if (a.removed) continue;
                                inject_paths.push_back(a.model_path);
                            }
                            for (const auto& mp : gen_asset_models) {
                                inject_paths.push_back(mp);
                            }
                            for (const auto& inj_path : inject_paths) {
                                const std::string lp =
                                    lower_model_path(inj_path);
                                const std::string want = norm_key(lp);
                                bool have = false;
                                if (!models_blob.empty()) {
                                    try {
                                        BNKReader lm(models_blob);
                                        have = nested_bank_has(lm, want);
                                    } catch (...) {}
                                }
                                if (!have &&
                                    BnkCache::find_index(globals_path,
                                                         want) >= 0) {
                                    have = true;
                                }
                                bool queued = false;
                                for (const auto& q : mdl_adds) {
                                    if (norm_key(q.name) == want) {
                                        queued = true;
                                        break;
                                    }
                                }
                                if (have || queued) continue;
                                std::string src_name;
                                std::vector<uint8_t> src_payload;
                                if (models_idx < 0 ||
                                    !find_in_nested_banks(
                                        streaming_path, "_models.bnk",
                                        want, src_name, src_payload)) {
                                    DebugTrace::log(
                                        "save: model body not found "
                                        "anywhere: %s", want.c_str());
                                    continue;
                                }
                                mdl_adds.push_back(
                                    {src_name, std::move(src_payload)});
                                const size_t slash = want.rfind('/');
                                if (slash != std::string::npos) {
                                    const std::string folder =
                                        want.substr(0, slash + 1);
                                    size_t got =
                                        collect_folder_from_nested_banks(
                                            s.lev.bnk_path,
                                            "_streaming.bnk", folder,
                                            stream_adds);
                                    if (got == 0) {
                                        for (const auto& other :
                                             S.bnk_paths) {
                                            if (other == s.lev.bnk_path) {
                                                continue;
                                            }
                                            got =
                                            collect_folder_from_nested_banks(
                                                other, "_streaming.bnk",
                                                folder, stream_adds);
                                            if (got) {
                                                DebugTrace::log(
                                                    "save: streaming donor "
                                                    "bnk %s",
                                                    other.c_str());
                                                break;
                                            }
                                        }
                                    }
                                    DebugTrace::log(
                                        "save: inject %s (+%zu streaming "
                                        "file(s))", src_name.c_str(),
                                        got);
                                }
                            }
                            if (!mdl_adds.empty()) {
                                const std::string scen_dir =
                                    stem.substr(0, stem.rfind('/') + 1);
                                const std::string hdrs_key =
                                    stem + "_texture_headers.bnk";
                                const std::string body_key =
                                    scen_dir + "textures.bnk";
                                const std::string mani_key =
                                    body_key + ".manifest";
                                const int hdrs_idx = BnkCache::find_index(
                                    s.lev.bnk_path, hdrs_key);
                                const int body_idx = BnkCache::find_index(
                                    s.lev.bnk_path, body_key);
                                const int mani_idx = BnkCache::find_index(
                                    s.lev.bnk_path, mani_key);
                                std::unordered_set<std::string> have_hdr;
                                std::unordered_set<std::string> have_body;
                                std::vector<uint8_t> hdrs_blob, body_blob;
                                if (hdrs_idx >= 0) {
                                    hdrs_blob = BnkCache::extract_bytes(
                                        s.lev.bnk_path, hdrs_idx);
                                    try {
                                        BNKReader r(hdrs_blob);
                                        for (const auto& fe :
                                             r.list_files())
                                            have_hdr.insert(
                                                norm_key(fe.name));
                                    } catch (...) {}
                                }
                                if (body_idx >= 0) {
                                    body_blob = BnkCache::extract_bytes(
                                        s.lev.bnk_path, body_idx);
                                    try {
                                        BNKReader r(body_blob);
                                        for (const auto& fe :
                                             r.list_files())
                                            have_body.insert(
                                                norm_key(fe.name));
                                    } catch (...) {}
                                }
                                {
                                    const int sh_idx =
                                        BnkCache::find_index(
                                            s.lev.bnk_path,
                                            "worlds/albion/shared/"
                                            "shared_6281.bnk");
                                    if (sh_idx >= 0) {
                                        try {
                                            std::vector<uint8_t> sh =
                                                BnkCache::extract_bytes(
                                                    s.lev.bnk_path,
                                                    sh_idx);
                                            BNKReader r(sh);
                                            for (const auto& fe :
                                                 r.list_files())
                                                have_body.insert(
                                                    norm_key(fe.name));
                                        } catch (...) {}
                                    }
                                }
                                std::vector<BnkWriter::EntryAddition>
                                    hdr_adds, body_adds;
                                std::string mani_append;
                                for (const auto& ma : mdl_adds) {
                                    std::vector<std::string> texs;
                                    collect_tex_refs(ma.payload, texs);
                                    for (const auto& t : texs) {
                                        if (!have_hdr.count(t)) {
                                            std::string sn;
                                            std::vector<uint8_t> sp;
                                            if (find_in_nested_banks(
                                                    s.lev.bnk_path,
                                                    "_texture_headers"
                                                    ".bnk",
                                                    t, sn, sp)) {
                                                hdr_adds.push_back(
                                                    {sn,
                                                     std::move(sp)});
                                                have_hdr.insert(t);
                                            } else {
                                                DebugTrace::log(
                                                    "save: tex header "
                                                    "not found: %s",
                                                    t.c_str());
                                            }
                                        }
                                        if (!have_body.count(t)) {
                                            std::string sn;
                                            std::vector<uint8_t> sp;
                                            if (find_in_nested_banks(
                                                    s.lev.bnk_path,
                                                    "/textures.bnk", t,
                                                    sn, sp)) {
                                                body_adds.push_back(
                                                    {sn,
                                                     std::move(sp)});
                                                have_body.insert(t);
                                                std::string tl = t;
                                                std::replace(tl.begin(),
                                                             tl.end(),
                                                             '/', '\\');
                                                mani_append +=
                                                    "\"" + tl + "\" \"" +
                                                    tl + "\" 0 0 3\r\n";
                                            } else {
                                                DebugTrace::log(
                                                    "save: tex body not "
                                                    "found: %s",
                                                    t.c_str());
                                            }
                                        }
                                    }
                                }
                                std::string terr;
                                if (!hdr_adds.empty() &&
                                    hdrs_idx >= 0 &&
                                    BnkWriter::AddEntriesToBnkBytes(
                                        hdrs_blob, hdr_adds, terr)) {
                                    BnkWriter::EntryReplacement r;
                                    r.file_index = hdrs_idx;
                                    r.payload = std::move(hdrs_blob);
                                    bake_more.push_back(std::move(r));
                                    DebugTrace::log(
                                        "save: +%zu texture header(s)",
                                        hdr_adds.size());
                                } else if (!hdr_adds.empty()) {
                                    DebugTrace::log(
                                        "save: tex header add failed: "
                                        "%s", terr.c_str());
                                }
                                if (!body_adds.empty() &&
                                    body_idx >= 0 &&
                                    BnkWriter::AddEntriesToBnkBytes(
                                        body_blob, body_adds, terr)) {
                                    BnkWriter::EntryReplacement r;
                                    r.file_index = body_idx;
                                    r.payload = std::move(body_blob);
                                    bake_more.push_back(std::move(r));
                                    DebugTrace::log(
                                        "save: +%zu texture bodies",
                                        body_adds.size());
                                    if (mani_idx >= 0 &&
                                        !mani_append.empty()) {
                                        std::vector<uint8_t> mani =
                                            BnkCache::extract_bytes(
                                                s.lev.bnk_path,
                                                mani_idx);
                                        const bool crlf =
                                            std::find(mani.begin(),
                                                      mani.end(),
                                                      (uint8_t)'\r') !=
                                            mani.end();
                                        if (!crlf) {
                                            std::string tmp;
                                            for (char c : mani_append)
                                                if (c != '\r')
                                                    tmp.push_back(c);
                                            mani_append = tmp;
                                        }
                                        mani.insert(mani.end(),
                                                    mani_append.begin(),
                                                    mani_append.end());
                                        BnkWriter::EntryReplacement r2;
                                        r2.file_index = mani_idx;
                                        r2.payload = std::move(mani);
                                        bake_more.push_back(
                                            std::move(r2));
                                    }
                                } else if (!body_adds.empty()) {
                                    DebugTrace::log(
                                        "save: tex body add failed: %s",
                                        terr.c_str());
                                }
                                std::string aerr;
                                if (!BnkWriter::AddEntriesToBnkBytes(
                                        models_blob, mdl_adds, aerr)) {
                                    DebugTrace::log(
                                        "save: models bank add failed: "
                                        "%s", aerr.c_str());
                                } else {
                                    bake_models_index = models_idx;
                                    bake_models_bytes =
                                        std::move(models_blob);
                                    bake_streaming_path = streaming_path;
                                }
                                if (bake_models_index >= 0 &&
                                    !stream_adds.empty()) {
                                    const std::string lvs_key =
                                        stem + "_streaming.bnk";
                                    const int lvs_idx =
                                        BnkCache::find_index(
                                            s.lev.bnk_path, lvs_key);
                                    if (lvs_idx >= 0) {
                                        std::vector<uint8_t> lvs =
                                            BnkCache::extract_bytes(
                                                s.lev.bnk_path, lvs_idx);
                                        std::string serr;
                                        std::vector<
                                            BnkWriter::EntryAddition>
                                            fresh;
                                        try {
                                            BNKReader lr(lvs);
                                            for (auto& sa : stream_adds) {
                                                if (!nested_bank_has(
                                                        lr,
                                                        norm_key(
                                                            sa.name))) {
                                                    fresh.push_back(
                                                        std::move(sa));
                                                }
                                            }
                                        } catch (...) {}
                                        if (!fresh.empty() &&
                                            BnkWriter::
                                                AddEntriesToBnkBytes(
                                                    lvs, fresh, serr)) {
                                            bake_lvstream_index = lvs_idx;
                                            bake_lvstream_bytes =
                                                std::move(lvs);
                                        } else if (!fresh.empty()) {
                                            DebugTrace::log(
                                                "save: level streaming "
                                                "add failed: %s",
                                                serr.c_str());
                                        }
                                    }
                                }
                            }
                        }
                    } catch (const std::exception& ex) {
                        bake_ed_index = -1;
                        bake_ed_bytes.clear();
                        bake_lmp_index = -1;
                        bake_lmp_bytes.clear();
                        DebugTrace::log(
                            "save: engine_data/lmp patch failed: %s",
                            ex.what());
                    } catch (...) {
                        bake_ed_index = -1;
                        bake_ed_bytes.clear();
                        bake_lmp_index = -1;
                        bake_lmp_bytes.clear();
                    }
                }
                s.saving = true;
            }
        } else {
            bake_note = std::string("; placed model(s) kept app-side "
                                    "(level BNK not rewritable: ") +
                        (s.lev.compressed ? "chunk-compressed"
                                          : "no locator") + ")";
        }
        if (!need_bake) {
            DebugTrace::log("save: bake not started:%s",
                            bake_note.c_str());
        }
    }

    if (!write_additions(s, msg)) return false;

    msg = "saved " + std::to_string(lev_written) +
          " level-file patch(es), " + std::to_string(gdb_written) +
          " gdb component(s)";
    if (rs_written) {
        msg += ", " + std::to_string(rs_written) + " rotation patch(es)";
    }
    if (skipped || rs_visual) {
        msg += " (" + std::to_string(skipped + rs_visual) +
               " visual-only edit(s) not saved)";
    }
    msg += bake_note;
    deferred_work = need_bake || !gdb_rewrite_bytes.empty() ||
                    save_rewrite_index >= 0 || !babel_edits.empty();
    if (!deferred_work) {
        s.dirty = false;
        if (lev_written || gdb_written || rs_written) {
            BnkCache::invalidate(s.lev.bnk_path);
            if (!s.gdb.bnk_path.empty()) {
                BnkCache::invalidate(s.gdb.bnk_path);
            }
            reload_needed = true;
            reload_entry = s.entry;
            msg += "; reloading";
        }
    } else {
        s.saving = true;
    }
    }

    if (!deferred_work) {
        if (reload_needed) Level::OpenAsync(reload_entry);
        return true;
    }

    std::string berr;
    bool rebuilt = true;
    if (need_bake) {
    progress_update(10, 100, "Rebuilding level BNK...");
    BnkCache::invalidate(bake_bnk_path);
    if (bake_iso) {
        rebuilt = BnkWriter::RebuildIsoLevelBnk(bake_vpath, bake_index,
                                                bake_bytes, berr);
    } else {
        std::vector<BnkWriter::EntryReplacement> reps(1);
        reps[0].file_index = bake_index;
        reps[0].payload = std::move(bake_bytes);
        if (bake_ed_index >= 0 && !bake_ed_bytes.empty()) {
            BnkWriter::EntryReplacement r;
            r.file_index = bake_ed_index;
            r.payload = std::move(bake_ed_bytes);
            reps.push_back(std::move(r));
        }
        if (bake_lmp_index >= 0 && !bake_lmp_bytes.empty()) {
            BnkWriter::EntryReplacement r;
            r.file_index = bake_lmp_index;
            r.payload = std::move(bake_lmp_bytes);
            reps.push_back(std::move(r));
        }
        if (bake_lvstream_index >= 0 && !bake_lvstream_bytes.empty()) {
            BnkWriter::EntryReplacement r;
            r.file_index = bake_lvstream_index;
            r.payload = std::move(bake_lvstream_bytes);
            reps.push_back(std::move(r));
        }
        for (auto& r : bake_more) reps.push_back(std::move(r));
        rebuilt = BnkWriter::RebuildWithReplacedEntries(bake_bnk_path,
                                                        reps, berr);
        if (rebuilt && bake_models_index >= 0 &&
            !bake_models_bytes.empty()) {
            const std::string bak = bake_streaming_path + ".bak";
            std::error_code ec;
            if (!std::filesystem::exists(bak, ec)) {
                progress_update(70, 100, "Backing up streaming.bnk...");
                if (!copy_file_with_progress(bake_streaming_path, bak,
                                             "streaming.bnk backup",
                                             berr)) {
                    DebugTrace::log(
                        "save: streaming.bnk backup failed: %s",
                        berr.c_str());
                    rebuilt = false;
                }
            }
            if (rebuilt) {
                progress_update(75, 100, "Rebuilding streaming.bnk...");
                BnkCache::invalidate(bake_streaming_path);
                std::vector<BnkWriter::EntryReplacement> sreps(1);
                sreps[0].file_index = bake_models_index;
                sreps[0].payload = std::move(bake_models_bytes);
                rebuilt = BnkWriter::RebuildWithReplacedEntries(
                    bake_streaming_path, sreps, berr);
                BnkCache::invalidate(bake_streaming_path);
                DebugTrace::log(
                    "save: streaming.bnk models inject %s %s",
                    rebuilt ? "OK" : "FAILED", berr.c_str());
            }
        }
    }
    DebugTrace::log("save: bake %s (iso=%d target='%s') %s",
                    rebuilt ? "OK" : "FAILED", bake_iso ? 1 : 0,
                    bake_iso ? bake_vpath.c_str() : bake_bnk_path.c_str(),
                    berr.c_str());
    }

    bool contents_ok = true;
    if (rebuilt && !gdb_rewrite_bytes.empty()) {
        progress_update(85, 100, "Writing chest contents...");
        std::string gerr;
        if (!gdb_rewrite_loose.empty()) {
            std::ofstream f(gdb_rewrite_loose,
                            std::ios::binary | std::ios::trunc);
            contents_ok = bool(f);
            if (contents_ok) {
                f.write(reinterpret_cast<const char*>(
                            gdb_rewrite_bytes.data()),
                        std::streamsize(gdb_rewrite_bytes.size()));
                contents_ok = f.good();
            }
            if (!contents_ok) gerr = "loose .gdb write failed";
            if (contents_ok && save_rewrite_index >= 0 &&
                !save_rewrite_bnk.empty()) {
                BnkCache::invalidate(save_rewrite_bnk);
                contents_ok = BnkWriter::RebuildWithReplacedEntry(
                    save_rewrite_bnk, save_rewrite_index,
                    save_rewrite_bytes, gerr);
                BnkCache::invalidate(save_rewrite_bnk);
            }
        } else if (gdb_rewrite_iso) {
            contents_ok = BnkWriter::RebuildIsoLevelBnk(
                ISO::IsoMount::strip_iso_prefix(gdb_rewrite_bnk),
                gdb_rewrite_index, gdb_rewrite_bytes, gerr);
            if (contents_ok && save_rewrite_index >= 0) {
                contents_ok = BnkWriter::RebuildIsoLevelBnk(
                    ISO::IsoMount::strip_iso_prefix(save_rewrite_bnk),
                    save_rewrite_index, save_rewrite_bytes, gerr);
            }
        } else if (gdb_rewrite_index >= 0) {
            BnkCache::invalidate(gdb_rewrite_bnk);
            std::vector<BnkWriter::EntryReplacement> reps(1);
            reps[0].file_index = gdb_rewrite_index;
            reps[0].payload = std::move(gdb_rewrite_bytes);
            const bool save_same_bnk =
                save_rewrite_index >= 0 &&
                save_rewrite_bnk == gdb_rewrite_bnk;
            if (save_same_bnk) {
                BnkWriter::EntryReplacement r;
                r.file_index = save_rewrite_index;
                r.payload = std::move(save_rewrite_bytes);
                reps.push_back(std::move(r));
            }
            contents_ok = BnkWriter::RebuildWithReplacedEntries(
                gdb_rewrite_bnk, reps, gerr);
            BnkCache::invalidate(gdb_rewrite_bnk);
            if (contents_ok && save_rewrite_index >= 0 && !save_same_bnk) {
                BnkCache::invalidate(save_rewrite_bnk);
                contents_ok = BnkWriter::RebuildWithReplacedEntry(
                    save_rewrite_bnk, save_rewrite_index,
                    save_rewrite_bytes, gerr);
                BnkCache::invalidate(save_rewrite_bnk);
            }
        } else {
            contents_ok = false;
            gerr = "no .gdb target";
        }
        DebugTrace::log(
            "save: gdb contents rewrite %s (%zu edit(s), %zu new "
            "chest(s), %zu deleted spawn point(s), %zu repaired spawn "
            "point(s), %zu repaired generator(s)) %s",
            contents_ok ? "OK" : "FAILED", contents_applied,
            chest_entities_created, spawn_points_deleted,
            spawn_points_repaired, generators_repaired, gerr.c_str());
    }

    if (rebuilt && contents_ok && gdb_rewrite_bytes.empty() &&
        save_rewrite_index >= 0 && !save_rewrite_bytes.empty()) {
        progress_update(85, 100, "Writing entity save data...");
        std::string gerr;
        BnkCache::invalidate(save_rewrite_bnk);
        if (ISO::IsoMount::is_iso_path(save_rewrite_bnk)) {
            contents_ok = BnkWriter::RebuildIsoLevelBnk(
                ISO::IsoMount::strip_iso_prefix(save_rewrite_bnk),
                save_rewrite_index, save_rewrite_bytes, gerr);
        } else {
            contents_ok = BnkWriter::RebuildWithReplacedEntry(
                save_rewrite_bnk, save_rewrite_index, save_rewrite_bytes,
                gerr);
        }
        BnkCache::invalidate(save_rewrite_bnk);
        DebugTrace::log("save: .save physics rewrite %s (%zu entit(ies)) %s",
                        contents_ok ? "OK" : "FAILED",
                        save_physics_patched, gerr.c_str());
    }

    size_t text_written = 0;
    if (rebuilt && contents_ok && !babel_edits.empty()) {
        progress_update(92, 100, "Writing text banks...");
        std::string root = S.root_dir;
        {
            std::error_code ec;
            std::filesystem::path rp(root);
            if (!root.empty() &&
                std::filesystem::is_regular_file(rp, ec)) {
                root = rp.parent_path().string();
            }
        }
        std::string terr;
        if (TextBank::ApplyEdits(root, babel_edits, terr)) {
            text_written = babel_edits.size();
        } else {
            contents_ok = false;
            OutputLog::error("level edit: text bank write failed: " +
                             terr);
        }
    }

    {
        std::lock_guard<std::mutex> lk(mtx());
        auto& s = st();
        s.saving = false;
        if (rebuilt && contents_ok) {
            if (text_written > 0) s.text_edits.clear();
            if (need_bake) {
                s.additions.clear();
                s.edits.clear();
                s.undo_stack.clear();
                {
                    std::error_code ec;
                    std::filesystem::remove(additions_path(), ec);
                }
            }
            if (contents_applied > 0) s.contents_edits.clear();
            if (generators_created > 0) {
                s.generators.clear();
                s.spawn_point_adds.clear();
            }
            if (spawn_points_deleted > 0) {
                s.spawn_point_deletes.clear();
            }
            BnkCache::invalidate(s.lev.bnk_path);
            if (!s.gdb.bnk_path.empty()) {
                BnkCache::invalidate(s.gdb.bnk_path);
            }
            fill_bnk_target(s.lev);
            if (!s.gdb.bnk_path.empty()) fill_bnk_target(s.gdb);
            s.dirty = false;
            reload_needed = true;
            reload_entry = s.entry;
            if (need_bake) {
                msg += "; baked " + std::to_string(bake_count) +
                       " model(s) into the level BNK";
            }
            if (contents_applied > 0) {
                msg += "; rewrote contents of " +
                       std::to_string(contents_applied) + " container(s)";
            }
            if (chest_entities_created > 0) {
                msg += "; created " +
                       std::to_string(chest_entities_created) +
                       " chest entit(ies)";
            }
            if (save_physics_patched > 0) {
                msg += "; updated " +
                       std::to_string(save_physics_patched) +
                       " entity save transform(s)";
            }
            if (generators_created > 0) {
                msg += "; authored " +
                       std::to_string(generators_created) +
                       " generator/spawn point(s)";
            }
            if (spawn_points_deleted > 0) {
                msg += "; deleted " +
                       std::to_string(spawn_points_deleted) +
                       " spawn point(s)";
            }
            if (spawn_points_repaired > 0) {
                msg += "; repaired " +
                       std::to_string(spawn_points_repaired) +
                       " legacy spawn point(s)";
            }
            if (generators_repaired > 0) {
                msg += "; repaired " +
                       std::to_string(generators_repaired) +
                       " legacy generator(s)";
            }
            if (text_written > 0) {
                msg += "; wrote " + std::to_string(text_written) +
                       " text entr(ies)";
            }
            msg += "; reloading";
        } else if (!rebuilt) {
            msg += "; BAKE FAILED: " + berr +
                   " (placements kept app-side)";
        } else {
            msg += "; CONTENTS REWRITE FAILED (edits kept app-side)";
        }
    }
    if (reload_needed) Level::OpenAsync(reload_entry);
    return rebuilt && contents_ok;
}

bool RestoreDefaults(std::string& msg) {
    FlatAssetEntry reload_entry;
    FileTarget lev_t, gdb_t;
    {
        std::lock_guard<std::mutex> lk(mtx());
        auto& s = st();
        if (!s.available) { msg = "no level loaded"; return false; }
        if (s.saving) { msg = "a save is in progress"; return false; }
        s.saving = true;
        lev_t = s.lev;
        gdb_t = s.gdb;
    }

    restore_dirent_bak();

    std::unordered_set<std::string> restored;
    std::string rerr;
    bool ok = restore_target(lev_t, "lev", restored, rerr) &&
              restore_target(gdb_t, "gdb", restored, rerr);
    if (ok && !lev_t.bnk_path.empty() &&
        !ISO::IsoMount::is_iso_path(lev_t.bnk_path)) {
        const std::string streaming_path =
            (std::filesystem::path(lev_t.bnk_path).parent_path() /
             "streaming.bnk").string();
        const std::string bak = streaming_path + ".bak";
        std::error_code ec;
        if (std::filesystem::exists(bak, ec)) {
            progress_update(80, 100, "Restoring streaming.bnk...");
            BnkCache::invalidate(streaming_path);
            std::string cerr2;
            if (copy_file_with_progress(bak, streaming_path,
                                        "streaming.bnk restore", cerr2,
                                        false)) {
                std::filesystem::remove(bak, ec);
            } else {
                rerr = cerr2;
                ok = false;
            }
            BnkCache::invalidate(streaming_path);
        }
    }

    {
        std::lock_guard<std::mutex> lk(mtx());
        auto& s = st();
        s.saving = false;
        if (!ok) {
            msg = rerr;
            return false;
        }
        if (!s.lev.bnk_path.empty()) BnkCache::invalidate(s.lev.bnk_path);
        if (!s.gdb.bnk_path.empty()) BnkCache::invalidate(s.gdb.bnk_path);
        s.edits.clear();
        s.undo_stack.clear();
        s.additions.clear();
        s.contents_edits.clear();
        {
            std::error_code ec;
            std::filesystem::remove(additions_path(), ec);
        }
        s.dirty = false;
        ++s.revision;
        reload_entry = s.entry;
    }

    Level::OpenAsync(reload_entry);
    msg = "level restored from backup; reloading";
    return true;
}

void ClearEdits() {
    std::lock_guard<std::mutex> lk(mtx());
    st().edits.clear();
    st().undo_stack.clear();
    st().contents_edits.clear();
    st().dirty = false;
    ++st().revision;
}

bool RunStreamFix(const std::string& streaming_path, std::string& msg) {
    const std::string bak = streaming_path + ".bak";
    std::error_code ec;
    if (!std::filesystem::exists(bak, ec)) {
        msg = "streamfix: no backup at " + bak;
        return false;
    }
    BnkCache::invalidate(streaming_path);
    std::string cerr2;
    if (!copy_file_with_progress(bak, streaming_path,
                                 "streaming.bnk restore", cerr2, false)) {
        msg = "streamfix: restore failed: " + cerr2;
        return false;
    }
    BnkCache::invalidate(streaming_path);

    const std::string models_key =
        "worlds/albion/bwsslums/defaultscenario/"
        "defaultscenario_models.bnk";
    const int models_idx =
        BnkCache::find_index(streaming_path, models_key);
    if (models_idx < 0) {
        msg = "streamfix: level models bank not found";
        return false;
    }
    std::vector<uint8_t> blob =
        BnkCache::extract_bytes(streaming_path, models_idx);

    const std::string want =
        "art/environment/regions/bower_lake/props/dotxsi/bl_lamp_post/"
        "bl_lamp_post.mdl";
    {
        BNKReader lm(blob);
        if (nested_bank_has(lm, want)) {
            msg = "streamfix: lamp already present after restore?";
            return false;
        }
    }
    std::string src_name;
    std::vector<uint8_t> src_payload;
    if (!find_in_nested_banks(streaming_path, "_models.bnk", want,
                              src_name, src_payload)) {
        msg = "streamfix: lamp source not found";
        return false;
    }
    std::vector<BnkWriter::EntryAddition> adds;
    adds.push_back({src_name, std::move(src_payload)});
    std::string aerr;
    if (!BnkWriter::AddEntriesToBnkBytes(blob, adds, aerr)) {
        msg = "streamfix: add failed: " + aerr;
        return false;
    }
    BnkCache::invalidate(streaming_path);
    std::vector<BnkWriter::EntryReplacement> reps(1);
    reps[0].file_index = models_idx;
    reps[0].payload = std::move(blob);
    if (!BnkWriter::RebuildWithReplacedEntries(streaming_path, reps,
                                               aerr)) {
        msg = "streamfix: rebuild failed: " + aerr;
        return false;
    }
    BnkCache::invalidate(streaming_path);
    msg = "streamfix OK: restored + re-injected lamp with aligned "
          "layout";
    DebugTrace::log("%s", msg.c_str());
    return true;
}

bool RunLevProbe(const std::string& bnk_path, std::string& msg) {
    return RunLevProbeMode(bnk_path, false, msg);
}

bool RunLevProbeMode(const std::string& bnk_path, bool float_only,
                     std::string& msg) {
    const std::string lev_key =
        "worlds/albion/bwsslums/defaultscenario/"
        "defaultscenario.engine_level";
    const std::string lmp_key =
        "worlds/albion/bwsslums/defaultscenario/defaultscenario.lmp";
    const int lev_idx = BnkCache::find_index(bnk_path, lev_key);
    const int lmp_idx = BnkCache::find_index(bnk_path, lmp_key);
    if (lev_idx < 0 || lmp_idx < 0) {
        msg = "probe: entries not found in " + bnk_path;
        return false;
    }
    std::vector<uint8_t> lev;
    std::vector<uint8_t> lmp_gz;
    try {
        lev = BnkCache::extract_bytes(bnk_path, lev_idx);
        lmp_gz = BnkCache::extract_bytes(bnk_path, lmp_idx);
    } catch (const std::exception& ex) {
        msg = std::string("probe: extract failed: ") + ex.what();
        return false;
    }

    Level::EngineLevelInfo info;
    if (!Level::ParseEngineLevel(lev, info)) {
        msg = "probe: lev parse failed: " + info.error;
        return false;
    }
    const Level::PropBlock* oak = nullptr;
    for (const auto& b : info.prop_blocks) {
        if (b.type == 2 &&
            b.model_path.find("bs_snowyoak") != std::string::npos) {
            oak = &b;
            break;
        }
    }
    if (!oak || oak->instances.empty()) {
        msg = "probe: snowyoak block not found";
        return false;
    }
    const Level::PropInstance& inst = oak->instances[0];
    const size_t fo = inst.pos_file_offset;
    const size_t rec_start = fo - 11;
    const size_t cnt_off = rec_start - 4;
    const uint32_t count = get_u32_be2(lev.data() + cnt_off);
    if (count != oak->instances.size() || fo + 80 > lev.size()) {
        msg = "probe: block layout mismatch";
        return false;
    }

    float z = inst.values[2] + 8.0f;
    uint32_t zb;
    std::memcpy(&zb, &z, 4);
    lev[fo + 8]  = uint8_t(zb >> 24);
    lev[fo + 9]  = uint8_t(zb >> 16);
    lev[fo + 10] = uint8_t(zb >> 8);
    lev[fo + 11] = uint8_t(zb);

    const uint64_t clone_hash =
        ((uint64_t)fnv1_32(oak->model_path) << 32) |
        fnv1_32(oak->model_path + "#probe_clone");
    float vals[20];
    std::memcpy(vals, inst.values, sizeof(vals));
    if (!float_only) {
        std::vector<uint8_t> rec;
        rec.push_back(1);
        rec.push_back(0);
        rec.push_back(0);
        put_u64_be(rec, clone_hash);
        vals[0] += 4.0f;
        for (int k = 0; k < 20; ++k) put_f32_be_v(rec, vals[k]);
        const size_t insert_at = rec_start + (size_t)count * 91;
        lev.insert(lev.begin() + insert_at, rec.begin(), rec.end());
        const uint32_t nc = count + 1;
        lev[cnt_off]     = uint8_t(nc >> 24);
        lev[cnt_off + 1] = uint8_t(nc >> 16);
        lev[cnt_off + 2] = uint8_t(nc >> 8);
        lev[cnt_off + 3] = uint8_t(nc);
    }

    if (float_only) {
        BnkCache::invalidate(bnk_path);
        std::vector<BnkWriter::EntryReplacement> reps(1);
        reps[0].file_index = lev_idx;
        reps[0].payload = std::move(lev);
        std::string berr;
        if (!BnkWriter::RebuildWithReplacedEntries(bnk_path, reps,
                                                   berr)) {
            msg = "probe: rebuild failed: " + berr;
            return false;
        }
        BnkCache::invalidate(bnk_path);
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "probe OK (float only): oak raised to z=%.2f at "
                      "(%.2f, %.2f)",
                      z, inst.values[0], inst.values[1]);
        msg = buf;
        DebugTrace::log("%s", buf);
        return true;
    }

    std::vector<uint8_t> raw;
    if (!gzip_inflate(lmp_gz, raw)) {
        msg = "probe: lmp gunzip failed";
        return false;
    }
    const size_t total = raw.size();
    size_t n = 0;
    for (size_t c = (total - 4) / 56; c >= 1; --c) {
        const size_t pos = total - 56 * c - 4;
        if (pos < 32) continue;
        if (get_u32_be2(raw.data() + pos) == (uint32_t)c) {
            n = c;
            break;
        }
    }
    if (!n) {
        msg = "probe: lmp probe section not found";
        return false;
    }
    const size_t sec = total - 56 * n;
    size_t donor = 0;
    for (size_t i = 0; i < n; ++i) {
        uint64_t h = 0;
        for (int k = 0; k < 8; ++k) {
            h = (h << 8) | raw[sec + i * 56 + (size_t)k];
        }
        if (h == inst.hash) {
            donor = sec + i * 56;
            break;
        }
    }
    if (!donor) {
        msg = "probe: snowyoak lmp record not found";
        return false;
    }
    std::vector<uint8_t> lrec;
    put_u64_be(lrec, clone_hash);
    lrec.insert(lrec.end(), raw.begin() + donor + 8,
                raw.begin() + donor + 56);
    raw.insert(raw.end(), lrec.begin(), lrec.end());
    const uint32_t nn = (uint32_t)(n + 1);
    raw[sec - 4] = uint8_t(nn >> 24);
    raw[sec - 3] = uint8_t(nn >> 16);
    raw[sec - 2] = uint8_t(nn >> 8);
    raw[sec - 1] = uint8_t(nn);
    std::vector<uint8_t> lmp_out;
    if (!gzip_deflate(raw, lmp_out)) {
        msg = "probe: lmp gzip failed";
        return false;
    }

    BnkCache::invalidate(bnk_path);
    std::vector<BnkWriter::EntryReplacement> reps(2);
    reps[0].file_index = lev_idx;
    reps[0].payload = std::move(lev);
    reps[1].file_index = lmp_idx;
    reps[1].payload = std::move(lmp_out);
    std::string berr;
    if (!BnkWriter::RebuildWithReplacedEntries(bnk_path, reps, berr)) {
        msg = "probe: rebuild failed: " + berr;
        return false;
    }
    BnkCache::invalidate(bnk_path);
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "probe OK: oak raised to z=%.2f at (%.2f, %.2f); clone "
                  "hash=%016llx at x=%.2f",
                  z, inst.values[0], inst.values[1],
                  (unsigned long long)clone_hash, vals[0]);
    msg = buf;
    DebugTrace::log("%s", buf);
    return true;
}

}
