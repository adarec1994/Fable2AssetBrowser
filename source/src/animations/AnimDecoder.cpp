
#include "AnimDecoder.h"

#include <cmath>
#include <cstring>

namespace Anim {

namespace {

constexpr float kQ15Scale = 1.0f / 32767.0f;
constexpr float kQ15Bias  = -1.0f;
constexpr float kEpsSmall = 1e-5f;

inline float dequant_q15(uint32_t raw_16) {
    return (float)(raw_16 & 0xFFFF) * kQ15Scale + kQ15Bias;
}

uint32_t read_u32_be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

struct BitReader {
    const uint8_t* base = nullptr;
    size_t         size = 0;
    uint32_t       cursor_bits = 0;

    bool good() const { return base && cursor_bits / 8 < size; }

    void seek_bits(uint32_t bit_offset) { cursor_bits = bit_offset; }

    uint32_t read(uint32_t n) {
        if (!base || n == 0 || n > 32) return 0;
        const uint32_t bit_pos = cursor_bits & 0x1F;
        const uint32_t word_off = (cursor_bits >> 5) * 4;
        if (word_off + 4 > size) { cursor_bits += n; return 0; }
        uint32_t lo = read_u32_be(base + word_off);
        uint32_t out;
        if (bit_pos + n > 32) {
            if (word_off + 8 > size) { cursor_bits += n; return 0; }
            uint32_t hi = read_u32_be(base + word_off + 4);
            out = ((hi << (32 - bit_pos)) | (lo >> bit_pos));
        } else {
            out = lo >> bit_pos;
        }
        if (n < 32) out &= ((1u << n) - 1u);
        cursor_bits += n;
        return out;
    }
};

}

bool AnimDecoder::decode(const AnimClip& clip, float time_seconds,
                         DecodedPose& out) {
    out.ok = false;
    out.bone_quats.clear();
    out.bone_trans.clear();

    if (!global_data_file().is_open()) return false;
    auto h = global_data_file().parse_clip_header(clip);
    if (!h.ok || h.bone_count == 0) return false;

    if (h.bone_count > 4096 || h.field_C > 65536) return false;

    const uint32_t frame_count =
        h.field_C ? h.field_C : 1;
    if (clip.fps <= 0.0f) return false;

    float frame_f = clip.fps * time_seconds;
    if (frame_f < 0.0f) frame_f = 0.0f;
    const float frame_max = (float)(frame_count - 1) - 0.001f;
    if (frame_max > 0.0f && frame_f > frame_max) frame_f = frame_max;

    const uint32_t frame_int = (uint32_t)frame_f;
    const uint32_t block_idx = frame_int >> 3;
    const float    frac_in_block = frame_f - (float)(block_idx << 3);

    auto sp = global_data_file().clip_bytes(clip);
    if (sp.empty()) return false;

    const uint32_t hash = cache_hash(sp.data, block_idx);
    Block& slot = cache_[hash];
    const bool hit =
        slot.clip_blob_ptr == sp.data &&
        slot.block_idx == block_idx &&
        slot.bone_count == h.bone_count;

    if (!hit) {
        if (!decode_block(h, sp.data, sp.size, h.bone_count,
                          block_idx, slot)) {

            slot = Block{};
            return false;
        }
        slot.clip_blob_ptr = sp.data;
        slot.block_idx     = block_idx;
        slot.bone_count    = h.bone_count;
    }

    interpolate(slot, frac_in_block, out);
    out.ok = true;
    return true;
}

void AnimDecoder::clear_cache() {
    for (auto& s : cache_) s = Block{};
}

bool AnimDecoder::decode_block(const AnimDataFile::ClipHeader& hdr,
                               const uint8_t* clip_blob,
                               size_t         clip_blob_size,
                               uint32_t bone_count,
                               uint32_t block_idx,
                               Block& out) {

    constexpr uint32_t kFloatsPerBoneFrame = 7;
    constexpr uint32_t kFramesPerBlock     = 8;
    if (bone_count == 0 || bone_count > 4096) return false;
    const size_t n = (size_t)bone_count * kFramesPerBlock * kFloatsPerBoneFrame;
    out.data.assign(n, 0.0f);

    for (uint32_t b = 0; b < bone_count; ++b) {
        for (uint32_t f = 0; f < kFramesPerBlock; ++f) {
            const size_t base =
                ((size_t)b * kFramesPerBlock + f) * kFloatsPerBoneFrame;
            out.data[base + 3] = 1.0f;
        }
    }

    const uint32_t body_off = 24 + hdr.field_C * 4;
    if (body_off > clip_blob_size) return false;
    BitReader br;
    br.base = clip_blob + body_off;

    br.size = clip_blob_size - body_off;

    auto write_channel_constant =
        [&](uint32_t bone, uint32_t channel, float value) {
            for (uint32_t f = 0; f < kFramesPerBlock; ++f) {
                const size_t base =
                    ((size_t)bone * kFramesPerBlock + f) *
                    kFloatsPerBoneFrame;
                if (channel < 3)
                    out.data[base + channel] = value;
                else if (channel < 6)
                    out.data[base + 4 + (channel - 3)] = value;

            }
        };

    auto decode_one_channel =
        [&](uint32_t bone, uint32_t channel) -> bool {
            const uint32_t mode = br.read(2);
            switch (mode) {
                case 0: return true;
                case 1: return true;
                case 2: {
                    const uint32_t raw = br.read(16);
                    const float val = dequant_q15(raw);
                    if (channel != UINT32_MAX)
                        write_channel_constant(bone, channel, val);
                    return true;
                }
                case 3:
                default:
                    return false;
            }
        };

    const uint32_t n_items = hdr.field_C;
    for (uint32_t b = 0; b < n_items; ++b) {
        if (b >= hdr.bone_offsets.size()) break;
        br.seek_bits(hdr.bone_offsets[b]);

        const uint32_t mode_hdr  = br.read(8);   (void)mode_hdr;
        const uint32_t count_sel = br.read(2);

        bool aborted = false;
        for (uint32_t ch = 0; ch < 7 && !aborted; ++ch) {
            if (!decode_one_channel(UINT32_MAX, UINT32_MAX))
                aborted = true;
        }
        if (aborted) continue;

        const uint32_t n_extras = count_sel;
        for (uint32_t e = 0; e < n_extras && !aborted; ++e) {
            if (!decode_one_channel(UINT32_MAX, UINT32_MAX))
                aborted = true;
        }
    }

    return true;
}

void AnimDecoder::interpolate(const Block& blk, float frac_frame,
                              DecodedPose& out) const {
    const uint32_t bones = blk.bone_count;
    const uint32_t kFloatsPerBoneFrame = 7;
    const uint32_t kFramesPerBlock     = 8;

    out.bone_quats.assign((size_t)bones * 4, 0.0f);
    out.bone_trans.assign((size_t)bones * 3, 0.0f);

    uint32_t f0 = (uint32_t)frac_frame;
    if (f0 >= kFramesPerBlock - 1) f0 = kFramesPerBlock - 2;
    const uint32_t f1 = f0 + 1;
    const float t  = frac_frame - (float)f0;
    const float it = 1.0f - t;

    for (uint32_t b = 0; b < bones; ++b) {
        const size_t a_base =
            ((size_t)b * kFramesPerBlock + f0) * kFloatsPerBoneFrame;
        const size_t b_base =
            ((size_t)b * kFramesPerBlock + f1) * kFloatsPerBoneFrame;

        float qx = blk.data[a_base + 0] * it + blk.data[b_base + 0] * t;
        float qy = blk.data[a_base + 1] * it + blk.data[b_base + 1] * t;
        float qz = blk.data[a_base + 2] * it + blk.data[b_base + 2] * t;
        float qw = blk.data[a_base + 3] * it + blk.data[b_base + 3] * t;
        const float q_len2 = qx*qx + qy*qy + qz*qz + qw*qw;
        const float q_inv = (q_len2 > 1e-12f)
            ? 1.0f / std::sqrt(q_len2) : 1.0f;
        qx *= q_inv; qy *= q_inv; qz *= q_inv; qw *= q_inv;
        out.bone_quats[(size_t)b * 4 + 0] = qx;
        out.bone_quats[(size_t)b * 4 + 1] = qy;
        out.bone_quats[(size_t)b * 4 + 2] = qz;
        out.bone_quats[(size_t)b * 4 + 3] = qw;

        const float tx = blk.data[a_base + 4] * it + blk.data[b_base + 4] * t;
        const float ty = blk.data[a_base + 5] * it + blk.data[b_base + 5] * t;
        const float tz = blk.data[a_base + 6] * it + blk.data[b_base + 6] * t;
        out.bone_trans[(size_t)b * 3 + 0] = tx;
        out.bone_trans[(size_t)b * 3 + 1] = ty;
        out.bone_trans[(size_t)b * 3 + 2] = tz;
    }
}

AnimDecoder& global_decoder() {
    static AnimDecoder inst;
    return inst;
}

}
