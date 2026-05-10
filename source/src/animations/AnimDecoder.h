#pragma once
// AnimDecoder — port-in-progress of Fable 2's animation bit-stream
// decoder (sub_822E1DA8 in default.xex). The runtime equivalent
// produces 8-frame "blocks" of decoded transforms keyed by a
// (clip_blob_ptr, frame >> 3) hash, then interpolates within the
// block to deliver the final per-bone pose for an arbitrary time.
//
// This file is the scaffolding — the bit-reader, the per-bone loop,
// the working-buffer layout, and the call-out points to the
// per-channel decoders. Stubs for the channel decoders themselves
// (and the lerp/normalise interpolator) live next to a
// TODO(phase-F-decoder) block in AnimDecoder.cpp; until each is
// implemented they fall through to identity, which keeps the model
// at rest pose while the playback clock advances.
//
// See docs/ANIM_FORMAT.md (Phase F section) for the full call chain
// and IDA addresses.

#include "AnimBank.h"
#include "AnimDataFile.h"

#include <cstdint>
#include <vector>

namespace Anim {

// Result of decoding a clip at a specific time: per-bone TRS as a
// flat float buffer in the same layout the existing skinning shader
// + skeleton overlay consume (see ModelPreview's MP_ComputeWorldPose
// for the shape — 4-quat then 3-translate per bone, 8 floats stride).
//
// Phase F WIP: AnimDecoder::decode populates rotation slots only.
// Translation + scale slots are zeroed (rest pose) until those
// channels are wired up.
struct DecodedPose {
    std::vector<float> bone_quats;   // bone_count × 4 (xyzw)
    std::vector<float> bone_trans;   // bone_count × 3
    bool ok = false;
};

class AnimDecoder {
public:
    // Decode `clip` at the given time (seconds). Pulls bytes through
    // AnimDataFile::global_data_file(); fails if the data file isn't
    // open or the clip header is unrecognised.
    bool decode(const AnimClip& clip, float time_seconds,
                DecodedPose& out);

    // Drop the 8-frame block cache (call after switching root /
    // unmounting ISO so cached pointers don't dangle).
    void clear_cache();

private:
    // 8-frame block of decoded transforms. Stored in the cache so
    // re-sampling within the same block doesn't re-decode. Size is
    // bone_count × 8 frames × (4 quat + 3 trans) floats = 56 floats
    // per bone per block.
    struct Block {
        const uint8_t* clip_blob_ptr = nullptr;   // owns the cache key
        uint32_t       block_idx     = 0;         // frame >> 3
        uint32_t       bone_count    = 0;
        std::vector<float> data;                  // 56*bone_count floats
    };

    // Open-addressing cache. Size matches the runtime's 1024 buckets.
    static constexpr size_t kCacheBuckets = 1024;
    Block cache_[kCacheBuckets];

    // Hash for cache lookup — same shape as the runtime
    //   (((clip_blob_ptr >> 4) ^ block_idx) & 0xFFF
    // (we mask to & (kCacheBuckets - 1) since our cache is smaller).
    static uint32_t cache_hash(const uint8_t* blob, uint32_t block_idx) {
        return ((uint32_t)((uintptr_t)blob >> 4) ^ block_idx)
               & (kCacheBuckets - 1);
    }

    // Decode an 8-frame block fresh (no cache hit). Internal — the
    // public decode() routes here on miss. `clip_blob_size` is the
    // remaining-bytes-to-EOF span the bit reader uses as its
    // upper bound, so a runaway cursor can't read past blob_'s
    // allocation.
    bool decode_block(const AnimDataFile::ClipHeader& hdr,
                      const uint8_t* clip_blob,
                      size_t         clip_blob_size,
                      uint32_t bone_count,
                      uint32_t block_idx,
                      Block& out);

    // Interpolate within a decoded block at fractional frame
    // [0, 8). Outputs into `out`. Quat slots get spherical-lerp
    // (slerp); trans slots get linear lerp.
    void interpolate(const Block& blk, float frac_frame,
                     DecodedPose& out) const;
};

// Process-wide singleton — one decoder shared across the app.
// (The cache is small + per-instance, no contention concerns at our
// thread count.)
AnimDecoder& global_decoder();

} // namespace Anim
