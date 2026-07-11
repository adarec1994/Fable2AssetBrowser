#include "LevelEdit.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../BNKCore.cpp"
#include "../ISO/IsoMount.h"
#include "../UI/OutputLog.h"
#include "../Utilities/State.h"
#include "LevelLoader.h"

namespace LevelEdit {
namespace {

struct EditEntry {
    float delta[3] = {0, 0, 0};
    float orig[3]  = {0, 0, 0};
    uint32_t lev_off = 0;
    uint32_t gdb_off[3] = {0, 0, 0};
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

struct UndoStep {
    std::vector<std::pair<uint32_t, std::array<float, 3>>> deltas_before;
};

struct ModuleState {
    bool available = false;
    bool enabled   = false;
    bool dirty     = false;
    uint64_t revision = 0;

    FlatAssetEntry entry{};
    FileTarget lev;
    FileTarget gdb;

    std::unordered_map<uint32_t, EditEntry> edits;
    std::vector<UndoStep> undo_stack;
};

ModuleState& st() {
    static ModuleState s;
    return s;
}

constexpr size_t kMaxUndoSteps = 128;

void fill_bnk_target(FileTarget& t) {
    t.valid = false;
    if (t.bnk_path.empty() || t.file_index < 0) return;
    t.in_iso = ISO::IsoMount::is_iso_path(t.bnk_path);
    try {
        auto& bc = BnkCache::get(t.bnk_path);
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

bool ensure_backup(const FileTarget& t, const char* tag,
                   std::unordered_set<std::string>& backed,
                   std::string& msg) {
    if (!t.valid && t.file_path.empty()) return true;
    std::error_code ec;
    if (!t.file_path.empty()) {
        if (!backed.insert(t.file_path).second) return true;
        const std::filesystem::path bak(t.file_path + ".bak");
        if (std::filesystem::exists(bak, ec)) return true;
        std::filesystem::copy_file(t.file_path, bak,
                                   std::filesystem::copy_options::none, ec);
        if (ec) {
            msg = "backup failed: " + ec.message() + " (" + bak.string() +
                  ")";
            return false;
        }
        OutputLog::success("level edit: backup written to " + bak.string());
        return true;
    }
    if (!t.in_iso) {
        if (!backed.insert(t.bnk_path).second) return true;
        const std::filesystem::path bak(t.bnk_path + ".bak");
        if (std::filesystem::exists(bak, ec)) return true;
        std::filesystem::copy_file(t.bnk_path, bak,
                                   std::filesystem::copy_options::none, ec);
        if (ec) {
            msg = "backup failed: " + ec.message() + " (" + bak.string() +
                  ")";
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
        std::filesystem::copy_file(
            bak, t.file_path,
            std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) { msg = "restore failed: " + ec.message(); return false; }
        return true;
    }
    if (!t.in_iso) {
        if (!restored.insert(t.bnk_path).second) return true;
        const std::filesystem::path bak(t.bnk_path + ".bak");
        if (!std::filesystem::exists(bak, ec)) return true;
        BnkCache::invalidate(t.bnk_path);
        std::filesystem::copy_file(
            bak, t.bnk_path,
            std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) { msg = "restore failed: " + ec.message(); return false; }
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

}

void OnLevelLoaded(const FlatAssetEntry& entry) {
    auto& s = st();
    if (s.dirty) {
        OutputLog::warn("level edit: unsaved object moves discarded "
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
    OutputLog::info(
        "level edit: tracking '" + entry.name + "' (" +
        (s.lev.compressed ? "chunked" : "raw") + " entry, slot " +
        std::to_string(s.lev.on_disk_size) + " B" +
        (s.lev.in_iso ? ", ISO-hosted)" : ")"));
}

void SetGdbSource(const std::string& bnk_path,
                  int file_index,
                  const std::string& loose_file) {
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

bool Available() { return st().available; }
bool Enabled()   { return st().available && st().enabled; }
bool Dirty()     { return st().dirty; }

size_t EditedCount() {
    size_t n = 0;
    for (const auto& kv : st().edits) {
        const auto& d = kv.second.delta;
        if (d[0] != 0.0f || d[1] != 0.0f || d[2] != 0.0f) ++n;
    }
    return n;
}

uint64_t Revision() { return st().revision; }

bool SetEnabled(bool on, std::string& msg) {
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
    std::unordered_set<std::string> backed;
    if (!ensure_backup(s.lev, "lev", backed, msg)) return false;
    if (!ensure_backup(s.gdb, "gdb", backed, msg)) return false;
    s.enabled = true;
    msg = "level edit mode on";
    return true;
}

const float* DeltaFor(uint32_t selection_id) {
    auto it = st().edits.find(selection_id);
    if (it == st().edits.end()) return nullptr;
    const auto& d = it->second.delta;
    if (d[0] == 0.0f && d[1] == 0.0f && d[2] == 0.0f) return nullptr;
    return d;
}

void AddDelta(uint32_t selection_id,
              const float step[3],
              const float orig[3],
              uint32_t lev_pos_offset,
              const uint32_t gdb_pos_off[3]) {
    auto& s = st();
    auto& e = s.edits[selection_id];
    if (e.lev_off == 0 && e.gdb_off[0] == 0) {
        e.lev_off = lev_pos_offset;
        if (gdb_pos_off) {
            e.gdb_off[0] = gdb_pos_off[0];
            e.gdb_off[1] = gdb_pos_off[1];
            e.gdb_off[2] = gdb_pos_off[2];
        }
        e.orig[0] = orig[0];
        e.orig[1] = orig[1];
        e.orig[2] = orig[2];
    }
    e.delta[0] += step[0];
    e.delta[1] += step[1];
    e.delta[2] += step[2];
    s.dirty = true;
    ++s.revision;
}

void CollectPreviewOffsets(
    std::unordered_map<uint32_t, std::array<float, 3>>& out) {
    out.clear();
    for (const auto& kv : st().edits) {
        const auto& d = kv.second.delta;
        if (d[0] == 0.0f && d[1] == 0.0f && d[2] == 0.0f) continue;

        out[kv.first] = { d[0], d[2], d[1] };
    }
}

void PushUndoSnapshot(const std::vector<uint32_t>& ids) {
    auto& s = st();
    UndoStep step;
    step.deltas_before.reserve(ids.size());
    for (uint32_t id : ids) {
        auto it = s.edits.find(id);
        std::array<float, 3> d = {0, 0, 0};
        if (it != s.edits.end()) {
            d = { it->second.delta[0], it->second.delta[1],
                  it->second.delta[2] };
        }
        step.deltas_before.emplace_back(id, d);
    }
    s.undo_stack.push_back(std::move(step));
    if (s.undo_stack.size() > kMaxUndoSteps) {
        s.undo_stack.erase(s.undo_stack.begin());
    }
}

bool Undo() {
    auto& s = st();
    if (s.undo_stack.empty()) return false;
    UndoStep step = std::move(s.undo_stack.back());
    s.undo_stack.pop_back();
    for (const auto& kv : step.deltas_before) {
        auto it = s.edits.find(kv.first);
        if (it == s.edits.end()) continue;
        it->second.delta[0] = kv.second[0];
        it->second.delta[1] = kv.second[1];
        it->second.delta[2] = kv.second[2];
    }
    s.dirty = true;
    ++s.revision;
    return true;
}

bool Save(std::string& msg) {
    auto& s = st();
    if (!s.available) { msg = "no level loaded"; return false; }

    size_t lev_written = 0, gdb_written = 0, skipped = 0;
    std::vector<std::pair<uint32_t, std::array<float, 3>>> lev_patches;
    struct GdbPatch { uint32_t off; float v; };
    std::vector<GdbPatch> gdb_patches;

    for (const auto& kv : s.edits) {
        const EditEntry& e = kv.second;
        if (e.delta[0] == 0.0f && e.delta[1] == 0.0f &&
            e.delta[2] == 0.0f) continue;
        const float np[3] = { e.orig[0] + e.delta[0],
                              e.orig[1] + e.delta[1],
                              e.orig[2] + e.delta[2] };
        if (e.lev_off != 0) {
            lev_patches.push_back({ e.lev_off, { np[0], np[1], np[2] } });
        } else if (e.gdb_off[0] || e.gdb_off[1] || e.gdb_off[2]) {
            for (int i = 0; i < 3; ++i) {
                if (e.gdb_off[i]) gdb_patches.push_back({ e.gdb_off[i],
                                                          np[i] });
            }
        } else {
            ++skipped;
        }
    }
    if (lev_patches.empty() && gdb_patches.empty()) {
        msg = skipped ? "no file-backed changes to save (" +
                            std::to_string(skipped) +
                            " visual-only move(s) skipped)"
                      : "no changes to save";
        return true;
    }

    std::string err;

    if (!lev_patches.empty()) {
        if (target_patchable_in_place(s.lev)) {
            for (const auto& p : lev_patches) {
                if ((uint64_t)p.first + 12 > s.lev.on_disk_size) continue;
                const float vals[3] = { p.second[0], p.second[1],
                                        p.second[2] };
                if (!patch_target(s.lev, p.first, vals, 3, err)) {
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
                if ((size_t)p.first + 12 > bytes.size()) continue;
                put_f32_be(bytes.data() + p.first + 0, p.second[0]);
                put_f32_be(bytes.data() + p.first + 4, p.second[1]);
                put_f32_be(bytes.data() + p.first + 8, p.second[2]);
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

    s.dirty = false;
    msg = "saved " + std::to_string(lev_written) +
          " level-file position(s), " + std::to_string(gdb_written) +
          " gdb component(s)";
    if (skipped) {
        msg += " (" + std::to_string(skipped) +
               " visual-only move(s) not saved)";
    }
    return true;
}

bool RestoreDefaults(std::string& msg) {
    auto& s = st();
    if (!s.available) { msg = "no level loaded"; return false; }

    std::unordered_set<std::string> restored;
    if (!restore_target(s.lev, "lev", restored, msg)) return false;
    if (!restore_target(s.gdb, "gdb", restored, msg)) return false;

    if (!s.lev.bnk_path.empty()) BnkCache::invalidate(s.lev.bnk_path);
    if (!s.gdb.bnk_path.empty()) BnkCache::invalidate(s.gdb.bnk_path);
    s.edits.clear();
    s.undo_stack.clear();
    s.dirty = false;
    ++s.revision;

    Level::OpenAsync(s.entry);
    msg = "level restored from backup; reloading";
    return true;
}

void ClearEdits() {
    st().edits.clear();
    st().undo_stack.clear();
    st().dirty = false;
    ++st().revision;
}

}
