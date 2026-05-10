// AnimDataFile — slurp + slice fable2_anims.animation_data.
//
// File header layout (big-endian, see docs/ANIM_FORMAT.md):
//   +0x00  u32  magic     0xCEA5EBED
//   +0x04  u32  version   == 7  (only this version observed in retail)
//   +0x08  u32  ?         (typically 0xFF)
//   +0x0C  u32  ?         (clip count proxy — 0x87 in retail)
//   +0x10  u32  ?
//   +0x14  u32  ?
//   +0x18  u32  zero
//   +0x1C  u32[]  master offset table (we don't use this — clip
//                 offsets come from the TOC's AnimRecord.data_offset)
//
// Decoding the body is Phase E. This file just gives the rest of the
// app raw byte slices keyed by clip.

#include "AnimDataFile.h"

#include "../UI/OutputLog.h"
#include "../ISO/IsoMount.h"

#include <cstring>
#include <fstream>

namespace Anim {

namespace {

constexpr uint32_t kExpectedMagic   = 0xCEA5EBEDu;
constexpr uint32_t kExpectedVersion = 7u;

uint32_t read_u32_be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

} // anonymous

bool AnimDataFile::open(const std::filesystem::path& data_path) {
    blob_.clear();
    std::ifstream f(data_path, std::ios::binary | std::ios::ate);
    if (!f) {
        OutputLog::error("AnimDataFile: cannot open " + data_path.string());
        return false;
    }
    std::streamsize sz = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes((size_t)sz);
    if (!f.read(reinterpret_cast<char*>(bytes.data()), sz)) {
        OutputLog::error("AnimDataFile: read failed for " +
                         data_path.string());
        return false;
    }
    return open_from_bytes(std::move(bytes));
}

bool AnimDataFile::open_from_bytes(std::vector<uint8_t> bytes) {
    blob_.clear();
    if (bytes.size() < 0x1C) {
        OutputLog::error("AnimDataFile: file too short (" +
                         std::to_string(bytes.size()) + " bytes)");
        return false;
    }
    blob_ = std::move(bytes);
    if (!header_ok()) {
        // header_ok already logs the specific mismatch — but only if we
        // actually have the bytes loaded, which is the case here. Drop
        // the blob so partial state doesn't leak into clip_bytes().
        blob_.clear();
        return false;
    }
    OutputLog::success("AnimDataFile: opened (" +
                       std::to_string(blob_.size()) + " bytes)");
    return true;
}

bool AnimDataFile::open_for_root(const std::string& root) {
    blob_.clear();
    constexpr const char* kRel = "data/animation/fable2_anims.animation_data";

    if (ISO::IsoMount::instance().is_mounted()) {
        // Free the LRU cache so a 26 MB read doesn't fight the cached
        // BNK blobs for contiguous heap (same reasoning as IsoDump).
        // The single read here goes straight into our own buffer; the
        // cache wouldn't help us anyway since clip lookups go through
        // the in-memory blob_, not back through IsoMount.
        ISO::IsoMount::instance().clear_cache();

        const ISO::MountedFile* mf =
            ISO::IsoMount::instance().find(kRel);
        if (!mf) {
            mf = ISO::IsoMount::instance().find_by_basename(
                "fable2_anims.animation_data");
        }
        if (!mf) {
            OutputLog::warn("AnimDataFile: data file not found in ISO");
            return false;
        }
        auto bytes = ISO::IsoMount::instance().read_file(mf->path);
        if (bytes.empty()) {
            OutputLog::error("AnimDataFile: failed to read " + mf->path +
                             " from ISO");
            return false;
        }
        return open_from_bytes(std::move(bytes));
    }

    // Folder mode — canonical path first, then a recursive basename
    // fallback for extracted-flat layouts.
    std::filesystem::path direct = std::filesystem::path(root) / kRel;
    if (std::filesystem::exists(direct)) {
        return open(direct);
    }
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (it->is_regular_file(ec) &&
            it->path().filename() == "fable2_anims.animation_data") {
            return open(it->path());
        }
    }
    OutputLog::warn("AnimDataFile: data file not found under " + root);
    return false;
}

void AnimDataFile::close() {
    blob_.clear();
    blob_.shrink_to_fit();
}

bool AnimDataFile::header_ok() const {
    if (blob_.size() < 8) return false;
    uint32_t magic = read_u32_be(blob_.data() + 0);
    uint32_t ver   = read_u32_be(blob_.data() + 4);
    if (magic != kExpectedMagic) {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "AnimDataFile: bad magic 0x%08X (expected 0x%08X)",
                      magic, kExpectedMagic);
        OutputLog::error(buf);
        return false;
    }
    if (ver != kExpectedVersion) {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "AnimDataFile: unsupported version %u (expected %u)",
                      ver, kExpectedVersion);
        OutputLog::error(buf);
        return false;
    }
    return true;
}

AnimDataFile::ClipHeader AnimDataFile::parse_clip_header(const AnimClip& clip) const {
    ClipHeader h;
    auto sp = clip_bytes(clip);
    if (sp.empty() || sp.size < 0x1C) return h;
    h.magic         = read_u32_be(sp.data + 0x00);
    h.version       = read_u32_be(sp.data + 0x04);
    h.field_8       = read_u32_be(sp.data + 0x08);
    h.field_C       = read_u32_be(sp.data + 0x0C);
    h.bone_count    = read_u32_be(sp.data + 0x10);
    h.bone_idx_bits = read_u32_be(sp.data + 0x14);
    if (h.magic != kExpectedMagic || h.version != kExpectedVersion) {
        // Not a clip we recognise — return the partially-parsed header
        // with ok=false so callers can still inspect the prefix in
        // dev mode without us pretending the parse succeeded.
        return h;
    }
    // Per-item offset directory. CORRECTION: the directory has
    // `field_C` entries (= a1[12] in the runtime), not bone_count.
    // Each entry is a BIT offset relative to the body section,
    // which starts at `clip + 24 + 4 * field_C`. The directory
    // itself starts at clip+24 (one entry can be 0 for the first
    // item — the runtime's `*(_DWORD *)(4 * v191 + a1 + 24)`
    // indexing makes a1+24 the first entry, including the
    // observed `00 00 00 00` we used to call a "zero pad").
    if (h.field_C == 0) return h;
    const size_t dir_off = 24;     // a1+24 in IDA's indexing
    const size_t dir_end = dir_off + (size_t)h.field_C * 4;
    if (dir_end > sp.size) return h;
    h.bone_offsets.reserve(h.field_C);
    for (uint32_t i = 0; i < h.field_C; ++i) {
        h.bone_offsets.push_back(read_u32_be(sp.data + dir_off + i * 4));
    }
    h.ok = true;
    return h;
}

AnimDataFile::Span AnimDataFile::clip_bytes(const AnimClip& clip) const {
    if (blob_.empty()) return {nullptr, 0};
    // The TOC's `data_length` field turned out NOT to be a byte
    // length — it's actually a count value matching clip blob
    // u32[4] (bone_count). The actual byte extent of a clip is
    // bounded only by the data file's total size + the next
    // clip's offset. Use "remaining bytes from data_offset to
    // EOF" as the safe upper bound here so parse_clip_header's
    // dir-bounds check doesn't reject valid clips that happen to
    // have a tiny `data_length`.
    if (clip.data_offset >= blob_.size()) return {nullptr, 0};
    const size_t remaining = blob_.size() - clip.data_offset;
    return {blob_.data() + clip.data_offset, remaining};
}

AnimDataFile& global_data_file() {
    // Function-local static — initialised on first use, destroyed at
    // process teardown. Avoids static-init-order issues with State.
    static AnimDataFile inst;
    return inst;
}

} // namespace Anim
