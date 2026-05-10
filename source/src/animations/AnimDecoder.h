#pragma once

#include "AnimBank.h"
#include "AnimDataFile.h"

#include <cstdint>
#include <vector>

namespace Anim {

struct DecodedPose {
    std::vector<float> bone_quats;
    std::vector<float> bone_trans;
    bool ok = false;
};

class AnimDecoder {
public:

    bool decode(const AnimClip& clip, float time_seconds,
                DecodedPose& out);

    void clear_cache();

private:

    struct Block {
        const uint8_t* clip_blob_ptr = nullptr;
        uint32_t       block_idx     = 0;
        uint32_t       bone_count    = 0;
        std::vector<float> data;
    };

    static constexpr size_t kCacheBuckets = 1024;
    Block cache_[kCacheBuckets];

    static uint32_t cache_hash(const uint8_t* blob, uint32_t block_idx) {
        return ((uint32_t)((uintptr_t)blob >> 4) ^ block_idx)
               & (kCacheBuckets - 1);
    }

    bool decode_block(const AnimDataFile::ClipHeader& hdr,
                      const uint8_t* clip_blob,
                      size_t         clip_blob_size,
                      uint32_t bone_count,
                      uint32_t block_idx,
                      Block& out);

    void interpolate(const Block& blk, float frac_frame,
                     DecodedPose& out) const;
};

AnimDecoder& global_decoder();

}
