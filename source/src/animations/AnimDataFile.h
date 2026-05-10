#pragma once
// fable2_anims.animation_data — the giant (~26 MB) blob that holds the
// actual animation track bytes. The TOC sidecar (AnimBank.h) tells us
// where each clip lives inside this file via byte offset + length;
// AnimDataFile is the corresponding "where do those bytes come from"
// half.
//
// We slurp the whole file into a single std::vector<uint8_t> at load
// time. 26 MB fits comfortably in a 32-bit address space even with
// IsoMount's LRU cache around (and we deliberately clear that cache
// before loading the file in ISO mode — see AnimDataFile::open). Reads
// are then O(1) span slices keyed by clip — no per-frame disk I/O.
//
// Phase C — this module is plumbing only. It exposes raw clip bytes;
// decoding (Phase E) consumes the span and produces poses. The format
// inside each clip blob is a serialised Havok hkaXxxAnimation object
// (interleaved / delta / wavelet) — see docs/ANIM_FORMAT.md.

#include "AnimBank.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Anim {

class AnimDataFile {
public:
    // Open the data file. Returns true on success; on failure logs the
    // reason via OutputLog and leaves the object in an empty/closed
    // state.
    bool open(const std::filesystem::path& data_path);

    // Bytes-based variant — used by the ISO loader path where the file
    // contents come from IsoMount::read_file rather than a disk path.
    bool open_from_bytes(std::vector<uint8_t> bytes);

    // ISO-aware locator. Mirrors AnimBank::load_toc_for_root: tries the
    // canonical sub-path first, then falls back to a basename search.
    bool open_for_root(const std::string& root);

    void close();
    bool is_open() const { return !blob_.empty(); }
    size_t size() const  { return blob_.size(); }

    // Get a read-only view into the byte range of `clip`. Returns
    // {nullptr, 0} if the clip's offset/length lies outside the loaded
    // blob (corrupted TOC or wrong file). Cheap — no allocation.
    struct Span {
        const uint8_t* data;
        size_t         size;
        bool empty() const { return data == nullptr || size == 0; }
    };
    Span clip_bytes(const AnimClip& clip) const;

    // Per-clip header — populated from the first 24 bytes of a clip
    // blob, plus the per-item offset directory that follows. See
    // docs/ANIM_FORMAT.md for the byte-level layout.
    //
    // CORRECTION (Phase F session N+1): the directory has `field_C`
    // entries (= a1[12] in the runtime), NOT `bone_count` entries —
    // confirmed by walking clip[6]'s 135 entries (= field_C) all
    // monotonically increasing through the bit-stream. Each entry is
    // a "keyframe" or compression-block bit offset, not a per-bone
    // pointer. The `bone_offsets` field name is now a misnomer —
    // it's really `item_offsets` — but kept for compatibility while
    // we converge.
    //
    // `item_offsets[i]` is a BIT offset, relative to the start of
    // the body section (which sits at `clip + 24 + 4 * field_C`).
    struct ClipHeader {
        uint32_t magic            = 0;
        uint32_t version          = 0;
        uint32_t field_8          = 0;   // ~always 0xFF
        uint32_t field_C          = 0;   // = directory length (a1[12])
        uint32_t bone_count       = 0;   // distinct bones referenced
        uint32_t bone_idx_bits    = 0;   // ceil(log2(bone_count))
        std::vector<uint32_t> bone_offsets;   // really item_offsets — see above
        bool ok = false;
    };
    ClipHeader parse_clip_header(const AnimClip& clip) const;

    // Validate the file header. Used at open time but exposed publicly
    // so the test harness / dev panel can re-check after the fact.
    // Returns true if the magic + version match what we expect.
    bool header_ok() const;

private:
    std::vector<uint8_t> blob_;
};

// Convenience: the same global the rest of the app reaches for. Lives
// at TU scope in AnimDataFile.cpp; State.h pokes a reference here so
// the UI can iterate clips without threading a separate handle around.
AnimDataFile& global_data_file();

} // namespace Anim
