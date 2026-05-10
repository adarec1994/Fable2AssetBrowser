#pragma once
// fable2_anims.animation_toc reader.
//
// The TOC is a small (~480 KB) sidecar to fable2_anims.animation_data that
// lists every animation clip in the game (4082 of them in the retail disc),
// each with its byte offset/length inside the giant .animation_data blob,
// its duration, and a timeline of timestamped events (animation triggers,
// IK target sets, sound effect plays, …).
//
// Format reverse-engineered from default.xex sub_82B80D70. Notes in
// docs/ANIM_FORMAT.md.
//
// This module ONLY parses the TOC. .animation_data slicing lives in
// AnimDataFile (Phase C); per-clip Havok decode lives in AnimPlayer
// (Phase E). Parsing is synchronous on whatever thread the caller runs on
// — the TOC is small enough that it doesn't need a worker.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Anim {

// One timestamped event inside a clip's event list. Names index into the
// shared TOC string table, NOT the per-clip space — empty string means
// the index was 0xFFFFFFFF (unset).
struct AnimEvent {
    float        time;       // seconds from clip start
    std::string  name;       // event verb, e.g. "ACTION PERFORM"
    std::string  param;      // event arg blob, e.g. "MyBone=Shadow_L_BlendHand …"
};

struct AnimClip {
    // Raw on-disk header fields. Field 0 / 1 are 32-bit hashes used by
    // the runtime to bind clips to animation graph nodes — we keep them
    // around so we can attempt name resolution later if needed.
    uint32_t key0           = 0;
    uint32_t key1           = 0;

    // Byte offset + length of the clip's blob inside fable2_anims.animation_data.
    // The data file is mmap'd separately (see AnimDataFile); this is the
    // slice into it.
    uint32_t data_offset    = 0;
    uint32_t data_length    = 0;

    // Frame rate (frames per second). Stored at AnimRecord+16 in the
    // TOC. Initially mistaken for "duration" because every clip in
    // the retail TOC holds 30.0 there — turns out it's the playback
    // rate, and the actual duration is `frame_count / fps`, where
    // `frame_count` lives in the per-clip header inside the data
    // file (see AnimDataFile::ClipHeader::field_C). Use
    // Anim::clip_duration_seconds() to get the real number.
    float    fps            = 0.0f;

    // Filled at parse time. Initially we synthesise "clip_NNNN" — the
    // human-readable name lives in the SpecialRecord side of the TOC
    // and resolution is a Phase B refinement.
    std::string name;

    std::vector<AnimEvent> events;
};

// Compute a clip's real duration in seconds. Pulls the frame count
// out of the per-clip header in the data file (must be loaded — see
// AnimDataFile::open_for_root) and divides by fps. Returns 0 when
// the data file isn't loaded or the clip header is unrecognised, so
// callers can use the result directly in UI without pre-checking.
float clip_duration_seconds(const AnimClip& clip);

// Read the TOC at `toc_path` into `out_clips`. File-backed convenience
// that reads the bytes off disk and dispatches to load_toc_bytes.
// Returns true on success. On failure logs the reason via OutputLog and
// leaves out_clips empty.
bool load_toc(const std::filesystem::path& toc_path,
              std::vector<AnimClip>& out_clips);

// Same parser, but operating on an in-memory blob. Used by the ISO path
// where the bytes are read out of the mount instead of off disk.
bool load_toc_bytes(const uint8_t* data, size_t size,
                    std::vector<AnimClip>& out_clips);

// Locate + load the TOC for an asset root. `root` is either an ISO path
// (handled via IsoMount) or a regular folder. The TOC lives under
//   <root>/data/animation/fable2_anims.animation_toc
// or its virtual-iso equivalent. Returns true and fills `out_clips`,
// false on missing-file / parse failure.
//
// On failure the reason is already logged through OutputLog — caller
// can ignore the bool unless it cares to suppress UI for a missing
// bank.
bool load_toc_for_root(const std::string& root,
                       std::vector<AnimClip>& out_clips);

// Walk every .lua file in the loaded asset tree, extract every
// quoted string, FNV-1 hash each, and override the placeholder
// `id_HHHHHHHH` clip names where a hash matches a clip's key0.
//
// Engine-side, the runtime computes the FNV-1 hash of an animation
// name string and looks the resulting u32 up in the TOC's sorted
// clip set (sub_821F3C28 is the hash function — see Phase F notes).
// Lua scripts call animations by friendly name (`PlayAnim(actor,
// "STANCE_REGULAR")`), so every quoted string in a script that
// references a clip will hash to a real clip key. We exploit that
// to recover names without parsing the per-character `.anim`
// configs (which we can't find in retail data).
//
// Returns the count of clips that got a friendly name. Idempotent
// — running it twice is harmless. Logs progress through OutputLog.
//
// Thread: runs synchronously on the caller's thread; the I/O here
// is BNK reads + regex over Lua bytes, ~1-2 seconds total on a
// retail dataset (~800 lua files, ~50K quoted strings).
size_t resolve_clip_names_from_luas(std::vector<AnimClip>& clips);

} // namespace Anim
