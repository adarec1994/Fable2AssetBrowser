#include "LevelEdit.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>
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
    uint32_t pos_file_offset = 0;
};

struct ModuleState {
    bool available = false;
    bool enabled   = false;
    bool dirty     = false;
    uint64_t revision = 0;

    FlatAssetEntry entry{};          // the loaded level, for reload
    uint64_t entry_disk_offset = 0;  // level payload offset inside the BNK
    uint32_t entry_on_disk_size = 0;
    bool     entry_compressed = false;
    bool     bnk_in_iso = false;

    std::unordered_map<uint32_t, EditEntry> edits;
};

ModuleState& st() {
    static ModuleState s;
    return s;
}

std::filesystem::path loose_bak_path(const std::string& bnk_path) {
    return std::filesystem::path(bnk_path + ".bak");
}

// Slot backups (ISO-hosted BNKs, where duplicating the ISO is unreasonable)
// live beside the app root under edited_levels/.
std::filesystem::path slot_bak_path() {
    std::filesystem::path root_p(S.root_dir);
    std::error_code ec;
    if (!S.root_dir.empty() &&
        std::filesystem::is_regular_file(root_p, ec)) {
        root_p = root_p.parent_path();
    }
    if (root_p.empty()) root_p = std::filesystem::current_path();
    std::string leaf =
        std::filesystem::path(st().entry.full_path).filename().string();
    if (leaf.empty()) leaf = "level";
    return root_p / "edited_levels" / (leaf + ".slot.bak");
}

bool read_slot_bytes(std::vector<uint8_t>& out, std::string& err) {
    auto& s = st();
    if (s.entry_on_disk_size == 0) {
        err = "missing BNK entry locator";
        return false;
    }
    if (s.bnk_in_iso) {
        err = "ISO slot read not supported; backup uses extracted payload";
        return false;
    }
    std::ifstream f(s.entry.bnk_path, std::ios::binary);
    if (!f) {
        err = "could not open " + s.entry.bnk_path;
        return false;
    }
    f.seekg((std::streamoff)s.entry_disk_offset);
    out.resize(s.entry_on_disk_size);
    f.read(reinterpret_cast<char*>(out.data()),
           (std::streamsize)out.size());
    if (!f) {
        err = "short read from " + s.entry.bnk_path;
        return false;
    }
    return true;
}

bool write_bytes_at(const std::string& bnk_path,
                    uint64_t offset,
                    const uint8_t* data,
                    size_t size,
                    std::string& err) {
    if (ISO::IsoMount::is_iso_path(bnk_path)) {
        const std::string vpath = ISO::IsoMount::strip_iso_prefix(bnk_path);
        if (!ISO::IsoMount::instance().write_at(vpath, offset, data, size)) {
            err = "ISO in-place write failed (" + vpath + ")";
            return false;
        }
        return true;
    }
    std::fstream f(bnk_path,
                   std::ios::binary | std::ios::in | std::ios::out);
    if (!f) {
        err = "could not open " + bnk_path + " for writing";
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

std::filesystem::path fallback_export_path() {
    std::filesystem::path root_p(S.root_dir);
    std::error_code ec;
    if (!S.root_dir.empty() &&
        std::filesystem::is_regular_file(root_p, ec)) {
        root_p = root_p.parent_path();
    }
    if (root_p.empty()) root_p = std::filesystem::current_path();
    std::string leaf =
        std::filesystem::path(st().entry.full_path).filename().string();
    if (leaf.empty()) leaf = "level.lev";
    return root_p / "edited_levels" / leaf;
}

}  // namespace

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
    s.bnk_in_iso = ISO::IsoMount::is_iso_path(entry.bnk_path);
    try {
        auto& bc = BnkCache::get(entry.bnk_path);
        const auto& files = bc.reader->list_files();
        if (entry.file_index >= 0 &&
            entry.file_index < (int)files.size()) {
            s.entry_disk_offset =
                bc.reader->entry_disk_offset(entry.file_index);
            s.entry_on_disk_size =
                bc.reader->entry_on_disk_size(entry.file_index);
            s.entry_compressed =
                bc.reader->entry_is_compressed(entry.file_index);
        }
    } catch (...) {
    }
    OutputLog::info(
        "level edit: tracking '" + entry.name + "' (" +
        (s.entry_compressed ? "chunked" : "raw") + " entry, slot " +
        std::to_string(s.entry_on_disk_size) + " B" +
        (s.bnk_in_iso ? ", ISO-hosted)" : ")"));
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

    // First enable: make the backup if it isn't there yet.
    if (s.bnk_in_iso) {
        // ISO-hosted: back up the extracted payload so Restore can
        // recompress-free write it back (raw entries only).
        const auto bak = slot_bak_path();
        std::error_code ec;
        if (!std::filesystem::exists(bak, ec)) {
            if (s.entry_compressed) {
                // Chunked entries can't be spliced back in place anyway;
                // Save falls back to an export, so no slot backup needed.
                OutputLog::warn(
                    "level edit: chunked ISO entry — Save will export "
                    "beside the app instead of patching in place");
            } else {
                std::vector<uint8_t> slot;
                std::string err;
                // Read the raw slot straight out of the ISO via extract
                // (raw entry == payload bytes).
                try {
                    slot = BnkCache::extract_bytes(s.entry.bnk_path,
                                                   s.entry.file_index);
                } catch (...) {
                    slot.clear();
                }
                if (slot.empty()) {
                    msg = "backup failed: could not extract level payload";
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
                (void)err;
                OutputLog::success("level edit: slot backup written to " +
                                   bak.string());
            }
        }
    } else {
        const auto bak = loose_bak_path(s.entry.bnk_path);
        std::error_code ec;
        if (!std::filesystem::exists(bak, ec)) {
            std::filesystem::copy_file(
                s.entry.bnk_path, bak,
                std::filesystem::copy_options::none, ec);
            if (ec) {
                msg = "backup failed: " + ec.message() + " (" +
                      bak.string() + ")";
                return false;
            }
            OutputLog::success("level edit: backup written to " +
                               bak.string());
        }
    }

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
              uint32_t pos_file_offset) {
    auto& s = st();
    auto& e = s.edits[selection_id];
    if (e.pos_file_offset == 0) {
        e.pos_file_offset = pos_file_offset;
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

uint64_t Revision() { return st().revision; }

void CollectPreviewOffsets(
    std::unordered_map<uint32_t, std::array<float, 3>>& out) {
    out.clear();
    for (const auto& kv : st().edits) {
        const auto& d = kv.second.delta;
        if (d[0] == 0.0f && d[1] == 0.0f && d[2] == 0.0f) continue;
        // engine (X, Y, Z-up) -> preview (X, up, Y)
        out[kv.first] = { d[0], d[2], d[1] };
    }
}

bool Save(std::string& msg) {
    auto& s = st();
    if (!s.available) { msg = "no level loaded"; return false; }

    struct Patch { uint32_t off; float pos[3]; };
    std::vector<Patch> patches;
    for (const auto& kv : s.edits) {
        const EditEntry& e = kv.second;
        if (e.pos_file_offset == 0) continue;
        if (e.delta[0] == 0.0f && e.delta[1] == 0.0f &&
            e.delta[2] == 0.0f) continue;
        Patch p;
        p.off = e.pos_file_offset;
        p.pos[0] = e.orig[0] + e.delta[0];
        p.pos[1] = e.orig[1] + e.delta[1];
        p.pos[2] = e.orig[2] + e.delta[2];
        patches.push_back(p);
    }
    if (patches.empty()) {
        msg = "no movable changes to save";
        return true;
    }

    if (s.entry_compressed || s.entry_on_disk_size == 0) {
        // Can't patch a chunked slot in place: export the patched level.
        std::vector<uint8_t> bytes;
        try {
            bytes = BnkCache::extract_bytes(s.entry.bnk_path,
                                            s.entry.file_index);
        } catch (...) {
            bytes.clear();
        }
        if (bytes.empty()) { msg = "level re-extract failed"; return false; }
        for (const auto& p : patches) {
            if ((size_t)p.off + 12 > bytes.size()) continue;
            put_f32_be(bytes.data() + p.off + 0, p.pos[0]);
            put_f32_be(bytes.data() + p.off + 4, p.pos[1]);
            put_f32_be(bytes.data() + p.off + 8, p.pos[2]);
        }
        const auto out = fallback_export_path();
        std::error_code ec;
        std::filesystem::create_directories(out.parent_path(), ec);
        std::ofstream f(out, std::ios::binary);
        if (!f) { msg = "could not write " + out.string(); return false; }
        f.write(reinterpret_cast<const char*>(bytes.data()),
                (std::streamsize)bytes.size());
        s.dirty = false;
        msg = "chunked BNK entry: patched level exported to " + out.string();
        return true;
    }

    // Raw entry: the payload is a contiguous slice of the BNK — write the
    // 12 position bytes per moved instance directly.
    size_t written = 0;
    for (const auto& p : patches) {
        if ((uint64_t)p.off + 12 > s.entry_on_disk_size) continue;
        uint8_t buf[12];
        put_f32_be(buf + 0, p.pos[0]);
        put_f32_be(buf + 4, p.pos[1]);
        put_f32_be(buf + 8, p.pos[2]);
        std::string err;
        if (!write_bytes_at(s.entry.bnk_path,
                            s.entry_disk_offset + p.off, buf, 12, err)) {
            msg = "save failed: " + err;
            return false;
        }
        ++written;
    }
    BnkCache::invalidate(s.entry.bnk_path);
    s.dirty = false;
    msg = "saved " + std::to_string(written) + " object position(s) into " +
          std::filesystem::path(s.entry.bnk_path).filename().string();
    return true;
}

bool RestoreDefaults(std::string& msg) {
    auto& s = st();
    if (!s.available) { msg = "no level loaded"; return false; }

    if (s.bnk_in_iso) {
        const auto bak = slot_bak_path();
        std::error_code ec;
        if (!std::filesystem::exists(bak, ec)) {
            msg = "no backup found (" + bak.string() + ")";
            return false;
        }
        std::ifstream f(bak, std::ios::binary);
        std::vector<uint8_t> slot(
            (std::istreambuf_iterator<char>(f)),
            std::istreambuf_iterator<char>());
        if (slot.empty()) { msg = "backup unreadable"; return false; }
        if (slot.size() > s.entry_on_disk_size) {
            msg = "backup larger than the BNK slot; refusing";
            return false;
        }
        std::string err;
        if (!write_bytes_at(s.entry.bnk_path, s.entry_disk_offset,
                            slot.data(), slot.size(), err)) {
            msg = "restore failed: " + err;
            return false;
        }
    } else {
        const auto bak = loose_bak_path(s.entry.bnk_path);
        std::error_code ec;
        if (!std::filesystem::exists(bak, ec)) {
            msg = "no backup found (" + bak.string() + ")";
            return false;
        }
        // Drop the cached reader before overwriting the BNK on disk.
        BnkCache::invalidate(s.entry.bnk_path);
        std::filesystem::copy_file(
            bak, s.entry.bnk_path,
            std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            msg = "restore failed: " + ec.message();
            return false;
        }
    }

    BnkCache::invalidate(s.entry.bnk_path);
    s.edits.clear();
    s.dirty = false;
    ++s.revision;

    // Reload the level so the viewer shows the restored data.
    Level::OpenAsync(s.entry);
    msg = "level restored from backup; reloading";
    return true;
}

void ClearEdits() {
    st().edits.clear();
    st().dirty = false;
    ++st().revision;
}

}  // namespace LevelEdit
